/*
Tests the real server login and plugin code (sv_auth.c, sv_plugin.c, sv_plugin_guard.c) against a mock
plugin, with the engine functions they call stubbed out (stubs.c). The "client" signs with the real
protocol code. Not part of the engine build, see run.sh.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "server.h"
#include "odf_plugin_api.h"
#include "stubs.h"
#include "../../code/qcommon/auth_proto.h"

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

// ---- the mock plugin's test interface ----

static void ( *Mock_Setup )( int mode, int result, long long id, const char *name, const char *reason );
static int ( *Mock_EventCount )( void );
static const char *( *Mock_EventName )( int i );
static const char *( *Mock_EventData )( int i );
static int ( *Mock_Frames )( void );
static int ( *Mock_Client )( int clientNum, odfClientInfo_t *out );
static void ( *Mock_ClearLog )( void );

enum { ANSWER_NOW = 0, ANSWER_LATER = 1, NEVER = 2 };

static void *LoadSym( void *lib, const char *name )
{
#ifdef _WIN32
	return (void *)GetProcAddress( (HMODULE)lib, name );
#else
	return dlsym( lib, name );
#endif
}

// ---- helpers ----

static const uint8_t seed[AUTH_SEED_SIZE] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };
static const char *serverAddr = "1.2.3.4:27960";

static void ClearOutput( void )
{
	numTestCmds = 0;
	numTestPrints = 0;
}

static int FindCmd( int client, const char *prefix )
{
	int i;

	for ( i = 0; i < numTestCmds; i++ ) {
		if ( testCmds[i].client == client && !strncmp( testCmds[i].text, prefix, strlen( prefix ) ) ) {
			return i;
		}
	}
	return -1;
}

static int FindPrint( const char *text )
{
	int i;

	for ( i = 0; i < numTestPrints; i++ ) {
		if ( strstr( testPrints[i], text ) ) {
			return i;
		}
	}
	return -1;
}

static client_t *Connect( int num, const char *userinfo )
{
	client_t *cl = &svs.clients[num];

	memset( cl, 0, sizeof( *cl ) );
	cl->state = CS_CONNECTED;
	cl->netchan.remoteAddress.type = NA_IP;
	snprintf( cl->userinfo, sizeof( cl->userinfo ), "%s", userinfo );
	SV_Auth_ClientConnect( cl );
	SV_Auth_UserinfoChanged( cl );
	cl->state = CS_ACTIVE;
	SV_Auth_ClientEnterWorld( cl );
	return cl;
}

static void SetUserinfo( client_t *cl, const char *userinfo )
{
	snprintf( cl->userinfo, sizeof( cl->userinfo ), "%s", userinfo );
	SV_Auth_UserinfoChanged( cl );
}

// what the client engine does with an "auth_challenge <nonce>" message
static void ClientAnswers( client_t *cl, const char *challenge )
{
	uint8_t nonce[AUTH_NONCE_SIZE], pub[AUTH_PUBKEY_SIZE], sig[AUTH_SIG_SIZE], msg[128];
	char pubHex[65], sigHex[129], cmd[400];
	int len;

	Auth_HexDecode( nonce, sizeof( nonce ), challenge + strlen( "auth_challenge " ) );
	len = Auth_LoginMessage( msg, sizeof( msg ), nonce, serverAddr );
	Auth_PublicKey( pub, seed );
	Auth_Sign( sig, seed, msg, len );
	Auth_HexEncode( pubHex, sizeof( pubHex ), pub, sizeof( pub ) );
	Auth_HexEncode( sigHex, sizeof( sigHex ), sig, sizeof( sig ) );
	snprintf( cmd, sizeof( cmd ), "auth_response %s %s %s", pubHex, sigHex, serverAddr );
	Cmd_TokenizeString( cmd );
	SV_Auth_Response_f( cl );
}

// returns the challenge text sent to the client, or NULL
static const char *Challenge( int client )
{
	int i = FindCmd( client, "auth_challenge " );

	return i >= 0 ? testCmds[i].text : NULL;
}

static void InfoValue( const char *data, const char *key, char *out, int size )
{
	char pattern[48];
	const char *p;

	out[0] = '\0';
	snprintf( pattern, sizeof( pattern ), "\\%s\\", key );
	p = strstr( data, pattern );
	if ( p ) {
		int i = 0;

		p += strlen( pattern );
		while ( *p && *p != '\\' && i < size - 1 ) {
			out[i++] = *p++;
		}
		out[i] = '\0';
	}
}

int main( int argc, char **argv )
{
	char path[600];
	void *mock;
	client_t *cl;
	const char *challenge;
	odfClientInfo_t info;
	char value[300], value2[300];
	int i, events;

	if ( argc < 2 ) {
		printf( "usage: server_auth_test <home dir containing plugins/mock.dll>\n" );
		return 2;
	}

	svs.clients = calloc( MAX_CLIENTS, sizeof( client_t ) );
	sv.maxclients = 8;
	svs.time = 100000;

	Test_SetCvar( "fs_homepath", argv[1] );
	Test_SetCvar( "sv_plugins", "mock" );

	printf( "plugin loading\n" );
	SV_Plugins_Init();
	CHECK( SV_Plugins_HandleAuth(), "the mock plugin should be running and handle logins" );
	CHECK( FindPrint( "Plugin 'mock'" ) >= 0, "no load message" );

#ifdef _WIN32
	snprintf( path, sizeof( path ), "%s/plugins/mock.dll", argv[1] );
	mock = (void *)LoadLibraryA( path );
#else
	snprintf( path, sizeof( path ), "%s/plugins/mock.so", argv[1] );
	mock = dlopen( path, RTLD_NOW );
#endif
	if ( !mock ) {
		printf( "cannot load %s\n", path );
		return 2;
	}
	*(void **)&Mock_Setup = LoadSym( mock, "Mock_Setup" );
	*(void **)&Mock_EventCount = LoadSym( mock, "Mock_EventCount" );
	*(void **)&Mock_EventName = LoadSym( mock, "Mock_EventName" );
	*(void **)&Mock_EventData = LoadSym( mock, "Mock_EventData" );
	*(void **)&Mock_Frames = LoadSym( mock, "Mock_Frames" );
	*(void **)&Mock_Client = LoadSym( mock, "Mock_Client" );
	*(void **)&Mock_ClearLog = LoadSym( mock, "Mock_ClearLog" );

	// ------------------------------------------------------------------
	printf( "login, plugin answers on a later frame\n" );
	Mock_Setup( ANSWER_LATER, ODF_AUTHRES_OK, 42, "mix", NULL );
	ClearOutput();
	cl = Connect( 0, "\\name\\Tester\\cl_login\\1" );
	challenge = Challenge( 0 );
	CHECK( challenge && strlen( challenge ) == strlen( "auth_challenge " ) + 64, "no challenge: %s", challenge ? challenge : "(none)" );
	CHECK( Mock_Client( 0, &info ) && info.authState == ODF_AUTHSTATE_PENDING, "client should be pending" );

	Mock_ClearLog();
	ClientAnswers( cl, challenge );
	CHECK( Mock_EventCount() == 1 && !strcmp( Mock_EventName( 0 ), "auth_response" ), "plugin did not get auth_response (%d events)", Mock_EventCount() );
	if ( Mock_EventCount() >= 1 ) {
		const char *data = Mock_EventData( 0 );
		uint8_t nonce[32], pub[32], sig[64], msg[128], myPub[32];
		int len;

		InfoValue( data, "client", value, sizeof( value ) );
		CHECK( !strcmp( value, "0" ), "client=%s", value );
		InfoValue( data, "serial", value, sizeof( value ) );
		CHECK( atoi( value ) == cl->auth.serial && cl->auth.serial > 0, "serial=%s", value );
		InfoValue( data, "addr", value, sizeof( value ) );
		CHECK( !strcmp( value, serverAddr ), "addr=%s", value );
		InfoValue( data, "ip", value, sizeof( value ) );
		CHECK( !strcmp( value, "9.9.9.9" ), "ip=%s", value );

		// what the plugin forwards to the API must verify with the real signature check
		InfoValue( data, "nonce", value, sizeof( value ) );
		CHECK( Auth_HexDecode( nonce, 32, value ) == 32 && !strcmp( value, challenge + strlen( "auth_challenge " ) ), "nonce does not match the challenge" );
		InfoValue( data, "pubkey", value, sizeof( value ) );
		Auth_PublicKey( myPub, seed );
		CHECK( Auth_HexDecode( pub, 32, value ) == 32 && !memcmp( pub, myPub, 32 ), "public key in the event" );
		InfoValue( data, "sig", value2, sizeof( value2 ) );
		CHECK( Auth_HexDecode( sig, 64, value2 ) == 64, "signature in the event" );
		len = Auth_LoginMessage( msg, sizeof( msg ), nonce, serverAddr );
		CHECK( Auth_Verify( sig, pub, msg, len ), "the signature in the event does not verify" );
	}

	CHECK( FindCmd( 0, "auth_state" ) < 0, "state sent before the plugin answered" );
	ClientAnswers( cl, challenge );
	CHECK( Mock_EventCount() == 1, "a second answer to the same challenge reached the plugin" );

	SV_Plugins_Frame();
	i = FindCmd( 0, "auth_state ok mix" );
	CHECK( i >= 0, "no 'auth_state ok mix'" );
	CHECK( Mock_Client( 0, &info ) && info.authState == ODF_AUTHSTATE_VERIFIED && info.accountID == 42 && !strcmp( info.accountName, "mix" ), "client info after login" );
	CHECK( FindPrint( "logged in as mix" ) >= 0, "no log line" );

	// ------------------------------------------------------------------
	printf( "opting out and back in\n" );
	ClearOutput();
	SetUserinfo( cl, "\\name\\Tester\\cl_login\\0" );
	CHECK( FindCmd( 0, "auth_state off" ) >= 0, "no 'auth_state off'" );
	CHECK( Mock_Client( 0, &info ) && info.authState == ODF_AUTHSTATE_NONE && info.accountID == 0, "account kept after opting out" );
	CHECK( !Challenge( 0 ), "challenged although the player opted out" );

	SetUserinfo( cl, "\\name\\Tester\\cl_login\\1" );
	CHECK( !Challenge( 0 ), "challenge sent again within the minimum gap" );
	svs.time += 4000;
	ClearOutput();
	SetUserinfo( cl, "\\name\\Tester\\cl_login\\0" );
	SetUserinfo( cl, "\\name\\Tester\\cl_login\\1" );
	CHECK( Challenge( 0 ) != NULL, "no new challenge after opting in again" );

	// ------------------------------------------------------------------
	printf( "clients that never log in\n" );
	ClearOutput();
	Connect( 5, "\\name\\OldClient" );
	CHECK( !Challenge( 5 ), "an old client (no cl_login) was challenged" );
	Connect( 6, "\\name\\Optout\\cl_login\\0" );
	CHECK( !Challenge( 6 ), "a client with cl_login 0 was challenged" );

	// ------------------------------------------------------------------
	printf( "results other than success\n" );
	{
		static const struct {
			int result;
			const char *reason, *expect;
		} cases[] = {
			{ ODF_AUTHRES_UNKNOWN_KEY, NULL, "auth_state unknown_key" },
			{ ODF_AUTHRES_REJECTED, "bad_signature", "auth_state failed bad_signature" },
			{ ODF_AUTHRES_REJECTED, "Bad Reason;", "auth_state failed rejected" },
			{ ODF_AUTHRES_REJECTED, NULL, "auth_state failed rejected" },
			{ ODF_AUTHRES_UNAVAILABLE, NULL, "auth_state unavailable" },
			{ ODF_AUTHRES_OK, NULL, "auth_state failed bad_account" },		// OK without a valid account
		};

		for ( i = 0; i < (int)( sizeof( cases ) / sizeof( cases[0] ) ); i++ ) {
			ClearOutput();
			Mock_Setup( ANSWER_NOW, cases[i].result, cases[i].result == ODF_AUTHRES_OK ? 0 : 42, "mix", cases[i].reason );
			cl = Connect( 1, "\\name\\P\\cl_login\\1" );
			challenge = Challenge( 1 );
			CHECK( challenge != NULL, "case %d: no challenge", i );
			if ( challenge ) {
				ClientAnswers( cl, challenge );
			}
			CHECK( FindCmd( 1, cases[i].expect ) >= 0, "case %d: expected '%s'", i, cases[i].expect );
			CHECK( Mock_Client( 1, &info ) && info.authState == ODF_AUTHSTATE_NONE, "case %d: client should stay anonymous", i );
		}
	}

	// ------------------------------------------------------------------
	printf( "an answer for a player that has left\n" );
	{
		int oldSerial;

		Mock_Setup( NEVER, ODF_AUTHRES_OK, 7, "old", NULL );
		ClearOutput();
		cl = Connect( 2, "\\name\\Leaver\\cl_login\\1" );
		ClientAnswers( cl, Challenge( 2 ) );
		oldSerial = cl->auth.serial;
		Mock_ClearLog();
		SV_Auth_ClientDropped( cl );
		CHECK( Mock_EventCount() == 1 && !strcmp( Mock_EventName( 0 ), "client_disconnect" ), "no client_disconnect event" );
		InfoValue( Mock_EventData( 0 ), "client", value, sizeof( value ) );
		InfoValue( Mock_EventData( 0 ), "serial", value2, sizeof( value2 ) );
		CHECK( !strcmp( value, "2" ) && atoi( value2 ) == oldSerial, "client_disconnect client=%s serial=%s", value, value2 );

		// somebody else takes the slot and is in the middle of logging in themselves
		ClearOutput();
		cl = Connect( 2, "\\name\\Newcomer\\cl_login\\1" );
		CHECK( cl->auth.serial != oldSerial, "the new connection got the old serial" );
		ClientAnswers( cl, Challenge( 2 ) );

		// the late answer meant for the old player must not log the new one in
		SV_Auth_Result( 2, oldSerial, ODF_AUTHRES_OK, 7, "old", NULL );
		CHECK( FindCmd( 2, "auth_state" ) < 0, "the old answer was applied to the new player" );
		CHECK( Mock_Client( 2, &info ) && info.authState == ODF_AUTHSTATE_PENDING && info.accountID == 0, "new player got the old account" );

		SV_Auth_Result( 2, cl->auth.serial, ODF_AUTHRES_OK, 8, "newb", NULL );
		CHECK( FindCmd( 2, "auth_state ok newb" ) >= 0 && Mock_Client( 2, &info ) && info.accountID == 8, "the right answer was not accepted" );
	}

	// ------------------------------------------------------------------
	printf( "timeouts\n" );
	Mock_Setup( NEVER, ODF_AUTHRES_OK, 1, "x", NULL );
	ClearOutput();
	cl = Connect( 3, "\\name\\Slow\\cl_login\\1" );
	ClientAnswers( cl, Challenge( 3 ) );
	cl = Connect( 4, "\\name\\Silent\\cl_login\\1" );		// never answers the challenge
	CHECK( Challenge( 4 ) != NULL, "no challenge for the silent client" );
	svs.time += 31000;
	ClearOutput();
	SV_Auth_Frame();
	CHECK( FindCmd( 3, "auth_state unavailable" ) >= 0, "a login nobody answered did not end with 'unavailable'" );
	CHECK( FindCmd( 4, "auth_state" ) < 0, "the silent client was told something" );
	CHECK( Mock_Client( 4, &info ) && info.authState == ODF_AUTHSTATE_NONE, "silent client still pending" );

	// ------------------------------------------------------------------
	printf( "events from the game\n" );
	Mock_ClearLog();
	SV_Plugin_GameEvent( "player_finish", "\\client\\0\\time\\12345" );
	CHECK( Mock_EventCount() == 1 && !strcmp( Mock_EventName( 0 ), "player_finish" ) && !strcmp( Mock_EventData( 0 ), "\\client\\0\\time\\12345" ), "valid event not delivered" );
	events = Mock_EventCount();
	SV_Plugin_GameEvent( "Bad-Name", "x" );
	SV_Plugin_GameEvent( "", "x" );
	SV_Plugin_GameEvent( "this_event_name_is_much_too_long_to_be_valid", "x" );
	{
		char big[1200];

		memset( big, 'a', sizeof( big ) - 1 );
		big[sizeof( big ) - 1] = '\0';
		SV_Plugin_GameEvent( "too_big", big );
	}
	SV_Plugin_GameEvent( NULL, "x" );
	SV_Plugin_GameEvent( "no_data", NULL );
	CHECK( Mock_EventCount() == events, "an invalid event reached the plugin (%d more)", Mock_EventCount() - events );

	// ------------------------------------------------------------------
	printf( "plugin crash\n" );
	Mock_Setup( ANSWER_LATER, ODF_AUTHRES_OK, 9, "late", NULL );
	ClearOutput();
	cl = Connect( 7, "\\name\\Waiting\\cl_login\\1" );
	ClientAnswers( cl, Challenge( 7 ) );
	ClearOutput();
	SV_Plugin_GameEvent( "crash", "" );
	CHECK( FindPrint( "crashed" ) >= 0, "no crash message in the log" );
	CHECK( FindPrint( "stopped" ) >= 0, "no 'stopped' in the log" );
	CHECK( !SV_Plugins_HandleAuth(), "a crashed plugin still handles logins" );
	CHECK( FindCmd( 7, "auth_state unavailable" ) >= 0, "the login that was being verified was not ended" );

	i = Mock_Frames();
	events = Mock_EventCount();
	SV_Plugins_Frame();
	SV_Plugin_GameEvent( "player_finish", "\\client\\0" );
	CHECK( Mock_Frames() == i && Mock_EventCount() == events, "code of the crashed plugin ran again" );

	ClearOutput();
	cl = Connect( 0, "\\name\\After\\cl_login\\1" );
	CHECK( !Challenge( 0 ), "challenge sent although no plugin can verify it" );

	SV_Plugins_Shutdown();

	printf( failures ? "FAILED: %d check(s)\n" : "all server login checks passed\n", failures );
	return failures ? 1 : 0;
}
