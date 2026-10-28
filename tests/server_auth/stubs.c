/*
The little bit of the engine that sv_plugin.c and sv_auth.c call, for the native test.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "server.h"

serverStatic_t svs;
server_t sv;

// ---- output the test looks at ----

#include "stubs.h"

testCmd_t testCmds[MAX_TEST_CMDS];
int numTestCmds;
char testPrints[MAX_TEST_PRINTS][512];
int numTestPrints;

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

#define MAX_STUB_CVARS 32
static cvar_t stubCvars[MAX_STUB_CVARS];
static char stubCvarNames[MAX_STUB_CVARS][64];
static char stubCvarValues[MAX_STUB_CVARS][256];
static int numStubCvars;

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

void Test_SetCvar( const char *name, const char *value )
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
}

cvar_t *Cvar_Get( const char *name, const char *value, int flags )
{
	(void)flags;
	if ( !FindCvar( name ) ) {
		Test_SetCvar( name, value );
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

void Cmd_TokenizeString( const char *text )
{
	char buf[1024];
	char *tok;

	argcStore = 0;
	snprintf( buf, sizeof( buf ), "%s", text );
	for ( tok = strtok( buf, " " ); tok && argcStore < 8; tok = strtok( NULL, " " ) ) {
		snprintf( argvStore[argcStore++], sizeof( argvStore[0] ), "%s", tok );
	}
}

int Cmd_Argc( void ) { return argcStore; }
const char *Cmd_Argv( int i ) { return i < argcStore ? argvStore[i] : ""; }

// ---- system ----

char *FS_BuildOSPath( const char *base, const char *game, const char *qpath )
{
	static char path[2][512];
	static int toggle;

	toggle ^= 1;
	snprintf( path[toggle], sizeof( path[0] ), "%s/%s/%s", base, game, qpath );
	return path[toggle];
}

FILE *Sys_FOpen( const char *ospath, const char *mode ) { return fopen( ospath, mode ); }

int Sys_Milliseconds( void ) { return 0; }

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

#ifdef _WIN32
void *Sys_LoadLibrary( const char *name ) { return (void *)LoadLibraryA( name ); }
void *Sys_LoadFunction( void *handle, const char *name ) { return (void *)GetProcAddress( (HMODULE)handle, name ); }
void Sys_UnloadLibrary( void *handle ) { if ( handle ) FreeLibrary( (HMODULE)handle ); }
#else
void *Sys_LoadLibrary( const char *name ) { return dlopen( name, RTLD_NOW ); }
void *Sys_LoadFunction( void *handle, const char *name ) { return dlsym( handle, name ); }
void Sys_UnloadLibrary( void *handle ) { if ( handle ) dlclose( handle ); }
#endif
