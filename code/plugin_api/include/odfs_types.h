/*
===========================================================================
oDFe server plugin API: the data a server plugin and the engine exchange.

Plain data only, laid out the same for the engine and for a plugin built for WebAssembly
(wasm32). The functions are in odfs.h.
===========================================================================
*/

#ifndef ODFS_TYPES_H
#define ODFS_TYPES_H

#include <stdint.h>

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
	ODF_CVAR_PRIVATE = 2		// readable by the plugin that created it only, hidden from other plugins, the game module and clients
};

// HttpRequest methods
enum {
	ODF_HTTP_GET = 0,
	ODF_HTTP_POST = 1
};

#endif // ODFS_TYPES_H
