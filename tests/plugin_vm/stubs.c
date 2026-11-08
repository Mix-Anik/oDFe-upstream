/*
The little bit of the engine the VM code needs.
*/

#include "q_shared.h"
#include "qcommon.h"
#include "stubs.h"
#include <stdarg.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

static cvar_t rtChecks;

void Stubs_Init( void )
{
	rtChecks.integer = 15;
	rtChecks.value = 15;
	vm_rtChecks = &rtChecks;
}

void QDECL Com_Printf( const char *fmt, ... )
{
	va_list ap;
	char text[4096];

	va_start( ap, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, ap );
	va_end( ap );
	if ( getenv( "VERBOSE" ) ) {
		printf( "%s", text );
	}
}

void QDECL Com_DPrintf( const char *fmt, ... )
{
}

void QDECL Com_Error( errorParm_t code, const char *fmt, ... )
{
	va_list ap;
	char text[4096];

	va_start( ap, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, ap );
	va_end( ap );
	printf( "Com_Error( %d ): %s\n", code, text );
	exit( 3 );
}

int64_t Sys_Microseconds( void )
{
#ifdef _WIN32
	LARGE_INTEGER f, c;

	QueryPerformanceFrequency( &f );
	QueryPerformanceCounter( &c );
	return (int64_t)( c.QuadPart * 1000000 / f.QuadPart );
#else
	struct timespec ts;

	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

// ---------------------------------------------------------------------------
// what vm.c links against; the tests never get there (they load plugins through plugin_vm.c only)
// ---------------------------------------------------------------------------

void *Z_Malloc( size_t size )
{
	void *p = calloc( 1, size ? size : 1 );

	if ( !p ) {
		Com_Error( ERR_FATAL, "out of memory" );
	}
	return p;
}

void Z_Free( void *ptr )
{
	free( ptr );
}

// the interpreter keeps its translated program here for good, as in the engine
void *Hunk_Alloc( size_t size, ha_pref preference )
{
	return Z_Malloc( size );
}

int Hunk_MemoryRemaining( void ) { return 0; }
cvar_t *Cvar_Get( const char *var_name, const char *value, int flags ) { return &rtChecks; }
int Cvar_VariableIntegerValue( const char *var_name ) { return 0; }
void Cmd_AddCommand( const char *cmd_name, xcommand_t function ) {}
int Cmd_Argc( void ) { return 0; }
const char *Cmd_Argv( int arg ) { return ""; }
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
