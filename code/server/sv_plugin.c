/*
===========================================================================
Native server plugins: loading, calling them with a crash guard and handing
them events. The interface is in odf_plugin_api.h.
===========================================================================
*/

#include "server.h"
#include "odf_plugin_api.h"
#include "sv_plugin_guard.h"

#ifdef _WIN32
#define PLUGIN_EXT	".dll"
#elif defined( __APPLE__ )
#define PLUGIN_EXT	".dylib"
#else
#define PLUGIN_EXT	".so"
#endif

#define MAX_PLUGINS			8
#define MAX_PLUGIN_NAME		32

typedef enum {
	PS_RUNNING,
	PS_STOPPED,		// shut down normally
	PS_FAILED,		// did not load or refused to start
	PS_FAULTED		// crashed, never called again
} pluginState_t;

typedef struct {
	char					name[MAX_PLUGIN_NAME];
	char					path[MAX_OSPATH];
	void					*lib;
	const odfPluginExport_t	*exp;
	pluginRange_t			range;
	qboolean				guarded;
	pluginState_t			state;
	char					note[192];		// why it is not running
} plugin_t;

static plugin_t			plugins[MAX_PLUGINS];
static int				numPlugins;
static plugin_t			*current;		// the plugin whose code is running, for log prefixes
static cvar_t			*sv_plugins;
static odfPluginImport_t imports;
static qboolean			guardInstalled;


// ---------------------------------------------------------------------------
// guarded calls into a plugin
// ---------------------------------------------------------------------------

typedef enum {
	PC_ENTRY,
	PC_INIT,
	PC_SHUTDOWN,
	PC_FRAME,
	PC_EVENT
} callKind_t;

typedef struct {
	callKind_t				kind;
	plugin_t				*plugin;
	odfPluginEntry_t		entry;		// PC_ENTRY
	const odfPluginExport_t	*result;	// PC_ENTRY
	int						rc;			// PC_INIT
	int						time;		// PC_FRAME
	const char				*name;		// PC_EVENT
	const char				*data;
} call_t;

static void CallThunk( void *arg )
{
	call_t *c = (call_t *)arg;

	switch ( c->kind ) {
	case PC_ENTRY:		c->result = c->entry( &imports ); break;
	case PC_INIT:		c->rc = c->plugin->exp->Init(); break;
	case PC_SHUTDOWN:	c->plugin->exp->Shutdown(); break;
	case PC_FRAME:		c->plugin->exp->Frame( c->time ); break;
	case PC_EVENT:		c->plugin->exp->OnEvent( c->name, c->data ); break;
	}
}

static const char *CallDescription( const call_t *c )
{
	switch ( c->kind ) {
	case PC_ENTRY:		return "ODF_PluginEntry";
	case PC_INIT:		return "Init";
	case PC_SHUTDOWN:	return "Shutdown";
	case PC_FRAME:		return "Frame";
	case PC_EVENT:		return va( "OnEvent(%s)", c->name );
	}
	return "?";
}

/*
===============
Plugin_Fault

A plugin crashed: it is never called again.
===============
*/
static void Plugin_Fault( plugin_t *p, const call_t *c, const pluginFault_t *f )
{
	Com_sprintf( p->note, sizeof( p->note ), "crashed in %s: %s at offset 0x%lx", CallDescription( c ), f->what, (unsigned long)f->offset );
	p->state = PS_FAULTED;

	Com_Printf( S_COLOR_RED "Plugin '%s' crashed in %s: %s at %s+0x%lx, accessing 0x%lx.\n"
		S_COLOR_RED "The plugin has been stopped and will not run again until the server is restarted.\n",
		p->name, CallDescription( c ), f->what, p->path, (unsigned long)f->offset, (unsigned long)f->dataAddress );

	SV_Auth_PluginStopped();
}

