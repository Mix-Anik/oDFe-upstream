/* Generated from server.api by gen_api.py - do not edit. */

/*
===========================================================================
oDFe server plugin API, version 1.

See odf_common.h for how a plugin is built. The functions below are what the plugin can
call (engine functions) and what it can implement for the engine to call (exports).
===========================================================================
*/

#ifndef ODFS_H
#define ODFS_H

#include "odf_common.h"
#include "odfs_types.h"

#define ODFS_API_VERSION	1

// Capabilities. A plugin that needs a group says so in its manifest (caps=a,b); it is only loaded when it was given
// them, and a call of a function of a group it does not have stops the plugin.
//   chat       receives the "!command" chat messages of players (event chat_command)
//   auth       (restricted) answers the logins of players: gets the signatures (event auth_response) and tells the engine who is logged in
//   ips        (restricted) sees the IP addresses of players
//   http       (restricted) makes HTTP requests, only to the hosts the operator listed (http=host1|host2)
//   storage    keeps files of its own in a private folder (8 MB, or what the operator gives: storage=64m)
//   rawcmd     (restricted) sends any server command to clients, not only print, cp and chat
// A function marked 'cheats only' does nothing and returns 0 unless cheats are allowed (see CheatsEnabled): the engine
// decides, not the plugin.


// Writes a line to the server console and log, prefixed with the plugin name.
void Print( const char *text );

// Milliseconds since the engine started (wall clock, not game time).
int Milliseconds( void );

// The date and time: seconds since 1970-01-01 00:00 UTC, as an unsigned 32 bit number (good until 2106; the VM has no 64 bit
// integers). Use it for timestamps, Milliseconds for measuring time.
int UnixTime( void );

// Fills buf with cryptographically secure random bytes. Returns 1 on success.
int RandomBytes( void *buf, int len );

// Creates the cvar if it does not exist. An existing value (config file, command line) is kept. ODF_CVAR_PRIVATE makes it
// readable by the plugin that created it only (not by other plugins, the game module or clients).
// A plugin can read the cvars it created and some server settings (mapname, sv_hostname, sv_maxclients, sv_mapChecksum, g_*,
// df_* and a few more), nothing else.
void CvarRegister( const char *name, const char *defaultValue, int flags );

void CvarString( const char *name, char *out, int outSize );

int CvarInteger( const char *name );

// Math. Angles are in radians.
float Sin( float x );

float Cos( float x );

float Atan2( float y, float x );

float Sqrt( float x );

// Fills size bytes at dst with the byte value. Copies size bytes from src to dst (they must not overlap).
void MemSet( void *dst, int value, int size );

void MemCpy( void *dst, const void *src, int size );

// Fills *out for the given slot. Returns 0 if the slot is free. The ip is empty unless the plugin has the capability ips.
int GetClientInfo( int clientNum, odfClientInfo_t *out );

// Queues a server command for a client, or for everybody when clientNum is -1: "print \"hello\n\"", "cp \"hello\"", "chat ...".
// Other commands need the capability rawcmd.
void SendServerCommand( int clientNum, const char *command );

// Answers an "auth_response" event. clientNum and serial are the values from the event; if the client has disconnected in the
// meantime (serial no longer matches) the call is ignored. accountID and accountName are used for ODF_AUTHRES_OK. reason is a
// short word without spaces ("bad_signature", "banned", ...) shown to the player for REJECTED, and may be NULL.
// Capability: auth
void AuthResult( int clientNum, int serial, int result, unsigned accountIDLo, unsigned accountIDHi, const char *accountName, const char *reason );

// Starts a request and returns its id (1 or more), or 0 if it could not be started (host not allowed, too many requests,
// bad arguments). method is ODF_HTTP_GET or ODF_HTTP_POST. headers is "Name: value" lines separated by \n (only Authorization,
// Content-Type and Accept are sent), may be NULL. The result arrives later in the export OnHttp. Nothing blocks.
// Capability: http
int HttpRequest( int method, const char *url, const char *headers, const void *body, int bodyLen, int timeoutMs );

// Copies a piece of the answer to out, from byte offset on, while OnHttp runs. Returns the number of bytes copied.
// Capability: http
int HttpBody( int id, int offset, void *out, int outSize );

// Files of the plugin's own folder. Names are letters, digits, . _ - (up to 63 long, not starting with a dot). The folder is
// limited in size (4 MB a file and 8 MB all together; with storage=<size> in sv_plugins one file may use the whole size); there
// are no paths.
// StoreRead copies up to outSize bytes from byte offset on, returns the number copied or -1 (no such file).
// Capability: storage
int StoreRead( const char *name, int offset, void *out, int outSize );

// Replaces the file (append 0) or adds to its end (append 1). Returns 1 on success, 0 if it did not fit or failed.
// Capability: storage
int StoreWrite( const char *name, const void *data, int len, int append );

// The size of the file, or -1 if there is none.
// Capability: storage
int StoreSize( const char *name );

// Returns 1 if the file existed and is gone now.
// Capability: storage
int StoreDelete( const char *name );


// ---------------------------------------------------------------------------
// Exports: implement the ones you need with exactly these prototypes and list them in the EXPORTS
// of the entry point (see odfs_entry.h).
// ---------------------------------------------------------------------------

// Called once after loading. Return 0 on success; anything else unloads the plugin (OnShutdown is not called then).
int OnInit( void );

// Called once when the server shuts down. Pending work should be saved here.
void OnShutdown( void );

// Called every server frame. serverTime is the server's time (as for the game).
void OnFrame( int serverTime );

// Called for every event the plugin is allowed to get. data is a Quake info string: \key\value\key\value (at most 1023
// characters, keys and values never contain backslashes or quotes).
void OnEvent( const char *name, const char *data );

// A request made with HttpRequest finished. status is the HTTP status, or 0 if it failed before an answer (timeout, host not
// reachable, answer too large); bodyLen is the size of the answer, read it with HttpBody.
void OnHttp( int id, int status, int bodyLen );

#endif // ODFS_H
