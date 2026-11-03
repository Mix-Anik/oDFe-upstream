/* A client plugin that adds the console command "hello".
   Build:  odf-build --client -o hello.qvm hello.c   and put hello.qvm in the plugins folder of the home path. */
#define ODF_PLUGIN_VERSION     "1.0"
#define ODF_PLUGIN_DESCRIPTION "says hello"
#define EXPORTS( X ) X(OnInit) X(OnCommand)
#include "odfc_entry.h"          /* defines vmMain; it has to come before the first function of the program */
#include "odf_cmd.h"             /* AddCommand( name, handler ) */

#include <stdio.h>

static void CmdHello( void )
{
	char text[64];

	snprintf( text, sizeof( text ), "hello, %.1f seconds since the start", Milliseconds() / 1000.0f );
	Print( text );
}

int OnInit( void )
{
	AddCommand( "hello", CmdHello );
	return 0;                    /* not 0: the plugin is not loaded */
}
