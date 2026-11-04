/* A client plugin with console commands that have a handler each (CommandRegister); run by cmd_test.sh through plugin_smoke. */

#define ODF_PLUGIN_VERSION     "1.0"
#define ODF_PLUGIN_CAPS        "command"
#define ODF_PLUGIN_DESCRIPTION "test of CommandRegister"
#define EXPORTS( X ) X(OnInit)
#include "odfc_entry.h"

#include <stdio.h>

static int betaRuns;

static void CmdAlpha( int argc, const char **argv )
{
	char text[128];

	snprintf( text, sizeof( text ), "alpha: %d args, first '%s'", argc, argc > 1 ? argv[1] : "" );
	Print( text );
}

static void CmdBeta( int argc, const char **argv )
{
	char text[64];

	(void)argc;
	(void)argv;
	betaRuns++;
	snprintf( text, sizeof( text ), "beta: run %d", betaRuns );
	Print( text );
}

static void Report( const char *what, int result )
{
	char text[96];

	snprintf( text, sizeof( text ), "add %s: %d", what, result );
	Print( text );
}

int OnInit( void )
{
	char longName[40];
	int i;

	Report( "alpha", CommandRegister( "alpha", CmdAlpha ) );
	Report( "beta", CommandRegister( "beta", CmdBeta ) );
	Report( "alpha again", CommandRegister( "alpha", CmdBeta ) );		/* the engine refuses a name that exists */
	Report( "quit", CommandRegister( "quit", CmdBeta ) );				/* the engine's own command */
	Report( "no handler", CommandRegister( "gamma", 0 ) );
	Report( "Bad Name", CommandRegister( "Bad Name", CmdBeta ) );
	for ( i = 0; i < 39; i++ ) {
		longName[i] = 'a';
	}
	longName[39] = '\0';
	Report( "too long", CommandRegister( longName, CmdBeta ) );
	return 0;
}
