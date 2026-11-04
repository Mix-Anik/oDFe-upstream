/* Generated from client.api by gen_api.py - do not edit. */

/*
===========================================================================
The entry point of a plugin. Include this once, in the first source file of the program (the VM wants vmMain
to be the first function), after defining

    #define ODF_PLUGIN_VERSION     "1.0"
    #define ODF_PLUGIN_CAPS        "capability,capability"     (optional: what the plugin needs to be given)
    #define ODF_PLUGIN_DESCRIPTION "what it does"              (optional)
    #define EXPORTS( X ) X(OnInit) X(OnShutdown) ...  the exports the program implements

The API version (1) is added by this header (ODF_PLUGIN_API, a string, overrides it: for tests).
===========================================================================
*/

#ifndef ODFC_ENTRY_H
#define ODFC_ENTRY_H

#include "odfc.h"

#ifndef ODF_PLUGIN_VERSION
#error define ODF_PLUGIN_VERSION before including odfc_entry.h
#endif
#ifndef ODF_PLUGIN_CAPS
#define ODF_PLUGIN_CAPS ""
#endif
#ifndef ODF_PLUGIN_DESCRIPTION
#define ODF_PLUGIN_DESCRIPTION ""
#endif
#ifndef ODF_PLUGIN_API
#define ODF_STRINGIFY_( x )	#x
#define ODF_STRINGIFY( x )	ODF_STRINGIFY_( x )
#define ODF_PLUGIN_API ODF_STRINGIFY( ODFC_API_VERSION )
#endif
#ifndef EXPORTS
#error define EXPORTS( X ) before including odfc_entry.h
#endif

#define ODFC_CASE_OnInit	case 0: return OnInit(  );
#define ODFC_BIT_OnInit	| ( 1 << 0 )
#define ODFC_CASE_OnShutdown	case 1: OnShutdown(  ); return 0;
#define ODFC_BIT_OnShutdown	| ( 1 << 1 )
#define ODFC_CASE_OnMapLoad	case 2: OnMapLoad( (const char *)a[0] ); return 0;
#define ODFC_BIT_OnMapLoad	| ( 1 << 2 )
#define ODFC_CASE_OnCgameShutdown	case 3: OnCgameShutdown(  ); return 0;
#define ODFC_BIT_OnCgameShutdown	| ( 1 << 3 )
#define ODFC_CASE_OnDraw3D	case 4: OnDraw3D( (const odfcView_t *)a[0] ); return 0;
#define ODFC_BIT_OnDraw3D	| ( 1 << 4 )
#define ODFC_CASE_OnDraw2D	case 5: OnDraw2D( a[0] ); return 0;
#define ODFC_BIT_OnDraw2D	| ( 1 << 5 )
#define ODFC_CASE_OnFilterSound	case 6: return OnFilterSound( a[0], a[1], a[2], a[3] );
#define ODFC_BIT_OnFilterSound	| ( 1 << 6 )
#define ODFC_CASE_OnDemoSeek	case 7: OnDemoSeek( a[0], a[1] ); return 0;
#define ODFC_BIT_OnDemoSeek	| ( 1 << 7 )
#define ODFC_HANDLER_CASES	case 8: ( (odfcCommandHandler_t)a[0] )( a[1], (const char **)a[2] ); return 0;
#define ODFC_HANDLER_BITS	 | ( 1 << 8 )
#define ODFC_X_CASE( name )	ODFC_CASE_##name
#define ODFC_X_BIT( name )	ODFC_BIT_##name

static char odf_io_buffer[ODF_IO_SIZE];

int vmMain( int command, int ioAddress, int unused1, int unused2 )
{
	const int *a = (const int *)ioAddress;

	switch ( command ) {
		case ODF_CMD_IO:
			return (int)odf_io_buffer;
		case ODF_CMD_MANIFEST:
			return (int)(
				"module=odfc\n"
				"version=" ODF_PLUGIN_VERSION "\n"
				"api=" ODF_PLUGIN_API "\n"
				"caps=" ODF_PLUGIN_CAPS "\n"
				"description=" ODF_PLUGIN_DESCRIPTION );
		case ODF_CMD_EXPORTS:
			return 0 EXPORTS( ODFC_X_BIT ) ODFC_HANDLER_BITS;
		EXPORTS( ODFC_X_CASE )
		ODFC_HANDLER_CASES
	}
	return 0;
}

#endif // ODFC_ENTRY_H
