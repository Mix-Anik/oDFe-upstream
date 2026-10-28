/*
===========================================================================
Key-pair login of players (server side).

A client with userinfo cl_login 1 is sent a random challenge when it enters the world.
It answers with its public key and a signature ("auth_response"); the engine does not
check them itself but hands them to the plugin that handles logins (event
"auth_response"). The plugin asks the records API and reports back with AuthResult,
which ends up in SV_Auth_Result. The account is then kept on the client until it
disconnects or turns cl_login off.

Messages to the client:
	auth_challenge <64 hex>
	auth_state ok <account name> | off | unknown_key | failed <reason> | unavailable
Message from the client:
	auth_response <public key hex> <signature hex> <server address>
===========================================================================
*/

#include "server.h"
#include "odf_plugin_api.h"
#include "../qcommon/auth_proto.h"

#define AUTH_ANSWER_TIME	30000	// ms the client has to answer a challenge
#define AUTH_VERIFY_TIME	30000	// ms the plugin has to report a result
#define AUTH_CHALLENGE_GAP	3000	// ms between two challenges to the same client

static int authSerial;

static qboolean IsBot( const client_t *cl )
{
	return cl->netchan.remoteAddress.type == NA_BOT;
}

static void ClearAccount( client_t *cl )
{
	cl->auth.state = ODF_AUTHSTATE_NONE;
	cl->auth.challengeTime = 0;
	cl->auth.verifyTime = 0;
	cl->auth.accountID = 0;
	cl->auth.accountName[0] = '\0';
}

void SV_Auth_ClientConnect( client_t *cl )
{
	// called right after the client structure was cleared
	cl->auth.serial = ++authSerial;
}

static void SendChallenge( client_t *cl )
{
	char hex[AUTH_NONCE_SIZE * 2 + 1];

	if ( IsBot( cl ) || !SV_Plugins_HandleAuth() ) {
		return;
	}
	if ( cl->auth.lastChallengeTime && svs.time - cl->auth.lastChallengeTime < AUTH_CHALLENGE_GAP ) {
		return;
	}
	if ( !Sys_RandomBytes( cl->auth.nonce, sizeof( cl->auth.nonce ) ) ) {
		return;
	}
	Auth_HexEncode( hex, sizeof( hex ), cl->auth.nonce, AUTH_NONCE_SIZE );

	cl->auth.state = ODF_AUTHSTATE_PENDING;
	cl->auth.challengeTime = svs.time;
	cl->auth.lastChallengeTime = svs.time;
	cl->auth.verifyTime = 0;
	SV_SendServerCommand( cl, "auth_challenge %s", hex );
}

/*
===============
SV_Auth_UserinfoChanged

Follows the cl_login setting of the client. Absent means the client does not know about
logins, 0 that the player does not want to be logged in.
===============
*/
void SV_Auth_UserinfoChanged( client_t *cl )
{
	const char *val;

	if ( IsBot( cl ) ) {
		return;
	}
	val = Info_ValueForKey( cl->userinfo, "cl_login" );
	cl->auth.wants = val[0] && atoi( val ) != 0;

	if ( !cl->auth.wants ) {
		if ( cl->auth.state != ODF_AUTHSTATE_NONE ) {
			ClearAccount( cl );
			if ( val[0] ) {
				SV_SendServerCommand( cl, "auth_state off" );
			}
		}
	} else if ( cl->auth.state == ODF_AUTHSTATE_NONE && cl->state == CS_ACTIVE ) {
		SendChallenge( cl );
	}
}

void SV_Auth_ClientEnterWorld( client_t *cl )
{
	if ( cl->auth.wants && cl->auth.state == ODF_AUTHSTATE_NONE ) {
		SendChallenge( cl );
	}
}

void SV_Auth_ClientDropped( client_t *cl )
{
	char data[MAX_INFO_STRING] = "";

	if ( IsBot( cl ) || cl->state < CS_CONNECTED ) {
		return;
	}
	Info_SetValueForKey( data, "client", va( "%i", (int)( cl - svs.clients ) ) );
	Info_SetValueForKey( data, "serial", va( "%i", cl->auth.serial ) );
	SV_Plugin_Event( "client_disconnect", data );
	ClearAccount( cl );
}

/*
===============
SV_Auth_Response_f

"auth_response <public key hex> <signature hex> <server address>"
===============
*/
void SV_Auth_Response_f( client_t *cl )
{
	uint8_t pub[AUTH_PUBKEY_SIZE], sig[AUTH_SIG_SIZE];
	char nonceHex[AUTH_NONCE_SIZE * 2 + 1];
	char data[MAX_INFO_STRING] = "";
	const char *addr;
	const char *ip;

	if ( IsBot( cl ) || cl->state != CS_ACTIVE || cl->auth.state != ODF_AUTHSTATE_PENDING || !cl->auth.challengeTime ) {
		return;		// nothing was asked, or already answered
	}
	if ( svs.time - cl->auth.challengeTime > AUTH_ANSWER_TIME ) {
		ClearAccount( cl );
		return;
	}
	if ( Cmd_Argc() != 4 ) {
		return;
	}
	addr = Cmd_Argv( 3 );
	if ( Auth_HexDecode( pub, sizeof( pub ), Cmd_Argv( 1 ) ) < 0 || Auth_HexDecode( sig, sizeof( sig ), Cmd_Argv( 2 ) ) < 0 || !Auth_ValidAddress( addr ) ) {
		return;
	}

	// the challenge is used up, a client gets one answer per challenge
	cl->auth.challengeTime = 0;
	cl->auth.verifyTime = svs.time;

	Auth_HexEncode( nonceHex, sizeof( nonceHex ), cl->auth.nonce, AUTH_NONCE_SIZE );
	ip = NET_IsLocalAddress( &cl->netchan.remoteAddress ) ? "127.0.0.1" : NET_AdrToString( &cl->netchan.remoteAddress );

	Info_SetValueForKey( data, "client", va( "%i", (int)( cl - svs.clients ) ) );
	Info_SetValueForKey( data, "serial", va( "%i", cl->auth.serial ) );
	Info_SetValueForKey( data, "nonce", nonceHex );
	Info_SetValueForKey( data, "pubkey", Cmd_Argv( 1 ) );
	Info_SetValueForKey( data, "sig", Cmd_Argv( 2 ) );
	Info_SetValueForKey( data, "addr", addr );
	Info_SetValueForKey( data, "ip", ip );
	SV_Plugin_Event( "auth_response", data );
}