/*
===============
Plugin_Run

Returns qfalse if the plugin crashed.
===============
*/
static qboolean Plugin_Run( plugin_t *p, call_t *c )
{
	plugin_t *prev = current;
	pluginFault_t fault;
	int ok;

	c->plugin = p;
	current = p;
	if ( p->guarded ) {
		ok = PluginGuard_Call( &p->range, CallThunk, c, &fault );
	} else {
		CallThunk( c );
		ok = 1;
	}
	current = prev;

	if ( !ok ) {
		Plugin_Fault( p, c, &fault );
		return qfalse;
	}
	return qtrue;
}


// ---------------------------------------------------------------------------
// what the plugins can call
// ---------------------------------------------------------------------------

static void Imp_Print( const char *text )
{
	if ( !text ) {
		return;
	}
	Com_Printf( "[%s] %s\n", current ? current->name : "plugin", text );
}

static int Imp_Milliseconds( void )
{
	return Sys_Milliseconds();
}

static int Imp_RandomBytes( void *buf, int len )
{
	if ( !buf || len < 0 ) {
		return 0;
	}
	return Sys_RandomBytes( (byte *)buf, len ) ? 1 : 0;
}

static void Imp_HomePath( char *out, int outSize )
{
	int len;

	if ( !out || outSize < 1 ) {
		return;
	}
	Cvar_VariableStringBuffer( "fs_homepath", out, outSize );
	len = (int)strlen( out );
	while ( len > 0 && ( out[len - 1] == '/' || out[len - 1] == '\\' ) ) {
		out[--len] = '\0';
	}
}

static void Imp_CvarRegister( const char *name, const char *defaultValue, int flags )
{
	int cvFlags = 0;

	if ( !name || !*name || !defaultValue ) {
		return;
	}
	if ( flags & ODF_CVAR_ARCHIVE ) {
		cvFlags |= CVAR_ARCHIVE_ND;
	}
	if ( flags & ODF_CVAR_PRIVATE ) {
		cvFlags |= CVAR_PRIVATE | CVAR_PROTECTED;
	}
	Cvar_Get( name, defaultValue, cvFlags );
}

static void Imp_CvarString( const char *name, char *out, int outSize )
{
	if ( !out || outSize < 1 ) {
		return;
	}
	if ( !name ) {
		out[0] = '\0';
		return;
	}
	Cvar_VariableStringBuffer( name, out, outSize );
}

static int Imp_CvarInteger( const char *name )
{
	return name ? Cvar_VariableIntegerValue( name ) : 0;
}

static int Imp_GetClientInfo( int clientNum, odfClientInfo_t *out )
{
	const client_t *cl;
	const char *accountName = "";
	int serial = 0;

	if ( !out || !svs.clients || clientNum < 0 || clientNum >= sv.maxclients ) {
		return 0;
	}
	cl = &svs.clients[clientNum];
	if ( cl->state < CS_CONNECTED ) {
		return 0;
	}

	Com_Memset( out, 0, sizeof( *out ) );
	out->clientNum = clientNum;
	out->state = cl->state == CS_CONNECTED ? ODF_CLIENT_CONNECTED : cl->state == CS_PRIMED ? ODF_CLIENT_PRIMED : ODF_CLIENT_ACTIVE;
	out->isBot = cl->netchan.remoteAddress.type == NA_BOT;
	Q_strncpyz( out->name, cl->name, sizeof( out->name ) );
	if ( NET_IsLocalAddress( &cl->netchan.remoteAddress ) ) {
		Q_strncpyz( out->ip, "127.0.0.1", sizeof( out->ip ) );
	} else {
		Q_strncpyz( out->ip, NET_AdrToString( &cl->netchan.remoteAddress ), sizeof( out->ip ) );
	}
	SV_Auth_ClientInfo( clientNum, &out->authState, &out->accountID, &accountName, &serial );
	out->serial = serial;
	Q_strncpyz( out->accountName, accountName, sizeof( out->accountName ) );
	return 1;
}

