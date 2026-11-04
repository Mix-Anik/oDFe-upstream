/*
===========================================================================
Server plugins: QVM programs that run in a sandbox and react to what happens on the
server (a player finished a run, chat commands, logins, ...). A plugin can only do what the
functions of the engine it calls do, and only what the server operator allowed it to (capabilities); see
code/plugin_api/server.api. It has no files of its own outside the little store the engine
keeps for it, no network except the hosts the operator named, no system calls and no way to
touch engine memory. The sandbox itself is in plugin_vm.c.
===========================================================================
*/

#include "server.h"
#include "sv_http.h"
#include "../qcommon/plugin_vm.h"
#include "../plugin_api/include/odfs_types.h"

#define PLUGIN_EXT			".qvm"
#define MAX_PLUGINS			64
#define MAX_PLUGIN_LIST		8192		// the longest sv_plugins that is read
#define MAX_PLUGIN_NAME		32
#define MAX_PLUGIN_CVARS	32
#define MAX_CVAR_NAME		48
#define MAX_HTTP_HOSTS		4
#define MAX_SUBSCRIPTIONS	16		// events a plugin listens to
#define MAX_CHAT_COMMANDS	32		// "!name" commands of a plugin
#define MAX_EVENT_NAME		32
#define MAX_HOST_NAME		64
#define MAX_MODULE_FILE		( 8 * 1024 * 1024 )

#define STORE_MAX_NAME		63
#define STORE_MAX_CHUNK		( 1024 * 1024 )
// the limits of a store when the operator gives no size ("storage"); with "storage=64m" one file may use all of the 64 MB
#define STORE_DEFAULT_FILE	( 4 * 1024 * 1024 )
#define STORE_DEFAULT_TOTAL	( 8 * 1024 * 1024 )
#define STORE_LIMIT_MAX		( 1024 * 1024 * 1024 )		// the most an operator can give

// the structs cross into the plugin as they are: the same layout as in the QVM compiler (4 byte alignment at most)
typedef char odfClientInfoSize_t[ sizeof( odfClientInfo_t ) == 144 ? 1 : -1 ];
typedef char odfClientInfoAccount_t[ offsetof( odfClientInfo_t, accountIDLo ) == 100 ? 1 : -1 ];

typedef enum {
	PS_RUNNING,
	PS_STOPPED,		// shut down normally
	PS_FAILED,		// did not load or refused to start
	PS_FAULTED		// trapped or ran out of its budget, stopped
} pluginState_t;

typedef struct {
	char			name[MAX_PLUGIN_NAME];
	char			path[MAX_OSPATH];
	pluginVM_t		*vm;
	pluginState_t	state;
	char			note[192];		// why it is not running
	char			version[64];
	unsigned		caps;			// what the operator granted
	char			hosts[MAX_HTTP_HOSTS][MAX_HOST_NAME];		// http=host|host
	int				numHosts;
	char			cvars[MAX_PLUGIN_CVARS][MAX_CVAR_NAME];		// the cvars it created
	qboolean		cvarPrivate[MAX_PLUGIN_CVARS];
	int				numCvars;
	int				storeTotal;		// bytes in its store
	int				storeMaxFile;	// storage[=size]: the most one file can have
	int				storeMaxTotal;	// and all of its files
	int				httpAnswer;		// the id of the request whose answer it may read right now
	struct {
		char		name[MAX_EVENT_NAME];
		int			handler;
	}				events[MAX_SUBSCRIPTIONS];
	int				numEvents;
	struct {
		char		name[MAX_EVENT_NAME];
		int			handler;
	}				chatCommands[MAX_CHAT_COMMANDS];
	int				numChatCommands;
} plugin_t;

// what to call when a request of a plugin is done (the engine keeps it: the request knows only its plugin)
typedef struct {
	int				id;			// 0: free
	plugin_t		*plugin;
	int				handler;
	int				userData;
} httpCall_t;
static httpCall_t		httpCalls[SV_HTTP_MAX_RUNNING + SV_HTTP_MAX_FINISHED];

static plugin_t			plugins[MAX_PLUGINS];
static int				numPlugins;
static plugin_t			*current;		// the plugin whose code is running, for log prefixes and the cvars it may use
static cvar_t			*sv_plugins;
static qboolean			httpStarted;

// The generated dispatcher calls ODF_CheatsAllowed() for a function marked "cheat" in server.api (there is none yet). Define it
// when the first one is added: return Cvar_VariableIntegerValue( "sv_cheats" ) != 0;
#include "../plugin_api/generated/sv_plugin_api.inc"
#include "../plugin_api/plugin_common_imp.h"

#ifdef ODF_PLUGIN_TESTS
// Two calls that only the test build has: the mock plugin keeps its state in the test program, outside of its sandbox.
extern void Test_MirrorPull( const char *plugin, void *dst, int size );
extern void Test_MirrorPush( const char *plugin, const void *src, int size );
#define ODFS_TEST_PULL	ODFS_NUM_IMPORTS
#define ODFS_TEST_PUSH	( ODFS_NUM_IMPORTS + 1 )
#endif

