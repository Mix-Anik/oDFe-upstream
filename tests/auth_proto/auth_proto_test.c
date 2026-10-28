/*
Tests the login protocol helpers and prints an interoperability test vector
(a fixed seed, nonce and address with the resulting public key and signature)
that the Go API verifies in internal/auth/vectors_test.go.
Not part of the engine build, see run.sh.
*/

#include <stdio.h>
#include <string.h>

#include "auth_proto.h"

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

int main( void )
{
	uint8_t seed[AUTH_SEED_SIZE], nonce[AUTH_NONCE_SIZE], pub[AUTH_PUBKEY_SIZE], sig[AUTH_SIG_SIZE], msg[256], back[64];
	char hex[200];
	const char *addr = "1.2.3.4:27960";
	int i, len;

	for ( i = 0; i < AUTH_SEED_SIZE; i++ ) seed[i] = (uint8_t)i;
	for ( i = 0; i < AUTH_NONCE_SIZE; i++ ) nonce[i] = (uint8_t)( 0xA0 + i );

	// hex
	CHECK( Auth_HexEncode( hex, sizeof( hex ), nonce, AUTH_NONCE_SIZE ) == 64, "hex encode length" );
	CHECK( strncmp( hex, "a0a1a2a3", 8 ) == 0, "hex encode is lower case: %.8s", hex );
	CHECK( Auth_HexDecode( back, AUTH_NONCE_SIZE, hex ) == AUTH_NONCE_SIZE && !memcmp( back, nonce, AUTH_NONCE_SIZE ), "hex round trip" );
	hex[3] = 'A';
	CHECK( Auth_HexDecode( back, AUTH_NONCE_SIZE, hex ) == AUTH_NONCE_SIZE, "upper case hex accepted" );
	hex[3] = 'g';
	CHECK( Auth_HexDecode( back, AUTH_NONCE_SIZE, hex ) == -1, "non hex accepted" );
	CHECK( Auth_HexDecode( back, AUTH_NONCE_SIZE, "abcd" ) == -1, "short hex accepted" );
	CHECK( Auth_HexEncode( hex, 10, nonce, AUTH_NONCE_SIZE ) == -1, "encode overflow not detected" );

	// addresses
	CHECK( Auth_ValidAddress( "1.2.3.4:27960" ) && Auth_ValidAddress( "[::1]:27960" ) && Auth_ValidAddress( "play.example.com:27960" ), "valid addresses rejected" );
	CHECK( !Auth_ValidAddress( "" ) && !Auth_ValidAddress( "a b" ) && !Auth_ValidAddress( "a\"b" ) && !Auth_ValidAddress( "a\\b" ), "invalid addresses accepted" );

	// sign and verify
	len = Auth_LoginMessage( msg, sizeof( msg ), nonce, addr );
	CHECK( len == 6 + 32 + (int)strlen( addr ), "message length %d", len );
	CHECK( !memcmp( msg, "authv1", 6 ) && !memcmp( msg + 6, nonce, 32 ) && !memcmp( msg + 38, addr, strlen( addr ) ), "message layout" );
	CHECK( Auth_LoginMessage( msg, 10, nonce, addr ) == -1, "message overflow not detected" );
	CHECK( Auth_LoginMessage( msg, sizeof( msg ), nonce, "bad address" ) == -1, "invalid address signed" );

	Auth_PublicKey( pub, seed );
	Auth_Sign( sig, seed, msg, len );
	CHECK( Auth_Verify( sig, pub, msg, len ), "own signature does not verify" );
	msg[10] ^= 1;
	CHECK( !Auth_Verify( sig, pub, msg, len ), "tampered message verifies" );
	msg[10] ^= 1;
	sig[0] ^= 1;
	CHECK( !Auth_Verify( sig, pub, msg, len ), "tampered signature verifies" );
	sig[0] ^= 1;

	// the seed given must not be changed (Monocypher wipes the buffer it is handed)
	for ( i = 0; i < AUTH_SEED_SIZE; i++ ) CHECK( seed[i] == i, "seed was modified" );

	// the vector for the Go side
	printf( "VECTOR\n" );
	Auth_HexEncode( hex, sizeof( hex ), seed, AUTH_SEED_SIZE );		printf( "seed  %s\n", hex );
	Auth_HexEncode( hex, sizeof( hex ), nonce, AUTH_NONCE_SIZE );	printf( "nonce %s\n", hex );
	printf( "addr  %s\n", addr );
	Auth_HexEncode( hex, sizeof( hex ), pub, AUTH_PUBKEY_SIZE );	printf( "pub   %s\n", hex );
	Auth_HexEncode( hex, sizeof( hex ), sig, AUTH_SIG_SIZE );		printf( "sig   %s\n", hex );

	printf( failures ? "FAILED: %d check(s)\n" : "all auth_proto checks passed\n", failures );
	return failures ? 1 : 0;
}
