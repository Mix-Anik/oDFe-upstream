/*
Tests the real server login and plugin code (sv_auth.c, sv_plugin.c, plugin_vm.c and the QVM code of the engine) against mock plugins
built as QVM programs, with the engine functions they call stubbed out (stubs.c). The "client" signs with the real
protocol code. Not part of the engine build, see run.sh.

Plugins used (all the same mock_plugin.c, built with different options; the grants are in the sv_plugins line below):
  mock     login, chat, storage and http for api.test and other.test, but not ips and not rawcmd
  trusted  all of it, http for api.test only
  lite     asks for nothing
  lite2    asks for nothing (the second one to be thrown out)
  nogrant  needs capabilities and gets none        -> refused
  wants    says in its manifest that it needs some -> refused
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"
#include "stubs.h"
#include "mock_state.h"
#include "../../code/qcommon/auth_proto.h"

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

// ---- the state of the mock plugins, kept here (see mock_state.h) ----

#define MAX_MIRRORS 16
static char mirrorNames[MAX_MIRRORS][32];
static mockState_t mirrors[MAX_MIRRORS];
static int numMirrors;

static mockState_t *State( const char *name )
{
	int i;

	for ( i = 0; i < numMirrors; i++ ) {
		if ( !strcmp( mirrorNames[i], name ) ) {
			return &mirrors[i];
		}
	}
	if ( numMirrors >= MAX_MIRRORS ) {
		fprintf( stderr, "too many mock plugins\n" );
		exit( 3 );
	}
	snprintf( mirrorNames[numMirrors], sizeof( mirrorNames[0] ), "%s", name );
	return &mirrors[numMirrors++];
}

void Test_MirrorPull( const char *plugin, void *dst, int size )
{
	if ( size != (int)sizeof( mockState_t ) ) {
		fprintf( stderr, "mock state size differs: %d here, %d in the plugin\n", (int)sizeof( mockState_t ), size );
		exit( 3 );
	}
	memcpy( dst, State( plugin ), size );
}

void Test_MirrorPush( const char *plugin, const void *src, int size )
{
	if ( size == (int)sizeof( mockState_t ) ) {
		memcpy( State( plugin ), src, size );
	}
}

enum { ANSWER_NOW = 0, ANSWER_LATER = 1, NEVER = 2 };

static mockState_t *mock, *trusted, *lite, *lite2, *big, *tiny;

static void ClearLogs( void )
{
	int i;

	for ( i = 0; i < numMirrors; i++ ) {
		mirrors[i].eventCount = 0;
	}
}

// the account number of a client info, which has it in two halves
#define ACCOUNT_ID( i ) ( (long long)(i).accountIDLo | ( (long long)(i).accountIDHi << 32 ) )

static void Setup( mockState_t *s, int mode, int result, long long id, const char *name, const char *reason )
{
	s->mode = mode;
	s->resultCode = result;
	s->resultIDLo = (unsigned)( (unsigned long long)id & 0xFFFFFFFFu );
	s->resultIDHi = (unsigned)( (unsigned long long)id >> 32 );
	snprintf( s->resultName, sizeof( s->resultName ), "%s", name ? name : "" );
	snprintf( s->resultReason, sizeof( s->resultReason ), "%s", reason ? reason : "" );
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

// ---- making a plugin do something ----

// A plugin that is a copy of the mock answers its own events only ("\t\<plugin>"), so the test can talk to one at a time.
// The event itself is not counted in its log.
static void Targeted( mockState_t *s, const char *event, const char *target, const char *rest )
{
	char data[1000];

	snprintf( data, sizeof( data ), "\\t\\%s%s", target, rest );
	SV_Plugin_GameEvent( event, data );
	if ( s && s->eventCount > 0 ) {
		s->eventCount--;
	}
}

static int Query( mockState_t *s, const char *target, int client )
{
	char rest[32];

	snprintf( rest, sizeof( rest ), "\\client\\%d", client );
	s->infoOk = -1;
	Targeted( s, "query", target, rest );
	return s->infoOk;
}

static int Act( mockState_t *s, const char *target, const char *action, const char *name, const char *text, int client )
{
	char rest[600];

	snprintf( rest, sizeof( rest ), "\\a\\%s\\n\\%s\\d\\%s\\c\\%d", action, name, text, client );
	s->actResult = -99;
	Targeted( s, "act", target, rest );
	return s->actResult;
}

static int FileExists( const char *home, const char *path )
{
	char full[700];
	FILE *f;

	snprintf( full, sizeof( full ), "%s/%s", home, path );
	f = fopen( full, "rb" );
	if ( f ) {
		fclose( f );
	}
	return f != NULL;
}

int main( int argc, char **argv )
{
	client_t *cl;
	const char *challenge;
	char value[300], value2[300];
	int i, events;
	const char *home;

	if ( argc < 2 ) {
		printf( "usage: server_auth_test <home dir containing plugins/*.qvm>\n" );
		return 2;
	}
	home = argv[1];

	svs.clients = calloc( MAX_CLIENTS, sizeof( client_t ) );
	sv.maxclients = 8;
	svs.time = 100000;

	Test_SetCvar( "fs_homepath", home, 0 );
	Test_SetCvar( "sv_plugins",
		"mock:auth,chat,storage,http=api.test|other.test "
		"trusted:auth,ips,chat,storage,rawcmd,http=api.test "
		"lite lite2 nogrant wants x.y bad:fly", 0 );

	// ------------------------------------------------------------------
	printf( "plugin loading and capabilities\n" );
	SV_Plugins_Init();
	CHECK( FindPrint( "Plugin 'mock' mock 1 loaded" ) >= 0, "mock not loaded" );
	CHECK( FindPrint( "Plugin 'trusted' mock 1 loaded" ) >= 0, "trusted not loaded" );
	CHECK( FindPrint( "Plugin 'lite' lite 1 loaded" ) >= 0, "lite not loaded" );
	CHECK( FindPrint( "Plugin 'lite2' lite 1 loaded" ) >= 0, "lite2 not loaded" );
	CHECK( SV_Plugins_HandleAuth(), "the mock plugin should be running and handle logins" );

	i = FindPrint( "Plugin 'nogrant' not loaded: needs capabilities that were not granted: " );
	CHECK( i >= 0, "a plugin that needs capabilities and got none was started" );
	if ( i >= 0 ) {
		// the message tells the operator what to write
		CHECK( strstr( testPrints[i], "sv_plugins \"nogrant:auth,http,storage\"" ) != NULL, "no line to copy: %s", testPrints[i] );
		CHECK( strstr( testPrints[i], "storage" ) && strstr( testPrints[i], "http" ), "capabilities missing from the message: %s", testPrints[i] );
	}
	i = FindPrint( "Plugin 'wants' not loaded: needs capabilities that were not granted: chat" );
	CHECK( i >= 0 && strstr( testPrints[i], "sv_plugins \"wants:chat,http\"" ), "the manifest of a plugin asking for capabilities was not honoured" );
	CHECK( FindPrint( "Plugin 'x.y' not loaded: invalid name" ) >= 0, "a bad plugin name was accepted" );
	CHECK( FindPrint( "Plugin 'bad' not loaded: unknown capability 'fly'" ) >= 0, "an unknown capability was accepted" );

	mock = State( "mock" );
	trusted = State( "trusted" );
	lite = State( "lite" );
	lite2 = State( "lite2" );
	CHECK( mock->initCalls == 1 && trusted->initCalls == 1 && lite->initCalls == 1, "init was not called once" );
	CHECK( State( "nogrant" )->initCalls == 0 && State( "wants" )->initCalls == 0, "a refused plugin ran" );
	Setup( trusted, NEVER, ODF_AUTHRES_OK, 0, NULL, NULL );		// only the mock answers logins, the other one just listens

	// ------------------------------------------------------------------
	printf( "login, plugin answers on a later frame\n" );
	Setup( mock, ANSWER_LATER, ODF_AUTHRES_OK, 42, "mix", NULL );
	ClearOutput();
	cl = Connect( 0, "\\name\\Tester\\cl_login\\1" );
	challenge = Challenge( 0 );
	CHECK( challenge && strlen( challenge ) == strlen( "auth_challenge " ) + 64, "no challenge: %s", challenge ? challenge : "(none)" );
	CHECK( Query( mock, "mock", 0 ) && mock->info.authState == ODF_AUTHSTATE_PENDING, "client should be pending" );

	ClearLogs();
	ClientAnswers( cl, challenge );
	CHECK( mock->eventCount == 1 && !strcmp( mock->eventName[0], "auth_response" ), "plugin did not get auth_response (%d events)", mock->eventCount );
	CHECK( trusted->eventCount == 1, "the other plugin with the auth capability did not get auth_response" );
	CHECK( lite->eventCount == 0 && lite2->eventCount == 0, "a plugin without the auth capability got the signatures" );
	if ( mock->eventCount >= 1 ) {
		const char *data = mock->eventData[0];
		uint8_t nonce[32], pub[32], sig[64], msg[128], myPub[32];
		int len;

		InfoValue( data, "client", value, sizeof( value ) );
		CHECK( !strcmp( value, "0" ), "client=%s", value );
		InfoValue( data, "serial", value, sizeof( value ) );
		CHECK( atoi( value ) == cl->auth.serial && cl->auth.serial > 0, "serial=%s", value );
		InfoValue( data, "addr", value, sizeof( value ) );
		CHECK( !strcmp( value, serverAddr ), "addr=%s", value );
		InfoValue( data, "ip", value, sizeof( value ) );
		CHECK( !value[0] && !strstr( data, "9.9.9.9" ), "a plugin without the ips capability got the address of the player (ip='%s')", value );
		InfoValue( trusted->eventData[0], "ip", value, sizeof( value ) );
		CHECK( !strcmp( value, "9.9.9.9" ), "a plugin with the ips capability did not get the address (ip='%s')", value );

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
	CHECK( mock->eventCount == 1, "a second answer to the same challenge reached the plugin" );

	SV_Plugins_Frame();
	i = FindCmd( 0, "auth_state ok mix" );
	CHECK( i >= 0, "no 'auth_state ok mix'" );
	CHECK( Query( mock, "mock", 0 ) && mock->info.authState == ODF_AUTHSTATE_VERIFIED && ACCOUNT_ID( mock->info ) == 42 && !strcmp( mock->info.accountName, "mix" ), "client info after login" );
	CHECK( FindPrint( "logged in as mix" ) >= 0, "no log line" );

	// ------------------------------------------------------------------
	printf( "opting out and back in\n" );
	ClearOutput();
	SetUserinfo( cl, "\\name\\Tester\\cl_login\\0" );
	CHECK( FindCmd( 0, "auth_state off" ) >= 0, "no 'auth_state off'" );
	CHECK( Query( mock, "mock", 0 ) && mock->info.authState == ODF_AUTHSTATE_NONE && ACCOUNT_ID( mock->info ) == 0, "account kept after opting out" );
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
			Setup( mock, ANSWER_NOW, cases[i].result, cases[i].result == ODF_AUTHRES_OK ? 0 : 42, "mix", cases[i].reason );
			cl = Connect( 1, "\\name\\P\\cl_login\\1" );
			challenge = Challenge( 1 );
			CHECK( challenge != NULL, "case %d: no challenge", i );
			if ( challenge ) {
				ClientAnswers( cl, challenge );
			}
			CHECK( FindCmd( 1, cases[i].expect ) >= 0, "case %d: expected '%s'", i, cases[i].expect );
			CHECK( Query( mock, "mock", 1 ) && mock->info.authState == ODF_AUTHSTATE_NONE, "case %d: client should stay anonymous", i );
		}
	}

	// ------------------------------------------------------------------
	printf( "an answer for a player that has left\n" );
	{
		int oldSerial;

		Setup( mock, NEVER, ODF_AUTHRES_OK, 7, "old", NULL );
		ClearOutput();
		cl = Connect( 2, "\\name\\Leaver\\cl_login\\1" );
		ClientAnswers( cl, Challenge( 2 ) );
		oldSerial = cl->auth.serial;
		ClearLogs();
		SV_Auth_ClientDropped( cl );
		CHECK( mock->eventCount == 1 && !strcmp( mock->eventName[0], "client_disconnect" ), "no client_disconnect event" );
		InfoValue( mock->eventData[0], "client", value, sizeof( value ) );
		InfoValue( mock->eventData[0], "serial", value2, sizeof( value2 ) );
		CHECK( !strcmp( value, "2" ) && atoi( value2 ) == oldSerial, "client_disconnect client=%s serial=%s", value, value2 );

		// somebody else takes the slot and is in the middle of logging in themselves
		ClearOutput();
		cl = Connect( 2, "\\name\\Newcomer\\cl_login\\1" );
		CHECK( cl->auth.serial != oldSerial, "the new connection got the old serial" );
		ClientAnswers( cl, Challenge( 2 ) );

		// the late answer meant for the old player must not log the new one in
		SV_Auth_Result( 2, oldSerial, ODF_AUTHRES_OK, 7, "old", NULL );
		CHECK( FindCmd( 2, "auth_state" ) < 0, "the old answer was applied to the new player" );
		CHECK( Query( mock, "mock", 2 ) && mock->info.authState == ODF_AUTHSTATE_PENDING && ACCOUNT_ID( mock->info ) == 0, "new player got the old account" );

		SV_Auth_Result( 2, cl->auth.serial, ODF_AUTHRES_OK, 8, "newb", NULL );
		CHECK( FindCmd( 2, "auth_state ok newb" ) >= 0 && Query( mock, "mock", 2 ) && ACCOUNT_ID( mock->info ) == 8, "the right answer was not accepted" );
	}

	// ------------------------------------------------------------------
	printf( "timeouts\n" );
	Setup( mock, NEVER, ODF_AUTHRES_OK, 1, "x", NULL );
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
	CHECK( Query( mock, "mock", 4 ) && mock->info.authState == ODF_AUTHSTATE_NONE, "silent client still pending" );

	// ------------------------------------------------------------------
	printf( "events from the game\n" );
	ClearLogs();
	SV_Plugin_GameEvent( "player_finish", "\\client\\0\\time\\12345" );
	CHECK( mock->eventCount == 1 && !strcmp( mock->eventName[0], "player_finish" ) && !strcmp( mock->eventData[0], "\\client\\0\\time\\12345" ), "valid event not delivered" );
	CHECK( lite->eventCount == 1 && trusted->eventCount == 1, "an event without a capability did not reach every plugin" );
	events = mock->eventCount;
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
	CHECK( mock->eventCount == events, "an invalid event reached the plugin (%d more)", mock->eventCount - events );

	// the game cannot play the engine: no fake logins, chat commands, disconnects or map changes
	SV_Plugin_GameEvent( "auth_response", "\\client\\0\\serial\\1" );
	SV_Plugin_GameEvent( "chat_command", "\\client\\0\\text\\!top" );
	SV_Plugin_GameEvent( "client_disconnect", "\\client\\0\\serial\\1" );
	SV_Plugin_GameEvent( "map_begin", "\\map\\x" );
	CHECK( mock->eventCount == events && trusted->eventCount == events, "the game module raised an event that belongs to the engine" );

	// the address of a player never reaches a plugin without the ips capability, even when the game puts it into an event
	ClearLogs();
	SV_Plugin_GameEvent( "player_finish", "\\client\\0\\ip\\6.6.6.6\\time\\5" );
	CHECK( mock->eventCount == 1 && !strstr( mock->eventData[0], "6.6.6.6" ) && strstr( mock->eventData[0], "\\time\\5" ), "ip not stripped: %s", mock->eventData[0] );
	CHECK( lite->eventCount == 1 && !strstr( lite->eventData[0], "6.6.6.6" ), "ip not stripped for lite" );
	CHECK( trusted->eventCount == 1 && strstr( trusted->eventData[0], "\\ip\\6.6.6.6" ), "ip removed for a plugin that may have it" );

	ClearLogs();
	SV_Plugins_MapBegin( "q3dm6", 123 );
	CHECK( mock->eventCount == 1 && !strcmp( mock->eventName[0], "map_begin" ), "no map_begin" );
	InfoValue( mock->eventData[0], "map", value, sizeof( value ) );
	InfoValue( mock->eventData[0], "checksum", value2, sizeof( value2 ) );
	CHECK( !strcmp( value, "q3dm6" ) && !strcmp( value2, "123" ), "map_begin data: %s", mock->eventData[0] );

	// ------------------------------------------------------------------
	printf( "chat commands\n" );
	{
		client_t *chatter = Connect( 4, "\\name\\Chatter\\cl_login\\0" );
		static char longText[260];
		static const struct {
			const char *command;
			const char *wantArgs;		/* NULL: the plugin is not called */
		} chat[] = {
			{ "say \"!top vq3\"",				"vq3" },
			{ "say \"!top\"",					"" },
			{ "say_team \"!top st1 cpm\"",		"st1 cpm" },
			{ "say !top vq3",					"vq3" },				/* the words arrive separately */
			{ "SAY \"!top\"",					"" },
			{ "say \"!TOP vq3\"",				"vq3" },				/* the name is not case sensitive */
			{ "say \"!other vq3\"",			NULL },					/* nobody registered it */
			{ "say \"!topvq3\"",				NULL },					/* a different word */
			{ "say \"hello !top\"",				NULL },					/* not at the start */
			{ "say \"top\"",					NULL },
			{ "say",							NULL },
			{ "vote \"!top\"",					NULL },					/* not chat */
			{ "tell 1 \"!top\"",				NULL },
			{ "say \"!top a\\b\"",				NULL },					/* characters an event cannot carry */
			{ "say \"!top a;b\"",				NULL },
			{ NULL, NULL }
		};
		int k;

		chatter->state = CS_ACTIVE;
		for ( k = 0; chat[k].command; k++ ) {
			ClearLogs();
			Cmd_TokenizeString( chat[k].command );
			SV_Plugin_ChatCommand( chatter );
			CHECK( lite->eventCount == 0, "'%s' reached a plugin without the chat capability", chat[k].command );
			if ( chat[k].wantArgs ) {
				CHECK( mock->eventCount == 1 && !strcmp( mock->eventName[0], "chat_top" ), "'%s' did not reach the plugin", chat[k].command );
				if ( mock->eventCount == 1 ) {
					InfoValue( mock->eventData[0], "args", value, sizeof( value ) );
					InfoValue( mock->eventData[0], "client", value2, sizeof( value2 ) );
					CHECK( !strcmp( value, chat[k].wantArgs ) && !strcmp( value2, "4" ), "'%s' arrived with arguments '%s' client '%s'", chat[k].command, value, value2 );
					CHECK( !strstr( mock->eventData[0], "9.9.9.9" ), "the address of the player is in the chat event" );
				}
			} else {
				CHECK( mock->eventCount == 0, "'%s' must not reach the plugin", chat[k].command );
			}
		}

		memset( longText, 'a', sizeof( longText ) - 1 );
		snprintf( value, sizeof( value ), "say \"!%s\"", longText );
		ClearLogs();
		Cmd_TokenizeString( value );
		SV_Plugin_ChatCommand( chatter );
		CHECK( mock->eventCount == 0, "a chat command longer than 200 characters reached the plugin" );

		/* clients that are not playing, and bots */
		chatter->state = CS_PRIMED;
		ClearLogs();
		Cmd_TokenizeString( "say \"!top\"" );
		SV_Plugin_ChatCommand( chatter );
		CHECK( mock->eventCount == 0, "a client that is not in the game got its chat command through" );
		chatter->state = CS_ACTIVE;
		chatter->netchan.remoteAddress.type = NA_BOT;
		SV_Plugin_ChatCommand( chatter );
		CHECK( mock->eventCount == 0, "a bot's chat command reached the plugin" );
		chatter->netchan.remoteAddress.type = NA_IP;
	}

	// ------------------------------------------------------------------
	printf( "the address of a player\n" );
	cl = Connect( 0, "\\name\\Spy\\cl_login\\0" );
	snprintf( cl->name, sizeof( cl->name ), "Spy" );		// the game fills this in from the userinfo
	CHECK( Query( mock, "mock", 0 ) == 1 && !strcmp( mock->info.name, "Spy" ), "no client info" );
	CHECK( !mock->info.ip[0], "a plugin without ips got the address '%s'", mock->info.ip );
	CHECK( Query( lite, "lite", 0 ) == 1 && !lite->info.ip[0], "lite got an address" );
	CHECK( Query( trusted, "trusted", 0 ) == 1 && !strcmp( trusted->info.ip, "9.9.9.9" ), "a plugin with ips did not get the address ('%s')", trusted->info.ip );
	CHECK( Query( mock, "mock", 31 ) == 0 && Query( mock, "mock", -1 ) == 0, "a client number outside of the server answered" );
	CHECK( Query( mock, "mock", 7 ) == 0, "an empty slot answered" );

	// ------------------------------------------------------------------
	printf( "commands to players\n" );
	ClearOutput();
	Act( mock, "mock", "cmd", "", "print \"hello\"", 0 );
	CHECK( FindCmd( 0, "print \"hello\"" ) >= 0, "a plain message was not delivered" );
	Act( mock, "mock", "cmd", "", "cp hi", -1 );
	CHECK( FindCmd( -1, "cp hi" ) >= 0, "a message to everybody was not delivered" );
	ClearOutput();
	Act( mock, "mock", "cmd", "", "map evil", -1 );
	CHECK( numTestCmds == 0 && FindPrint( "not allowed without rawcmd" ) >= 0, "a plugin without rawcmd sent 'map evil'" );
	Act( mock, "mock", "cmd", "", "printx foo", 0 );
	Act( mock, "mock", "cmd", "", "cs 5 x", 0 );
	Act( mock, "mock", "cmd", "", "disconnect", 0 );
	Act( mock, "mock", "cmd", "", "chatty hi", 0 );
	CHECK( numTestCmds == 0, "a plugin without rawcmd sent a command that is not a message (%s)", numTestCmds ? testCmds[0].text : "" );
	Act( trusted, "trusted", "cmd", "", "map evil", -1 );
	CHECK( FindCmd( -1, "map evil" ) >= 0, "a plugin with rawcmd could not send a command" );

	// ------------------------------------------------------------------
	printf( "cvars\n" );
	Test_SetCvar( "opt_level", "3", 0 );				// set by the operator in a config
	Test_SetCvar( "mapname", "q3dm6", 0 );
	Test_SetCvar( "sv_cheats", "1", 0 );
	Test_SetCvar( "rconpassword", "hunter2", 0 );
	Test_SetCvar( "sv_auth_secret", "s3cret", 0 );
	Cvar_Get( "engine_thing", "7", CVAR_ROM );

	ClearOutput();
	Act( mock, "mock", "cvar", "mymod_score", "5", 0 );
	CHECK( !strcmp( mock->actText, "5" ), "a plugin cannot read its own cvar ('%s')", mock->actText );
	CHECK( ( Cvar_Flags( "mymod_score" ) & CVAR_PRIVATE ) != 0, "a private cvar was not made private" );
	Act( mock, "mock", "cvar", "sv_evil", "1", 0 );
	CHECK( Cvar_Flags( "sv_evil" ) == CVAR_NONEXISTENT && !mock->actText[0] && FindPrint( "invalid or reserved" ) >= 0, "a plugin created a cvar in the sv_ namespace" );
	Act( mock, "mock", "cvar", "cg_evil", "1", 0 );
	Act( mock, "mock", "cvar", "nounderscore", "1", 0 );
	CHECK( Cvar_Flags( "cg_evil" ) == CVAR_NONEXISTENT && Cvar_Flags( "nounderscore" ) == CVAR_NONEXISTENT, "a reserved or odd cvar name was accepted" );
	Act( mock, "mock", "cvar", "engine_thing", "9", 0 );
	CHECK( !strcmp( Cvar_VariableString( "engine_thing" ), "7" ) && !mock->actText[0], "a plugin took over a cvar of the engine" );
	Act( mock, "mock", "cvar", "opt_level", "9", 0 );
	CHECK( !strcmp( mock->actText, "3" ), "a plugin could not use a cvar the operator set up for it (value '%s')", mock->actText );
	Act( trusted, "trusted", "cvar", "mymod_score", "6", 0 );
	CHECK( !strcmp( Cvar_VariableString( "mymod_score" ), "5" ) && !trusted->actText[0], "a plugin took over the cvar of another plugin" );
	Act( trusted, "trusted", "cvarread", "mymod_score", "", 0 );
	CHECK( !trusted->actText[0], "a plugin read the private cvar of another plugin ('%s')", trusted->actText );

	Act( mock, "mock", "cvarread", "mapname", "", 0 );
	CHECK( !strcmp( mock->actText, "q3dm6" ), "cannot read the map name" );
	Act( mock, "mock", "cvarread", "sv_cheats", "", 0 );
	CHECK( mock->actResult == 1, "cannot read sv_cheats" );
	Act( mock, "mock", "cvarread", "rconpassword", "", 0 );
	CHECK( !mock->actText[0] && mock->actResult == 0, "a plugin read the rcon password ('%s')", mock->actText );
	Act( mock, "mock", "cvarread", "sv_auth_secret", "", 0 );
	CHECK( !mock->actText[0], "a plugin read a secret under sv_ ('%s')", mock->actText );
	Act( mock, "mock", "cvarread", "fs_homepath", "", 0 );
	CHECK( !mock->actText[0], "a plugin read fs_homepath ('%s')", mock->actText );

	// ------------------------------------------------------------------
	printf( "store\n" );
	CHECK( Act( mock, "mock", "storew", "a.txt", "hello", 0 ) == 1, "write failed" );
	CHECK( FileExists( home, "plugin-data/mock/a.txt" ), "the file is not in the folder of the plugin" );
	CHECK( Act( mock, "mock", "stores", "a.txt", "", 0 ) == 5, "size" );
	CHECK( Act( mock, "mock", "storer", "a.txt", "0", 0 ) == 5 && !strcmp( mock->readBuf, "hello" ), "read back '%s'", mock->readBuf );
	CHECK( Act( mock, "mock", "storea", "a.txt", " world", 0 ) == 1 && Act( mock, "mock", "stores", "a.txt", "", 0 ) == 11, "append" );
	CHECK( Act( mock, "mock", "storer", "a.txt", "6", 0 ) == 5 && !strcmp( mock->readBuf, "world" ), "read at an offset '%s'", mock->readBuf );
	CHECK( Act( mock, "mock", "storer", "a.txt", "50", 0 ) == 0, "read behind the end" );
	CHECK( Act( mock, "mock", "storer", "missing.txt", "0", 0 ) == -1 && Act( mock, "mock", "stores", "missing.txt", "", 0 ) == -1, "a missing file" );
	CHECK( Act( mock, "mock", "stored", "a.txt", "", 0 ) == 1 && Act( mock, "mock", "stores", "a.txt", "", 0 ) == -1, "delete" );
	CHECK( Act( mock, "mock", "stored", "a.txt", "", 0 ) == 0, "delete of a file that is gone" );

	{
		static const char *const bad[] = { "../evil", "..", "a/b", "a\\b", ".hidden", "c:evil", "", "/etc/passwd", "con:",
			"CON", "nul", "Aux.txt", "prn.tar.gz", "COM1", "com9.dat", "lpt1", "LPT3.txt", "dot.", "name..." };
		// names that only look like devices
		static const char *const good[] = { "console.txt", "com10", "com0x.txt", "lpt", "nulls", "aux_1", "a.b" };

		for ( i = 0; i < (int)( sizeof( bad ) / sizeof( bad[0] ) ); i++ ) {
			// a backslash cannot be in the test event itself, it would end the value: that one is covered by the others
			if ( strchr( bad[i], '\\' ) ) {
				continue;
			}
			CHECK( Act( mock, "mock", "storew", bad[i], "x", 0 ) == 0, "the file name '%s' was accepted", bad[i] );
		}
		CHECK( !FileExists( home, "plugin-data/evil" ) && !FileExists( home, "evil" ) && !FileExists( home, "plugin-data/mock/.hidden" ), "a file ended up where it should not" );
		for ( i = 0; i < (int)( sizeof( good ) / sizeof( good[0] ) ); i++ ) {
			CHECK( Act( mock, "mock", "storew", good[i], "x", 0 ) == 1, "the file name '%s' was refused", good[i] );
			Act( mock, "mock", "stored", good[i], "", 0 );
		}
	}

	// every plugin has its own folder
	CHECK( Act( mock, "mock", "storew", "b.txt", "mock-data", 0 ) == 1, "write b" );
	CHECK( Act( trusted, "trusted", "stores", "b.txt", "", 0 ) == -1 && Act( trusted, "trusted", "storer", "b.txt", "0", 0 ) == -1, "a plugin saw the file of another plugin" );
	CHECK( Act( trusted, "trusted", "storew", "b.txt", "mine", 0 ) == 1 && Act( mock, "mock", "stores", "b.txt", "", 0 ) == 9, "the file of another plugin was overwritten" );
	Act( mock, "mock", "stored", "b.txt", "", 0 );
	Act( trusted, "trusted", "stored", "b.txt", "", 0 );

	// limits without a size in the grant: 4 MB a file, 8 MB all together (storefill says how many KB fit)
	CHECK( Act( mock, "mock", "storefill", "big1", "8", 0 ) == 4096, "a file in the store can be bigger than 4 MB or smaller than allowed (%d KB fit)", mock->actResult );
	CHECK( Act( mock, "mock", "storefill", "big2", "8", 0 ) == 4096, "the second file did not fit (%d KB)", mock->actResult );
	CHECK( Act( mock, "mock", "storefill", "big3", "8", 0 ) == 0, "more than 8 MB fit into the store" );
	CHECK( Act( mock, "mock", "stored", "big1", "", 0 ) == 1 && Act( mock, "mock", "storefill", "big3", "8", 0 ) == 4096, "deleting did not free the space" );
	Act( mock, "mock", "stored", "big2", "", 0 );
	Act( mock, "mock", "stored", "big3", "", 0 );

	// ------------------------------------------------------------------
	printf( "http\n" );
	ClearOutput();
	CHECK( Act( mock, "mock", "http", "", "https://api.test/x", 1 ) > 0 && numTestHttp == 1, "a request to an allowed host" );
	CHECK( numTestHttp == 1 && !strcmp( testHttp[0].url, "https://api.test/x" ) && testHttp[0].owner != NULL, "the request" );
	CHECK( Act( mock, "mock", "http", "", "https://API.TEST:8443/y", 1 ) > 0, "host names are not case sensitive, ports are fine" );
	CHECK( Act( mock, "mock", "http", "", "http://other.test/z", 1 ) > 0, "the second host" );
	events = numTestHttp;
	CHECK( events == 3, "requests made: %d", events );

	ClearOutput();
	CHECK( Act( mock, "mock", "http", "", "https://evil.test/", 1 ) == 0 && FindPrint( "not one of the hosts" ) >= 0, "a host that is not allowed" );
	CHECK( Act( mock, "mock", "http", "", "https://api.test.evil.com/", 1 ) == 0, "a host that starts like an allowed one" );
	CHECK( Act( mock, "mock", "http", "", "https://notapi.test/", 1 ) == 0, "a host that ends like an allowed one" );
	CHECK( Act( mock, "mock", "http", "", "http://api.test@evil.test/", 1 ) == 0, "credentials in the address" );
	CHECK( Act( mock, "mock", "http", "", "https://api.test:80x/", 1 ) == 0, "a broken port" );
	CHECK( Act( mock, "mock", "http", "", "ftp://api.test/", 1 ) == 0, "ftp" );
	CHECK( Act( mock, "mock", "http", "", "file:///etc/passwd", 1 ) == 0, "a file" );
	CHECK( Act( mock, "mock", "http", "", "https://[::1]/", 1 ) == 0, "an ipv6 literal" );
	CHECK( Act( mock, "mock", "http", "", "https://api.test /x", 1 ) == 0, "a space in the address" );
	CHECK( Act( mock, "mock", "http", "", "api.test/x", 1 ) == 0, "no scheme" );
	CHECK( numTestHttp == events, "a refused request was started anyway" );

	CHECK( Act( trusted, "trusted", "http", "", "http://other.test/z", 1 ) == 0, "a plugin used the host of another plugin" );
	CHECK( Act( trusted, "trusted", "http", "", "https://api.test/t", 1 ) > 0, "the allowed host of the second plugin" );

	// a request needs a handler
	{
		int before = numTestHttp;

		CHECK( Act( mock, "mock", "httpnohandler", "", "https://api.test/x", 0 ) == 0 && numTestHttp == before, "a request without a handler was started" );
	}

	// the answers go to the plugin that asked, and only while the handler of the request runs
	Test_HttpFinish( 0, 200, "pong" );
	Test_HttpFinish( 3, 404, "nope" );
	SV_Plugins_Frame();
	CHECK( mock->httpDone == 1 && mock->httpStatus[0] == 200 && mock->httpBodyLen[0] == 4 && !strcmp( mock->httpBody[0], "pong" ),
		"answer for the first plugin (%d: %d '%s')", mock->httpDone, mock->httpStatus[0], mock->httpBody[0] );
	CHECK( mock->httpUser[0] == 7001 && trusted->httpUser[0] == 7001, "the handler did not get the userData of its request (%d)", mock->httpUser[0] );
	CHECK( trusted->httpDone == 1 && trusted->httpStatus[0] == 404 && !strcmp( trusted->httpBody[0], "nope" ), "answer for the second plugin" );
	CHECK( Act( mock, "mock", "httpbody", "", "100", 0 ) == 0 && Act( trusted, "trusted", "httpbody", "", "100", 0 ) == 0, "an answer could be read outside of its handler" );

	// ------------------------------------------------------------------
	printf( "events: subscribing\n" );
	CHECK( Act( mock, "mock", "sub", "extra_event", "", 0 ) == 1, "a plugin could not subscribe" );
	ClearLogs();
	SV_Plugin_GameEvent( "extra_event", "\\x\\1" );
	CHECK( mock->eventCount == 1 && !strcmp( mock->eventName[0], "sub" ) && !strcmp( mock->eventData[0], "\\x\\1" ), "the new subscription got nothing" );
	CHECK( lite->eventCount == 0 && trusted->eventCount == 0, "a plugin got an event it did not subscribe to" );
	CHECK( Act( mock, "mock", "sub", "Bad Name", "", 0 ) == 0 && Act( mock, "mock", "sub", "", "", 0 ) == 0, "a bad event name was accepted" );
	CHECK( Act( mock, "mock", "subnohandler", "other_event", "", 0 ) == 0, "a subscription without a handler was accepted" );
	CHECK( Act( lite, "lite", "sub", "auth_response", "", 0 ) == 0, "a plugin without the capability auth subscribed to the logins" );
	CHECK( Act( lite, "lite", "sub", "player_finish", "", 0 ) == 1, "an event that needs no capability was refused" );
	{
		int ok = 0;

		/* a plugin has a limited number of subscriptions */
		for ( i = 0; i < 40; i++ ) {
			ok += Act( lite2, "lite2", "sub", "limit_test", "", 0 ) == 1;
		}
		CHECK( ok > 0 && ok < 40, "the number of subscriptions of a plugin is not limited (%d)", ok );
	}

	// ------------------------------------------------------------------
	printf( "plugins that misbehave\n" );
	ClearOutput();
	ClearLogs();
	Act( lite, "lite", "spin", "", "", 0 );
	CHECK( FindPrint( "Plugin 'lite' stopped" ) >= 0, "an endless loop was not stopped" );
	Act( lite2, "lite2", "badptr", "", "", 0 );
	CHECK( FindPrint( "Plugin 'lite2' stopped" ) >= 0, "a pointer outside of the memory of the plugin was accepted" );
	i = lite->frames;
	ClearLogs();
	SV_Plugins_Frame();
	SV_Plugin_GameEvent( "player_finish", "\\client\\0" );
	CHECK( lite->frames == i && lite->eventCount == 0 && lite2->eventCount == 0, "code of a stopped plugin ran again" );
	CHECK( mock->eventCount == 1 && trusted->eventCount == 1, "the other plugins stopped as well" );

	// ------------------------------------------------------------------
	printf( "plugin crash\n" );
	Setup( mock, ANSWER_LATER, ODF_AUTHRES_OK, 9, "late", NULL );
	ClearOutput();
	cl = Connect( 7, "\\name\\Waiting\\cl_login\\1" );
	ClientAnswers( cl, Challenge( 7 ) );
	ClearOutput();
	SV_Plugin_GameEvent( "crash", "\\t\\mock" );
	CHECK( FindPrint( "Plugin 'mock' stopped" ) >= 0, "no 'stopped' in the log" );
	CHECK( FindCmd( 7, "auth_state unavailable" ) >= 0, "the login that was being verified was not ended" );
	CHECK( SV_Plugins_HandleAuth(), "the other plugin that handles logins does not any more" );
	SV_Plugin_GameEvent( "crash", "\\t\\trusted" );
	CHECK( !SV_Plugins_HandleAuth(), "stopped plugins still handle logins" );

	i = mock->frames;
	events = mock->eventCount;
	SV_Plugins_Frame();
	SV_Plugin_GameEvent( "player_finish", "\\client\\0" );
	CHECK( mock->frames == i && mock->eventCount == events, "code of the crashed plugin ran again" );

	ClearOutput();
	cl = Connect( 0, "\\name\\After\\cl_login\\1" );
	CHECK( !Challenge( 0 ), "challenge sent although no plugin can verify it" );

	SV_Plugins_Shutdown();

	// ------------------------------------------------------------------
	// the size of the store: "storage=<number>[k|m|g]", 1 byte to 1 GB. A second start with other plugins. Plugins that get a bad size
	// are refused before their file is read, so they need none.
	printf( "size of the store\n" );
	numTestPrints = 0;
	Test_SetCvar( "sv_plugins",
		"big:storage=10m tiny:storage=100k "
		"q0:storage=0 q1:storage=abc q2:storage=2g q3:storage= q4:storage=1.5m q5:chat=5m q6:storage=1G", 0 );
	SV_Plugins_Init();
	CHECK( FindPrint( "Plugin 'big' store 1 loaded" ) >= 0 && FindPrint( "Plugin 'tiny' store 1 loaded" ) >= 0, "storage=10m / storage=100k not accepted" );
	{
		static const char *const badSizes[] = { "q0", "q1", "q2", "q3", "q4" };
		char text[64];
		int b;

		for ( b = 0; b < (int)( sizeof( badSizes ) / sizeof( badSizes[0] ) ); b++ ) {
			Com_sprintf( text, sizeof( text ), "Plugin '%s' not loaded: bad size for storage", badSizes[b] );
			CHECK( FindPrint( text ) >= 0, "a bad storage size for %s was accepted", badSizes[b] );
		}
	}
	CHECK( FindPrint( "Plugin 'q5' not loaded: the capability 'chat' takes no value" ) >= 0, "a value on chat was accepted" );
	CHECK( FindPrint( "Plugin 'q6' not loaded: bad size" ) < 0, "storage=1G is the largest size and is valid" );
	big = State( "big" );
	tiny = State( "tiny" );

	// storage=10m: one file can be 10 MB, and the 10 MB are all there is
	CHECK( Act( big, "big", "storefill", "f1", "16", 0 ) == 10240, "storage=10m: %d KB fit into one file", big->actResult );
	CHECK( Act( big, "big", "storefill", "f2", "16", 0 ) == 0, "storage=10m: more than 10 MB fit into the store" );
	CHECK( Act( big, "big", "stored", "f1", "", 0 ) == 1 && Act( big, "big", "storefill", "f2", "16", 0 ) == 10240, "storage=10m: deleting did not free the space" );
	Act( big, "big", "stored", "f2", "", 0 );

	// storage=100k: a plugin can be given less than the default, too
	CHECK( Act( tiny, "tiny", "storefill", "f1", "", 0 ) == 100, "storage=100k: %d KB fit", tiny->actResult );
	CHECK( Act( tiny, "tiny", "storew", "g1", "x", 0 ) == 0, "storage=100k: a byte more than 100 KB fit" );
	Act( tiny, "tiny", "stored", "f1", "", 0 );
	SV_Plugins_Shutdown();

	// ------------------------------------------------------------------
	// how many plugins: 64, and a list longer than 256 characters is read to its end. The names have no file, which is a
	// refusal that still takes a place.
	printf( "number of plugins\n" );
	{
		char list[1024];
		int n, len = 0;

		for ( n = 0; n < 70; n++ ) {
			len += Com_sprintf( list + len, sizeof( list ) - len, "nofile%02d:chat ", n );
		}
		CHECK( len > 256 && len < (int)sizeof( list ), "the test list is %d characters", len );
		numTestPrints = 0;
		Test_SetCvar( "sv_plugins", list, 0 );
		SV_Plugins_Init();
		CHECK( FindPrint( "Plugin 'nofile63'" ) >= 0, "the 64th plugin of a long list was not looked at" );
		CHECK( FindPrint( "Plugin 'nofile64' not loaded: at most 64 plugins" ) >= 0, "the 65th plugin was accepted" );
		SV_Plugins_Shutdown();
	}

	printf( failures ? "FAILED: %d check(s)\n" : "all server plugin checks passed\n", failures );
	return failures ? 1 : 0;
}
