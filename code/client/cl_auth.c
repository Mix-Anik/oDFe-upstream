/*
===========================================================================
Key-pair login of the player (client side).

The player has an Ed25519 key. Its seed is kept in odf_key.dat in the home path; game code
cannot read that file, the engine file functions open files inside the game directories
only. The public key is registered on the records website. A server that records runs
sends "auth_challenge <nonce>"; the client answers with its public key and a signature
over the nonce and the address it connected to ("auth_response ..."), and the server
reports the outcome with "auth_state ...".

cl_login is a userinfo cvar, so the server sees whether the player wants to be logged in:
	login / logout set it to 1 / 0.
The server messages are handled here and never reach the cgame.
===========================================================================
*/

#include "client.h"
#include "../qcommon/auth_proto.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#define KEY_FILE	"odf_key.dat"

static cvar_t *cl_login;

/*
===============
Key file
===============
*/

typedef enum {
	KEY_OK,
	KEY_MISSING,
	KEY_DAMAGED
} keyStatus_t;

static keyStatus_t LoadSeed( uint8_t seed[AUTH_SEED_SIZE] )
{
	char text[AUTH_SEED_SIZE * 2 + 16];
	fileHandle_t f = FS_INVALID_HANDLE;
	int len, i;

	len = FS_SV_FOpenFileRead( KEY_FILE, &f );
	if ( len < 0 || f == FS_INVALID_HANDLE ) {
		FS_FCloseFile( f );
		return KEY_MISSING;
	}
	if ( len > (int)sizeof( text ) - 1 ) {
		len = sizeof( text ) - 1;
	}
	len = FS_Read( text, len, f );
	FS_FCloseFile( f );
	if ( len < AUTH_SEED_SIZE * 2 ) {
		return KEY_DAMAGED;
	}
	text[len] = '\0';
	for ( i = 0; text[i]; i++ ) {
		if ( (unsigned char)text[i] <= ' ' ) {
			text[i] = '\0';		// ignore the line ending
			break;
		}
	}
	return Auth_HexDecode( seed, AUTH_SEED_SIZE, text ) == AUTH_SEED_SIZE ? KEY_OK : KEY_DAMAGED;
}

static qboolean CreateSeed( uint8_t seed[AUTH_SEED_SIZE] )
{
	char hex[AUTH_SEED_SIZE * 2 + 1];
	fileHandle_t f;

	// no fallback to weak randomness here: a guessable key would be an account takeover
	if ( !Sys_RandomBytes( seed, AUTH_SEED_SIZE ) ) {
		Com_Printf( S_COLOR_RED "Cannot create a login key: no secure random source.\n" );
		return qfalse;
	}
	Auth_HexEncode( hex, sizeof( hex ), seed, AUTH_SEED_SIZE );

	f = FS_SV_FOpenFileWrite( KEY_FILE );
	if ( f == FS_INVALID_HANDLE ) {
		Com_Printf( S_COLOR_RED "Cannot write %s to the home path.\n", KEY_FILE );
		return qfalse;
	}
	FS_Write( hex, AUTH_SEED_SIZE * 2, f );
	FS_Write( "\n", 1, f );
	FS_FCloseFile( f );

#ifndef _WIN32
	chmod( FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), KEY_FILE, NULL ), S_IRUSR | S_IWUSR );
#endif
	Com_Printf( S_COLOR_YELLOW "A new login key was created (%s in the home path). Keep that file private.\n", KEY_FILE );
	return qtrue;
}

/*
===============
GetSeed

Loads the key, creating it on first use. A damaged key file is never overwritten.
===============
*/
static qboolean GetSeed( uint8_t seed[AUTH_SEED_SIZE] )
{
	switch ( LoadSeed( seed ) ) {
	case KEY_OK:
		return qtrue;
	case KEY_MISSING:
		return CreateSeed( seed );
	default:
		Com_Printf( S_COLOR_RED "%s in the home path is damaged. Fix it, or delete it to get a new key (the old public key will stop working).\n", KEY_FILE );
		return qfalse;
	}
}

static qboolean GetPublicKeyHex( char hex[AUTH_PUBKEY_SIZE * 2 + 1] )
{
	uint8_t seed[AUTH_SEED_SIZE], pub[AUTH_PUBKEY_SIZE];

	if ( !GetSeed( seed ) ) {
		return qfalse;
	}
	Auth_PublicKey( pub, seed );
	Auth_HexEncode( hex, AUTH_PUBKEY_SIZE * 2 + 1, pub, AUTH_PUBKEY_SIZE );
	Com_Memset( seed, 0, sizeof( seed ) );
	return qtrue;
}

/*
===============
Server messages
===============
*/

static void SetStatus( const char *status )
{
	Cvar_Set( "cl_loginStatus", status );
}

