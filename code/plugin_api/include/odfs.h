/* Generated from server.api by gen_api.py - do not edit. */

/*
===========================================================================
oDFe server plugin API, version 1.

See odf_common.h for how a plugin is built. The functions below are what the plugin can
call (imports) and what it can implement for the engine to call (exports).
===========================================================================
*/

#ifndef ODFS_H
#define ODFS_H

#include <stdint.h>
#include "odf_common.h"
#include "odfs_types.h"

#define ODFS_API_VERSION	1

// Capabilities. A plugin that imports a function of a group, or lists the group in its manifest (caps=a,b),
// is only loaded when it was given the capability.
//   chat       receives the "!command" chat messages of players (event chat_command)
//   auth       (restricted) answers the logins of players: gets the signatures (event auth_response) and tells the engine who is logged in
//   ips        (restricted) sees the IP addresses of players
//   http       (restricted) makes HTTP requests, only to the hosts the operator listed (http=host1|host2)
//   storage    keeps files of its own in a private folder (a few megabytes)
//   rawcmd     (restricted) sends any server command to clients, not only print, cp and chat


// Writes a line to the server console and log, prefixed with the plugin name.
ODF_IMPORT( "odfs", "Print" ) void odfs_Print( const char *text );

// Milliseconds since the engine started (wall clock, not game time).
ODF_IMPORT( "odfs", "Milliseconds" ) int odfs_Milliseconds( void );

// Fills buf with cryptographically secure random bytes. Returns 1 on success.
ODF_IMPORT( "odfs", "RandomBytes" ) int odfs_RandomBytes( void *buf, int len );

// Creates the cvar if it does not exist. An existing value (config file, command line) is kept. ODF_CVAR_PRIVATE makes it
// readable by the plugin that created it only (not by other plugins, the game module or clients).
// A plugin can read the cvars it created and some server settings (mapname, sv_hostname, sv_maxclients, sv_mapChecksum, g_*,
// df_* and a few more), nothing else.
ODF_IMPORT( "odfs", "CvarRegister" ) void odfs_CvarRegister( const char *name, const char *defaultValue, int flags );

ODF_IMPORT( "odfs", "CvarString" ) void odfs_CvarString( const char *name, char *out, int outSize );

ODF_IMPORT( "odfs", "CvarInteger" ) int odfs_CvarInteger( const char *name );

// Fills *out for the given slot. Returns 0 if the slot is free. The ip is empty unless the plugin has the capability ips.
ODF_IMPORT( "odfs", "GetClientInfo" ) int odfs_GetClientInfo( int clientNum, odfClientInfo_t *out );

// Queues a server command for a client, or for everybody when clientNum is -1: "print \"hello\n\"", "cp \"hello\"", "chat ...".
// Other commands need the capability rawcmd.
ODF_IMPORT( "odfs", "SendServerCommand" ) void odfs_SendServerCommand( int clientNum, const char *command );

// Answers an "auth_response" event. clientNum and serial are the values from the event; if the client has disconnected in the
// meantime (serial no longer matches) the call is ignored. accountID and accountName are used for ODF_AUTHRES_OK. reason is a
// short word without spaces ("bad_signature", "banned", ...) shown to the player for REJECTED, and may be NULL.
// Capability: auth
ODF_IMPORT( "odfs", "AuthResult" ) void odfs_AuthResult( int clientNum, int serial, int result, int64_t accountID, const char *accountName, const char *reason );

// Starts a request and returns its id (1 or more), or 0 if it could not be started (host not allowed, too many requests,
// bad arguments). method is ODF_HTTP_GET or ODF_HTTP_POST. headers is "Name: value" lines separated by \n (only Authorization,
// Content-Type and Accept are sent), may be NULL. The result arrives later in the export on_http. Nothing blocks.
// Capability: http
ODF_IMPORT( "odfs", "HttpRequest" ) int odfs_HttpRequest( int method, const char *url, const char *headers, const void *body, int bodyLen, int timeoutMs );

// Copies a piece of the answer to out, from byte offset on, while on_http runs. Returns the number of bytes copied.
// Capability: http
ODF_IMPORT( "odfs", "HttpBody" ) int odfs_HttpBody( int id, int offset, void *out, int outSize );

// Files of the plugin's own folder. Names are letters, digits, . _ - (up to 63 long, not starting with a dot). The folder is
// limited in size; there are no paths.
// StoreRead copies up to outSize bytes from byte offset on, returns the number copied or -1 (no such file).
// Capability: storage
ODF_IMPORT( "odfs", "StoreRead" ) int odfs_StoreRead( const char *name, int offset, void *out, int outSize );

// Replaces the file (append 0) or adds to its end (append 1). Returns 1 on success, 0 if it did not fit or failed.
// Capability: storage
ODF_IMPORT( "odfs", "StoreWrite" ) int odfs_StoreWrite( const char *name, const void *data, int len, int append );

// The size of the file, or -1 if there is none.
// Capability: storage
ODF_IMPORT( "odfs", "StoreSize" ) int odfs_StoreSize( const char *name );

// Returns 1 if the file existed and is gone now.
// Capability: storage
ODF_IMPORT( "odfs", "StoreDelete" ) int odfs_StoreDelete( const char *name );


// ---------------------------------------------------------------------------
// Exports: define the ones you need with exactly these prototypes.
// ---------------------------------------------------------------------------

// Called once after loading. Return 0 on success; anything else unloads the plugin (shutdown is not called then).
ODF_EXPORT_FN( "init" ) int odfs_init( void );

// Called once when the server shuts down. Pending work should be saved here.
ODF_EXPORT_FN( "shutdown" ) void odfs_shutdown( void );

// Called every server frame. serverTime is the server's time (as for the game).
ODF_EXPORT_FN( "frame" ) void odfs_frame( int serverTime );

// Called for every event the plugin is allowed to get. data is a Quake info string: \key\value\key\value (at most 1023
// characters, keys and values never contain backslashes or quotes).
ODF_EXPORT_FN( "on_event" ) void odfs_on_event( const char *name, const char *data );

// A request made with HttpRequest finished. status is the HTTP status, or 0 if it failed before an answer (timeout, host not
// reachable, answer too large); bodyLen is the size of the answer, read it with HttpBody.
ODF_EXPORT_FN( "on_http" ) void odfs_on_http( int id, int status, int bodyLen );

#endif // ODFS_H
