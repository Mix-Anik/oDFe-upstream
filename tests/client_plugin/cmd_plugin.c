/* A client plugin that uses odf_cmd.h; run by cmd_test.sh through plugin_smoke. */

#define ODF_PLUGIN_VERSION     "1.0"
#define ODF_PLUGIN_CAPS        "command"
#define ODF_PLUGIN_DESCRIPTION "test of odf_cmd.h"
#define EXPORTS( X ) X(OnInit) X(OnCommand)
#include "odfc_entry.h"
#include "odf_cmd.h"

#include <stdio.h>

static int betaRuns;

static void CmdAlpha( void )
{
	char text[128], arg[32];

	CommandArgv( 1, arg, sizeof( arg ) );
	snprintf( text, sizeof( text ), "alpha: %d args, first '%s'", CommandArgc(), arg );
	Print( text );
}

static void CmdBeta( void )
{
	char text[64];

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

	Report( "alpha", AddCommand( "alpha", CmdAlpha ) );
	Report( "beta", AddCommand( "beta", CmdBeta ) );
	Report( "alpha again", AddCommand( "alpha", CmdBeta ) );		/* the engine refuses a name that exists */
	Report( "quit", AddCommand( "quit", CmdBeta ) );				/* the engine's own command */
	Report( "no handler", AddCommand( "gamma", 0 ) );
	Report( "no name", AddCommand( 0, CmdBeta ) );
	Report( "Bad Name", AddCommand( "Bad Name", CmdBeta ) );
	for ( i = 0; i < 39; i++ ) {
		longName[i] = 'a';
	}
	longName[39] = '\0';
	Report( "too long", AddCommand( longName, CmdBeta ) );
	return 0;
}
