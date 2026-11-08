/*
Runs the real records plugin (records.qvm of the df-records-plugin project) in the real server plugin code of the engine
(sv_plugin.c, plugin_vm.c, the QVM code), against the made-up engine of tests/server_auth/stubs.c and a made-up API: the test
looks at the HTTP requests the plugin asked for and says what came back.

    records_e2e <home dir containing plugins/records.qvm>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"
#include "stubs.h"
#include "../../code/qcommon/auth_proto.h"

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

static const uint8_t seed[AUTH_SEED_SIZE] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };
static const char *serverAddr = "1.2.3.4:27960";
static const char *home;

static void ClearOutput( void )
{
	numTestCmds = 0;
	numTestPrints = 0;
}

static int FindCmd( int client, const char *text )
{
	int i;

	for ( i = 0; i < numTestCmds; i++ ) {
		if ( testCmds[i].client == client && strstr( testCmds[i].text, text ) ) {
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

static int FileSize( const char *name )
{
	char path[600];
	FILE *f;
	long n;

	snprintf( path, sizeof( path ), "%s/plugin-data/records/%s", home, name );
	f = fopen( path, "rb" );
	if ( !f ) {
		return -1;
	}
	fseek( f, 0, SEEK_END );
	n = ftell( f );
	fclose( f );
	return (int)n;
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

// what the client engine does with an "auth_challenge <nonce>" message
static void ClientAnswers( client_t *cl )
{
	uint8_t nonce[AUTH_NONCE_SIZE], pub[AUTH_PUBKEY_SIZE], sig[AUTH_SIG_SIZE], msg[128];
	char pubHex[65], sigHex[129], cmd[400];
	int i = FindCmd( (int)( cl - svs.clients ), "auth_challenge " ), len;

	if ( i < 0 ) {
		return;
	}
	Auth_HexDecode( nonce, sizeof( nonce ), testCmds[i].text + strlen( "auth_challenge " ) );
	len = Auth_LoginMessage( msg, sizeof( msg ), nonce, serverAddr );
	Auth_PublicKey( pub, seed );
	Auth_Sign( sig, seed, msg, len );
	Auth_HexEncode( pubHex, sizeof( pubHex ), pub, sizeof( pub ) );
	Auth_HexEncode( sigHex, sizeof( sigHex ), sig, sizeof( sig ) );
	snprintf( cmd, sizeof( cmd ), "auth_response %s %s %s", pubHex, sigHex, serverAddr );
	Cmd_TokenizeString( cmd );
	SV_Auth_Response_f( cl );
}

// the HTTP request number n since the last reset, or NULL
static testHttp_t *Request( int n )
{
	return n < numTestHttp ? &testHttp[n] : NULL;
}

// the answer to a request, then a frame so that the plugin gets it
static void Answer( int n, int status, const char *body )
{
	Test_HttpFinish( n, status, body );
	SV_Plugins_Frame();
}

static void Finish( client_t *cl, const char *extra )
{
	char data[400];

	snprintf( data, sizeof( data ), "\\client\\%d\\time\\12345\\est\\12340\\promode\\0\\mode\\1\\submode\\-1\\interference_off\\0\\killobs\\0\\version\\19001%s",
		(int)( cl - svs.clients ), extra );
	SV_Plugin_Event( "player_finish", data );
}

static void Start( void )
{
	Test_SetCvar( "fs_homepath", home, 0 );
	Test_SetCvar( "sv_plugins", "records:auth,chat,storage,http=api.test", 0 );
	Test_SetCvar( "records_url", "https://api.test/", 0 );
	Test_SetCvar( "records_key", "secret-key", 0 );
	Test_SetCvar( "mapname", "st1", 0 );
	Test_SetCvar( "df_promode", "0", 0 );
	Test_SetCvar( "sv_mapChecksum", "12345", 0 );
	ClearOutput();
	SV_Plugins_Init();
}

int main( int argc, char **argv )
{
	client_t *cl;
	testHttp_t *r;
	char path[600], text[300];
	int i;

	if ( argc < 2 ) {
		printf( "usage: records_e2e <home dir containing plugins/records.qvm>\n" );
		return 2;
	}
	home = argv[1];
	svs.clients = calloc( MAX_CLIENTS, sizeof( client_t ) );
	sv.maxclients = 8;
	svs.time = 100000;
	testMilliseconds = 1000;

	snprintf( path, sizeof( path ), "%s/plugin-data/records/records.dat", home );
	remove( path );
	snprintf( path, sizeof( path ), "%s/plugin-data/records/records.head", home );
	remove( path );

	// ------------------------------------------------------------------
	printf( "start\n" );
	Start();
	CHECK( FindPrint( "Plugin 'records'" ) >= 0 && FindPrint( "loaded" ) >= 0, "the plugin was not loaded" );
	CHECK( FindPrint( "saving records to https://api.test (0 waiting from earlier)" ) >= 0, "no start message (see the prints)" );
	CHECK( SV_Plugins_HandleAuth(), "the plugin should answer logins" );

	// ------------------------------------------------------------------
	printf( "login\n" );
	Test_HttpReset();
	cl = Connect( 0, "\\name\\Tester\\cl_login\\1" );
	ClientAnswers( cl );
	r = Request( 0 );
	CHECK( r != NULL, "no request to verify the login" );
	if ( r ) {
		CHECK( r->method == 1 && !strcmp( r->url, "https://api.test/v1/auth/verify" ), "login request: %d %s", r->method, r->url );
		CHECK( strstr( r->headers, "Authorization: Bearer secret-key" ) != NULL, "the server key is missing: %s", r->headers );
		CHECK( strstr( r->body, "\"signed_address\":\"1.2.3.4:27960\"" ) && strstr( r->body, "\"public_key\":\"" ) && strstr( r->body, "\"signature\":\"" ) &&
			strstr( r->body, "\"nonce\":\"" ), "login body: %s", r->body );
	}
	// 2^53 + 1: does not fit in a float or a 32 bit number
	Answer( 0, 200, "{\"user_id\":9007199254740993,\"username\":\"mix\"}" );
	CHECK( FindCmd( 0, "auth_state ok mix" ) >= 0, "the player was not told about the login" );
	CHECK( cl->auth.accountID == 9007199254740993LL, "account number %lld", (long long)cl->auth.accountID );

	// ------------------------------------------------------------------
	printf( "a finished run\n" );
	Test_HttpReset();
	ClearOutput();
	Finish( cl, "" );
	SV_Plugins_Frame();
	CHECK( FileSize( "records.dat" ) > 0, "the record was not queued in the store" );
	r = Request( 0 );
	CHECK( r != NULL, "the record was not sent" );
	if ( r ) {
		CHECK( !strcmp( r->url, "https://api.test/v1/records" ) && r->method == 1, "record request: %s", r->url );
		CHECK( strstr( r->headers, "Authorization: Bearer secret-key" ) != NULL, "the server key is missing" );
		CHECK( strstr( r->body, "\"user_id\":9007199254740993" ), "the account number in the record: %s", r->body );
		CHECK( strstr( r->body, "\"map\":\"st1\"" ) && strstr( r->body, "\"physics\":\"vq3\"" ) && strstr( r->body, "\"mode\":\"run\"" ) &&
			strstr( r->body, "\"time_ms\":12345" ) && strstr( r->body, "\"est_ms\":12340" ) && strstr( r->body, "\"map_checksum\":12345" ) &&
			strstr( r->body, "\"client_version\":19001" ) && strstr( r->body, "\"event_id\":\"" ), "record body: %s", r->body );
		{
			const char *p = strstr( r->body, "\"played_at\":\"" );
			int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;

			CHECK( p && sscanf( p + 13, "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month, &day, &hour, &minute, &second ) == 6, "no date in the record" );
			CHECK( year >= 2025 && year < 2100 && month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour < 24 && minute < 60 && second < 61,
				"date: %04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second );
		}
	}
	Answer( 0, 201, "{\"rank\":1,\"is_pb\":true,\"is_wr\":true}" );
	CHECK( FindCmd( 0, "Record saved" ) >= 0 && FindCmd( 0, "12.345" ) >= 0, "the player was not told that the record was saved" );
	CHECK( FindCmd( -1, "world record" ) >= 0, "no announcement of the world record" );
	CHECK( FileSize( "records.dat" ) < 0, "the queue was not emptied (%d bytes)", FileSize( "records.dat" ) );

	// ------------------------------------------------------------------
	printf( "the API cannot be reached: the record waits and is sent again\n" );
	Test_HttpReset();
	ClearOutput();
	Finish( cl, "" );
	SV_Plugins_Frame();
	CHECK( Request( 0 ) != NULL, "not sent" );
	Answer( 0, 0, "" );
	CHECK( FileSize( "records.dat" ) > 0, "the record was dropped although the API could not be reached" );
	CHECK( FindPrint( "cannot reach the records API" ) >= 0, "no message about the outage" );
	Test_HttpReset();
	SV_Plugins_Frame();
	CHECK( Request( 0 ) == NULL, "sent again at once" );
	testMilliseconds += 3000;
	SV_Plugins_Frame();
	CHECK( Request( 0 ) != NULL, "not sent again after the pause" );
	Answer( 0, 201, "{\"rank\":2,\"is_pb\":false,\"is_wr\":false}" );
	CHECK( FileSize( "records.dat" ) < 0, "the queue was not emptied after the API came back" );

	// ------------------------------------------------------------------
	printf( "the server stops while a record waits\n" );
	Test_HttpReset();
	ClearOutput();
	Finish( cl, "" );
	SV_Plugins_Frame();
	Answer( 0, 503, "{\"error\":{\"code\":\"unavailable\"}}" );
	CHECK( FileSize( "records.dat" ) > 0, "the record is not kept" );
	SV_Plugins_Shutdown();
	ClearOutput();
	Test_HttpReset();
	testMilliseconds += 10000;
	Start();
	CHECK( FindPrint( "(1 waiting from earlier)" ) >= 0, "the record did not survive the restart" );
	SV_Plugins_Frame();
	r = Request( 0 );
	CHECK( r != NULL && strstr( r->body, "\"time_ms\":12345" ), "the record was not sent after the restart" );
	Answer( 0, 201, "{\"rank\":1,\"is_pb\":false,\"is_wr\":false}" );
	CHECK( FileSize( "records.dat" ) < 0, "the queue was not emptied" );

	// ------------------------------------------------------------------
	printf( "a record the API will never take\n" );
	Test_HttpReset();
	ClearOutput();
	Finish( cl, "" );
	SV_Plugins_Frame();
	Answer( 0, 422, "{\"error\":{\"code\":\"invalid_time\",\"message\":\"time is not possible\"}}" );
	CHECK( FileSize( "records.dat" ) < 0, "a refused record stays in the queue" );
	CHECK( FindPrint( "was refused by the API" ) >= 0, "no message about the refused record" );

	// ------------------------------------------------------------------
	printf( "not logged in, bad runs\n" );
	Test_HttpReset();
	ClearOutput();
	Connect( 3, "\\name\\Anon\\cl_login\\0" );
	Finish( &svs.clients[3], "" );
	SV_Plugins_Frame();
	CHECK( Request( 0 ) == NULL && FileSize( "records.dat" ) < 0, "a run of a player who is not logged in was saved" );
	SV_Plugin_Event( "player_finish", "\\client\\0\\time\\0\\promode\\0\\mode\\1" );
	SV_Plugin_Event( "player_finish", "\\client\\0\\time\\1000\\promode\\7\\mode\\1" );
	SV_Plugin_Event( "player_finish", "\\client\\0\\time\\1000\\promode\\0\\mode\\9" );
	SV_Plugin_Event( "player_finish", "\\client\\99\\time\\1000\\promode\\0\\mode\\1" );
	SV_Plugins_Frame();
	CHECK( Request( 0 ) == NULL && FileSize( "records.dat" ) < 0, "a bad run was saved" );

	// ------------------------------------------------------------------
	printf( "logins the API turns down\n" );
	Test_HttpReset();
	ClearOutput();
	cl = Connect( 1, "\\name\\Stranger\\cl_login\\1" );
	ClientAnswers( cl );
	CHECK( Request( 0 ) != NULL, "no login request" );
	Answer( 0, 404, "{\"error\":{\"code\":\"unknown_key\"}}" );
	CHECK( FindCmd( 1, "auth_state unknown_key" ) >= 0, "unknown key not reported to the player" );
	Test_HttpReset();
	ClearOutput();
	cl = Connect( 2, "\\name\\Banned\\cl_login\\1" );
	ClientAnswers( cl );
	Answer( 0, 403, "{\"error\":{\"code\":\"banned\"}}" );
	CHECK( FindCmd( 2, "auth_state failed banned" ) >= 0, "ban not reported to the player" );
	Test_HttpReset();
	ClearOutput();
	cl = Connect( 4, "\\name\\Unlucky\\cl_login\\1" );
	ClientAnswers( cl );
	Answer( 0, 0, "" );
	CHECK( FindCmd( 4, "auth_state unavailable" ) >= 0, "an outage not reported to the player as such" );
	Test_HttpReset();
	ClearOutput();
	cl = Connect( 5, "\\name\\Cheater\\cl_login\\1" );
	ClientAnswers( cl );
	Answer( 0, 401, "{\"error\":{\"code\":\"unauthorized\"}}" );
	CHECK( FindCmd( 5, "auth_state unavailable" ) >= 0 && FindPrint( "did not accept this server's key" ) >= 0, "a wrong server key is a problem of the server, not of the player" );

	// ------------------------------------------------------------------
	printf( "!top\n" );
	Test_HttpReset();
	ClearOutput();
	cl = &svs.clients[0];
	snprintf( text, sizeof( text ), "\\client\\0\\serial\\%d\\text\\!top", cl->auth.serial );
	SV_Plugin_Event( "chat_command", text );
	r = Request( 0 );
	CHECK( r != NULL, "no request for the rankings" );
	if ( r ) {
		CHECK( r->method == 0 && !strcmp( r->url, "https://api.test/v1/maps/st1/leaderboard?physics=vq3&per_page=10" ), "rankings request: %s", r->url );
	}
	Answer( 0, 200, "{\"total\":2,\"entries\":[{\"rank\":1,\"time_ms\":5560,\"username\":\"mix\"},{\"rank\":2,\"time_ms\":6000,\"username\":\"bob\"}]}" );
	CHECK( FindCmd( 0, "Rankings on st1 (vq3)" ) >= 0, "no heading" );
	CHECK( FindCmd( 0, "mix" ) >= 0 && FindCmd( 0, "5:560" ) >= 0 && FindCmd( 0, "bob" ) >= 0 && FindCmd( 0, "+0:440" ) >= 0, "the table is wrong" );
	// the same table again: from the memory, not from the API (after the pause for the same player)
	Test_HttpReset();
	ClearOutput();
	testMilliseconds += 4000;
	SV_Plugin_Event( "chat_command", text );
	CHECK( Request( 0 ) == NULL && FindCmd( 0, "bob" ) >= 0, "the table was not reused" );
	Test_HttpReset();
	ClearOutput();
	SV_Plugin_Event( "chat_command", text );
	CHECK( FindCmd( 0, "Please wait" ) >= 0, "no pause between two !top of one player" );
	Test_HttpReset();
	ClearOutput();
	testMilliseconds += 4000;
	snprintf( text, sizeof( text ), "\\client\\0\\serial\\%d\\text\\!top nomap cpm", cl->auth.serial );
	SV_Plugin_Event( "chat_command", text );
	r = Request( 0 );
	CHECK( r != NULL && strstr( r->url, "/maps/nomap/leaderboard?physics=cpm" ), "another map and physics: %s", r ? r->url : "(no request)" );
	Answer( 0, 404, "{\"error\":{\"code\":\"not_found\"}}" );
	CHECK( FindCmd( 0, "No records on nomap (cpm) yet." ) >= 0, "a map without records is not said so" );

	// ------------------------------------------------------------------
	printf( "shutdown\n" );
	ClearOutput();
	SV_Plugins_Shutdown();
	CHECK( FindPrint( "stopped" ) < 0 && FindPrint( "trap" ) < 0, "the plugin was stopped by the engine" );

	printf( failures ? "FAILED: %d check(s)\n" : "all records plugin checks passed\n", failures );
	return failures ? 1 : 0;
}