static void Imp_SendServerCommand( int clientNum, const char *command )
{
	if ( !command || !*command || !svs.clients ) {
		return;
	}
	if ( clientNum == -1 ) {
		SV_SendServerCommand( NULL, "%s", command );
	} else if ( clientNum >= 0 && clientNum < sv.maxclients && svs.clients[clientNum].state >= CS_CONNECTED ) {
		SV_SendServerCommand( &svs.clients[clientNum], "%s", command );
	}
}

static void Imp_AuthResult( int clientNum, int serial, int result, int64_t accountID, const char *accountName, const char *reason )
{
	SV_Auth_Result( clientNum, serial, result, accountID, accountName, reason );
}


// ---------------------------------------------------------------------------
// loading
// ---------------------------------------------------------------------------

static qboolean ValidPluginName( const char *name )
{
	int i, len = (int)strlen( name );

	if ( len < 1 || len >= MAX_PLUGIN_NAME ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		char c = name[i];

		if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '_' || c == '-' ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

static void FORMAT_PRINTF( 2, 3 ) Plugin_Fail( plugin_t *p, const char *fmt, ... );
static void Plugin_Fail( plugin_t *p, const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	Q_vsnprintf( p->note, sizeof( p->note ), fmt, ap );
	va_end( ap );
	p->state = PS_FAILED;
	Com_Printf( S_COLOR_YELLOW "Plugin '%s' not loaded: %s\n", p->name, p->note );
}

static void Plugin_Load( const char *name )
{
	plugin_t *p;
	odfPluginEntry_t entry;
	call_t call;
	FILE *f;
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		if ( !Q_stricmp( plugins[i].name, name ) ) {
			return;		// listed twice
		}
	}
	if ( numPlugins >= MAX_PLUGINS ) {
		Com_Printf( S_COLOR_YELLOW "Plugin '%s' not loaded: at most %i plugins\n", name, MAX_PLUGINS );
		return;
	}

	p = &plugins[numPlugins++];
	Com_Memset( p, 0, sizeof( *p ) );
	Q_strncpyz( p->name, name, sizeof( p->name ) );

	if ( !ValidPluginName( name ) ) {
		Plugin_Fail( p, "invalid name (letters, digits, _ and - only)" );
		return;
	}

	Q_strncpyz( p->path, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "plugins", va( "%s" PLUGIN_EXT, name ) ), sizeof( p->path ) );
	f = Sys_FOpen( p->path, "rb" );
	if ( !f ) {
		Plugin_Fail( p, "%s not found", p->path );
		return;
	}
	fclose( f );

	p->lib = Sys_LoadLibrary( p->path );
	if ( !p->lib ) {
		Plugin_Fail( p, "cannot load %s (missing dependency or wrong architecture?)", p->path );
		return;
	}
	entry = (odfPluginEntry_t)Sys_LoadFunction( p->lib, "ODF_PluginEntry" );
	if ( !entry ) {
		Plugin_Fail( p, "%s has no ODF_PluginEntry", p->path );
		Sys_UnloadLibrary( p->lib );
		p->lib = NULL;
		return;
	}

	p->guarded = guardInstalled && PluginGuard_ModuleRange( p->lib, (const void *)entry, &p->range );
	if ( !p->guarded ) {
		Com_Printf( S_COLOR_YELLOW "Plugin '%s': crash protection is not available on this platform, a crash in the plugin will stop the server.\n", name );
	}

	Com_Memset( &call, 0, sizeof( call ) );
	call.kind = PC_ENTRY;
	call.entry = entry;
	if ( !Plugin_Run( p, &call ) ) {
		return;		// state and message set by Plugin_Fault
	}
	p->exp = call.result;
	if ( !p->exp ) {
		Plugin_Fail( p, "the plugin refused to load" );
		return;
	}
	if ( p->exp->apiVersion > ODF_PLUGIN_API_VERSION ) {
		Plugin_Fail( p, "built for plugin API %i, this server has %i", p->exp->apiVersion, ODF_PLUGIN_API_VERSION );
		p->exp = NULL;
		return;
	}

	if ( p->exp->Init ) {
		Com_Memset( &call, 0, sizeof( call ) );
		call.kind = PC_INIT;
		if ( !Plugin_Run( p, &call ) ) {
			return;
		}
		if ( call.rc != 0 ) {
			Plugin_Fail( p, "Init returned %i", call.rc );
			return;
		}
	}

	p->state = PS_RUNNING;
	Com_Printf( "Plugin '%s' %s loaded%s\n", p->name, p->exp->version ? p->exp->version : "", p->guarded ? "" : " (unguarded)" );
}

