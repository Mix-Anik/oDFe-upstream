/*
===========================================================================
oDFe server plugin API

DRAFT FOR REVIEW - nothing implements this yet.

This header is self-contained (it needs only <stdint.h>) so a plugin can be built
without the engine sources: copy this file into the plugin project.

A plugin is a shared library, <homepath>/plugins/<name>.dll (Windows) or
<name>.so (everything else), loaded when the server starts. Which ones are loaded is
decided by the cvar sv_plugins, a space separated list of names ("records"). Plugins
are only ever loaded from that folder, never from downloaded paks.

HOW IT WORKS

 * The plugin exports one function, ODF_PluginEntry. The engine calls it once with a
   table of engine functions (odfPluginImport_t) and gets back a table of plugin
   functions (odfPluginExport_t).
 * Events (a player finished a run, a client sent a login response, ...) are handed
   to the plugin's OnEvent. A plugin ignores events it does not know.
 * Everything runs on the server's main thread. Plugin functions are called from it,
   and the engine functions below may only be called from it. A plugin that needs
   threads (for example inside an HTTP library) must not call back into the engine
   from them, and must hand results over in Frame().
 * OnEvent and Frame run inside the server frame. They must return quickly and must
   never block on the network or disk for long: start the work, return, and collect
   the result in a later Frame().

CRASH CONTAINMENT (best effort)

 Every call into a plugin is guarded. If it faults (access violation, SIGSEGV, SIGFPE,
 SIGILL, SIGBUS, abort) and the faulting address lies inside that plugin's own module,
 the engine logs the plugin name, the fault and the offset, stops the plugin for good
 (none of its functions is called again, its events and pending work are dropped) and
 carries on. This does NOT cover: a fault inside engine code, corruption done before
 the fault, a crash in a thread the plugin started, endless loops, or exit().

COMPATIBILITY

 The import table starts with its size, and only ever grows at the end. A plugin built
 against API version N works with an engine of version >= N, and checks
 imp->size before using functions that were added later (see the comments).
 The engine refuses a plugin whose apiVersion is higher than its own.

THE LOGIN FLOW (what the "records" plugin does)

 1. A client that wants to log in (userinfo cl_login 1) enters the world. The engine
    sends it a random challenge.
 2. The client signs ("authv1" + nonce + the server address it connected to) with its
    Ed25519 key and answers. The engine raises the event "auth_response".
 3. The plugin sends the data to the records API (POST /v1/auth/verify) and, when the
    answer arrives, calls imp->AuthResult().
 4. The engine remembers the account on the client, tells the client, and reports it in
    GetClientInfo. From then on the plugin can attach finished runs to that account.
===========================================================================
*/

#ifndef ODF_PLUGIN_API_H
#define ODF_PLUGIN_API_H

#include <stdint.h>

#define ODF_PLUGIN_API_VERSION	1

#ifdef _WIN32
#define ODF_EXPORT	__declspec( dllexport )
#else
#define ODF_EXPORT	__attribute__( ( visibility( "default" ) ) )
#endif

#define ODF_MAX_CLIENTS		64

// ---------------------------------------------------------------------------
// clients
// ---------------------------------------------------------------------------

// odfClientInfo_t.state
enum {
	ODF_CLIENT_CONNECTED = 1,	// connecting, no gamestate yet
	ODF_CLIENT_PRIMED = 2,		// has the gamestate, loading the map
	ODF_CLIENT_ACTIVE = 3		// playing
};

// odfClientInfo_t.authState
enum {
	ODF_AUTHSTATE_NONE = 0,		// anonymous: not logged in, or opted out (cl_login 0)
	ODF_AUTHSTATE_PENDING = 1,	// a challenge was sent or a verification is running
	ODF_AUTHSTATE_VERIFIED = 2	// logged in, accountID / accountName are valid
};

typedef struct {
	int			clientNum;
	int			state;				// ODF_CLIENT_*
	int			isBot;
	int			serial;				// counts connections of this slot, see "client_disconnect"
	char		name[32];			// player name, may contain colour codes
	char		ip[48];				// address without port; "127.0.0.1" for a local client
	int			authState;			// ODF_AUTHSTATE_*
	int64_t		accountID;			// valid when authState == ODF_AUTHSTATE_VERIFIED
	char		accountName[33];	// ditto
} odfClientInfo_t;

// ---------------------------------------------------------------------------
// login result, see AuthResult()
// ---------------------------------------------------------------------------

enum {
	ODF_AUTHRES_OK = 0,
	ODF_AUTHRES_UNKNOWN_KEY = 1,	// valid signature, but the key is not registered to an account
	ODF_AUTHRES_REJECTED = 2,		// bad signature, revoked key, banned, wrong address, ...
	ODF_AUTHRES_UNAVAILABLE = 3		// the login service could not be reached
};

// cvar flags for CvarRegister
enum {
	ODF_CVAR_ARCHIVE = 1,		// saved to the config file
	ODF_CVAR_PRIVATE = 2		// readable by the plugin only, hidden from the game module and from clients
};

// ---------------------------------------------------------------------------
// what the engine offers the plugin
// ---------------------------------------------------------------------------

