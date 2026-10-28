/*
===========================================================================
Login protocol helpers shared by client and server: hex coding, the signed message
and the Ed25519 operations (Monocypher). No dependency on the rest of the engine.

The client signs   "authv1" || nonce (32 bytes) || server address (text)
where the nonce is the challenge sent by the server and the address is the one the
client connected to. See oDF-api, internal/auth.
===========================================================================
*/

#ifndef AUTH_PROTO_H
#define AUTH_PROTO_H

#include <stdint.h>

#define AUTH_NONCE_SIZE		32
#define AUTH_PUBKEY_SIZE	32
#define AUTH_SIG_SIZE		64
#define AUTH_SEED_SIZE		32

#define AUTH_MAX_ADDR		63		// longest server address text that is signed

// Writes lower case hex plus a terminating zero. Returns the number of characters, -1 if outSize is too small.
int		Auth_HexEncode( char *out, int outSize, const uint8_t *in, int len );

// Reads exactly outSize bytes worth of hex (upper or lower case). Returns the byte count, -1 if the text
// is not valid hex or has a different length.
int		Auth_HexDecode( uint8_t *out, int outSize, const char *in );

// True for the characters an address may contain in the protocol: letters, digits and . : [ ] - %
int		Auth_ValidAddress( const char *addr );

// Builds the message that is signed. Returns its length, -1 if out is too small or addr is not valid.
int		Auth_LoginMessage( uint8_t *out, int outSize, const uint8_t nonce[AUTH_NONCE_SIZE], const char *addr );

void	Auth_PublicKey( uint8_t pub[AUTH_PUBKEY_SIZE], const uint8_t seed[AUTH_SEED_SIZE] );
void	Auth_Sign( uint8_t sig[AUTH_SIG_SIZE], const uint8_t seed[AUTH_SEED_SIZE], const uint8_t *msg, int len );

// Returns 1 if the signature is valid. The engine itself never needs this (the API verifies), it is for tests.
int		Auth_Verify( const uint8_t sig[AUTH_SIG_SIZE], const uint8_t pub[AUTH_PUBKEY_SIZE], const uint8_t *msg, int len );

#endif
