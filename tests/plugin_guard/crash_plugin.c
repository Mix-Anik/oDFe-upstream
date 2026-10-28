/*
A plugin that misbehaves on purpose, for testing the crash guard.
OnEvent( name ) does what the event name says.
*/

#include <string.h>

#include "odf_plugin_api.h"

static const odfPluginImport_t *engine;
static volatile int zero;
static volatile int *volatile nullPtr;
static int counter;

static void Fault( const char *what )
{
	if ( !strcmp( what, "null_write" ) ) {
		*nullPtr = 1;
	} else if ( !strcmp( what, "null_read" ) ) {
		counter = *nullPtr;
	} else if ( !strcmp( what, "wild_pointer" ) ) {
		volatile int *volatile wild = (volatile int *)(uintptr_t)0x10;
		*wild = 1;
	} else if ( !strcmp( what, "div0" ) ) {
		counter = 100 / zero;
	} else if ( !strcmp( what, "illegal" ) ) {
#if defined( __x86_64__ ) || defined( __i386__ )
		__asm__ volatile ( "ud2" );
#else
		__builtin_trap();
#endif
	} else if ( !strcmp( what, "stack_overflow" ) ) {
		volatile char big[1 << 20];
		memset( (void *)big, 1, sizeof( big ) );
		Fault( "stack_overflow" );
		big[0] = 2;
	}
}

static void OnEvent( const char *name, const char *data )
{
	(void)data;
	if ( !strcmp( name, "ok" ) ) {
		counter++;
	} else if ( !strcmp( name, "print_then_null_write" ) ) {
		engine->Print( "about to crash" );
		Fault( "null_write" );
	} else {
		Fault( name );
	}
}

static void Frame( int serverTimeMs )
{
	(void)serverTimeMs;
	counter++;
}

static int Init( void )
{
	engine->Print( "crash_plugin loaded" );
	return 0;
}

static const odfPluginExport_t exports = {
	ODF_PLUGIN_API_VERSION, "crash", "test", 0, Init, 0, Frame, OnEvent
};

ODF_EXPORT const odfPluginExport_t *ODF_PluginEntry( const odfPluginImport_t *imp )
{
	engine = imp;
	return &exports;
}

// lets the test see the counter without a getter in the plugin API
ODF_EXPORT int Crash_Counter( void )
{
	return counter;
}