static void Plugins_List_f( void )
{
	static const char *const stateNames[] = { "running", "stopped", "failed", "crashed" };
	int i;

	if ( !numPlugins ) {
		Com_Printf( "No plugins. Set sv_plugins to a list of names to load them from %s/plugins at startup.\n", Cvar_VariableString( "fs_homepath" ) );
		return;
	}
	Com_Printf( "%-16s %-8s %s\n", "name", "state", "details" );
	for ( i = 0; i < numPlugins; i++ ) {
		const plugin_t *p = &plugins[i];

		Com_Printf( "%-16s %-8s %s%s\n", p->name, stateNames[p->state],
			p->state == PS_RUNNING && p->exp->version ? p->exp->version : p->note,
			p->state == PS_RUNNING && !p->guarded ? " (no crash protection)" : "" );
	}
}

void SV_Plugins_Init( void )
{
	char list[MAX_CVAR_VALUE_STRING];
	char *s, *token;

	Cmd_AddCommand( "plugins", Plugins_List_f );

	sv_plugins = Cvar_Get( "sv_plugins", "", CVAR_INIT | CVAR_PROTECTED );
	Cvar_SetDescription( sv_plugins, "Plugins to load at startup, separated by spaces. Each is a library in the plugins folder of the home path." );

	Com_Memset( &imports, 0, sizeof( imports ) );
	imports.size = sizeof( imports );
	imports.apiVersion = ODF_PLUGIN_API_VERSION;
	imports.Print = Imp_Print;
	imports.Milliseconds = Imp_Milliseconds;
	imports.RandomBytes = Imp_RandomBytes;
	imports.HomePath = Imp_HomePath;
	imports.CvarRegister = Imp_CvarRegister;
	imports.CvarString = Imp_CvarString;
	imports.CvarInteger = Imp_CvarInteger;
	imports.GetClientInfo = Imp_GetClientInfo;
	imports.SendServerCommand = Imp_SendServerCommand;
	imports.AuthResult = Imp_AuthResult;

	Q_strncpyz( list, sv_plugins->string, sizeof( list ) );
	if ( !list[0] ) {
		return;
	}

	guardInstalled = PluginGuard_Init() != 0;

	for ( s = list; ( token = strtok( s, " ,;" ) ) != NULL; s = NULL ) {
		Plugin_Load( token );
	}
}

void SV_Plugins_Shutdown( void )
{
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING ) {
			if ( p->exp->Shutdown ) {
				call_t call;

				Com_Memset( &call, 0, sizeof( call ) );
				call.kind = PC_SHUTDOWN;
				if ( !Plugin_Run( p, &call ) ) {
					continue;
				}
			}
			p->state = PS_STOPPED;
		}
		// a crashed plugin stays mapped: nothing of it runs again, and unloading could run more of its code
		if ( p->lib && p->state != PS_FAULTED ) {
			Sys_UnloadLibrary( p->lib );
			p->lib = NULL;
		}
	}
	numPlugins = 0;

	if ( guardInstalled ) {
		PluginGuard_Shutdown();
		guardInstalled = qfalse;
	}
}


// ---------------------------------------------------------------------------
// running
// ---------------------------------------------------------------------------

void SV_Plugins_Frame( void )
{
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING && p->exp->Frame ) {
			call_t call;

			Com_Memset( &call, 0, sizeof( call ) );
			call.kind = PC_FRAME;
			call.time = svs.time;
			Plugin_Run( p, &call );
		}
	}
}

