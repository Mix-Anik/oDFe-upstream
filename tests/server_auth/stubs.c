/*
The little bit of the engine that sv_plugin.c and sv_auth.c call, for the native test.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#endif

#include "server.h"
#include "sv_http.h"

serverStatic_t svs;
server_t sv;

// ---- output the test looks at ----

#include "stubs.h"

testCmd_t testCmds[MAX_TEST_CMDS];
int numTestCmds;
char testPrints[MAX_TEST_PRINTS][512];
int numTestPrints;
testHttp_t testHttp[8];
int numTestHttp;

// ---- printing ----

void QDECL Com_Printf( const char *fmt, ... )
{
	va_list ap;
	char buf[1024];

	va_start( ap, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );
	if ( numTestPrints < MAX_TEST_PRINTS ) {
		snprintf( testPrints[numTestPrints++], sizeof( testPrints[0] ), "%s", buf );
	}
	printf( "  | %s", buf );
	if ( !*buf || buf[strlen( buf ) - 1] != '\n' ) {
		printf( "\n" );
	}
}

void QDECL Com_DPrintf( const char *fmt, ... ) { (void)fmt; }

void QDECL Com_Error( errorParm_t code, const char *fmt, ... )
{
	va_list ap;

	(void)code;
	va_start( ap, fmt );
	vfprintf( stderr, fmt, ap );
	va_end( ap );
	exit( 3 );
}

void QDECL SV_SendServerCommand( client_t *cl, const char *fmt, ... )
{
	va_list ap;

	if ( numTestCmds >= MAX_TEST_CMDS ) {
		return;
	}
	va_start( ap, fmt );
	vsnprintf( testCmds[numTestCmds].text, sizeof( testCmds[0].text ), fmt, ap );
	va_end( ap );
	testCmds[numTestCmds].client = cl ? (int)( cl - svs.clients ) : -1;
	numTestCmds++;
}

// ---- cvars ----

#define MAX_STUB_CVARS 64
static cvar_t stubCvars[MAX_STUB_CVARS];
static char stubCvarNames[MAX_STUB_CVARS][64];
static char stubCvarValues[MAX_STUB_CVARS][256];
static int numStubCvars;
int stCvarFlagsLast;

static cvar_t *FindCvar( const char *name )
{
	int i;

	for ( i = 0; i < numStubCvars; i++ ) {
		if ( !strcmp( stubCvarNames[i], name ) ) {
			return &stubCvars[i];
		}
	}
	return NULL;
}

static void SetCvar( const char *name, const char *value, int flags )
{
	cvar_t *c = FindCvar( name );

	if ( !c ) {
		c = &stubCvars[numStubCvars];
		snprintf( stubCvarNames[numStubCvars], 64, "%s", name );
		c->name = stubCvarNames[numStubCvars];
		c->string = stubCvarValues[numStubCvars];
		numStubCvars++;
	}
	snprintf( (char *)c->string, 256, "%s", value );
	c->integer = atoi( value );
	c->flags = flags;
}

// what the operator's config files do before the engine knows a cvar: it exists, flagged as made by the user
void Test_SetCvar( const char *name, const char *value, int flags )
{
	SetCvar( name, value, flags | CVAR_USER_CREATED );
}

unsigned Cvar_Flags( const char *name )
{
	cvar_t *c = FindCvar( name );

	return c ? (unsigned)c->flags : CVAR_NONEXISTENT;
}

cvar_t *Cvar_Get( const char *name, const char *value, int flags )
{
	cvar_t *c = FindCvar( name );

	stCvarFlagsLast = flags;
	if ( !c ) {
		SetCvar( name, value, flags );
	} else {
		c->flags = ( c->flags & ~CVAR_USER_CREATED ) | flags;		// like the engine: the code owns it now, the value stays
	}
	return FindCvar( name );
}

void Cvar_SetDescription( cvar_t *var, const char *desc ) { (void)var; (void)desc; }

const char *Cvar_VariableString( const char *name )
{
	cvar_t *c = FindCvar( name );

	return c ? c->string : "";
}

void Cvar_VariableStringBuffer( const char *name, char *buf, int size )
{
	snprintf( buf, size, "%s", Cvar_VariableString( name ) );
}

int Cvar_VariableIntegerValue( const char *name )
{
	cvar_t *c = FindCvar( name );

	return c ? c->integer : 0;
}

// ---- commands ----

static char argvStore[8][512];
static int argcStore;

void Cmd_AddCommand( const char *name, xcommand_t fn ) { (void)name; (void)fn; }

/* words are separated by spaces; a word in double quotes can contain spaces (as in the engine's tokenizer) */
void Cmd_TokenizeString( const char *text )
{
	argcStore = 0;
	while ( *text && argcStore < 8 ) {
		char *out = argvStore[argcStore];
		size_t n = 0;

		while ( *text == ' ' ) {
			text++;
		}
		if ( !*text ) {
			break;
		}
		if ( *text == '"' ) {
			text++;
			while ( *text && *text != '"' && n < sizeof( argvStore[0] ) - 1 ) {
				out[n++] = *text++;
			}
			if ( *text == '"' ) {
				text++;
			}
		} else {
			while ( *text && *text != ' ' && n < sizeof( argvStore[0] ) - 1 ) {
				out[n++] = *text++;
			}
		}
		out[n] = '\0';
		argcStore++;
	}
}

int Cmd_Argc( void ) { return argcStore; }
const char *Cmd_Argv( int i ) { return i < argcStore ? argvStore[i] : ""; }

// ---- system ----

