/* Generated from server.api by gen_api.py - do not edit. */

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

#ifndef ODFS_ENTRY_H
#define ODFS_ENTRY_H

#include "odfs.h"

#ifndef ODF_PLUGIN_VERSION
#error define ODF_PLUGIN_VERSION before including odfs_entry.h
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
#define ODF_PLUGIN_API ODF_STRINGIFY( ODFS_API_VERSION )
#endif
#ifndef EXPORTS
#error define EXPORTS( X ) before including odfs_entry.h
#endif

#define ODFS_CASE_OnInit	case 0: return OnInit(  );
#define ODFS_BIT_OnInit	| ( 1 << 0 )
#define ODFS_CASE_OnShutdown	case 1: OnShutdown(  ); return 0;
#define ODFS_BIT_OnShutdown	| ( 1 << 1 )
#define ODFS_CASE_OnFrame	case 2: OnFrame( a[0] ); return 0;
#define ODFS_BIT_OnFrame	| ( 1 << 2 )
#define ODFS_HANDLER_CASES	case 3: ( (odfsEventHandler_t)a[0] )( (const char *)a[1] ); return 0; case 4: ( (odfsChatCommandHandler_t)a[0] )( a[1], a[2], (const char *)a[3] ); return 0; case 5: ( (odfsHttpHandler_t)a[0] )( a[1], a[2], a[3], a[4] ); return 0;
#define ODFS_HANDLER_BITS	 | ( 1 << 3 ) | ( 1 << 4 ) | ( 1 << 5 )
#define ODFS_X_CASE( name )	ODFS_CASE_##name
#define ODFS_X_BIT( name )	ODFS_BIT_##name

static char odf_io_buffer[ODF_IO_SIZE];

int vmMain( int command, int ioAddress, int unused1, int unused2 )
{
	const int *a = (const int *)ioAddress;

	switch ( command ) {
		case ODF_CMD_IO:
			return (int)odf_io_buffer;
		case ODF_CMD_MANIFEST:
			return (int)(
				"module=odfs\n"
				"version=" ODF_PLUGIN_VERSION "\n"
				"api=" ODF_PLUGIN_API "\n"
				"caps=" ODF_PLUGIN_CAPS "\n"
				"description=" ODF_PLUGIN_DESCRIPTION );
		case ODF_CMD_EXPORTS:
			return 0 EXPORTS( ODFS_X_BIT ) ODFS_HANDLER_BITS;
		EXPORTS( ODFS_X_CASE )
		ODFS_HANDLER_CASES
	}
	return 0;
}

#endif // ODFS_ENTRY_H
