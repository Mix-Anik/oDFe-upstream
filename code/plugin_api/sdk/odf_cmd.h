/*
Console commands with a handler function each (client plugins).

The engine has one export for all commands of a plugin: OnCommand( name ). This header is the usual implementation of it: a table
of names and handlers, and the OnCommand function that finds the handler and calls it. Include it once, in the first source
file after odfc_entry.h, and list the export as usual:

    #define EXPORTS( X ) X(OnInit) X(OnCommand)
    #include "odfc_entry.h"
    #include "odf_cmd.h"

    static void CmdHello( void ) { Print( "hello" ); }

    int OnInit( void )
    {
        AddCommand( "hello", CmdHello );
        return 0;
    }

A plugin that wants to handle the export itself does not include this header. The plugin needs the capability command.
Inside a handler CommandArgc / CommandArgv give the arguments.
*/

#ifndef ODF_CMD_H
#define ODF_CMD_H

#include <string.h>

#ifndef ODF_MODULE_CLIENT
#error odf_cmd.h is for client plugins (odfc.h); a server plugin gets chat commands through the chat_command event
#endif

typedef void ( *odfcCommandHandler_t )( void );

#define ODFC_MAX_COMMANDS	32

static struct {
	char				name[32];		// the engine allows up to 31 characters
	odfcCommandHandler_t	handler;
} odfc_commands[ODFC_MAX_COMMANDS];
static int odfc_numCommands;

// Registers the console command and remembers the handler. Returns 0 (and registers nothing) when the engine refuses the name
// (see CommandRegister), the table is full or the handler is NULL.
int AddCommand( const char *name, odfcCommandHandler_t handler )
{
	int n;

	if ( !name || !handler || odfc_numCommands >= ODFC_MAX_COMMANDS || strlen( name ) >= sizeof( odfc_commands[0].name ) ) {
		return 0;
	}
	if ( !CommandRegister( name ) ) {
		return 0;
	}
	n = odfc_numCommands++;
	strcpy( odfc_commands[n].name, name );
	odfc_commands[n].handler = handler;
	return 1;
}

// the export: the engine calls it with the name the command was registered under
void OnCommand( const char *name )
{
	int i;

	for ( i = 0; i < odfc_numCommands; i++ ) {
		if ( !strcmp( name, odfc_commands[i].name ) ) {
			odfc_commands[i].handler();
			return;
		}
	}
}

#endif // ODF_CMD_H
