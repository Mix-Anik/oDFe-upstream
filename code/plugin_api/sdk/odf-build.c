/*
odf-build: builds an oDFe plugin (a QVM program) from C sources.

    odf-build --client -o plugins/myplugin.qvm main.c other.c
    odf-build --server -o plugins/myplugin.qvm -DDEBUG -Iinclude main.c

The first source file has to include the entry point (odfc_entry.h or odfs_entry.h), the VM wants it first in the program.
The C library of the SDK (odf_lib.c) and the engine functions (odfc_syscalls.asm / odfs_syscalls.asm) are added at the end,
and so is every library of the SDK a source includes the header of (odf_json.h: odf_json.c): see add_libraries.

This one program has everything: the preprocessor, the compiler and the assembler (lcc and q3asm of ioquake3, in
../toolchain) and the files of the SDK (headers, C library). It runs the tools as itself, one process for each step:
"odf-build --run-tool cpp|rcc|asm ...". --sdk (or the environment variable ODF_SDK) names a folder code/plugin_api to use
its files instead of the ones in the program.

Build it with the Makefile of this folder.
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
/* declared here instead of including <mach-o/dyld.h>, which defines macros (SEG_DATA ...) that clash with names of this file */
extern int _NSGetExecutablePath( char *buf, uint32_t *bufsize );
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

/* the file of this program */
static char *exe_path( const char *argv0 )
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
	if ( !path ) {
		die( "cannot find the file of this program" );
	}
	return path;
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

static void run( char **argv, const char *cwd, const char *what )
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
		die( "%s failed (%d)", what, rc );
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

enum { ASMSEG_NONE = -1, ASMSEG_CODE, ASMSEG_DATA, ASMSEG_LIT, ASMSEG_BSS };

static int segment_of( const char *t, size_t n )
{
	static const char *const names[] = { "code", "data", "lit", "bss" };
	int i;

	for ( i = 0; i < 4; i++ ) {
		if ( strlen( names[i] ) == n && memcmp( names[i], t, n ) == 0 ) {
			return i;
		}
	}
	return ASMSEG_NONE;
}