// what a plugin calls goes through here
static intptr_t Plugin_Dispatch( pluginVM_t *vm, intptr_t *args )
{
#ifdef ODF_PLUGIN_TESTS
	if ( args[0] == ODFS_TEST_PULL || args[0] == ODFS_TEST_PUSH ) {
		void *p = NULL;

		if ( args[2] > 0 ) {
			PluginVM_Ptr( vm, args[1], (unsigned)args[2], 1, qfalse, &p );
			if ( args[0] == ODFS_TEST_PULL ) {
				Test_MirrorPull( current->name, p, (int)args[2] );
			} else {
				Test_MirrorPush( current->name, p, (int)args[2] );
			}
		}
		return 0;
	}
#endif
	return Odfs_Dispatch( vm, args );
}


// ---------------------------------------------------------------------------
// calls into a plugin
// ---------------------------------------------------------------------------

static void Plugin_Fault( plugin_t *p, const char *what );

// Runs one call of a plugin. A plugin that traps (bad memory access, bad pointer, out of budget) is stopped.
#define PLUGIN_RUN( p_, what_, call_ ) \
	do { \
		plugin_t *prev_ = current; \
		qboolean ok_; \
		current = (p_); \
		ok_ = (call_); \
		current = prev_; \
		if ( !ok_ ) { \
			Plugin_Fault( (p_), (what_) ); \
		} \
	} while ( 0 )

#define PLUGIN_RUN_OK( ok_, p_, what_, call_ ) \
	do { \
		plugin_t *prev_ = current; \
		current = (p_); \
		(ok_) = (call_); \
		current = prev_; \
		if ( !(ok_) ) { \
			Plugin_Fault( (p_), (what_) ); \
		} \
	} while ( 0 )