void SV_Plugin_Event( const char *name, const char *data )
{
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING && p->exp->OnEvent ) {
			call_t call;

			Com_Memset( &call, 0, sizeof( call ) );
			call.kind = PC_EVENT;
			call.name = name;
			call.data = data;
			Plugin_Run( p, &call );
		}
	}
}

/*
===============
SV_Plugin_GameEvent

An event raised by the game module with the trap_Event syscall. The strings come from
the VM, so they are checked before any plugin sees them.
===============
*/
void SV_Plugin_GameEvent( const char *name, const char *data )
{
	char eventName[MAX_PLUGIN_NAME];
	char eventData[MAX_INFO_STRING];
	size_t i;

	if ( !numPlugins || !name || !data ) {
		return;
	}
	for ( i = 0; i < sizeof( eventName ) - 1 && name[i]; i++ ) {
		char c = name[i];

		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) ) {
			return;		// event names are lower case words
		}
		eventName[i] = c;
	}
	if ( i == 0 || name[i] ) {
		return;
	}
	eventName[i] = '\0';

	for ( i = 0; i < sizeof( eventData ) - 1 && data[i]; i++ ) {
		eventData[i] = data[i];
	}
	if ( data[i] ) {
		return;		// too long
	}
	eventData[i] = '\0';

	SV_Plugin_Event( eventName, eventData );
}

void SV_Plugins_MapBegin( const char *mapname, int checksum )
{
	char data[MAX_INFO_STRING] = "";

	if ( !numPlugins ) {
		return;
	}
	Info_SetValueForKey( data, "map", mapname );
	Info_SetValueForKey( data, "checksum", va( "%i", checksum ) );
	SV_Plugin_Event( "map_begin", data );
}

#define CHAT_COMMAND_MAX	200

/*
===============
SV_Plugin_ChatCommand

Called for a client command on its way to the game. A chat message that starts with '!' ("!top vq3") is handed to the
plugins as the event "chat_command"; the game still gets the message as usual. Messages with characters that an event
cannot carry are not passed on.
===============
*/
void SV_Plugin_ChatCommand( client_t *cl )
{
	char text[CHAT_COMMAND_MAX + 1];
	char data[MAX_INFO_STRING] = "";
	const char *cmd;
	int i, len = 0;

	if ( !numPlugins || cl->state != CS_ACTIVE || cl->netchan.remoteAddress.type == NA_BOT ) {
		return;
	}
	cmd = Cmd_Argv( 0 );
	if ( Q_stricmp( cmd, "say" ) && Q_stricmp( cmd, "say_team" ) ) {
		return;
	}
	if ( Cmd_Argc() < 2 || Cmd_Argv( 1 )[0] != '!' ) {
		return;
	}

	// the words of the message, joined by single spaces, as the game would read them
	for ( i = 1; i < Cmd_Argc(); i++ ) {
		const char *word = Cmd_Argv( i );
		int n = (int)strlen( word );

		if ( len + n + 1 > CHAT_COMMAND_MAX ) {
			return;
		}
		if ( i > 1 ) {
			text[len++] = ' ';
		}
		memcpy( text + len, word, n );
		len += n;
	}
	text[len] = '\0';

	for ( i = 0; i < len; i++ ) {
		unsigned char c = (unsigned char)text[i];

		if ( c < 0x20 || c == 0x7F || c == '\\' || c == '"' || c == ';' ) {
			return;
		}
	}

	Info_SetValueForKey( data, "client", va( "%i", (int)( cl - svs.clients ) ) );
	Info_SetValueForKey( data, "serial", va( "%i", cl->auth.serial ) );
	Info_SetValueForKey( data, "text", text );
	SV_Plugin_Event( "chat_command", data );
}

qboolean SV_Plugins_HandleAuth( void )
{
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		if ( plugins[i].state == PS_RUNNING && ( plugins[i].exp->flags & ODF_PLUGIN_FLAG_AUTH ) ) {
			return qtrue;
		}
	}
	return qfalse;
}