/* Changes the 'lit' segments that hold addresses of data, not code, to 'data' (see fix_assembly). */
static void move_data_tables( buf_t *out, const char *text, size_t len )
{
	size_t count, i;
	line_t *lines = split_lines( text, len, &count );
	set_t code_labels = { 0 };
	char *to_data = (char *)xmalloc( count + 1 );
	int segment = ASMSEG_NONE;
	long start = -1;
	int data_address = 0;

	memset( to_data, 0, count + 1 );
	for ( i = 0; i < count; i++ ) {
		const char *t = lines[i].s, *w;
		size_t tn = lines[i].n, wn;
		int seg;

		strip( &t, &tn );
		seg = segment_of( t, tn );
		if ( seg != ASMSEG_NONE ) {
			segment = seg;
		} else if ( starts_with( t, tn, "proc " ) ) {
			if ( nth_word( t, tn, 1, &w, &wn ) ) {
				set_add( &code_labels, w, wn );
			}
		} else if ( segment == ASMSEG_CODE && starts_with( t, tn, "LABELV " ) ) {
			if ( nth_word( t, tn, 1, &w, &wn ) ) {
				set_add( &code_labels, w, wn );
			}
		}
	}
	/* one more step after the last line stands for the end of the last segment */
	for ( i = 0; i <= count; i++ ) {
		const char *t = NULL, *w;
		size_t tn = 0, wn;
		int seg = ASMSEG_CODE;

		if ( i < count ) {
			t = lines[i].s;
			tn = lines[i].n;
			strip( &t, &tn );
			seg = segment_of( t, tn );
		}
		if ( seg != ASMSEG_NONE ) {
			if ( start >= 0 && data_address ) {
				to_data[start] = 1;
			}
			start = seg == ASMSEG_LIT ? (long)i : -1;
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

/* The main functions of the three tools of the compiler (toolchain/): the preprocessor, the compiler and the assembler.
   odf-build runs them as itself ("odf-build --run-tool cpp ..."), one process for each step as q3lcc did. */
extern int cpp_main( int argc, char **argv );
extern int rcc_main( int argc, char **argv );
extern int asm_main( int argc, char **argv );

/* the files of the SDK, written by mkembed (sdkdata.c) */
typedef struct {
	const char			*path;
	const unsigned char	*data;
	unsigned			size;
} odfEmbedded_t;

extern const odfEmbedded_t odf_embedded[];
extern const int odf_num_embedded;

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
		"  --sdk folder          use the files of the SDK in this folder (code/plugin_api) instead of the ones in this program\n"
		"  --no-lib              do not add the C library of the SDK (odf_lib.c)\n"
		"  --syscalls file       the .asm of the engine functions (default: the one of the API)\n"
		"  --map file            also write the map file of the assembler (where every function is)\n"
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

/* the tools understand / in the paths on every system, and the preprocessor knows nothing else */
static char *with_slashes( char *path )
{
	char *p;

	for ( p = path; *p; p++ ) {
		if ( *p == '\\' ) {
			*p = '/';
		}
	}
	return path;
}

/* An SDK library is a header odf_<name>.h in the SDK folder with an odf_<name>.c beside it (odf_json, odf_http ...). A program that
   includes the header, also through other headers, gets the .c file added to the build. pre is the preprocessed source: its #line
   lines name every file that was read. */
#define MAX_LIBS	16

static void add_libraries( const char *pre, const char *sdk_dir, char **sources, int *num_sources )
{
	size_t size, dir_len;
	char *text = read_file( pre, &size ), *dir = with_slashes( xstrdup( sdk_dir ) ), *p = text;

	dir_len = strlen( dir );
	while ( *p ) {
		char *line_end = strchr( p, '\n' );

		if ( !strncmp( p, "#line ", 6 ) ) {
			char *open = strchr( p, '"' ), *close = open ? strchr( open + 1, '"' ) : NULL;

			if ( open && close && ( !line_end || close < line_end ) ) {
				char *path = (char *)xmalloc( (size_t)( close - open ) );
				const char *name;
				size_t name_len;

				memcpy( path, open + 1, (size_t)( close - open - 1 ) );
				path[close - open - 1] = '\0';
				with_slashes( path );
				name = strlen( path ) > dir_len + 1 ? path + dir_len + 1 : "";
				name_len = strlen( name );
				/* sdk/odf_<name>.h, not in a folder below it */
				if ( !strncmp( path, dir, dir_len ) && path[dir_len] == '/' && !strncmp( name, "odf_", 4 ) && name_len > 6 &&
						!strcmp( name + name_len - 2, ".h" ) && !strchr( name, '/' ) ) {
					char *lib = path_join( sdk_dir, fmt( "%.*s.c", (int)( name_len - 2 ), name ) );
					int k, have = 0;

					if ( is_file( lib ) ) {
						for ( k = 0; k < *num_sources; k++ ) {
							char *a = with_slashes( xstrdup( sources[k] ) ), *b = with_slashes( xstrdup( lib ) );

							have |= !strcmp( a, b );
							free( a );
							free( b );
						}
						if ( !have ) {
							if ( *num_sources >= MAX_LIST + MAX_LIBS ) {
								die( "too many source files" );
							}
							sources[( *num_sources )++] = lib;
						}
					}
				}
				free( path );
			}
		}
		if ( !line_end ) {
			break;
		}
		p = line_end + 1;
	}
	free( dir );
	free( text );
}

int main( int argc, char **argv )
{
	int module = -1, no_lib = 0, i, n, num_includes = 0, num_defines = 0, num_sources = 0;
	const char *output = NULL, *sdk = getenv( "ODF_SDK" ), *syscalls_arg = NULL, *map = NULL, *v;
	const char *inputs[MAX_LIST], *define_list[MAX_LIST], *include_arg[MAX_LIST];
	char *include_dirs[MAX_LIST + 3], *sources[MAX_LIST + MAX_LIBS + 1];
	char *self, *syscalls, *sdk_dir, *include_root, *work, *out, *objects[MAX_LIST + MAX_LIBS + 1];
	size_t size;

	if ( argc >= 3 && !strcmp( argv[1], "--run-tool" ) ) {
		if ( !strcmp( argv[2], "cpp" ) ) {
			return cpp_main( argc - 2, argv + 2 );
		}
		if ( !strcmp( argv[2], "rcc" ) ) {
			return rcc_main( argc - 2, argv + 2 );
		}
		if ( !strcmp( argv[2], "asm" ) ) {
			return asm_main( argc - 2, argv + 2 );
		}
		die( "no tool %s", argv[2] );
	}

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

	self = exe_path( argv[0] );
	out = abs_path( output );
	make_dirs( path_dirname( out ) );
	/* the temporary folder of the system, and when that is not usable (TEMP is not set ...) the folder of the plugin */
	work = try_temp_dir( system_temp_dir(), "odfbuild" );
	if ( !work ) {
		work = make_temp_dir( path_dirname( out ), ".odfbuild" );
	}

	/* the files of the SDK: the ones in this program, or the ones in the folder given */
	if ( sdk && sdk[0] ) {
		char *root = abs_path( sdk );

		sdk_dir = path_join( root, "sdk" );
		include_root = path_join( root, "include" );
		if ( !is_file( path_join( sdk_dir, "odf_lib.c" ) ) || !is_file( path_join( include_root, "odf_common.h" ) ) ) {
			die( "the SDK is not in %s and %s; --sdk and ODF_SDK name the folder code/plugin_api", sdk_dir, include_root );
		}
	} else {
		char *files = path_join( work, "sdk" );

		for ( i = 0; i < odf_num_embedded; i++ ) {
			char *path = path_join( files, odf_embedded[i].path );

			make_dirs( path_dirname( path ) );
			write_file( path, (const char *)odf_embedded[i].data, odf_embedded[i].size );
		}
		sdk_dir = path_join( files, "sdk" );
		include_root = path_join( files, "include" );
	}
	syscalls = abs_path( syscalls_arg ? syscalls_arg : fmt( "%s%c%s_syscalls.asm", include_root, PATH_SEP, modules[module].prefix ) );
	if ( !is_file( syscalls ) ) {
		die( "%s not found", syscalls );
	}

	/* the preprocessor takes the include folders as they are written and the file from the folder it is in, so every file is
	   compiled from its own folder */
	n = 0;
	for ( i = 0; i < num_includes; i++ ) {
		include_dirs[n++] = with_slashes( abs_path( include_arg[i] ) );
	}
	include_dirs[n++] = with_slashes( path_join( sdk_dir, "libc" ) );
	include_dirs[n++] = with_slashes( xstrdup( sdk_dir ) );
	include_dirs[n++] = with_slashes( xstrdup( include_root ) );
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

	for ( i = 0; i < num_sources; i++ ) {
		char *folder = path_dirname( sources[i] ), *base = xstrdup( path_basename( sources[i] ) );
		char *args[MAX_LIST * 2 + 24], *pre, *obj, *stem;
		int k, na = 0;

		stem = xstrdup( base );
		if ( strrchr( stem, '.' ) && strrchr( stem, '.' ) != stem ) {
			*strrchr( stem, '.' ) = '\0';
		}
		pre = fmt( "%s%c%d_%s.i", work, PATH_SEP, i, stem );
		obj = fmt( "%s%c%d_%s.asm", work, PATH_SEP, i, stem );

		/* the preprocessor */
		args[na++] = self;
		args[na++] = "--run-tool";
		args[na++] = "cpp";
		args[na++] = "-D__STDC__=1";
		args[na++] = "-D__STRICT_ANSI__";
		args[na++] = "-D__signed__=signed";
		args[na++] = "-DQ3_VM";
		args[na++] = "-D__LCC__";
		args[na++] = "-DQ3_VM";
		args[na++] = fmt( "-D%s", modules[module].symbol );
		for ( k = 0; k < num_defines; k++ ) {
			args[na++] = fmt( "-D%s", define_list[k] );
		}
		for ( k = 0; k < n; k++ ) {
			args[na++] = fmt( "-I%s", include_dirs[k] );
		}
		args[na++] = base;
		args[na++] = pre;
		args[na] = NULL;
		run( args, folder, "the preprocessor" );
		add_libraries( pre, sdk_dir, sources, &num_sources );

		/* the compiler */
		na = 0;
		args[na++] = self;
		args[na++] = "--run-tool";
		args[na++] = "rcc";
		args[na++] = "-target=bytecode";
		args[na++] = pre;
		args[na++] = obj;
		args[na] = NULL;
		run( args, folder, "the compiler" );
		remove_tree( pre );

		fix_assembly( obj );
		objects[i] = obj;
	}

	/* the assembler writes a map file next to the program, so it works in the temporary folder */
	{
		char *args[MAX_LIST + MAX_LIBS + 16];
		int na = 0;

		args[na++] = self;
		args[na++] = "--run-tool";
		args[na++] = "asm";
		if ( map ) {
			args[na++] = "-m";
		}
		args[na++] = "-o";
		args[na++] = "plugin";
		for ( i = 0; i < num_sources; i++ ) {
			args[na++] = with_slashes( objects[i] );
		}
		args[na++] = with_slashes( syscalls );
		args[na] = NULL;
		run( args, work, "the assembler" );
	}
	{
		char *qvm = path_join( work, "plugin.qvm" ), *mapfile = path_join( work, "plugin.map" );

		if ( !is_file( qvm ) ) {
			die( "the assembler did not write the plugin" );
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
