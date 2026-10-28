/*
===========================================================================
Login protocol helpers, see auth_proto.h
===========================================================================
*/

#include <string.h>

#include "auth_proto.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"

static const char hexDigits[] = "0123456789abcdef";

int Auth_HexEncode( char *out, int outSize, const uint8_t *in, int len )
{
	int i;

	if ( len < 0 || outSize < len * 2 + 1 ) {
		return -1;
	}
	for ( i = 0; i < len; i++ ) {
		out[i * 2] = hexDigits[in[i] >> 4];
		out[i * 2 + 1] = hexDigits[in[i] & 15];
	}
	out[len * 2] = '\0';
	return len * 2;
}

static int HexValue( char c )
{
	if ( c >= '0' && c <= '9' ) return c - '0';
	if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
	if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
	return -1;
}

int Auth_HexDecode( uint8_t *out, int outSize, const char *in )
{
	int i;

	if ( strlen( in ) != (size_t)outSize * 2 ) {
		return -1;
	}
	for ( i = 0; i < outSize; i++ ) {
		int hi = HexValue( in[i * 2] ), lo = HexValue( in[i * 2 + 1] );

		if ( hi < 0 || lo < 0 ) {
			return -1;
		}
		out[i] = (uint8_t)( hi << 4 | lo );
	}
	return outSize;
}

int Auth_ValidAddress( const char *addr )
{
	size_t i, len = strlen( addr );

	if ( len == 0 || len > AUTH_MAX_ADDR ) {
		return 0;
	}
	for ( i = 0; i < len; i++ ) {
		char c = addr[i];

		if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
				c == '.' || c == ':' || c == '[' || c == ']' || c == '-' || c == '%' ) ) {
			return 0;
		}
	}
	return 1;
}

int Auth_LoginMessage( uint8_t *out, int outSize, const uint8_t nonce[AUTH_NONCE_SIZE], const char *addr )
{
	static const char tag[] = "authv1";
	int tagLen = (int)sizeof( tag ) - 1, addrLen;

	if ( !Auth_ValidAddress( addr ) ) {
		return -1;
	}
	addrLen = (int)strlen( addr );
	if ( outSize < tagLen + AUTH_NONCE_SIZE + addrLen ) {
		return -1;
	}
	memcpy( out, tag, tagLen );
	memcpy( out + tagLen, nonce, AUTH_NONCE_SIZE );
	memcpy( out + tagLen + AUTH_NONCE_SIZE, addr, addrLen );
	return tagLen + AUTH_NONCE_SIZE + addrLen;
}

void Auth_PublicKey( uint8_t pub[AUTH_PUBKEY_SIZE], const uint8_t seed[AUTH_SEED_SIZE] )
{
	uint8_t secret[64], seedCopy[AUTH_SEED_SIZE];

	memcpy( seedCopy, seed, AUTH_SEED_SIZE );		// key_pair wipes the seed it is given
	crypto_ed25519_key_pair( secret, pub, seedCopy );
	crypto_wipe( secret, sizeof( secret ) );
}

void Auth_Sign( uint8_t sig[AUTH_SIG_SIZE], const uint8_t seed[AUTH_SEED_SIZE], const uint8_t *msg, int len )
{
	uint8_t secret[64], pub[AUTH_PUBKEY_SIZE], seedCopy[AUTH_SEED_SIZE];

	memcpy( seedCopy, seed, AUTH_SEED_SIZE );
	crypto_ed25519_key_pair( secret, pub, seedCopy );
	crypto_ed25519_sign( sig, secret, msg, (size_t)len );
	crypto_wipe( secret, sizeof( secret ) );
}

int Auth_Verify( const uint8_t sig[AUTH_SIG_SIZE], const uint8_t pub[AUTH_PUBKEY_SIZE], const uint8_t *msg, int len )
{
	return crypto_ed25519_check( sig, pub, msg, (size_t)len ) == 0;
}
