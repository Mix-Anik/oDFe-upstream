/*
The little bit of the engine that cl_plugin.c calls, for the native test: cvars, a fake renderer that
counts what it is given, a fake collision trace, and the client state (cl, clc, cls).
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

#include "client.h"
#include "../../code/qcommon/cm_public.h"
#include "stubs.h"

clientActive_t cl;
clientConnection_t clc;
clientStatic_t cls;
vm_t *cgvm;
refexport_t re;
int cl_connectedToCheatServer;

char testPrints[MAX_TEST_PRINTS][1024];
int numTestPrints;

int stPolyCalls, stPolyVerts;
int stBeamCalls, stBeamShader;
float stBeamFrom[3], stBeamTo[3];
unsigned char stBeamRGBA[4];
int stSetColorCalls, stSetColorLastNull;
int stStretchCalls;
float stStretchLast[4];
int stTraceCalls, stTraceBoxCalls;
int stModelCalls, stModelHandle, stClearScenes, stRenderScenes;
int stRenderRect[4], stRenderFlags;
int stSoundCalls;
int numAddedCommands;
char addedCommands[8][32];
xcommand_t addedCommandFn[8];
char stCmdArgs[8][64];
int stCmdArgc;
int stCvarFlagsLast;

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

void Test_SetCvar( const char *name, const char *value, int flags )
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

cvar_t *Cvar_Get( const char *name, const char *value, int flags )
{
	stCvarFlagsLast = flags;
	if ( ( flags & CVAR_ROM ) && FindCvar( name ) ) {
		Test_SetCvar( name, value, flags );		// a ROM cvar always has the value the program gives
	}
	if ( !FindCvar( name ) ) {
		Test_SetCvar( name, value, flags );
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

// like the engine: a cvar with one of the flags is reported as empty
void Cvar_VariableStringBufferSafe( const char *name, char *buf, int size, int flag )
{
	cvar_t *c = FindCvar( name );

	if ( c && ( c->flags & flag ) ) {
		buf[0] = '\0';
		return;
	}
	Cvar_VariableStringBuffer( name, buf, size );
}

// ---- commands ----

void Cmd_AddCommand( const char *name, xcommand_t fn )
{
	if ( fn && numAddedCommands < 8 ) {
		snprintf( addedCommands[numAddedCommands], sizeof( addedCommands[0] ), "%s", name );
		addedCommandFn[numAddedCommands++] = fn;
	}
}

void Cmd_RemoveCommand( const char *name )
{
	int i;

	for ( i = 0; i < numAddedCommands; i++ ) {
		if ( !strcmp( addedCommands[i], name ) ) {
			addedCommands[i][0] = '\0';
			addedCommandFn[i] = NULL;
		}
	}
}

// the engine's own commands, and those added through Cmd_AddCommand
qboolean Cmd_CommandExists( const char *name )
{
	int i;

	if ( !strcmp( name, "quit" ) ) {
		return qtrue;
	}
	for ( i = 0; i < numAddedCommands; i++ ) {
		if ( !strcmp( addedCommands[i], name ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

int Cmd_Argc( void ) { return stCmdArgc; }
const char *Cmd_Argv( int n ) { return n >= 0 && n < stCmdArgc ? stCmdArgs[n] : ""; }
void Cmd_ArgvBuffer( int n, char *buf, int size ) { snprintf( buf, size, "%s", Cmd_Argv( n ) ); }

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

#ifdef _WIN32
void *Sys_LoadLibrary( const char *name ) { return (void *)LoadLibraryA( name ); }
void *Sys_LoadFunction( void *handle, const char *name ) { return (void *)GetProcAddress( (HMODULE)handle, name ); }
void Sys_UnloadLibrary( void *handle ) { if ( handle ) FreeLibrary( (HMODULE)handle ); }
#else
void *Sys_LoadLibrary( const char *name ) { return dlopen( name, RTLD_NOW ); }
void *Sys_LoadFunction( void *handle, const char *name ) { return dlsym( handle, name ); }
void Sys_UnloadLibrary( void *handle ) { if ( handle ) dlclose( handle ); }
#endif

// ---- collision: every trace stops half way, on a floor ----

void CM_BoxTrace( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
	clipHandle_t model, int brushmask, qboolean capsule )
{
	int i;

	(void)brushmask; (void)capsule;
	stTraceCalls++;
	if ( mins != vec3_origin || maxs != vec3_origin ) {
		stTraceBoxCalls++;
	}
	if ( model != 0 ) {
		Com_Error( ERR_DROP, "trace against model %i", (int)model );
	}
	memset( results, 0, sizeof( *results ) );
	results->fraction = 0.5f;
	for ( i = 0; i < 3; i++ ) {
		results->endpos[i] = start[i] + 0.5f * ( end[i] - start[i] );
	}
	results->plane.normal[2] = 1.0f;
	results->contents = 1;
}

// everything below z = 0 is water
int CM_PointContents( const vec3_t p, clipHandle_t model )
{
	(void)model;
	return p[2] < 0 ? CONTENTS_WATER : 0;
}

// ---- renderer ----

static qhandle_t R_RegisterModel( const char *name ) { return name && !strcmp( name, "models/test.md3" ) ? 9 : 0; }
static void R_ClearScene( void ) { stClearScenes++; }
static void R_RenderScene( const refdef_t *fd )
{
	stRenderScenes++;
	stRenderRect[0] = fd->x; stRenderRect[1] = fd->y; stRenderRect[2] = fd->width; stRenderRect[3] = fd->height;
	stRenderFlags = fd->rdflags;
}

static qhandle_t R_RegisterShader( const char *name ) { return name && !strcmp( name, "railCore" ) ? 7 : 0; }

static void R_AddPolyToScene( qhandle_t shader, int numVerts, const polyVert_t *verts, int num )
{
	(void)shader; (void)verts; (void)num;
	stPolyCalls++;
	stPolyVerts += numVerts;
}

static void R_AddRefEntityToScene( const refEntity_t *ent, qboolean intShaderTime )
{
	(void)intShaderTime;
	if ( ent->reType == RT_MODEL ) {
		stModelCalls++;
		stModelHandle = ent->hModel;
		return;
	}
	if ( ent->reType != RT_RAIL_CORE ) {
		Com_Error( ERR_DROP, "beam is not a rail core" );
	}
	stBeamCalls++;
	stBeamShader = ent->customShader;
	memcpy( stBeamFrom, ent->oldorigin, sizeof( stBeamFrom ) );
	memcpy( stBeamTo, ent->origin, sizeof( stBeamTo ) );
	memcpy( stBeamRGBA, ent->shader.rgba, 4 );
}

static void R_SetColor( const float *rgba )
{
	stSetColorCalls++;
	stSetColorLastNull = rgba == NULL;
}

static void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t shader )
{
	(void)s1; (void)t1; (void)s2; (void)t2; (void)shader;
	stStretchCalls++;
	stStretchLast[0] = x; stStretchLast[1] = y; stStretchLast[2] = w; stStretchLast[3] = h;
}

// the same as the engine's (cl_scrn.c)
void SCR_AdjustFrom640( float *x, float *y, float *w, float *h )
{
	float xscale = cls.glconfig.vidWidth / 640.0;
	float yscale = cls.glconfig.vidHeight / 480.0;

	if ( x ) *x *= xscale;
	if ( y ) *y *= yscale;
	if ( w ) *w *= xscale;
	if ( h ) *h *= yscale;
}

void Stubs_InitRenderer( void )
{
	memset( &re, 0, sizeof( re ) );
	re.RegisterShader = R_RegisterShader;
	re.AddPolyToScene = R_AddPolyToScene;
	re.AddRefEntityToScene = R_AddRefEntityToScene;
	re.SetColor = R_SetColor;
	re.DrawStretchPic = R_DrawStretchPic;
	re.RegisterModel = R_RegisterModel;
	re.RegisterShaderNoMip = R_RegisterShader;
	re.ClearScene = R_ClearScene;
	re.RenderScene = R_RenderScene;
}
