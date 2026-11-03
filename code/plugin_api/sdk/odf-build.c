/*
odf-build: builds an oDFe plugin (a QVM program) from C sources with q3lcc and q3asm.

    odf-build --client -o plugins/myplugin.qvm main.c other.c
    odf-build --server -o plugins/myplugin.qvm -DDEBUG -Iinclude main.c

The first source file has to include the entry point (odfc_entry.h or odfs_entry.h), the VM wants it first in the program.
The C library of the SDK (odf_lib.c) and the engine functions (odfc_syscalls.asm / odfs_syscalls.asm) are added at the end.

Needs q3lcc, q3cpp, q3rcc and q3asm (the tools of ioquake3, ec- or oDFs builds). Give their folder with --tools or the
environment variable ODF_TOOLS; without either they are searched for in the PATH.

The files of the SDK (include/, sdk/libc/, sdk/odf_lib.c) are found from the place of this program: next to the sources
(code/plugin_api/sdk) or in the folder of the program (include/ and sdk/ beside it). --sdk or ODF_SDK name the folder that
has include/ and sdk/ (code/plugin_api) when it is somewhere else.

Build it with any C compiler:  cc -O2 -o odf-build odf-build.c
*/

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEP '\\'
#define PATH_LIST_SEP ';'
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#define PATH_SEP '/'
#define PATH_LIST_SEP ':'
#endif

/* ------------------------------------------------------------------------------------------------------------------ */
/* small things                                                                                                       */
/* ------------------------------------------------------------------------------------------------------------------ */

#define MAX_LIST 256

static char *remove_list[16];
static int num_remove;

static void remove_tree( const char *path );

static void cleanup( void )
{
	int i;

	for ( i = 0; i < num_remove; i++ ) {
		remove_tree( remove_list[i] );
	}
	num_remove = 0;
}

static void die( const char *format, ... )
{
	va_list args;

	fflush( stdout );
	fprintf( stderr, "odf-build: " );
	va_start( args, format );
	vfprintf( stderr, format, args );
	va_end( args );
	fprintf( stderr, "\n" );
	exit( 1 );
}

static void *xmalloc( size_t size )
{
	void *p = malloc( size ? size : 1 );

	if ( !p ) {
		die( "out of memory" );
	}
	return p;
}

static void *xrealloc( void *old, size_t size )
{
	void *p = realloc( old, size ? size : 1 );

	if ( !p ) {
		die( "out of memory" );
	}
	return p;
}

static char *xstrndup( const char *s, size_t n )
{
	char *p = (char *)xmalloc( n + 1 );

	memcpy( p, s, n );
	p[n] = '\0';
	return p;
}

static char *xstrdup( const char *s )
{
	return xstrndup( s, strlen( s ) );
}

static char *fmt( const char *format, ... )
{
	char buf[8192];
	va_list args;
	int n;

	va_start( args, format );
	n = vsnprintf( buf, sizeof( buf ), format, args );
	va_end( args );
	if ( n < 0 || n >= (int)sizeof( buf ) ) {
		die( "a name is too long" );
	}
	return xstrdup( buf );
}

/* a string that grows */
typedef struct {
	char	*p;
	size_t	n, cap;
} buf_t;

static void buf_add( buf_t *b, const char *s, size_t n )
{
	if ( b->n + n + 1 > b->cap ) {
		b->cap = ( b->n + n + 1 ) * 2;
		b->p = (char *)xrealloc( b->p, b->cap );
	}
	memcpy( b->p + b->n, s, n );
	b->n += n;
	b->p[b->n] = '\0';
}

static void buf_str( buf_t *b, const char *s )
{
	buf_add( b, s, strlen( s ) );
}

/* a set of strings; the strings are given as pointer and length, so they need not end with a 0 */
typedef struct {
	char	**v;
	int		n, cap;
	int		sorted;
} set_t;

static int set_cmp( const void *a, const void *b )
{
	return strcmp( *(char * const *)a, *(char * const *)b );
}

static void set_add( set_t *s, const char *p, size_t n )
{
	if ( s->n == s->cap ) {
		s->cap = s->cap ? s->cap * 2 : 256;
		s->v = (char **)xrealloc( s->v, s->cap * sizeof( char * ) );
	}
	s->v[s->n++] = xstrndup( p, n );
	s->sorted = 0;
}