typedef struct {
	int		size;			// sizeof( odfPluginImport_t ) of the engine
	int		apiVersion;		// ODF_PLUGIN_API_VERSION of the engine

	// -- general --

	// Writes a line to the server console and log, prefixed with the plugin name.
	void	( *Print )( const char *text );

	// Milliseconds since the engine started (wall clock, not game time).
	int		( *Milliseconds )( void );

	// Fills buf with cryptographically secure random bytes. Returns 1 on success.
	int		( *RandomBytes )( void *buf, int len );

	// Path of the engine's home directory (where config and the plugins folder live),
	// without a trailing slash.
	void	( *HomePath )( char *out, int outSize );

	// -- cvars --

	// Creates the cvar if it does not exist. An existing value (config file, command
	// line) is kept.
	void	( *CvarRegister )( const char *name, const char *defaultValue, int flags );
	void	( *CvarString )( const char *name, char *out, int outSize );
	int		( *CvarInteger )( const char *name );

	// -- clients --

	// Fills *out for the given slot. Returns 0 if the slot is free.
	int		( *GetClientInfo )( int clientNum, odfClientInfo_t *out );

	// Queues a server command for a client, or for everybody when clientNum is -1.
	// The text is sent as is: "print \"hello\n\"", "cp \"hello\"", ...
	void	( *SendServerCommand )( int clientNum, const char *command );

	// -- login --

	// Answers an "auth_response" event. clientNum and serial are the values from the
	// event; if the client has disconnected in the meantime (serial no longer matches)
	// the call is ignored. accountID and accountName are used for ODF_AUTHRES_OK.
	// reason is a short word without spaces ("bad_signature", "banned", ...) shown to
	// the player for REJECTED, and may be NULL.
	void	( *AuthResult )( int clientNum, int serial, int result,
							int64_t accountID, const char *accountName, const char *reason );

	// -- added in later versions go below this line; check size first --
} odfPluginImport_t;

// ---------------------------------------------------------------------------
// what the plugin offers the engine
// ---------------------------------------------------------------------------

// odfPluginExport_t.flags
enum {
	// The plugin answers "auth_response" events with AuthResult(). The engine only sends
	// login challenges to players while at least one running plugin has this flag.
	ODF_PLUGIN_FLAG_AUTH = 1
};

typedef struct {
	int			apiVersion;		// ODF_PLUGIN_API_VERSION the plugin was built against
	const char	*name;			// short name, as in sv_plugins ("records")
	const char	*version;		// free text, shown in the log
	int			flags;			// ODF_PLUGIN_FLAG_*

	// Called once after loading. Return 0 on success; anything else unloads the plugin
	// (it is not called again, Shutdown is not called either).
	int		( *Init )( void );

	// Called once when the server shuts down or the plugin is stopped.
	// Pending work should be saved to disk here.
	void	( *Shutdown )( void );

	// Called every server frame. serverTimeMs is the server's time (as for the game).
	void	( *Frame )( int serverTimeMs );

	// Called for every event. data is a Quake info string: \key\value\key\value
	// (at most 1024 characters, keys and values never contain backslashes or quotes).
	// Any of the four function pointers may be NULL.
	void	( *OnEvent )( const char *name, const char *data );
} odfPluginExport_t;

// The one function a plugin must export. Return NULL to refuse to load.
typedef const odfPluginExport_t *( *odfPluginEntry_t )( const odfPluginImport_t *imp );
// ODF_EXPORT const odfPluginExport_t *ODF_PluginEntry( const odfPluginImport_t *imp );

// ---------------------------------------------------------------------------
// events (version 1)
//
// Values are decimal integers unless noted.
//
// "player_finish"      raised by the game module (trap_Event) when a player finishes
//                      a timed run.
//      client          client slot
//      time            run time in ms
//      est             sub-millisecond corrected estimate, in ms
//      promode         physics: 0 VQ3, 1 CPM
//      mode            1 run, 2 tricks, 3 fastcaps
//      submode         raw sub-mode value, -1 in run mode
//      interference_off  df_mp_InterferenceOff: bit 0 no player-player collision, bit 1 no player-player damage; 0 = off
//      killobs         df_ob_killobs, 0 = obstacles are not killed
//      version         game module version number
//   The game module is trusted to send what it wants; any other event name it sends
//   is passed to the plugins as it is. The account of the player is not part of it:
//   look at GetClientInfo( client ).
//
// "auth_response"      raised by the engine when a client answered a login challenge.
//      client, serial  the slot and its connection counter; hand both to AuthResult
//      nonce           64 hex characters, the challenge the engine sent
//      pubkey          64 hex characters, the client's Ed25519 public key
//      sig             128 hex characters, the signature
//      addr            the server address the client signed, "ip:port"
//      ip              the client's address
//
// "chat_command"       a player's chat message ("say" or "say_team") starts with '!'. The game still gets the
//                      message and shows it like any other. Not sent for bots, and not for messages that contain
//                      control characters, a backslash, a double quote or a semicolon.
//      client, serial  the slot and its connection counter
//      text            the message, at most 200 characters, for example "!top vq3"
//
// "client_disconnect"  a player left.
//      client, serial
//
// "map_begin"          a map was loaded.
//      map             map name
//      checksum        map checksum (sv_mapChecksum)
// ---------------------------------------------------------------------------

#endif // ODF_PLUGIN_API_H
