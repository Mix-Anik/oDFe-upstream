/*
The little bit of the engine that cl_plugin.c calls, for the native test: cvars, a fake renderer that
counts what it is given, a fake collision trace, and the client state (cl, clc, cls).
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <dirent.h>
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

// ---- the game snapshots (sv_state.c) and the cgame time change ----
int stStateHandle = 7, stStateOwnerOf;
int stStateCreates, stStateCreateOwner, stStateApplies, stStateFrees, stStateFreeOwnerCalls, stStateFreeOwner;
int stSeekable = 1;
int stTimeChangeCalls, stTimeChangeArgs[3];

int SV_StateCreate( int owner )
{
	stStateCreates++;
	stStateCreateOwner = owner;
	stStateOwnerOf = owner;
	return stStateHandle;
}
qboolean SV_StateCanApply( int handle ) { return handle == stStateHandle; }
qboolean SV_StateApply( int handle ) { stStateApplies++; return handle == stStateHandle; }
void SV_StateFree( int handle ) { (void)handle; stStateFrees++; }
void SV_StateFreeOwner( int owner ) { stStateFreeOwnerCalls++; stStateFreeOwner = owner; }
int SV_StateOwner( int handle ) { return handle == stStateHandle ? stStateOwnerOf : 0; }

// ---- the demo that is recorded in segments (cl_demoroute.c) ----
int stRouteCreated, stRouteCreatedHandle, stRouteApplied, stRouteAppliedHandle, stRouteKnown = 1, stRouteRestarts;
void DemoRoute_Created( int handle ) { stRouteCreated++; stRouteCreatedHandle = handle; }
qboolean DemoRoute_Applied( int handle ) { stRouteApplied++; stRouteAppliedHandle = handle; return stRouteKnown ? qtrue : qfalse; }
void CL_DemoRouteRestart( void ) { stRouteRestarts++; }

// ---- the demo calls of the plugin API (cl_main.c) ----
int stDemoRecords, stDemoStops, stDemoStopDiscard, stDemoSaves, stDemoRecordResult = 1;
char stDemoSavedName[160];
qboolean CL_PluginDemoRecord( void ) { stDemoRecords++; return stDemoRecordResult ? qtrue : qfalse; }
qboolean CL_PluginDemoStop( qboolean discard ) { stDemoStops++; stDemoStopDiscard = discard; return qtrue; }
qboolean CL_PluginDemoSave( const char *name ) { stDemoSaves++; Q_strncpyz( stDemoSavedName, name, sizeof( stDemoSavedName ) ); return qtrue; }
void CL_PluginDemoState( int *recording, int *segments, int *mine, int *bytes ) { *recording = 1; *segments = 1; *mine = 1; *bytes = 4242; }

qboolean CLDemo_CgameSeekable( void ) { return stSeekable ? qtrue : qfalse; }
void CL_CGameTimeChange( int serverTime, int snapshotNum, int serverCommandSequence )
{
	stTimeChangeCalls++;
	stTimeChangeArgs[0] = serverTime;
	stTimeChangeArgs[1] = snapshotNum;
	stTimeChangeArgs[2] = serverCommandSequence;
}

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
char addedCommands[32][32];
xcommand_t addedCommandFn[32];
char stCmdArgs[24][256];
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

#define MAX_STUB_CVARS 256
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

static void SetCvar( const char *name, const char *value, int flags )
{
	cvar_t *c = FindCvar( name );

	if ( !c ) {
		if ( numStubCvars >= MAX_STUB_CVARS ) {
			Com_Error( ERR_FATAL, "the test has too many cvars" );
		}
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

// what the player's config files do before the engine knows a cvar: it exists, flagged as made by the user
void Test_SetCvar( const char *name, const char *value, int flags )
{
	SetCvar( name, value, ( flags & CVAR_PRIVATE ) ? flags : ( flags | CVAR_USER_CREATED ) );
}

unsigned Cvar_Flags( const char *name )
{
	cvar_t *c = FindCvar( name );

	return c ? (unsigned)c->flags : CVAR_NONEXISTENT;
}

cvar_t *Cvar_Get( const char *name, const char *value, int flags )
{
	cvar_t *c;

	stCvarFlagsLast = flags;
	c = FindCvar( name );
	if ( !c ) {
		SetCvar( name, value, flags );
	} else {
		int old = c->flags & ~CVAR_USER_CREATED;		// like the engine: the code now owns it, the user's value stays

		if ( flags & CVAR_ROM ) {
			SetCvar( name, value, old | flags );		// a ROM cvar always has the value the program gives
		} else {
			c->flags = old | flags;
		}
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
	if ( fn && numAddedCommands < 32 ) {
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

// the names of the files in a folder that end with the extension, like the engine's
char **Sys_ListFiles( const char *directory, const char *extension, const char *filter, int *numfiles, int subdirs )
{
	char **list = (char **)calloc( 257, sizeof( char * ) );
	int n = 0, extLen = (int)strlen( extension );

	(void)filter; (void)subdirs;
#ifdef _WIN32
	{
		char pattern[600];
		WIN32_FIND_DATAA fd;
		HANDLE h;

		snprintf( pattern, sizeof( pattern ), "%s\\*%s", directory, extension );
		h = FindFirstFileA( pattern, &fd );
		if ( h != INVALID_HANDLE_VALUE ) {
			do {
				if ( n < 256 && !( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) ) {
					list[n++] = strdup( fd.cFileName );
				}
			} while ( FindNextFileA( h, &fd ) );
			FindClose( h );
		}
	}
#else
	{
		DIR *d = opendir( directory );
		struct dirent *e;

		while ( d && ( e = readdir( d ) ) != NULL ) {
			int len = (int)strlen( e->d_name );

			if ( n < 256 && len >= extLen && !strcmp( e->d_name + len - extLen, extension ) ) {
				list[n++] = strdup( e->d_name );
			}
		}
		if ( d ) {
			closedir( d );
		}
	}
#endif
	(void)extLen;
	*numfiles = n;
	if ( !n ) {
		free( list );
		return NULL;
	}
	return list;
}

void Sys_FreeFileList( char **list )
{
	int i;

	for ( i = 0; list && list[i]; i++ ) {
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

// ---------------------------------------------------------------------------
// what the VM code of the engine (vm.c, vm_x86.c, vm_interpreted.c) links against: plugins are loaded through plugin_vm.c only
// ---------------------------------------------------------------------------

static cvar_t vmChecksCvar;

__attribute__( ( constructor ) ) static void InitVMChecks( void )
{
	vmChecksCvar.integer = 15;
	vmChecksCvar.value = 15;
	vm_rtChecks = &vmChecksCvar;
}

void *Z_Malloc( size_t size )
{
	void *p = calloc( 1, size ? size : 1 );

	if ( !p ) {
		Com_Error( ERR_FATAL, "out of memory" );
	}
	return p;
}

void Z_Free( void *ptr ) { free( ptr ); }
void *Hunk_Alloc( size_t size, ha_pref preference ) { return Z_Malloc( size ); }
int Hunk_MemoryRemaining( void ) { return 0; }
int FS_FOpenFileRead( const char *qpath, fileHandle_t *file, qboolean uniqueFILE ) { return -1; }
int FS_Read( void *buffer, int len, fileHandle_t f ) { return 0; }
void FS_FCloseFile( fileHandle_t f ) {}
int FS_ReadFile( const char *qpath, void **buffer ) { *buffer = NULL; return -1; }
void FS_FreeFile( void *buffer ) {}
void *FS_LoadLibrary( const char *name ) { return NULL; }
void *Sys_LoadFunction( void *handle, const char *name ) { return NULL; }
void Sys_UnloadLibrary( void *handle ) {}
cvar_t *com_developer;
int fs_lastPakIndex;
int CPU_Flags;
int Cvar_VariableIntegerValue( const char *var_name ) { return 0; }