static int set_has( set_t *s, const char *p, size_t n )
{
	int lo = 0, hi = s->n - 1;

	if ( !s->sorted ) {
		qsort( s->v, s->n, sizeof( char * ), set_cmp );
		s->sorted = 1;
	}
	while ( lo <= hi ) {
		int mid = ( lo + hi ) / 2;
		const char *v = s->v[mid];
		size_t vn = strlen( v );
		int c = memcmp( p, v, n < vn ? n : vn );

		if ( c == 0 ) {
			c = n < vn ? -1 : n > vn;
		}
		if ( c == 0 ) {
			return 1;
		}
		if ( c < 0 ) {
			hi = mid - 1;
		} else {
			lo = mid + 1;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* files and paths                                                                                                    */
/* ------------------------------------------------------------------------------------------------------------------ */

static int is_sep( char c )
{
#ifdef _WIN32
	return c == '/' || c == '\\';
#else
	return c == '/';
#endif
}

static int is_file( const char *path )
{
	struct stat st;

	return stat( path, &st ) == 0 && ( st.st_mode & S_IFMT ) == S_IFREG;
}

static int is_dir( const char *path )
{
	struct stat st;

	return stat( path, &st ) == 0 && ( st.st_mode & S_IFMT ) == S_IFDIR;
}

static char *path_join( const char *a, const char *b )
{
	size_t n = strlen( a );

	if ( n && is_sep( a[n - 1] ) ) {
		return fmt( "%s%s", a, b );
	}
	return fmt( "%s%c%s", a, PATH_SEP, b );
}

static char *path_dirname( const char *path )
{
	size_t n = strlen( path );

	while ( n && !is_sep( path[n - 1] ) ) {
		n--;
	}
	if ( n == 0 ) {
		return xstrdup( "." );
	}
	if ( n > 1 && !( n == 3 && path[1] == ':' ) ) {
		n--;
	}
	return xstrndup( path, n );
}

static const char *path_basename( const char *path )
{
	const char *p = path + strlen( path );

	while ( p > path && !is_sep( p[-1] ) ) {
		p--;
	}
	return p;
}

/* a complete path without . and .. and doubled slashes */
static char *abs_path( const char *path )
{
#ifdef _WIN32
	char buf[4096];

	if ( !_fullpath( buf, path, sizeof( buf ) ) ) {
		die( "bad path %s", path );
	}
	return xstrdup( buf );
#else
	char cwd[4096];
	char *full, **comp, *p, *start;
	buf_t out = { 0 };
	int n = 0, i;

	if ( path[0] == '/' ) {
		full = xstrdup( path );
	} else {
		if ( !getcwd( cwd, sizeof( cwd ) ) ) {
			die( "cannot find the current folder" );
		}
		full = fmt( "%s/%s", cwd, path );
	}
	comp = (char **)xmalloc( ( strlen( full ) / 2 + 2 ) * sizeof( char * ) );
	for ( p = start = full; ; p++ ) {
		if ( *p == '/' || *p == '\0' ) {
			int last = *p == '\0';

			*p = '\0';
			if ( !strcmp( start, ".." ) ) {
				if ( n ) {
					n--;
				}
			} else if ( start[0] && strcmp( start, "." ) ) {
				comp[n++] = start;
			}
			if ( last ) {
				break;
			}
			start = p + 1;
		}
	}
	if ( n == 0 ) {
		buf_str( &out, "/" );
	}
	for ( i = 0; i < n; i++ ) {
		buf_str( &out, "/" );
		buf_str( &out, comp[i] );
	}
	return out.p;
#endif
}

/* the parts of a complete path */
static int split_path( const char *path, char ***parts )
{
	char **v = (char **)xmalloc( ( strlen( path ) / 2 + 2 ) * sizeof( char * ) );
	const char *p = path, *start;
	int n = 0;

	while ( *p ) {
		while ( *p && is_sep( *p ) ) {
			p++;
		}
		start = p;
		while ( *p && !is_sep( *p ) ) {
			p++;
		}
		if ( p > start ) {
			v[n++] = xstrndup( start, p - start );
		}
	}
	*parts = v;
	return n;
}

static int same_part( const char *a, const char *b )
{
#ifdef _WIN32
	return _stricmp( a, b ) == 0;
#else
	return strcmp( a, b ) == 0;
#endif
}

/* 'path' as seen from the folder 'start', written with /; NULL when there is no way (another drive) */
static char *rel_path( const char *path, const char *start )
{
	char **pp, **sp;
	int pn = split_path( path, &pp ), sn = split_path( start, &sp );
	int common = 0, i;
	buf_t out = { 0 };

#ifdef _WIN32
	if ( pn == 0 || sn == 0 || !same_part( pp[0], sp[0] ) ) {
		return NULL;
	}
#endif
	while ( common < pn && common < sn && same_part( pp[common], sp[common] ) ) {
		common++;
	}
	for ( i = common; i < sn; i++ ) {
		buf_str( &out, out.n ? "/.." : ".." );
	}
	for ( i = common; i < pn; i++ ) {
		if ( out.n ) {
			buf_str( &out, "/" );
		}
		buf_str( &out, pp[i] );
	}
	if ( out.n == 0 ) {
		buf_str( &out, "." );
	}
	return out.p;
}

static char *read_file( const char *path, size_t *size )
{
	FILE *f = fopen( path, "rb" );
	char *data;
	size_t n, cap = 1 << 16;

	if ( !f ) {
		die( "cannot read %s", path );
	}
	data = (char *)xmalloc( cap + 1 );
	for ( n = 0; ; ) {
		size_t got = fread( data + n, 1, cap - n, f );

		n += got;
		if ( n < cap ) {
			break;
		}
		cap *= 2;
		data = (char *)xrealloc( data, cap + 1 );
	}
	fclose( f );
	data[n] = '\0';
	*size = n;
	return data;
}

static void write_file( const char *path, const char *data, size_t size )
{
	FILE *f = fopen( path, "wb" );

	if ( !f || fwrite( data, 1, size, f ) != size || fclose( f ) != 0 ) {
		die( "cannot write %s", path );
	}
}

static void copy_file( const char *from, const char *to )
{
	size_t size;
	char *data = read_file( from, &size );

	write_file( to, data, size );
	free( data );
}

static int make_dir( const char *path )
{
#ifdef _WIN32
	return _mkdir( path ) == 0;
#else
	return mkdir( path, 0777 ) == 0;
#endif
}

static void make_dirs( const char *path )
{
	if ( is_dir( path ) ) {
		return;
	}
	if ( !is_sep( path[strlen( path ) - 1] ) ) {
		char *parent = path_dirname( path );

		if ( strcmp( parent, path ) ) {
			make_dirs( parent );
		}
	}
	if ( !make_dir( path ) && !is_dir( path ) ) {
		die( "cannot create the folder %s", path );
	}
}

/* the names in a folder, without . and .. */
static char **list_dir( const char *path, int *count )
{
	char **names = NULL;
	int n = 0, cap = 0;
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	char *pattern = path_join( path, "*" );
	HANDLE h = FindFirstFileA( pattern, &fd );

	if ( h != INVALID_HANDLE_VALUE ) {
		do {
			const char *name = fd.cFileName;
#else
	DIR *d = opendir( path );
	struct dirent *e;

	if ( d ) {
		while ( ( e = readdir( d ) ) != NULL ) {
			const char *name = e->d_name;
#endif
			if ( strcmp( name, "." ) && strcmp( name, ".." ) ) {
				if ( n == cap ) {
					cap = cap ? cap * 2 : 16;
					names = (char **)xrealloc( names, cap * sizeof( char * ) );
				}
				names[n++] = xstrdup( name );
			}
#ifdef _WIN32
		} while ( FindNextFileA( h, &fd ) );
		FindClose( h );
	}
#else
		}
		closedir( d );
	}
#endif
	*count = n;
	return names;
}

/* 'skip' is a folder inside the one being copied (the folder the copy goes to) */
static void copy_tree( const char *from, const char *to, const char *skip, int depth )
{
	char **names;
	int n, i;

	if ( !make_dir( to ) && !is_dir( to ) ) {
		die( "cannot create the folder %s", to );
	}
	if ( depth > 32 ) {
		return;
	}
	names = list_dir( from, &n );
	for ( i = 0; i < n; i++ ) {
		char *a = path_join( from, names[i] ), *b = path_join( to, names[i] );

		if ( skip && same_part( a, skip ) ) {
			continue;
		}
		if ( is_dir( a ) ) {
			copy_tree( a, b, skip, depth + 1 );
		} else if ( is_file( a ) ) {
			copy_file( a, b );
		}
	}
}

static void remove_tree( const char *path )
{
#ifdef _WIN32
	DWORD attr = GetFileAttributesA( path );

	if ( attr == INVALID_FILE_ATTRIBUTES ) {
		return;
	}
	if ( attr & FILE_ATTRIBUTE_DIRECTORY ) {
		int n, i;
		char **names = list_dir( path, &n );

		if ( !( attr & FILE_ATTRIBUTE_REPARSE_POINT ) ) {
			for ( i = 0; i < n; i++ ) {
				remove_tree( path_join( path, names[i] ) );
			}
		}
		RemoveDirectoryA( path );
	} else {
		SetFileAttributesA( path, FILE_ATTRIBUTE_NORMAL );
		DeleteFileA( path );
	}
#else
	struct stat st;

	if ( lstat( path, &st ) != 0 ) {
		return;
	}
	if ( ( st.st_mode & S_IFMT ) == S_IFDIR ) {
		int n, i;
		char **names = list_dir( path, &n );

		for ( i = 0; i < n; i++ ) {
			remove_tree( path_join( path, names[i] ) );
		}
		rmdir( path );
	} else {
		unlink( path );
	}
#endif
}

/* a new empty folder in 'parent', NULL when there is none; it is removed when the program ends */
static char *try_temp_dir( const char *parent, const char *prefix )
{
	static unsigned counter;
	int tries;

	for ( tries = 0; tries < 1000; tries++ ) {
#ifdef _WIN32
		unsigned id = (unsigned)GetCurrentProcessId();
#else
		unsigned id = (unsigned)getpid();
#endif
		char *path = fmt( "%s%c%s%x%x%x", parent, PATH_SEP, prefix, id, (unsigned)time( NULL ), counter++ );

		if ( make_dir( path ) ) {
			if ( num_remove == (int)( sizeof( remove_list ) / sizeof( remove_list[0] ) ) ) {
				die( "too many temporary folders" );
			}
			remove_list[num_remove++] = path;
			return path;
		}
	}
	return NULL;
}

static char *make_temp_dir( const char *parent, const char *prefix )
{
	char *path = try_temp_dir( parent, prefix );

	if ( !path ) {
		die( "cannot create a temporary folder in %s", parent );
	}
	return path;
}

static char *system_temp_dir( void )
{
#ifdef _WIN32
	char buf[MAX_PATH + 2];
	DWORD n = GetTempPathA( sizeof( buf ), buf );

	if ( n == 0 || n >= sizeof( buf ) ) {
		return xstrdup( "." );
	}
	while ( n > 3 && is_sep( buf[n - 1] ) ) {
		buf[--n] = '\0';
	}
	return xstrdup( buf );
#else
	const char *t = getenv( "TMPDIR" );

	return xstrdup( t && t[0] && is_dir( t ) ? t : "/tmp" );
#endif
}

/* the first of the names found in the folder of the PATH (the current folder first on Windows) */
static char *find_in_path( const char *name )
{
	const char *path = getenv( "PATH" ), *p;
	char *candidate;

#ifdef _WIN32
	candidate = path_join( ".", name );
	if ( is_file( candidate ) ) {
		return abs_path( candidate );
	}
#endif
	if ( !path ) {
		return NULL;
	}
	for ( p = path; ; ) {
		const char *end = strchr( p, PATH_LIST_SEP );
		size_t n = end ? (size_t)( end - p ) : strlen( p );

		if ( n >= 2 && p[0] == '"' && p[n - 1] == '"' ) {
			p++;
			n -= 2;
		}
		if ( n ) {
			char *dir = xstrndup( p, n );

			candidate = path_join( dir, name );
#ifndef _WIN32
			if ( is_file( candidate ) && access( candidate, X_OK ) == 0 ) {
#else
			if ( is_file( candidate ) ) {
#endif
				return abs_path( candidate );
			}
		}
		if ( !end ) {
			break;
		}
		p = end + 1;
	}
	return NULL;
}

/* the place of this program */
static char *exe_folder( const char *argv0 )
{
	char *path = NULL;

#if defined( _WIN32 )
	char buf[4096];
	DWORD n = GetModuleFileNameA( NULL, buf, sizeof( buf ) );

	if ( n && n < sizeof( buf ) ) {
		path = xstrdup( buf );
	}
#elif defined( __APPLE__ )
	char buf[4096];
	uint32_t size = sizeof( buf );

	if ( _NSGetExecutablePath( buf, &size ) == 0 ) {
		path = abs_path( buf );
	}
#else
	char buf[4096];
	ssize_t n = readlink( "/proc/self/exe", buf, sizeof( buf ) - 1 );

	if ( n > 0 ) {
		buf[n] = '\0';
		path = xstrdup( buf );
	}
#endif
	if ( !path ) {
		if ( strchr( argv0, '/' ) || strchr( argv0, PATH_SEP ) ) {
			path = abs_path( argv0 );
		} else {
			path = find_in_path( argv0 );
		}
	}
	return path ? path_dirname( path ) : NULL;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* running the tools                                                                                                  */
/* ------------------------------------------------------------------------------------------------------------------ */

#ifdef _WIN32
/* one argument of a command line the way CreateProcess and the C runtime read it */
static void quote_arg( buf_t *b, const char *a )
{
	if ( *a && !strpbrk( a, " \t\n\v\"" ) ) {
		buf_str( b, a );
		return;
	}
	buf_str( b, "\"" );
	for ( ; ; a++ ) {
		int backslashes = 0;

		while ( *a == '\\' ) {
			a++;
			backslashes++;
		}
		if ( *a == '\0' ) {
			while ( backslashes-- > 0 ) {
				buf_str( b, "\\\\" );
			}
			break;
		}
		if ( *a == '"' ) {
			while ( backslashes-- > 0 ) {
				buf_str( b, "\\\\" );
			}
			buf_str( b, "\\\"" );
		} else {
			while ( backslashes-- > 0 ) {
				buf_str( b, "\\" );
			}
			buf_add( b, a, 1 );
		}
	}
	buf_str( b, "\"" );
}
#endif

/* runs a program with the arguments (the last one is NULL) in a folder; returns what the program returned, -1 if it did not start */
static int run_program( char **argv, const char *cwd )
{
#ifdef _WIN32
	buf_t line = { 0 };
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	DWORD code = 0;
	int i;

	for ( i = 0; argv[i]; i++ ) {
		if ( i ) {
			buf_str( &line, " " );
		}
		quote_arg( &line, argv[i] );
	}
	memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );
	if ( !CreateProcessA( NULL, line.p, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi ) ) {
		fprintf( stderr, "odf-build: cannot start %s (error %lu)\n", argv[0], (unsigned long)GetLastError() );
		return -1;
	}
	WaitForSingleObject( pi.hProcess, INFINITE );
	GetExitCodeProcess( pi.hProcess, &code );
	CloseHandle( pi.hProcess );
	CloseHandle( pi.hThread );
	return (int)code;
#else
	int status;
	pid_t pid = fork();

	if ( pid < 0 ) {
		return -1;
	}
	if ( pid == 0 ) {
		if ( chdir( cwd ) != 0 ) {
			fprintf( stderr, "odf-build: cannot enter %s\n", cwd );
			_exit( 127 );
		}
		execv( argv[0], argv );
		fprintf( stderr, "odf-build: cannot start %s\n", argv[0] );
		_exit( 127 );
	}
	if ( waitpid( pid, &status, 0 ) < 0 ) {
		return -1;
	}
	if ( WIFEXITED( status ) ) {
		return WEXITSTATUS( status );
	}
	return -1;
#endif
}

static int verbose;

static void run( char **argv, const char *cwd )
{
	int rc;

	if ( verbose ) {
		int i;

		for ( i = 0; argv[i]; i++ ) {
			printf( "%s%s", i ? " " : "", argv[i] );
		}
		printf( "\n" );
	}
	fflush( stdout );
	rc = run_program( argv, cwd );
	if ( rc != 0 ) {
		die( "%s failed (%d)", path_basename( argv[0] ), rc );
	}
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* the mending of the assembly                                                                                        */
/* ------------------------------------------------------------------------------------------------------------------ */

typedef struct {
	const char	*s;
	size_t		n;
} line_t;

/* the lines of a text (what follows the last newline is a line too, maybe an empty one) */
static line_t *split_lines( const char *text, size_t len, size_t *count )
{
	size_t n = 1, i, k = 0, start = 0;
	line_t *lines;

	for ( i = 0; i < len; i++ ) {
		if ( text[i] == '\n' ) {
			n++;
		}
	}
	lines = (line_t *)xmalloc( n * sizeof( line_t ) );
	for ( i = 0; i <= len; i++ ) {
		if ( i == len || text[i] == '\n' ) {
			lines[k].s = text + start;
			lines[k].n = i - start;
			k++;
			start = i + 1;
		}
	}
	*count = n;
	return lines;
}

static int is_space( char c )
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static void strip( const char **s, size_t *n )
{
	while ( *n && is_space( (*s)[0] ) ) {
		( *s )++;
		( *n )--;
	}
	while ( *n && is_space( (*s)[*n - 1] ) ) {
		( *n )--;
	}
}

static int starts_with( const char *s, size_t n, const char *prefix )
{
	size_t pn = strlen( prefix );

	return n >= pn && memcmp( s, prefix, pn ) == 0;
}

/* the word number 'index' (from 0) of a line, the words are separated by white space */
static int nth_word( const char *s, size_t n, int index, const char **word, size_t *wn )
{
	size_t i = 0;

	for ( ; ; index-- ) {
		while ( i < n && is_space( s[i] ) ) {
			i++;
		}
		if ( i == n ) {
			return 0;
		}
		*word = s + i;
		while ( i < n && !is_space( s[i] ) ) {
			i++;
		}
		*wn = (size_t)( s + i - *word );
		if ( index == 0 ) {
			return 1;
		}
	}
}

enum { SEG_NONE = -1, SEG_CODE, SEG_DATA, SEG_LIT, SEG_BSS };

static int segment_of( const char *t, size_t n )
{
	static const char *const names[] = { "code", "data", "lit", "bss" };
	int i;

	for ( i = 0; i < 4; i++ ) {
		if ( strlen( names[i] ) == n && memcmp( names[i], t, n ) == 0 ) {
			return i;
		}
	}
	return SEG_NONE;
}

/* Changes the 'lit' segments that hold addresses of data, not code, to 'data' (see fix_assembly). */
static void move_data_tables( buf_t *out, const char *text, size_t len )
{
	size_t count, i;
	line_t *lines = split_lines( text, len, &count );
	set_t code_labels = { 0 };
	char *to_data = (char *)xmalloc( count + 1 );
	int segment = SEG_NONE;
	long start = -1;
	int data_address = 0;

	memset( to_data, 0, count + 1 );
	for ( i = 0; i < count; i++ ) {
		const char *t = lines[i].s, *w;
		size_t tn = lines[i].n, wn;
		int seg;

		strip( &t, &tn );
		seg = segment_of( t, tn );
		if ( seg != SEG_NONE ) {
			segment = seg;
		} else if ( starts_with( t, tn, "proc " ) ) {
			if ( nth_word( t, tn, 1, &w, &wn ) ) {
				set_add( &code_labels, w, wn );
			}
		} else if ( segment == SEG_CODE && starts_with( t, tn, "LABELV " ) ) {
			if ( nth_word( t, tn, 1, &w, &wn ) ) {
				set_add( &code_labels, w, wn );
			}
		}
	}
	/* one more step after the last line stands for the end of the last segment */
	for ( i = 0; i <= count; i++ ) {
		const char *t = NULL, *w;
		size_t tn = 0, wn;
		int seg = SEG_CODE;

		if ( i < count ) {
			t = lines[i].s;
			tn = lines[i].n;
			strip( &t, &tn );
			seg = segment_of( t, tn );
		}
		if ( seg != SEG_NONE ) {
			if ( start >= 0 && data_address ) {
				to_data[start] = 1;
			}
			start = seg == SEG_LIT ? (long)i : -1;
			data_address = 0;
		} else if ( start >= 0 && starts_with( t, tn, "address " ) ) {
			if ( nth_word( t, tn, 1, &w, &wn ) ) {
				size_t k = 0;

				while ( k < wn && w[k] != '+' && w[k] != '-' ) {
					k++;
				}
				if ( !set_has( &code_labels, w, k ) ) {
					data_address = 1;
				}
			}
		}
	}
	buf_add( out, "", 0 );
	for ( i = 0; i < count; i++ ) {
		if ( i ) {
			buf_str( out, "\n" );
		}
		if ( to_data[i] ) {
			const char *s = lines[i].s;
			size_t n = lines[i].n, k = 0, from = 0;

			for ( ; k + 3 <= n; k++ ) {
				if ( memcmp( s + k, "lit", 3 ) == 0 ) {
					buf_add( out, s + from, k - from );
					buf_str( out, "data" );
					from = k + 3;
					k += 2;
				}
			}
			buf_add( out, s + from, n - from );
		} else {
			buf_add( out, lines[i].s, lines[i].n );
		}
	}
}

static int word_char( char c )
{
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '$' || c == '.';
}

/* collects the word after 'prefix' of the lines that start with it */
static void collect_names( set_t *set, const line_t *lines, size_t count, const char *prefix )
{
	size_t i, pn = strlen( prefix );

	for ( i = 0; i < count; i++ ) {
		size_t k = pn;

		if ( !starts_with( lines[i].s, lines[i].n, prefix ) ) {
			continue;
		}
		while ( k < lines[i].n && !is_space( lines[i].s[k] ) ) {
			k++;
		}
		if ( k > pn ) {
			set_add( set, lines[i].s + pn, k - pn );
		}
	}
}

/*
Two things q3lcc and q3asm get wrong, mended in the assembly of a file:

- q3asm knows only the symbols that begin with $ as belonging to one file, so two files with a static function (or
  variable) of the same name collide. The names a file defines without exporting them get a $ in front.
- q3asm does not know the conversion to unsigned short ((unsigned short)x of an unsigned value), q3rcc writes it. It is
  the same as keeping the lower 16 bits.
- q3rcc puts constant data in the segment q3asm takes the jump tables of switch statements from, and every address in
  that segment is taken for a place the program jumps to. A table of strings or other data (static const char *names[])
  would be hundreds of jump targets that are not code, which the VM then has to give up on. Tables of data go to the data
  segment; tables of functions stay.
*/
static void fix_assembly( const char *path )
{
	size_t len, count, i;
	char *text = read_file( path, &len );
	buf_t moved = { 0 }, renamed = { 0 }, result = { 0 };
	set_t exported = { 0 }, defined = { 0 }, rename = { 0 };
	line_t *lines;
	const char *p, *end;

	move_data_tables( &moved, text, len );
	free( text );

	lines = split_lines( moved.p, moved.n, &count );
	collect_names( &exported, lines, count, "export " );
	collect_names( &defined, lines, count, "proc " );
	collect_names( &defined, lines, count, "LABELV " );
	for ( i = 0; i < (size_t)defined.n; i++ ) {
		const char *name = defined.v[i];

		if ( name[0] != '$' && !set_has( &exported, name, strlen( name ) ) ) {
			set_add( &rename, name, strlen( name ) );
		}
	}
	/* every whole word that is one of those names; a word is made of letters, digits, _ $ and . */
	buf_add( &renamed, "", 0 );
	for ( p = moved.p, end = moved.p + moved.n; p < end; ) {
		const char *word = p;

		if ( !word_char( *p ) ) {
			buf_add( &renamed, p, 1 );
			p++;
			continue;
		}
		while ( p < end && word_char( *p ) ) {
			p++;
		}
		if ( rename.n && word[0] != '$' && set_has( &rename, word, (size_t)( p - word ) ) ) {
			buf_str( &renamed, "$" );
		}
		buf_add( &renamed, word, (size_t)( p - word ) );
	}
	/* CVUU2 n  ->  CNSTU4 65535, BANDU4 */
	lines = split_lines( renamed.p, renamed.n, &count );
	buf_add( &result, "", 0 );
	for ( i = 0; i < count; i++ ) {
		const char *s = lines[i].s;
		size_t n = lines[i].n, k = 6;
		const char *cr = "";

		if ( i ) {
			buf_str( &result, "\n" );
		}
		if ( starts_with( s, n, "CVUU2 " ) ) {
			size_t digits = k;

			while ( digits < n && s[digits] >= '0' && s[digits] <= '9' ) {
				digits++;
			}
			if ( digits > k ) {
				if ( digits < n && s[digits] == '\r' ) {
					cr = "\r";
					digits++;
				}
				if ( digits == n ) {
					buf_str( &result, "CNSTU4 65535" );
					buf_str( &result, cr );
					buf_str( &result, "\nBANDU4" );
					buf_str( &result, cr );
					continue;
				}
			}
		}
		buf_add( &result, s, n );
	}
	write_file( path, result.p, result.n );
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* the program                                                                                                        */
/* ------------------------------------------------------------------------------------------------------------------ */

static void usage( FILE *f )
{
	fprintf( f,
		"usage: odf-build (--client | --server | --test) -o plugin.qvm [options] main.c [more.c ...]\n"
		"\n"
		"builds an oDFe plugin; the file with the entry point (odfc_entry.h or odfs_entry.h) has to be the first\n"
		"\n"
		"  --client, --server    a client plugin (odfc.h) or a server plugin (odfs.h)\n"
		"  --test                for the tests of the SDK\n"
		"  -o file               the plugin to write (name.qvm)\n"
		"  -I folder             a folder for #include\n"
		"  -D symbol[=value]     a preprocessor symbol\n"
		"  --tools folder        the folder with q3lcc and q3asm (default: ODF_TOOLS, then the PATH)\n"
		"  --sdk folder          the folder with include/ and sdk/ (default: found from the place of this program; ODF_SDK)\n"
		"  --no-lib              do not add the C library of the SDK (odf_lib.c)\n"
		"  --syscalls file       the .asm of the engine functions (default: the one of the API)\n"
		"  --map file            also write the map file of q3asm (where every function is)\n"
		"  -v                    show the commands\n" );
}

typedef struct {
	const char	*prefix;		/* of the generated files */
	const char	*symbol;		/* preprocessor symbol */
} module_t;

static const module_t modules[] = {
	{ "odfc", "ODF_MODULE_CLIENT" },
	{ "odfs", "ODF_MODULE_SERVER" },
	{ "odft", "ODF_MODULE_TEST" },
};

static char *find_tool( const char *name, const char *folder )
{
	char *candidates[2];
	int i;

	candidates[0] = xstrdup( name );
	candidates[1] = fmt( "%s.exe", name );
	for ( i = 0; i < 2; i++ ) {
		if ( folder ) {
			char *path = path_join( folder, candidates[i] );

			/* q3lcc finds q3cpp and q3rcc from its own name, which has to be written with one kind of slash */
			if ( is_file( path ) ) {
				return abs_path( path );
			}
		} else {
			char *path = find_in_path( candidates[i] );

			if ( path ) {
				return path;
			}
		}
	}
	if ( folder ) {
		die( "%s not found in %s", name, folder );
	}
	die( "%s not found; give the folder of the tools with --tools or ODF_TOOLS", name );
	return NULL;
}

/* the folders of the SDK: sdk/ (libc/, odf_lib.c ...) and include/ */
static void find_sdk( const char *given, const char *argv0, char **sdk_dir, char **include_dir )
{
	char *root;

	if ( given && given[0] ) {
		root = abs_path( given );
		*sdk_dir = path_join( root, "sdk" );
		*include_dir = path_join( root, "include" );
	} else {
		char *here = exe_folder( argv0 );
		char *source_include;

		if ( !here ) {
			die( "cannot find the SDK; give its folder with --sdk or ODF_SDK" );
		}
		/* the program built in the sources (code/plugin_api/sdk), or put in the folder that has include/ and sdk/ */
		source_include = abs_path( path_join( here, "../include" ) );
		if ( is_file( path_join( here, "odf_lib.c" ) ) && is_file( path_join( source_include, "odf_common.h" ) ) ) {
			*sdk_dir = here;
			*include_dir = source_include;
		} else {
			*sdk_dir = path_join( here, "sdk" );
			*include_dir = path_join( here, "include" );
		}
	}
	if ( !is_file( path_join( *sdk_dir, "odf_lib.c" ) ) || !is_file( path_join( *include_dir, "odf_common.h" ) ) ) {
		die( "the SDK is not in %s and %s; give its folder with --sdk or ODF_SDK", *sdk_dir, *include_dir );
	}
}

/* copies of the include folders in a new folder inside 'folder'; returns the folder with the copies in 0, 1 ... */
static char *stage_includes( char **include_dirs, int count, const char *folder )
{
	char *stage = make_temp_dir( folder, ".odfsdk" );
	int i;

	for ( i = 0; i < count; i++ ) {
		char *copy = fmt( "%s%c%d", stage, PATH_SEP, i );

		if ( is_dir( include_dirs[i] ) ) {
			copy_tree( include_dirs[i], copy, stage, 0 );
		} else {
			make_dirs( copy );
		}
	}
	return stage;
}

/* the value of an option: -Ifolder or -I folder */
static const char *short_value( int argc, char **argv, int *i, const char *name )
{
	if ( argv[*i][2] ) {
		return argv[*i] + 2;
	}
	if ( *i + 1 >= argc ) {
		die( "%s needs a value", name );
	}
	return argv[++*i];
}

/* the value of --name value or --name=value; NULL when the argument is another option */
static const char *long_value( int argc, char **argv, int *i, const char *name )
{
	size_t n = strlen( name );

	if ( strcmp( argv[*i], name ) == 0 ) {
		if ( *i + 1 >= argc ) {
			die( "%s needs a value", name );
		}
		return argv[++*i];
	}
	if ( strncmp( argv[*i], name, n ) == 0 && argv[*i][n] == '=' ) {
		return argv[*i] + n + 1;
	}
	return NULL;
}

int main( int argc, char **argv )
{
	int module = -1, no_lib = 0, i, n, num_includes = 0, num_defines = 0, num_sources = 0;
	const char *output = NULL, *tools = getenv( "ODF_TOOLS" ), *sdk = getenv( "ODF_SDK" ), *syscalls_arg = NULL, *map = NULL, *v;
	const char *inputs[MAX_LIST], *define_list[MAX_LIST], *include_arg[MAX_LIST];
	char *include_dirs[MAX_LIST + 3], *sources[MAX_LIST + 1];
	char *lcc, *asm_tool, *syscalls, *sdk_dir, *include_root, *work, *out, *objects[MAX_LIST + 1];
	char *staged_folders[8], *staged_keys[8];
	int num_staged = 0;
	size_t size;

	atexit( cleanup );
	if ( argc < 2 ) {
		usage( stderr );
		return 2;
	}
	for ( i = 1; i < argc; i++ ) {
		const char *a = argv[i];

		if ( !strcmp( a, "-h" ) || !strcmp( a, "--help" ) ) {
			usage( stdout );
			return 0;
		} else if ( !strcmp( a, "--client" ) || !strcmp( a, "--server" ) || !strcmp( a, "--test" ) ) {
			int m = !strcmp( a, "--client" ) ? 0 : !strcmp( a, "--server" ) ? 1 : 2;

			if ( module >= 0 && module != m ) {
				die( "--client, --server and --test exclude each other" );
			}
			module = m;
		} else if ( !strcmp( a, "--no-lib" ) ) {
			no_lib = 1;
		} else if ( !strcmp( a, "-v" ) ) {
			verbose = 1;
		} else if ( !strncmp( a, "-o", 2 ) ) {
			output = short_value( argc, argv, &i, "-o" );
		} else if ( !strncmp( a, "-I", 2 ) ) {
			if ( num_includes == MAX_LIST ) {
				die( "too many -I" );
			}
			include_arg[num_includes++] = short_value( argc, argv, &i, "-I" );
		} else if ( !strncmp( a, "-D", 2 ) ) {
			if ( num_defines == MAX_LIST ) {
				die( "too many -D" );
			}
			define_list[num_defines++] = short_value( argc, argv, &i, "-D" );
		} else if ( ( v = long_value( argc, argv, &i, "--tools" ) ) != NULL ) {
			tools = v;
		} else if ( ( v = long_value( argc, argv, &i, "--sdk" ) ) != NULL ) {
			sdk = v;
		} else if ( ( v = long_value( argc, argv, &i, "--syscalls" ) ) != NULL ) {
			syscalls_arg = v;
		} else if ( ( v = long_value( argc, argv, &i, "--map" ) ) != NULL ) {
			map = v;
		} else if ( a[0] == '-' && a[1] ) {
			fprintf( stderr, "odf-build: unknown option %s\n", a );
			usage( stderr );
			return 2;
		} else {
			if ( num_sources == MAX_LIST ) {
				die( "too many source files" );
			}
			inputs[num_sources++] = a;
		}
	}
	if ( module < 0 ) {
		die( "one of --client, --server, --test is needed" );
	}
	if ( !output ) {
		die( "-o is needed" );
	}
	if ( num_sources == 0 ) {
		die( "no source files" );
	}
	if ( tools && !tools[0] ) {
		tools = NULL;
	}

	lcc = find_tool( "q3lcc", tools );
	asm_tool = find_tool( "q3asm", tools );
	find_sdk( sdk, argv[0], &sdk_dir, &include_root );
	syscalls = abs_path( syscalls_arg ? syscalls_arg : fmt( "%s%c%s_syscalls.asm", include_root, PATH_SEP, modules[module].prefix ) );
	if ( !is_file( syscalls ) ) {
		die( "%s not found", syscalls );
	}

	/* the preprocessor of q3lcc takes include folders relative to the folder of the source file and cannot use complete
	   paths, so every file is compiled from its own folder with the include folders written relative to it */
	n = 0;
	for ( i = 0; i < num_includes; i++ ) {
		include_dirs[n++] = abs_path( include_arg[i] );
	}
	include_dirs[n++] = path_join( sdk_dir, "libc" );
	include_dirs[n++] = sdk_dir;
	include_dirs[n++] = include_root;
	for ( i = 0; i < num_sources; i++ ) {
		sources[i] = abs_path( inputs[i] );
	}
	if ( !no_lib ) {
		sources[num_sources++] = path_join( sdk_dir, "odf_lib.c" );
	}
	for ( i = 0; i < num_sources; i++ ) {
		if ( !is_file( sources[i] ) ) {
			die( "%s not found", sources[i] );
		}
	}

	out = abs_path( output );
	make_dirs( path_dirname( out ) );
	/* the temporary folder of the system, and when that is not usable (TEMP is not set ...) the folder of the plugin */
	work = try_temp_dir( system_temp_dir(), "odfbuild" );
	if ( !work ) {
		work = make_temp_dir( path_dirname( out ), ".odfbuild" );
	}

	for ( i = 0; i < num_sources; i++ ) {
		char *folder = path_dirname( sources[i] ), *base = xstrdup( path_basename( sources[i] ) );
		char *rel[MAX_LIST + 3], *args[MAX_LIST * 2 + 16], *obj, *stem;
		int k, na = 0, bad = 0, m;

		for ( k = 0; k < n; k++ ) {
			rel[k] = rel_path( include_dirs[k], folder );
			if ( !rel[k] ) {
				bad = 1;
			}
		}
		if ( bad ) {
			/* another drive (Windows): the preprocessor cannot reach the include folders, so copies of them are put next
			   to the source for the time of the build */
			char key[3] = { 0 };
			char *stage = NULL;

			key[0] = (char)( folder[0] >= 'A' && folder[0] <= 'Z' ? folder[0] + 32 : folder[0] );
			key[1] = folder[1];
			for ( m = 0; m < num_staged; m++ ) {
				if ( !strcmp( staged_keys[m], key ) ) {
					stage = staged_folders[m];
				}
			}
			if ( !stage ) {
				if ( num_staged == 8 ) {
					die( "too many drives" );
				}
				stage = stage_includes( include_dirs, n, folder );
				staged_keys[num_staged] = xstrdup( key );
				staged_folders[num_staged++] = stage;
			}
			for ( k = 0; k < n; k++ ) {
				rel[k] = rel_path( fmt( "%s%c%d", stage, PATH_SEP, k ), folder );
			}
		}
		stem = xstrdup( base );
		if ( strrchr( stem, '.' ) && strrchr( stem, '.' ) != stem ) {
			*strrchr( stem, '.' ) = '\0';
		}
		obj = fmt( "%s%c%d_%s.asm", work, PATH_SEP, i, stem );

		args[na++] = lcc;
		args[na++] = "-S";
		args[na++] = "-N";
		args[na++] = "-DQ3_VM";
		args[na++] = fmt( "-D%s", modules[module].symbol );
		for ( k = 0; k < num_defines; k++ ) {
			args[na++] = fmt( "-D%s", define_list[k] );
		}
		for ( k = 0; k < n; k++ ) {
			args[na++] = fmt( "-I%s", rel[k] );
		}
		args[na++] = "-o";
		args[na++] = obj;
		args[na++] = base;
		args[na] = NULL;
		run( args, folder );
		fix_assembly( obj );
		objects[i] = obj;
	}

	/* q3asm writes a map file next to the program, so it works in the temporary folder */
	{
		char *args[MAX_LIST + 16];
		int na = 0;
		char *p;

		args[na++] = asm_tool;
		if ( map ) {
			args[na++] = "-m";
		}
		args[na++] = "-o";
		args[na++] = "plugin";
		for ( i = 0; i < num_sources; i++ ) {
			for ( p = objects[i]; *p; p++ ) {
				if ( *p == '\\' ) {
					*p = '/';
				}
			}
			args[na++] = objects[i];
		}
		for ( p = syscalls; *p; p++ ) {
			if ( *p == '\\' ) {
				*p = '/';
			}
		}
		args[na++] = syscalls;
		args[na] = NULL;
		run( args, work );
	}
	{
		char *qvm = path_join( work, "plugin.qvm" ), *mapfile = path_join( work, "plugin.map" );

		if ( !is_file( qvm ) ) {
			die( "q3asm did not write the plugin" );
		}
		copy_file( qvm, out );
		if ( map && is_file( mapfile ) ) {
			copy_file( mapfile, map );
		}
	}
	{
		char *text = read_file( out, &size );

		free( text );
		printf( "%s: %lu bytes\n", output, (unsigned long)size );
	}
	return 0;
}