char *FS_BuildOSPath( const char *base, const char *game, const char *qpath )
{
	static char path[4][512];
	static int toggle;

	toggle = ( toggle + 1 ) & 3;
	snprintf( path[toggle], sizeof( path[0] ), "%s/%s/%s", base, game, qpath );
	return path[toggle];
}

FILE *Sys_FOpen( const char *ospath, const char *mode ) { return fopen( ospath, mode ); }

qboolean Sys_Mkdir( const char *path )
{
	char dir[600];
	size_t n;

	snprintf( dir, sizeof( dir ), "%s", path );
	n = strlen( dir );
	while ( n > 0 && ( dir[n - 1] == '/' || dir[n - 1] == '\\' ) ) {
		dir[--n] = '\0';
	}
#ifdef _WIN32
	return _mkdir( dir ) == 0;
#else
	return mkdir( dir, 0777 ) == 0;
#endif
}

char **Sys_ListFiles( const char *directory, const char *extension, const char *filter, int *numfiles, int subdirs )
{
	char **list;
	int count = 0;

	(void)extension; (void)filter; (void)subdirs;
	*numfiles = 0;
	list = (char **)calloc( 128, sizeof( char * ) );
#ifdef _WIN32
	{
		char pattern[600];
		struct _finddata_t fd;
		intptr_t h;

		snprintf( pattern, sizeof( pattern ), "%s/*", directory );
		h = _findfirst( pattern, &fd );
		if ( h != -1 ) {
			do {
				if ( !( fd.attrib & _A_SUBDIR ) && count < 127 ) {
					list[count++] = strdup( fd.name );
				}
			} while ( _findnext( h, &fd ) == 0 );
			_findclose( h );
		}
	}
#else
	{
		DIR *d = opendir( directory );
		struct dirent *e;

		if ( d ) {
			while ( ( e = readdir( d ) ) != NULL ) {
				if ( e->d_name[0] != '.' && count < 127 ) {
					list[count++] = strdup( e->d_name );
				}
			}
			closedir( d );
		}
	}
#endif
	if ( !count ) {
		free( list );
		return NULL;
	}
	*numfiles = count;
	return list;
}

void Sys_FreeFileList( char **list )
{
	int i;

	if ( !list ) {
		return;
	}
	for ( i = 0; list[i]; i++ ) {
		free( list[i] );
	}
	free( list );
}

int Sys_Milliseconds( void ) { return 0; }

int64_t Sys_Microseconds( void )
{
#ifdef _WIN32
	LARGE_INTEGER freq, now;

	QueryPerformanceFrequency( &freq );
	QueryPerformanceCounter( &now );
	return now.QuadPart * 1000000 / freq.QuadPart;
#else
	struct timespec ts;

	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

qboolean Sys_RandomBytes( byte *string, int len )
{
	int i;

	for ( i = 0; i < len; i++ ) {
		string[i] = (byte)( rand() >> 3 );
	}
	return qtrue;
}

qboolean NET_IsLocalAddress( const netadr_t *adr ) { return adr->type == NA_LOOPBACK; }
const char *NET_AdrToString( const netadr_t *a ) { (void)a; return "9.9.9.9"; }

// ---- HTTP: nothing goes out, the test sees what was asked and says what came back ----

static int httpNext = 100;
static struct {
	int		id, status;
	void	*owner;
	char	body[64];
	qboolean told;
} httpDone[4];
static int numHttpDone;

qboolean SV_Http_Init( void ) { return qtrue; }
void SV_Http_Shutdown( void ) {}
void SV_Http_Pump( void ) {}

int SV_Http_Start( int method, const char *url, const char *headers, const void *body, int bodyLen, int timeoutMs, void *owner )
{
	testHttp_t *r;

	(void)timeoutMs;
	if ( numTestHttp >= (int)( sizeof( testHttp ) / sizeof( testHttp[0] ) ) ) {
		return 0;
	}
	r = &testHttp[numTestHttp++];
	r->id = httpNext++;
	r->method = method;
	r->owner = owner;
	r->bodyLen = bodyLen;
	snprintf( r->url, sizeof( r->url ), "%s", url );
	snprintf( r->headers, sizeof( r->headers ), "%s", headers ? headers : "" );
	(void)body;
	return r->id;
}

void Test_HttpFinish( int index, int status, const char *body )
{
	if ( numHttpDone < 4 ) {
		httpDone[numHttpDone].id = testHttp[index].id;
		httpDone[numHttpDone].owner = testHttp[index].owner;
		httpDone[numHttpDone].status = status;
		httpDone[numHttpDone].told = qfalse;
		snprintf( httpDone[numHttpDone].body, sizeof( httpDone[0].body ), "%s", body );
		numHttpDone++;
	}
}

qboolean SV_Http_Finished( int *id, int *status, int *bodyLen, void **owner )
{
	int i;

	for ( i = 0; i < numHttpDone; i++ ) {
		if ( !httpDone[i].told ) {
			httpDone[i].told = qtrue;
			*id = httpDone[i].id;
			*status = httpDone[i].status;
			*bodyLen = (int)strlen( httpDone[i].body );
			*owner = httpDone[i].owner;
			return qtrue;
		}
	}
	return qfalse;
}

int SV_Http_Body( int id, int offset, char *out, int outSize )
{
	int i;

	for ( i = 0; i < numHttpDone; i++ ) {
		if ( httpDone[i].id == id ) {
			int len = (int)strlen( httpDone[i].body ) - offset;

			if ( len <= 0 ) {
				return 0;
			}
			if ( len > outSize ) {
				len = outSize;
			}
			memcpy( out, httpDone[i].body + offset, len );
			return len;
		}
	}
	return 0;
}

void SV_Http_Release( int id ) { (void)id; }
void SV_Http_Abort( void *owner ) { (void)owner; }