#define PLUGIN_HAS( p_, fn_ ) \
	( (p_)->vm && PluginVM_Has( (p_)->vm, ODFS_EXP_##fn_ ) )


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

// seconds since 1970 (UTC) as an unsigned number: the VM has no 64 bit integers, and this is good until 2106
static int Imp_UnixTime( void )
{
	return (int)(unsigned)time( NULL );
}

static int Imp_RandomBytes( char *buf, int len )
{
	if ( !buf || len < 0 || len > 4096 ) {
		return 0;
	}
	return Sys_RandomBytes( (byte *)buf, len ) ? 1 : 0;
}

// The cvars a plugin may create. The names of the engine and the game are theirs.
static const char *const reservedPrefixes[] = {
	"sv_", "g_", "df_", "com_", "fs_", "net_", "rcon", "cl_", "cg_", "r_", "s_", "ui_", "vm_", "bot_", "cvar_", "pmove_", "timelimit", "fraglimit"
};

static qboolean ValidCvarName( const char *name )
{
	int i, len = (int)strlen( name );
	qboolean underscore = qfalse;

	if ( len < 3 || len >= MAX_CVAR_NAME ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		char c = name[i];

		if ( c == '_' && i > 0 ) {
			underscore = qtrue;
		} else if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ) ) {
			return qfalse;
		}
	}
	if ( !underscore ) {
		return qfalse;
	}
	for ( i = 0; i < (int)ARRAY_LEN( reservedPrefixes ); i++ ) {
		if ( !Q_stricmpn( name, reservedPrefixes[i], (int)strlen( reservedPrefixes[i] ) ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

static int Plugin_CvarIndex( const plugin_t *p, const char *name )
{
	int i;

	for ( i = 0; i < p->numCvars; i++ ) {
		if ( !Q_stricmp( p->cvars[i], name ) ) {
			return i;
		}
	}
	return -1;
}

static void Imp_CvarRegister( const char *name, const char *defaultValue, int flags )
{
	int cvFlags = 0;
	unsigned existing;

	if ( !current || !name || !defaultValue || !ValidCvarName( name ) ) {
		if ( current && name && *name ) {
			Com_Printf( S_COLOR_YELLOW "Plugin '%s': cannot create the cvar %s (invalid or reserved name)\n", current->name, name );
		}
		return;
	}
	if ( Plugin_CvarIndex( current, name ) < 0 ) {
		// a value the operator set in a config before the plugin started is fine, a cvar of anyone else is not
		existing = Cvar_Flags( name );
		if ( existing != CVAR_NONEXISTENT && !( existing & CVAR_USER_CREATED ) ) {
			Com_Printf( S_COLOR_YELLOW "Plugin '%s': the cvar %s already exists\n", current->name, name );
			return;
		}
		if ( current->numCvars >= MAX_PLUGIN_CVARS ) {
			return;
		}
		Q_strncpyz( current->cvars[current->numCvars], name, MAX_CVAR_NAME );
		current->cvarPrivate[current->numCvars] = ( flags & ODF_CVAR_PRIVATE ) ? qtrue : qfalse;
		current->numCvars++;
	}
	if ( flags & ODF_CVAR_ARCHIVE ) {
		cvFlags |= CVAR_ARCHIVE_ND;
	}
	if ( flags & ODF_CVAR_PRIVATE ) {
		cvFlags |= CVAR_PRIVATE | CVAR_PROTECTED;
	}
	Cvar_Get( name, defaultValue, cvFlags );
}

// A plugin reads the cvars it created and a few settings of the server; nothing else (not passwords, not the cvars of other plugins).
static const char *const readablePrefixes[] = {
	"df_", "mapname", "sv_hostname", "sv_maxclients", "sv_mapChecksum", "sv_fps", "sv_cheats", "g_gametype", "g_speed", "g_gravity",
	"pmove_", "timelimit", "fraglimit", "version", "protocol"
};

static qboolean Plugin_CanRead( const plugin_t *p, const char *name )
{
	int i;

	if ( !p || !name || !*name ) {
		return qfalse;
	}
	if ( Plugin_CvarIndex( p, name ) >= 0 ) {
		return qtrue;
	}
	for ( i = 0; i < (int)ARRAY_LEN( readablePrefixes ); i++ ) {
		if ( !Q_stricmpn( name, readablePrefixes[i], (int)strlen( readablePrefixes[i] ) ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void Imp_CvarString( const char *name, char *out, int outSize )
{
	if ( !out || outSize < 1 ) {
		return;
	}
	out[0] = '\0';
	if ( !Plugin_CanRead( current, name ) ) {
		return;
	}
	// a private cvar is the plugin's own (nobody else gets here), so it may read it; the others are plain settings
	Cvar_VariableStringBuffer( name, out, outSize );
}

static int Imp_CvarInteger( const char *name )
{
	char buf[MAX_CVAR_VALUE_STRING];

	Imp_CvarString( name, buf, sizeof( buf ) );
	return atoi( buf );
}

static int Imp_GetClientInfo( int clientNum, odfClientInfo_t *out )
{
	const client_t *cl;
	const char *accountName = "";
	int64_t accountID = 0;
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
	// the address of a player is private: only a plugin the operator trusted with it gets it
	if ( current && ( current->caps & ODFS_CAP_ips ) ) {
		if ( NET_IsLocalAddress( &cl->netchan.remoteAddress ) ) {
			Q_strncpyz( out->ip, "127.0.0.1", sizeof( out->ip ) );
		} else {
			Q_strncpyz( out->ip, NET_AdrToString( &cl->netchan.remoteAddress ), sizeof( out->ip ) );
		}
	}
	SV_Auth_ClientInfo( clientNum, &out->authState, &accountID, &accountName, &serial );
	out->accountIDLo = (unsigned)( (uint64_t)accountID & 0xFFFFFFFFu );
	out->accountIDHi = (unsigned)( (uint64_t)accountID >> 32 );
	out->serial = serial;
	Q_strncpyz( out->accountName, accountName, sizeof( out->accountName ) );
	return 1;
}

// the commands a plugin may send without the capability rawcmd: text for the player's screen
static qboolean PlainMessage( const char *command )
{
	static const char *const words[] = { "print", "cp", "chat", "tchat" };
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( words ); i++ ) {
		int len = (int)strlen( words[i] );

		if ( !Q_strncmp( command, words[i], len ) && ( command[len] == ' ' || command[len] == '\0' ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void Imp_SendServerCommand( int clientNum, const char *command )
{
	if ( !command || !*command || !svs.clients || !current ) {
		return;
	}
	if ( !( current->caps & ODFS_CAP_rawcmd ) && !PlainMessage( command ) ) {
		Com_Printf( S_COLOR_YELLOW "Plugin '%s': the command is not allowed without rawcmd\n", current->name );
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

// ---- HTTP ----

// the host of http://host[:port]/path or https://..., or NULL for anything else
static qboolean UrlHost( const char *url, char *host, int hostSize )
{
	const char *s, *end;
	int len, i;

	if ( !Q_strncmp( url, "https://", 8 ) ) {
		s = url + 8;
	} else if ( !Q_strncmp( url, "http://", 7 ) ) {
		s = url + 7;
	} else {
		return qfalse;
	}
	for ( i = 0; url[i]; i++ ) {
		if ( (unsigned char)url[i] <= ' ' || url[i] == 0x7F || url[i] == '\\' ) {
			return qfalse;
		}
	}
	end = s;
	while ( *end && *end != '/' && *end != ':' && *end != '?' && *end != '#' ) {
		if ( *end == '@' || *end == '[' ) {
			return qfalse;		// credentials in the address, or an IPv6 literal: not supported
		}
		end++;
	}
	len = (int)( end - s );
	if ( len < 1 || len >= hostSize ) {
		return qfalse;
	}
	Com_Memcpy( host, s, len );
	host[len] = '\0';
	// a port, if there is one, is digits
	if ( *end == ':' ) {
		end++;
		if ( *end < '0' || *end > '9' ) {
			return qfalse;
		}
		while ( *end >= '0' && *end <= '9' ) {
			end++;
		}
		if ( *end && *end != '/' && *end != '?' && *end != '#' ) {
			return qfalse;
		}
	}
	return qtrue;
}

static int Imp_HttpRequest( int method, const char *url, const char *headers, const char *body, int bodyLen, int timeoutMs,
	int handler, int userData )
{
	char host[MAX_HOST_NAME];
	httpCall_t *call = NULL;
	int i, id;

	if ( !current || !url || handler == 0 || !UrlHost( url, host, sizeof( host ) ) ) {
		return 0;
	}
	for ( i = 0; i < current->numHosts; i++ ) {
		if ( !Q_stricmp( current->hosts[i], host ) ) {
			break;
		}
	}
	if ( i >= current->numHosts ) {
		Com_Printf( S_COLOR_YELLOW "Plugin '%s': %s is not one of the hosts it may talk to\n", current->name, host );
		return 0;
	}
	for ( i = 0; i < (int)ARRAY_LEN( httpCalls ); i++ ) {
		if ( !httpCalls[i].id ) {
			call = &httpCalls[i];
			break;
		}
	}
	if ( !call ) {
		return 0;
	}
	id = SV_Http_Start( method, url, headers, body, bodyLen, timeoutMs, current );
	if ( id > 0 ) {
		call->id = id;
		call->plugin = current;
		call->handler = handler;
		call->userData = userData;
	}
	return id;
}

static int Imp_HttpBody( int id, int offset, char *out, int outSize )
{
	if ( !current || id <= 0 || id != current->httpAnswer ) {
		return 0;		// only the answer that is being handed over right now
	}
	return SV_Http_Body( id, offset, out, outSize );
}

// ---- events and chat commands ----

static unsigned EventCap( const char *name );

// lower case letters, digits and _ (the names of events and of chat commands)
static qboolean ValidWord( const char *name )
{
	int i, len = (int)strlen( name );

	if ( len < 1 || len >= MAX_EVENT_NAME ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		char c = name[i];

		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

static int Imp_EventSubscribe( const char *name, int handler )
{
	unsigned cap;

	if ( !current || !name || handler == 0 || !ValidWord( name ) || current->numEvents >= MAX_SUBSCRIPTIONS ) {
		return 0;
	}
	cap = EventCap( name );
	if ( cap && !( current->caps & cap ) ) {
		return 0;
	}
	Q_strncpyz( current->events[current->numEvents].name, name, MAX_EVENT_NAME );
	current->events[current->numEvents].handler = handler;
	current->numEvents++;
	return 1;
}

static int Imp_ChatCommandRegister( const char *name, int handler )
{
	if ( !current || !name || handler == 0 || !ValidWord( name ) || current->numChatCommands >= MAX_CHAT_COMMANDS ) {
		return 0;
	}
	Q_strncpyz( current->chatCommands[current->numChatCommands].name, name, MAX_EVENT_NAME );
	current->chatCommands[current->numChatCommands].handler = handler;
	current->numChatCommands++;
	return 1;
}

// ---- the store ----

static qboolean ValidStoreName( const char *name )
{
	int i, len = (int)strlen( name );

	if ( len < 1 || len > STORE_MAX_NAME || name[0] == '.' ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		char c = name[i];

		if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '.' || c == '_' || c == '-' ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

static const char *StoreDir( const plugin_t *p )
{
	return FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), va( "plugin-data/%s", p->name ), "" );
}

static const char *StorePath( const plugin_t *p, const char *name )
{
	return FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), va( "plugin-data/%s", p->name ), name );
}

static int FileSize( const char *path )
{
	FILE *f = Sys_FOpen( path, "rb" );
	long size;

	if ( !f ) {
		return -1;
	}
	fseek( f, 0, SEEK_END );
	size = ftell( f );
	fclose( f );
	return size < 0 || size > 0x7fffffff ? -1 : (int)size;
}

static int Imp_StoreSize( const char *name )
{
	if ( !current || !ValidStoreName( name ) ) {
		return -1;
	}
	return FileSize( StorePath( current, name ) );
}

static int Imp_StoreRead( const char *name, int offset, char *out, int outSize )
{
	FILE *f;
	int n;

	if ( !current || !ValidStoreName( name ) || offset < 0 || !out || outSize < 1 || outSize > STORE_MAX_CHUNK ) {
		return -1;
	}
	f = Sys_FOpen( StorePath( current, name ), "rb" );
	if ( !f ) {
		return -1;
	}
	if ( fseek( f, offset, SEEK_SET ) != 0 ) {
		fclose( f );
		return 0;
	}
	n = (int)fread( out, 1, outSize, f );
	fclose( f );
	return n;
}

static int Imp_StoreWrite( const char *name, const char *data, int len, int append )
{
	FILE *f;
	int before, after;

	if ( !current || !ValidStoreName( name ) || len < 0 || len > STORE_MAX_CHUNK || ( len > 0 && !data ) ) {
		return 0;
	}
	before = FileSize( StorePath( current, name ) );
	after = ( append && before > 0 ? before : 0 ) + len;
	if ( after > current->storeMaxFile || (long long)current->storeTotal - ( before > 0 ? before : 0 ) + after > current->storeMaxTotal ) {
		return 0;
	}
	Sys_Mkdir( FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "plugin-data", "" ) );
	Sys_Mkdir( StoreDir( current ) );
	f = Sys_FOpen( StorePath( current, name ), append ? "ab" : "wb" );
	if ( !f ) {
		return 0;
	}
	if ( len > 0 && fwrite( data, 1, len, f ) != (size_t)len ) {
		fclose( f );
		return 0;
	}
	fclose( f );
	current->storeTotal += after - ( before > 0 ? before : 0 );
	return 1;
}

static int Imp_StoreDelete( const char *name )
{
	int size;

	if ( !current || !ValidStoreName( name ) ) {
		return 0;
	}
	size = FileSize( StorePath( current, name ) );
	if ( size < 0 || remove( StorePath( current, name ) ) != 0 ) {
		return 0;
	}
	current->storeTotal -= size;
	return 1;
}

// what is in the store already, for the quota
static void Store_Measure( plugin_t *p )
{
	char **files;
	int numFiles, i;

	p->storeTotal = 0;
	files = Sys_ListFiles( StoreDir( p ), NULL, NULL, &numFiles, qfalse );
	if ( !files ) {
		return;
	}
	for ( i = 0; i < numFiles; i++ ) {
		int size = FileSize( StorePath( p, files[i] ) );

		if ( size > 0 ) {
			p->storeTotal += size;
		}
	}
	Sys_FreeFileList( files );
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

static void Plugin_Unload( plugin_t *p )
{
	int i;

	SV_Http_Abort( p );
	for ( i = 0; i < (int)ARRAY_LEN( httpCalls ); i++ ) {
		if ( httpCalls[i].plugin == p ) {
			Com_Memset( &httpCalls[i], 0, sizeof( httpCalls[i] ) );
		}
	}
	PluginVM_Free( p->vm );
	p->vm = NULL;
}

static void Plugin_Fail( plugin_t *p, const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	Q_vsnprintf( p->note, sizeof( p->note ), fmt, ap );
	va_end( ap );
	p->state = PS_FAILED;
	Com_Printf( S_COLOR_YELLOW "Plugin '%s' not loaded: %s\n", p->name, p->note );
	Plugin_Unload( p );
}

// A plugin that trapped (bad memory access, a pointer outside its memory, out of instructions) is stopped and not called again.
static void Plugin_Fault( plugin_t *p, const char *what )
{
	Com_sprintf( p->note, sizeof( p->note ), "stopped in %s: %s", what, PluginVM_Error( p->vm ) );
	p->state = PS_FAULTED;
	Com_Printf( S_COLOR_RED "Plugin '%s' stopped in %s: %s\n" S_COLOR_RED "It will not run again until the server is restarted.\n",
		p->name, what, PluginVM_Error( p->vm ) );
	Plugin_Unload( p );
	SV_Auth_PluginStopped();
}

// the value of a "key=value" line of the manifest of a plugin
static qboolean Manifest_Value( const char *manifest, const char *key, char *out, int outSize )
{
	int keyLen = (int)strlen( key );
	const char *s = manifest;

	while ( *s ) {
		const char *end = s;

		while ( *end && *end != '\n' ) {
			end++;
		}
		if ( !Q_strncmp( s, key, keyLen ) && s[keyLen] == '=' ) {
			int len = (int)( end - ( s + keyLen + 1 ) );

			while ( len > 0 && s[keyLen + len] == '\r' ) {
				len--;
			}
			if ( len >= outSize ) {
				len = outSize - 1;
			}
			Com_Memcpy( out, s + keyLen + 1, len );
			out[len] = '\0';
			return qtrue;
		}
		s = *end ? end + 1 : end;
	}
	out[0] = '\0';
	return qfalse;
}

static unsigned CapByName( const char *name, int len )
{
	int i;

	for ( i = 0; i < ODFS_NUM_CAPS; i++ ) {
		if ( (int)strlen( odfs_capNames[i] ) == len && !Q_stricmpn( odfs_capNames[i], name, len ) ) {
			return 1u << i;
		}
	}
	return 0;
}

// "64m", "512k", "1g", "3000000": a size in bytes (k, m and g are 1024 based). qfalse when it is not that, is 0 or is over
// STORE_LIMIT_MAX.
static qboolean ParseStoreSize( const char *s, int len, int *out )
{
	long long value = 0, unit = 1;
	int i;

	if ( len < 1 ) {
		return qfalse;
	}
	switch ( s[len - 1] ) {
		case 'k': case 'K': unit = 1024; len--; break;
		case 'm': case 'M': unit = 1024 * 1024; len--; break;
		case 'g': case 'G': unit = 1024 * 1024 * 1024; len--; break;
		default: break;
	}
	if ( len < 1 || len > 10 ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		if ( s[i] < '0' || s[i] > '9' ) {
			return qfalse;
		}
		value = value * 10 + ( s[i] - '0' );
	}
	if ( value < 1 || value > STORE_LIMIT_MAX / unit ) {
		return qfalse;
	}
	value *= unit;
	*out = (int)value;
	return qtrue;
}

/*
===============
Plugin_ParseGrants

"auth,chat,storage=64m,http=api.example.com|other.example.com": the capabilities of a plugin. storage may be followed by the
size of the store (digits and k, m or g), http by the hosts. Returns qfalse (and says why in err) when a name is not a
capability or a value is wrong.
===============
*/
static qboolean Plugin_ParseGrants( plugin_t *p, const char *text, char *err, int errSize )
{
	const char *s = text;

	p->caps = 0;
	p->numHosts = 0;
	p->storeMaxFile = STORE_DEFAULT_FILE;
	p->storeMaxTotal = STORE_DEFAULT_TOTAL;
	while ( *s ) {
		const char *end = s, *eq = NULL;
		unsigned cap;

		while ( *end && *end != ',' ) {
			if ( *end == '=' && !eq ) {
				eq = end;
			}
			end++;
		}
		cap = CapByName( s, (int)( ( eq ? eq : end ) - s ) );
		if ( !cap ) {
			Com_sprintf( err, errSize, "unknown capability '%.*s'", (int)( ( eq ? eq : end ) - s ), s );
			return qfalse;
		}
		p->caps |= cap;
		if ( eq && cap == ODFS_CAP_storage ) {
			int size;

			if ( !ParseStoreSize( eq + 1, (int)( end - ( eq + 1 ) ), &size ) ) {
				Com_sprintf( err, errSize, "bad size for storage '%.*s': a number with k, m or g (1k to 1g), like storage=64m",
					(int)( end - ( eq + 1 ) ), eq + 1 );
				return qfalse;
			}
			p->storeMaxTotal = size;
			p->storeMaxFile = size;
		} else if ( eq ) {
			const char *h = eq + 1;

			if ( cap != ODFS_CAP_http ) {
				Com_sprintf( err, errSize, "the capability '%.*s' takes no value", (int)( eq - s ), s );
				return qfalse;
			}
			while ( h < end ) {
				const char *he = h;
				int len;

				while ( he < end && *he != '|' ) {
					he++;
				}
				len = (int)( he - h );
				if ( len < 1 || len >= MAX_HOST_NAME || p->numHosts >= MAX_HTTP_HOSTS ) {
					Com_sprintf( err, errSize, "bad host list for http" );
					return qfalse;
				}
				Com_Memcpy( p->hosts[p->numHosts], h, len );
				p->hosts[p->numHosts][len] = '\0';
				p->numHosts++;
				h = he < end ? he + 1 : he;
			}
		}
		s = *end ? end + 1 : end;
	}
	return qtrue;
}

static void Plugin_Config( pluginVMConfig_t *cfg, const plugin_t *p )
{
	Com_Memset( cfg, 0, sizeof( *cfg ) );
	cfg->name = p->name;
	cfg->module = "odfs";
	cfg->dispatch = Plugin_Dispatch;
	cfg->exportNames = odfs_exportNames;
	cfg->numExports = ODFS_NUM_EXPORTS;
	cfg->grantedCaps = p->caps;
	cfg->capNames = odfs_capNames;
	cfg->numCaps = ODFS_NUM_CAPS;
	cfg->maxDataBytes = 32 * 1024 * 1024;
}

static void *Plugin_ReadFile( const char *path, int *size, char *err, int errSize )
{
	FILE *f = Sys_FOpen( path, "rb" );
	long len;
	void *data;

	if ( !f ) {
		Com_sprintf( err, errSize, "%s not found", path );
		return NULL;
	}
	fseek( f, 0, SEEK_END );
	len = ftell( f );
	fseek( f, 0, SEEK_SET );
	if ( len < 8 || len > MAX_MODULE_FILE ) {
		fclose( f );
		Com_sprintf( err, errSize, "%s is not a plugin (size %ld)", path, len );
		return NULL;
	}
	data = malloc( len );
	if ( !data || fread( data, 1, len, f ) != (size_t)len ) {
		free( data );
		fclose( f );
		Com_sprintf( err, errSize, "cannot read %s", path );
		return NULL;
	}
	fclose( f );
	*size = (int)len;
	return data;
}

static void Plugin_Load( const char *entry )
{
	plugin_t *p;
	pluginVMConfig_t cfg;
	char name[MAX_PLUGIN_NAME + 1], err[256], value[64];
	const char *colon = strchr( entry, ':' );
	void *data;
	int i, size = 0, rc = 0;
	qboolean ok;

	i = (int)( colon ? colon - entry : strlen( entry ) );
	if ( i >= (int)sizeof( name ) ) {
		i = (int)sizeof( name ) - 1;
	}
	Com_Memcpy( name, entry, i );
	name[i] = '\0';

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
	if ( colon && !Plugin_ParseGrants( p, colon + 1, err, sizeof( err ) ) ) {
		Plugin_Fail( p, "%s", err );
		return;
	}

	if ( ( p->caps & ODFS_CAP_http ) && !httpStarted ) {
		httpStarted = SV_Http_Init();
		if ( !httpStarted ) {
			Plugin_Fail( p, "it may use http, but libcurl is not available on this server" );
			return;
		}
	}

	Q_strncpyz( p->path, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "plugins", va( "%s" PLUGIN_EXT, name ) ), sizeof( p->path ) );
	data = Plugin_ReadFile( p->path, &size, err, sizeof( err ) );
	if ( !data ) {
		Plugin_Fail( p, "%s", err );
		return;
	}

	Plugin_Config( &cfg, p );
	p->vm = PluginVM_Load( data, size, &cfg, p, err, sizeof( err ) );
	free( data );
	if ( !p->vm ) {
		if ( !Q_strncmp( err, "needs capabilities that were not granted: ", 42 ) ) {
			// say what to write, so the operator can decide
			Plugin_Fail( p, "%s. If you trust it, list them: sv_plugins \"%s:%s\"", err, name, err + 42 );
		} else {
			Plugin_Fail( p, "%s", err );
		}
		return;
	}

	Manifest_Value( PluginVM_Manifest( p->vm ), "version", p->version, sizeof( p->version ) );
	if ( Manifest_Value( PluginVM_Manifest( p->vm ), "api", value, sizeof( value ) ) && atoi( value ) > ODFS_API_VERSION ) {
		Plugin_Fail( p, "built for server plugin API %i, this server has %i", atoi( value ), ODFS_API_VERSION );
		return;
	}
	// capabilities it asks for in its manifest: "caps=chat,auth"
	if ( Manifest_Value( PluginVM_Manifest( p->vm ), "caps", value, sizeof( value ) ) ) {
		const char *s = value;

		while ( *s ) {
			const char *end = s;
			unsigned cap;

			while ( *end && *end != ',' ) {
				end++;
			}
			cap = CapByName( s, (int)( end - s ) );
			if ( cap && !( p->caps & cap ) ) {
				Plugin_Fail( p, "needs capabilities that were not granted: %.*s. If you trust it, list them: sv_plugins \"%s:%s\"",
					(int)( end - s ), s, name, value );
				return;
			}
			s = *end ? end + 1 : end;
		}
	}

	if ( p->caps & ODFS_CAP_storage ) {
		Store_Measure( p );
	}

	if ( PLUGIN_HAS( p, OnInit ) ) {
		PLUGIN_RUN_OK( ok, p, "OnInit", ODFS_Call_OnInit( p->vm, &rc ) );
		if ( !ok ) {
			return;
		}
		if ( rc != 0 ) {
			Plugin_Fail( p, "init returned %i", rc );
			return;
		}
	}

	p->state = PS_RUNNING;
	Com_Printf( "Plugin '%s' %s loaded\n", p->name, p->version );
}

static void Plugins_List_f( void )
{
	static const char *const stateNames[] = { "running", "stopped", "failed", "stopped*" };
	int i, c;

	// plugins stats: what the calls of every plugin cost (wall time, with the engine's work for its imports); plugins reset
	if ( !Q_stricmp( Cmd_Argv( 1 ), "stats" ) || !Q_stricmp( Cmd_Argv( 1 ), "reset" ) ) {
		qboolean reset = Q_stricmp( Cmd_Argv( 1 ), "reset" ) == 0;

		for ( i = 0; i < numPlugins; i++ ) {
			plugin_t *p = &plugins[i];

			if ( !p->vm ) {
				continue;
			}
			if ( reset ) {
				PluginVM_ResetStats( p->vm );
				continue;
			}
			Com_Printf( "%s\n", p->name );
			PluginVM_PrintStats( p->vm, odfs_exportNames, ODFS_NUM_EXPORTS );
		}
		return;
	}

	if ( !numPlugins ) {
		Com_Printf( "No plugins. Set sv_plugins to a list of names to load them from %s/plugins at startup.\n", Cvar_VariableString( "fs_homepath" ) );
		return;
	}
	Com_Printf( "%-16s %-8s %s\n", "name", "state", "details" );
	for ( i = 0; i < numPlugins; i++ ) {
		const plugin_t *p = &plugins[i];
		char caps[128] = "";

		for ( c = 0; c < ODFS_NUM_CAPS; c++ ) {
			if ( p->caps & ( 1u << c ) ) {
				if ( caps[0] ) {
					Q_strcat( caps, sizeof( caps ), "," );
				}
				Q_strcat( caps, sizeof( caps ), odfs_capNames[c] );
			}
		}
		Com_Printf( "%-16s %-8s %s%s%s\n", p->name, stateNames[p->state],
			p->state == PS_RUNNING ? p->version : p->note, caps[0] ? "  caps: " : "", caps );
	}
}

void SV_Plugins_Init( void )
{
	static char list[MAX_PLUGIN_LIST];
	char *s, *token;

	Cmd_AddCommand( "plugins", Plugins_List_f );

	sv_plugins = Cvar_Get( "sv_plugins", "", CVAR_INIT | CVAR_PROTECTED );
	Cvar_SetDescription( sv_plugins, "Plugins to load at startup, separated by spaces: name or name:capability,capability,http=host (see docs/plugins.md)." );

	Q_strncpyz( list, sv_plugins->string, sizeof( list ) );
	if ( !list[0] ) {
		return;
	}
	if ( strlen( sv_plugins->string ) >= sizeof( list ) ) {
		Com_Printf( S_COLOR_YELLOW "sv_plugins is longer than %i characters, the rest is ignored\n", MAX_PLUGIN_LIST - 1 );
	}

	for ( s = list; ( token = strtok( s, " \t;" ) ) != NULL; s = NULL ) {
		Plugin_Load( token );
	}
}

void SV_Plugins_Shutdown( void )
{
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING && PLUGIN_HAS( p, OnShutdown ) ) {
			PLUGIN_RUN( p, "OnShutdown", ODFS_Call_OnShutdown( p->vm ) );
		}
		if ( p->state == PS_RUNNING ) {
			p->state = PS_STOPPED;
		}
		Plugin_Unload( p );
	}
	numPlugins = 0;

	if ( httpStarted ) {
		SV_Http_Shutdown();
		httpStarted = qfalse;
	}
}


// ---------------------------------------------------------------------------
// running
// ---------------------------------------------------------------------------

void SV_Plugins_Frame( void )
{
	int i, id, status, bodyLen;
	void *owner;

	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING && PLUGIN_HAS( p, OnFrame ) ) {
			PLUGIN_RUN( p, "OnFrame", ODFS_Call_OnFrame( p->vm, svs.time ) );
		}
	}

	if ( httpStarted ) {
		SV_Http_Pump();
		while ( SV_Http_Finished( &id, &status, &bodyLen, &owner ) ) {
			plugin_t *p = (plugin_t *)owner;
			httpCall_t *call = NULL;
			int k;

			for ( k = 0; k < (int)ARRAY_LEN( httpCalls ); k++ ) {
				if ( httpCalls[k].id == id && httpCalls[k].plugin == p ) {
					call = &httpCalls[k];
					break;
				}
			}
			if ( call && p->state == PS_RUNNING && PLUGIN_HAS( p, HttpHandler ) ) {
				int handler = call->handler, userData = call->userData;

				p->httpAnswer = id;
				PLUGIN_RUN( p, "http handler", ODFS_Call_HttpHandler( p->vm, handler, id, status, bodyLen, userData ) );
				p->httpAnswer = 0;
			}
			if ( call ) {
				Com_Memset( call, 0, sizeof( *call ) );
			}
			SV_Http_Release( id );
		}
	}
}

// the capability an event needs (0: every plugin gets it)
static unsigned EventCap( const char *name )
{
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( odfs_eventCaps ); i++ ) {
		if ( !strcmp( odfs_eventCaps[i].name, name ) ) {
			return odfs_eventCaps[i].cap;
		}
	}
	return 0;
}

void SV_Plugin_Event( const char *name, const char *data )
{
	unsigned cap = EventCap( name );
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];
		char stripped[MAX_INFO_STRING];
		const char *send = data;
		int k;

		if ( p->state != PS_RUNNING || !p->numEvents || !PLUGIN_HAS( p, EventHandler ) || ( cap && !( p->caps & cap ) ) ) {
			continue;
		}
		// the address of a player is only for the plugins that may have it
		if ( !( p->caps & ODFS_CAP_ips ) && strstr( data, "\\ip\\" ) ) {
			Q_strncpyz( stripped, data, sizeof( stripped ) );
			Info_RemoveKey( stripped, "ip" );
			send = stripped;
		}
		for ( k = 0; k < p->numEvents && p->state == PS_RUNNING; k++ ) {
			if ( !strcmp( p->events[k].name, name ) ) {
				PLUGIN_RUN( p, "event handler", ODFS_Call_EventHandler( p->vm, p->events[k].handler, send ) );
			}
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

	// the game module raises its own events, not the ones the engine raises (logins, chat, players leaving, maps)
	{
		static const char *const engineEvents[] = { "auth_response", "chat_command", "client_disconnect", "map_begin" };
		int k;

		for ( k = 0; k < (int)ARRAY_LEN( engineEvents ); k++ ) {
			if ( !strcmp( eventName, engineEvents[k] ) ) {
				return;
			}
		}
	}
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

// text is "!name arguments": hands it to the plugins that registered the name
static void ChatCommand( int clientNum, int serial, const char *text )
{
	char name[MAX_EVENT_NAME];
	const char *args;
	int i, n;

	for ( n = 0; text[1 + n] && text[1 + n] != ' '; n++ ) {
		if ( n >= MAX_EVENT_NAME - 1 ) {
			return;
		}
		name[n] = text[1 + n];
	}
	name[n] = '\0';
	args = text + 1 + n;
	while ( *args == ' ' ) {
		args++;
	}

	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];
		int k;

		if ( p->state != PS_RUNNING || !p->numChatCommands || !( p->caps & ODFS_CAP_chat ) || !PLUGIN_HAS( p, ChatCommandHandler ) ) {
			continue;
		}
		for ( k = 0; k < p->numChatCommands && p->state == PS_RUNNING; k++ ) {
			if ( !Q_stricmp( p->chatCommands[k].name, name ) ) {
				PLUGIN_RUN( p, "chat command handler",
					ODFS_Call_ChatCommandHandler( p->vm, p->chatCommands[k].handler, clientNum, serial, args ) );
			}
		}
	}
}

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

	ChatCommand( (int)( cl - svs.clients ), cl->auth.serial, text );
}

qboolean SV_Plugins_HandleAuth( void )
{
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		if ( plugins[i].state == PS_RUNNING && ( plugins[i].caps & ODFS_CAP_auth ) ) {
			return qtrue;
		}
	}
	return qfalse;
}
