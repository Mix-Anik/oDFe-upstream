/* A client plugin that adds the console command "hello" (try "hello World" in the console).
   Build:  odf-build --client -o hello.qvm hello.c   and put hello.qvm in the plugins folder of the home path. */
#define ODF_PLUGIN_VERSION     "1.0"
#define ODF_PLUGIN_DESCRIPTION "says hello"
#define EXPORTS( X ) X(OnInit)
#include "odfc_entry.h"          /* defines vmMain; it has to come before the first function of the program */

#include <stdio.h>

/* A handler gets the words of the command line like main: "hello World" is argc 2, argv[0] "hello", argv[1] "World". */
static void CmdHello( int argc, const char **argv )
{
	char text[96];

	snprintf( text, sizeof( text ), "hello, %s! %.1f seconds since the start", argc > 1 ? argv[1] : "stranger", Milliseconds() / 1000.0f );
	Print( text );
}

int OnInit( void )
{
	CommandRegister( "hello", CmdHello );
	return 0;                    /* not 0: the plugin is not loaded */
}
