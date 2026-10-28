/*
Tests the plugin crash guard with a plugin that crashes on purpose (crash_plugin.c).
Not part of the engine build, see run.sh.
*/

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "odf_plugin_api.h"
#include "sv_plugin_guard.h"

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

static void EnginePrint( const char *text ) { printf( "  [engine] %s\n", text ); }
static int EngineMs( void ) { return 0; }

static odfPluginImport_t imports;

typedef struct {
	const odfPluginExport_t *plugin;
	const char *event;
} eventCall_t;

static void CallOnEvent( void *arg )
{
	eventCall_t *c = (eventCall_t *)arg;
	c->plugin->OnEvent( c->event, "" );
}

static void CallFrame( void *arg )
{
	const odfPluginExport_t *p = (const odfPluginExport_t *)arg;
	p->Frame( 1000 );
}

int main( int argc, char **argv )
{
	void *lib;
	odfPluginEntry_t entry;
	int ( *counter )( void );
	const odfPluginExport_t *plugin;
	pluginRange_t range;
	pluginFault_t fault;
	eventCall_t call;
	int i;
	static const char *crashes[] = { "null_write", "null_read", "wild_pointer", "div0", "illegal", "print_then_null_write",
#ifndef _WIN32
		"stack_overflow",
#endif
		NULL };

	if ( argc < 2 ) {
		printf( "usage: guard_test <plugin library>\n" );
		return 2;
	}

#ifdef _WIN32
	lib = (void *)LoadLibraryA( argv[1] );
	entry = lib ? (odfPluginEntry_t)(void *)GetProcAddress( (HMODULE)lib, "ODF_PluginEntry" ) : NULL;
	counter = lib ? (int ( * )( void ))(void *)GetProcAddress( (HMODULE)lib, "Crash_Counter" ) : NULL;
#else
	lib = dlopen( argv[1], RTLD_NOW );
	entry = lib ? (odfPluginEntry_t)dlsym( lib, "ODF_PluginEntry" ) : NULL;
	counter = lib ? (int ( * )( void ))dlsym( lib, "Crash_Counter" ) : NULL;
#endif

	imports.size = sizeof( imports );
	imports.apiVersion = ODF_PLUGIN_API_VERSION;
	imports.Print = EnginePrint;
	imports.Milliseconds = EngineMs;
	if ( !entry || !counter ) {
		printf( "cannot load %s\n", argv[1] );
		return 2;
	}

	CHECK( PluginGuard_Init(), "PluginGuard_Init failed" );
	CHECK( PluginGuard_Available(), "guard not available" );
	CHECK( PluginGuard_ModuleRange( lib, (const void *)entry, &range ), "no module range" );
	CHECK( range.hi > range.lo && (uintptr_t)entry >= range.lo && (uintptr_t)entry < range.hi, "entry point outside the range" );

	plugin = entry( &imports );
	CHECK( plugin && plugin->OnEvent && plugin->Init, "bad export table" );
	plugin->Init();

	// normal calls
	call.plugin = plugin;
	call.event = "ok";
	CHECK( PluginGuard_Call( &range, CallOnEvent, &call, &fault ) == 1, "normal call reported a fault" );
	CHECK( counter() == 1, "normal call did not run (%d)", counter() );

	// every kind of crash is survived, one after the other, and the guard keeps working in between
	for ( i = 0; crashes[i]; i++ ) {
		int before;

		memset( &fault, 0, sizeof( fault ) );
		call.event = crashes[i];
		before = counter();
		CHECK( PluginGuard_Call( &range, CallOnEvent, &call, &fault ) == 0, "%s: crash was not caught", crashes[i] );
		CHECK( fault.what[0] != 0, "%s: no description", crashes[i] );
		CHECK( fault.address >= range.lo && fault.address < range.hi, "%s: fault address outside the plugin", crashes[i] );
		CHECK( fault.offset == fault.address - range.lo, "%s: offset does not match", crashes[i] );
		printf( "  %-22s -> caught: %s at plugin+0x%lx\n", crashes[i], fault.what, (unsigned long)fault.offset );

		call.event = "ok";
		CHECK( PluginGuard_Call( &range, CallOnEvent, &call, &fault ) == 1, "%s: guard broken after the crash", crashes[i] );
		CHECK( counter() == before + 1, "%s: plugin code after the crash did not run", crashes[i] );

		CHECK( PluginGuard_Call( &range, CallFrame, (void *)plugin, &fault ) == 1, "%s: Frame failed after the crash", crashes[i] );
	}

	// a call from inside a guarded call is not guarded again (it just runs)
	PluginGuard_Shutdown();
	CHECK( !PluginGuard_Available(), "still available after shutdown" );
	CHECK( PluginGuard_Init(), "cannot install again" );

	printf( failures ? "FAILED: %d check(s)\n" : "all guard checks passed\n", failures );
	return failures ? 1 : 0;
}