static qboolean ValidAccountName( const char *s )
{
	size_t i, len = strlen( s );

	if ( len < 1 || len > 32 ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		char c = s[i];

		if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '_' || c == '.' || c == '-' ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
===============
SV_Auth_Result

The plugin's answer. Ignored when the client left in the meantime or nothing was being verified.
===============
*/
void SV_Auth_Result( int clientNum, int serial, int result, int64_t accountID, const char *accountName, const char *reason )
{
	client_t *cl;
	char word[33];
	size_t i;

	if ( !svs.clients || clientNum < 0 || clientNum >= sv.maxclients ) {
		return;
	}
	cl = &svs.clients[clientNum];
	if ( cl->state < CS_CONNECTED || cl->auth.serial != serial || cl->auth.state != ODF_AUTHSTATE_PENDING || !cl->auth.verifyTime ) {
		return;
	}
	cl->auth.verifyTime = 0;

	if ( result == ODF_AUTHRES_OK && ( !accountName || !ValidAccountName( accountName ) || accountID <= 0 ) ) {
		result = ODF_AUTHRES_REJECTED;
		reason = "bad_account";
	}

	switch ( result ) {
	case ODF_AUTHRES_OK:
		cl->auth.state = ODF_AUTHSTATE_VERIFIED;
		cl->auth.accountID = accountID;
		Q_strncpyz( cl->auth.accountName, accountName, sizeof( cl->auth.accountName ) );
		SV_SendServerCommand( cl, "auth_state ok %s", cl->auth.accountName );
		Com_Printf( "%s" S_COLOR_WHITE " logged in as %s\n", cl->name, cl->auth.accountName );
		break;
	case ODF_AUTHRES_UNKNOWN_KEY:
		ClearAccount( cl );
		SV_SendServerCommand( cl, "auth_state unknown_key" );
		break;
	case ODF_AUTHRES_UNAVAILABLE:
		ClearAccount( cl );
		SV_SendServerCommand( cl, "auth_state unavailable" );
		break;
	default:
		ClearAccount( cl );
		// the reason is shown to the player, so only a plain word gets through
		word[0] = '\0';
		if ( reason ) {
			for ( i = 0; i < sizeof( word ) - 1 && reason[i]; i++ ) {
				char c = reason[i];

				if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) ) {
					word[0] = '\0';
					break;
				}
				word[i] = c;
				word[i + 1] = '\0';
			}
		}
		SV_SendServerCommand( cl, "auth_state failed %s", word[0] ? word : "rejected" );
		break;
	}
}

/*
===============
SV_Auth_PluginStopped

A plugin crashed: nobody will ever answer the logins it was verifying.
===============
*/
void SV_Auth_PluginStopped( void )
{
	int i;

	if ( !svs.clients ) {
		return;
	}
	for ( i = 0; i < sv.maxclients; i++ ) {
		client_t *cl = &svs.clients[i];

		if ( cl->state >= CS_CONNECTED && cl->auth.state == ODF_AUTHSTATE_PENDING && cl->auth.verifyTime ) {
			SV_Auth_Result( i, cl->auth.serial, ODF_AUTHRES_UNAVAILABLE, 0, NULL, NULL );
		}
	}
}

void SV_Auth_Frame( void )
{
	int i;

	if ( !svs.clients ) {
		return;
	}
	for ( i = 0; i < sv.maxclients; i++ ) {
		client_t *cl = &svs.clients[i];

		if ( cl->state < CS_CONNECTED || cl->auth.state != ODF_AUTHSTATE_PENDING ) {
			continue;
		}
		if ( cl->auth.verifyTime && svs.time - cl->auth.verifyTime > AUTH_VERIFY_TIME ) {
			SV_Auth_Result( i, cl->auth.serial, ODF_AUTHRES_UNAVAILABLE, 0, NULL, NULL );
		} else if ( cl->auth.challengeTime && svs.time - cl->auth.challengeTime > AUTH_ANSWER_TIME ) {
			ClearAccount( cl );		// the client never answered (an old client, or the player opted out in between)
		}
	}
}

qboolean SV_Auth_ClientInfo( int clientNum, int *authState, int64_t *accountID, const char **accountName, int *serial )
{
	const client_t *cl;

	if ( !svs.clients || clientNum < 0 || clientNum >= sv.maxclients ) {
		return qfalse;
	}
	cl = &svs.clients[clientNum];
	*authState = cl->auth.state;
	*accountID = cl->auth.accountID;
	*accountName = cl->auth.accountName;
	*serial = cl->auth.serial;
	return qtrue;
}