static void Challenge( const char *nonceHex )
{
	uint8_t seed[AUTH_SEED_SIZE], pub[AUTH_PUBKEY_SIZE], sig[AUTH_SIG_SIZE], nonce[AUTH_NONCE_SIZE], msg[128];
	char pubHex[AUTH_PUBKEY_SIZE * 2 + 1], sigHex[AUTH_SIG_SIZE * 2 + 1];
	const char *addr;
	int len;

	if ( !cl_login->integer || cls.state < CA_CONNECTED ) {
		return;
	}
	if ( Auth_HexDecode( nonce, sizeof( nonce ), nonceHex ) < 0 ) {
		return;
	}
	// the address the connection was made to (resolved, with port): the server it is, and nobody else
	addr = NET_AdrToStringwPort( &clc.serverAddress );
	len = Auth_LoginMessage( msg, sizeof( msg ), nonce, addr );
	if ( len < 0 ) {
		Com_DPrintf( "Login: cannot sign for address '%s'\n", addr );
		return;
	}
	if ( !GetSeed( seed ) ) {
		return;
	}
	Auth_PublicKey( pub, seed );
	Auth_Sign( sig, seed, msg, len );
	Com_Memset( seed, 0, sizeof( seed ) );

	Auth_HexEncode( pubHex, sizeof( pubHex ), pub, sizeof( pub ) );
	Auth_HexEncode( sigHex, sizeof( sigHex ), sig, sizeof( sig ) );
	CL_AddReliableCommand( va( "auth_response %s %s %s", pubHex, sigHex, addr ), qfalse );
}

static void State( int argc )
{
	const char *what = Cmd_Argv( 1 );
	char pubHex[AUTH_PUBKEY_SIZE * 2 + 1];

	if ( !Q_stricmp( what, "ok" ) && argc >= 3 ) {
		SetStatus( va( "ok %s", Cmd_Argv( 2 ) ) );
		Com_Printf( "Logged in as " S_COLOR_GREEN "%s" S_COLOR_WHITE ", your records on this server are saved.\n", Cmd_Argv( 2 ) );
	} else if ( !Q_stricmp( what, "off" ) ) {
		SetStatus( "off" );
		Com_Printf( "Logged out, your records are not saved.\n" );
	} else if ( !Q_stricmp( what, "unknown_key" ) ) {
		SetStatus( "unknown_key" );
		Com_Printf( S_COLOR_YELLOW "Your login key is not registered to an account.\n" S_COLOR_WHITE
			"Add this public key to your account on the records website:\n" );
		if ( GetPublicKeyHex( pubHex ) ) {
			Com_Printf( "%s\n", pubHex );
		}
	} else if ( !Q_stricmp( what, "failed" ) ) {
		SetStatus( va( "failed %s", argc >= 3 ? Cmd_Argv( 2 ) : "rejected" ) );
		Com_Printf( S_COLOR_YELLOW "Login failed: %s\n", argc >= 3 ? Cmd_Argv( 2 ) : "rejected" );
	} else if ( !Q_stricmp( what, "unavailable" ) ) {
		SetStatus( "unavailable" );
		Com_Printf( S_COLOR_YELLOW "The login service is not available right now, your records are not saved.\n" );
	}
}

/*
===============
CL_Auth_ServerCommand

Called for every server command as it arrives. Returns qtrue if it was a login message
(then the cgame does not get to see it).
===============
*/
qboolean CL_Auth_ServerCommand( const char *s )
{
	if ( Q_stricmpn( s, "auth_challenge ", 15 ) && Q_stricmpn( s, "auth_state", 10 ) ) {
		return qfalse;
	}
	if ( clc.demoplaying || !cl_login ) {
		return qtrue;		// recorded demo: nobody to answer
	}

	Cmd_TokenizeString( s );
	if ( !Q_stricmp( Cmd_Argv( 0 ), "auth_challenge" ) && Cmd_Argc() == 2 ) {
		Challenge( Cmd_Argv( 1 ) );
	} else if ( !Q_stricmp( Cmd_Argv( 0 ), "auth_state" ) && Cmd_Argc() >= 2 ) {
		State( Cmd_Argc() );
	}
	return qtrue;
}

void CL_Auth_Disconnected( void )
{
	if ( cl_login ) {
		SetStatus( "" );
	}
}

/*
===============
Console commands
===============
*/

static void Login_f( void )
{
	Cvar_Set( "cl_login", "1" );
	Com_Printf( "Login on. Servers that record runs will log you in with your key.\n" );
}

static void Logout_f( void )
{
	Cvar_Set( "cl_login", "0" );
	Com_Printf( "Login off. Your runs are not saved to your account.\n" );
}

static void Key_f( void )
{
	char pubHex[AUTH_PUBKEY_SIZE * 2 + 1];

	if ( GetPublicKeyHex( pubHex ) ) {
		Com_Printf( "Your public login key (add it to your account on the records website):\n%s\n", pubHex );
	}
}

void CL_Auth_Init( void )
{
	cl_login = Cvar_Get( "cl_login", "1", CVAR_ARCHIVE | CVAR_USERINFO );
	Cvar_SetDescription( cl_login, "Log in to servers that record runs, using your login key. 0 to play without being logged in." );
	Cvar_Get( "cl_loginStatus", "", CVAR_ROM | CVAR_TEMP );

	Cmd_AddCommand( "login", Login_f );
	Cmd_AddCommand( "logout", Logout_f );
	Cmd_AddCommand( "auth_key", Key_f );
}
