/*
A QVM program that tests the library of the SDK: known results, and the C library of the host for the things it can tell
(formatting of numbers, strtof). Failures go to Report(); run() returns how many checks it did.
*/

#define ODF_PLUGIN_VERSION "1"
#define EXPORTS( X ) X(OnRun)
#include "odft_entry.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <limits.h>

static int checks;

static void Fail( int line, const char *what )
{
	char text[256];

	snprintf( text, sizeof( text ), "line %d: %s", line, what );
	Report( text );
}

#define CHECK( cond ) do { checks++; if ( !( cond ) ) Fail( __LINE__, #cond ); } while ( 0 )

static void CheckText( int line, const char *got, const char *want )
{
	checks++;
	if ( strcmp( got, want ) ) {
		char text[256];

		snprintf( text, sizeof( text ), "line %d: got \"%s\", wanted \"%s\"", line, got, want );
		Report( text );
	}
}

#define CHECKTEXT( got, want ) CheckText( __LINE__, got, want )

static void CheckNear( int line, float got, float want, float tolerance )
{
	checks++;
	if ( !( fabsf( got - want ) <= tolerance ) ) {
		char text[256];

		snprintf( text, sizeof( text ), "line %d: got %.6f, wanted %.6f", line, got, want );
		Report( text );
	}
}

#define CHECKNEAR( got, want, tolerance ) CheckNear( __LINE__, got, want, tolerance )

static unsigned int seed = 12345;

static unsigned int Random( void )
{
	seed = seed * 1664525u + 1013904223u;
	return seed;
}

typedef union {
	float f;
	unsigned int u;
} bits_t;

static void TestStrings( void )
{
	char buf[32];
	const char *s = "hello world";

	CHECK( strlen( "" ) == 0 );
	CHECK( strlen( s ) == 11 );
	CHECK( strcmp( "abc", "abc" ) == 0 );
	CHECK( strcmp( "abc", "abd" ) < 0 );
	CHECK( strcmp( "abd", "abc" ) > 0 );
	CHECK( strcmp( "ab", "abc" ) < 0 );
	CHECK( strncmp( "abcdef", "abcxyz", 3 ) == 0 );
	CHECK( strncmp( "abcdef", "abcxyz", 4 ) < 0 );
	CHECK( strcasecmp( "HeLLo", "hello" ) == 0 );
	CHECK( strcasecmp( "hello", "help" ) < 0 );
	CHECK( strncasecmp( "HELLO", "help", 3 ) == 0 );
	strcpy( buf, "abc" );
	strcat( buf, "def" );
	CHECKTEXT( buf, "abcdef" );
	strncat( buf, "ghijkl", 2 );
	CHECKTEXT( buf, "abcdefgh" );
	memset( buf, 'x', sizeof( buf ) );
	strncpy( buf, "ab", 5 );
	CHECK( buf[0] == 'a' && buf[1] == 'b' && buf[2] == 0 && buf[4] == 0 && buf[5] == 'x' );
	CHECK( strchr( s, 'o' ) == s + 4 );
	CHECK( strrchr( s, 'o' ) == s + 7 );
	CHECK( strchr( s, 'z' ) == NULL );
	CHECK( strchr( s, 0 ) == s + 11 );
	CHECK( strstr( s, "world" ) == s + 6 );
	CHECK( strstr( s, "" ) == s );
	CHECK( strstr( s, "worlds" ) == NULL );
	CHECK( memchr( s, 'w', 11 ) == s + 6 );
	CHECK( strspn( "aabbc", "ab" ) == 4 && strcspn( "hello", "lo" ) == 2 && strcspn( "abc", "z" ) == 3 );
	CHECK( strpbrk( s, "xyzw" ) == s + 6 && strpbrk( s, "xyz" ) == NULL );
	CHECK( memcmp( "abc", "abd", 3 ) < 0 );
	CHECK( memcmp( "abc", "abd", 2 ) == 0 );
}

static char big1[300], big2[300];

static void TestMemory( void )
{
	int i, ok;

	// small and big copies (big ones go to the engine), overlapping moves in both directions
	for ( i = 0; i < 300; i++ ) {
		big1[i] = (char)i;
	}
	memcpy( big2, big1, 10 );
	CHECK( !memcmp( big2, big1, 10 ) );
	memcpy( big2, big1, 300 );
	CHECK( !memcmp( big2, big1, 300 ) );
	memset( big2, 7, 100 );
	for ( ok = 1, i = 0; i < 100; i++ ) {
		ok &= big2[i] == 7;
	}
	CHECK( ok && big2[100] == (char)100 );
	memset( big2, 9, 5 );
	CHECK( big2[4] == 9 && big2[5] == 7 );
	memmove( big1 + 2, big1, 20 );
	CHECK( big1[2] == 0 && big1[21] == 19 && big1[0] == 0 && big1[1] == 1 );
	for ( i = 0; i < 300; i++ ) {
		big1[i] = (char)i;
	}
	memmove( big1, big1 + 2, 20 );
	CHECK( big1[0] == 2 && big1[19] == 21 && big1[20] == 20 );
}

static void TestCtype( void )
{
	CHECK( isdigit( '5' ) && !isdigit( 'a' ) );
	CHECK( isalpha( 'a' ) && isalpha( 'Z' ) && !isalpha( '1' ) );
	CHECK( isalnum( '1' ) && isalnum( 'q' ) && !isalnum( '_' ) );
	CHECK( isspace( ' ' ) && isspace( '\t' ) && isspace( '\n' ) && !isspace( 'a' ) );
	CHECK( isupper( 'A' ) && !isupper( 'a' ) && islower( 'a' ) && !islower( 'A' ) );
	CHECK( isxdigit( 'f' ) && isxdigit( 'A' ) && isxdigit( '9' ) && !isxdigit( 'g' ) );
	CHECK( isprint( 'a' ) && isprint( ' ' ) && !isprint( 10 ) && ispunct( '!' ) && !ispunct( 'a' ) );
	CHECK( tolower( 'Q' ) == 'q' && tolower( '5' ) == '5' && toupper( 'q' ) == 'Q' && toupper( '!' ) == '!' );
}

static int CompareInts( const void *a, const void *b )
{
	return *(const int *)a - *(const int *)b;
}

static void TestStdlib( void )
{
	char *end;
	int v[40], i, ok;

	CHECK( abs( -5 ) == 5 && abs( 5 ) == 5 && abs( 0 ) == 0 );
	CHECK( atoi( "  -42abc" ) == -42 );
	CHECK( atoi( "x" ) == 0 );
	CHECK( strtol( "0x1F", &end, 16 ) == 31 && *end == 0 );
	CHECK( strtol( "0x1F", &end, 0 ) == 31 );
	CHECK( strtol( "017", &end, 0 ) == 15 );
	CHECK( strtol( "17", &end, 8 ) == 15 );
	CHECK( strtol( "zz", &end, 36 ) == 35 * 36 + 35 );
	CHECK( strtol( "12 34", &end, 10 ) == 12 && *end == ' ' );
	CHECK( strtol( "abc", &end, 10 ) == 0 && end[0] == 'a' );
	CHECK( strtol( "2147483647", NULL, 10 ) == INT_MAX );
	CHECK( strtol( "99999999999", NULL, 10 ) == LONG_MAX );
	CHECK( strtol( "-2147483648", NULL, 10 ) == LONG_MIN );
	CHECK( strtol( "-99999999999", NULL, 10 ) == LONG_MIN );
	CHECK( strtoul( "4294967295", NULL, 10 ) == 4294967295u );
	CHECK( strtoul( "ff", NULL, 16 ) == 255 );

	for ( i = 0; i < 40; i++ ) {
		v[i] = (int)( Random() >> 20 ) - 2048;
	}
	qsort( v, 40, sizeof( int ), CompareInts );
	for ( ok = 1, i = 1; i < 40; i++ ) {
		ok &= v[i - 1] <= v[i];
	}
	CHECK( ok );
	qsort( v, 0, sizeof( int ), CompareInts );
	CHECK( rand() >= 0 && rand() <= RAND_MAX );
}

static void TestMath( void )
{
	CHECKNEAR( sinf( 0.5f ), 0.4794255f, 1e-6f );
	CHECKNEAR( cosf( 0.5f ), 0.8775826f, 1e-6f );
	CHECKNEAR( tanf( 0.5f ), 0.5463025f, 1e-6f );
	CHECKNEAR( atan2f( 1.0f, -1.0f ), 2.3561945f, 1e-6f );
	CHECKNEAR( atanf( 1.0f ), 0.7853982f, 1e-6f );
	CHECKNEAR( asinf( 0.5f ), 0.5235988f, 1e-6f );
	CHECKNEAR( acosf( 0.5f ), 1.0471976f, 1e-6f );
	CHECKNEAR( acosf( 2.0f ), 0.0f, 0.0f );
	CHECKNEAR( acosf( -2.0f ), M_PI, 1e-6f );
	CHECKNEAR( asinf( -2.0f ), -M_PI_2, 1e-6f );
	CHECKNEAR( sqrtf( 2.0f ), 1.4142135f, 1e-6f );
	CHECKNEAR( sqrt( 9.0 ), 3.0f, 0.0f );
	CHECK( fabsf( -2.5f ) == 2.5f && fabsf( 2.5f ) == 2.5f && fabs( -1.0 ) == 1.0 );
	CHECK( floorf( 2.7f ) == 2.0f && floorf( -2.7f ) == -3.0f && floorf( -3.0f ) == -3.0f && floorf( 0.0f ) == 0.0f );
	CHECK( ceilf( 2.2f ) == 3.0f && ceilf( -2.2f ) == -2.0f && ceilf( 4.0f ) == 4.0f );
	CHECK( truncf( 2.7f ) == 2.0f && truncf( -2.7f ) == -2.0f );
	CHECK( roundf( 2.5f ) == 3.0f && roundf( -2.5f ) == -3.0f && roundf( 2.4f ) == 2.0f && roundf( -2.4f ) == -2.0f );
	CHECK( roundf( 0.49999997f ) == 0.0f );
	CHECK( lroundf( 7.5f ) == 8 && lroundf( -7.5f ) == -8 && lround( 7.4 ) == 7 );
	CHECK( floorf( 1e20f ) == 1e20f && floorf( 8388609.0f ) == 8388609.0f );
	CHECKNEAR( fmodf( 7.5f, 2.0f ), 1.5f, 0.0f );
	CHECKNEAR( fmodf( -7.5f, 2.0f ), -1.5f, 0.0f );
	CHECKNEAR( fmodf( 370.0f, 360.0f ), 10.0f, 0.0f );
	CHECK( isnan( sqrtf( -1.0f ) ) || sqrtf( -1.0f ) == 0.0f );
}

static void TestFormat( void )
{
	char buf[64];
	bits_t minusZero;
	int n;

	n = snprintf( buf, sizeof( buf ), "%d|%i|%u|%x|%X|%o|%c|%s|%%", -5, 6, 4000000000u, 255, 255, 8, 'z', "str" );
	CHECKTEXT( buf, "-5|6|4000000000|ff|FF|10|z|str|%" );
	CHECK( n == 32 );
	snprintf( buf, sizeof( buf ), "[%5d][%-5d][%05d][%+d][% d][%.3d]", 42, 42, 42, 42, 42, 7 );
	CHECKTEXT( buf, "[   42][42   ][00042][+42][ 42][007]" );
	snprintf( buf, sizeof( buf ), "[%5s][%-5s][%.2s][%*d][%.*s]", "ab", "ab", "abcdef", 4, 7, 3, "abcdef" );
	CHECKTEXT( buf, "[   ab][ab   ][ab][   7][abc]" );
	snprintf( buf, sizeof( buf ), "[%#x][%#X][%08x][%-8x]", 255, 255, 255, 255 );
	CHECKTEXT( buf, "[0xff][0XFF][000000ff][ff      ]" );
	snprintf( buf, sizeof( buf ), "%s|%d|%x", (char *)0, INT_MIN, INT_MIN );
	CHECKTEXT( buf, "(null)|-2147483648|80000000" );
	snprintf( buf, sizeof( buf ), "[%.0d][%5.0d][%-04d]", 0, 0, 5 );
	CHECKTEXT( buf, "[][     ][5   ]" );
	snprintf( buf, sizeof( buf ), "%f %.2f %.0f %.1f", 3.14159f, 2.0f, 2.5f, 0.05f );
	CHECKTEXT( buf, "3.141590 2.00 2 0.1" );
	snprintf( buf, sizeof( buf ), "%.2f %.2f %.0f %.0f %.0f", 0.125f, 0.375f, 0.5f, 1.5f, 3.5f );
	CHECKTEXT( buf, "0.12 0.38 0 2 4" );
	minusZero.u = 0x80000000u;
	snprintf( buf, sizeof( buf ), "%.0f %.1f %f", 0.9f, 9.96f, minusZero.f );
	CHECKTEXT( buf, "1 10.0 -0.000000" );
	snprintf( buf, sizeof( buf ), "[%8.3f][%-8.3f][%08.3f][%+.1f]", 3.14159f, 3.14159f, -3.14159f, 2.0f );
	CHECKTEXT( buf, "[   3.142][3.142   ][-003.142][+2.0]" );
	snprintf( buf, sizeof( buf ), "%f", 4294967296.0f );
	CHECKTEXT( buf, "4294967296.000000" );
	snprintf( buf, sizeof( buf ), "%.0f", FLT_MAX );
	CHECKTEXT( buf, "340282346638528859811704183484516925440" );

	// cut to the size, always terminated, the result is the full length
	n = snprintf( buf, 5, "%s", "abcdefgh" );
	CHECK( n == 8 );
	CHECKTEXT( buf, "abcd" );
	n = snprintf( buf, 1, "%s", "abcdefgh" );
	CHECK( n == 8 && buf[0] == 0 );
	n = snprintf( NULL, 0, "%d", 12345 );
	CHECK( n == 5 );
	n = sprintf( buf, "%d-%d", 1, 2 );
	CHECK( n == 3 );
	CHECKTEXT( buf, "1-2" );
	snprintf( buf, sizeof( buf ), "%q %" );
	CHECKTEXT( buf, "%q " );
}

// the formats that are compared with the C library of the host
static const char *const floatFormats[] = {
	"%f", "%.0f", "%.1f", "%.2f", "%.3f", "%.6f", "%10.2f", "%-10.2f|", "%+.3f", "%08.2f", "% .2f", "%#.0f", "%12.4f", "%.5f"
};

static void FuzzFormat( void )
{
	char got[128], want[128];
	int i, f, kind, bad = 0;
	bits_t b;
	float v;

	for ( i = 0; i < 6000 && bad < 8; i++ ) {
		kind = i % 4;
		b.u = Random();
		if ( kind == 0 ) {			// any float
			if ( ( ( b.u >> 23 ) & 255 ) == 255 ) {
				continue;
			}
			v = b.f;
		} else if ( kind == 1 ) {	// the sizes a game has
			v = ( (float)( Random() >> 8 ) / 16777216.0f - 0.5f ) * 20000.0f;
		} else if ( kind == 2 ) {	// small numbers and fractions that are exact in binary
			v = (float)( (int)( Random() >> 20 ) - 2048 ) / (float)( 1 << ( Random() >> 28 ) );
		} else {					// 1.something with a few bits
			v = (float)( Random() & 0xFFFF ) * 0.015625f;
		}
		f = Random() % ( sizeof( floatFormats ) / sizeof( floatFormats[0] ) );
		snprintf( got, sizeof( got ), floatFormats[f], v );
		HostFormatFloat( want, sizeof( want ), floatFormats[f], v );
		checks++;
		if ( strcmp( got, want ) ) {
			char text[300];

			snprintf( text, sizeof( text ), "format %s of bits %x: got \"%s\", host \"%s\"", floatFormats[f], b.u, got, want );
			Report( text );
			bad++;
		}
	}
}

static const char *const intFormats[] = { "%d", "%5d", "%-5d|", "%05d", "%+d", "% d", "%.4d", "%8.3d", "%u", "%x", "%X", "%#x", "%08X", "%o", "%c" };

static void FuzzInt( void )
{
	char got[128], want[128];
	int i, f, bad = 0, v;

	for ( i = 0; i < 3000 && bad < 8; i++ ) {
		v = (int)Random();
		if ( i % 3 == 1 ) {
			v >>= 16;
		} else if ( i % 3 == 2 ) {
			v = ( v & 0x7F ) + 32;
		}
		f = Random() % ( sizeof( intFormats ) / sizeof( intFormats[0] ) );
		snprintf( got, sizeof( got ), intFormats[f], v );
		HostFormatInt( want, sizeof( want ), intFormats[f], v );
		checks++;
		if ( strcmp( got, want ) ) {
			char text[300];

			snprintf( text, sizeof( text ), "format %s of %d: got \"%s\", host \"%s\"", intFormats[f], v, got, want );
			Report( text );
			bad++;
		}
	}
}

static void StrtofCase( const char *text, int line )
{
	bits_t got, want;
	int diff;

	got.f = strtof( text, NULL );
	want.f = HostStrtof( text );
	diff = (int)got.u - (int)want.u;
	checks++;
	if ( diff < -2 || diff > 2 ) {		// two units of the last place are allowed (the roundings of a float)
		char msg[200];

		snprintf( msg, sizeof( msg ), "line %d: strtof(\"%s\") bits %x, host %x", line, text, got.u, want.u );
		Report( msg );
	}
}

static void TestStrtof( void )
{
	static const char *const cases[] = {
		"0", "1", "-1", "+2.5", "3.14159265", "0.1", "0.000123", "123456789", "1234567890123", "1e10", "1E-5", "2.5e+3", ".5", "5.",
		"-0.0", "1e38", "1e-38", "123.456e2", "0.30000001192092896", "16777217", "1e45", "1e-50", "9.99999999", "100000000000000000000"
	};
	char *end;
	char text[48];
	int i, d, e, len;
	float v;

	for ( i = 0; i < (int)( sizeof( cases ) / sizeof( cases[0] ) ); i++ ) {
		StrtofCase( cases[i], __LINE__ );
	}
	// where it stops
	v = strtof( "12.5abc", &end );
	CHECK( v == 12.5f && *end == 'a' );
	v = strtof( "  7e", &end );
	CHECK( v == 7.0f && *end == 'e' );
	v = strtof( "abc", &end );
	CHECK( v == 0.0f && end[0] == 'a' );
	v = strtof( ".", &end );
	CHECK( v == 0.0f && end[0] == '.' );
	CHECK( atof( "2.25" ) == 2.25f );
	// random: digits.digits e exponent
	for ( i = 0; i < 1500; i++ ) {
		len = 1 + Random() % 7;
		d = Random() % 100000;
		e = (int)( Random() % 30 ) - 15;
		snprintf( text, sizeof( text ), "%s%d.%0*de%d", Random() & 1 ? "-" : "", d, len, (int)( Random() % 1000 ), e );
		StrtofCase( text, __LINE__ );
	}
}

static void TestRoundTrip( void )
{
	char text[64];
	int i;
	bits_t b;
	float back;

	// what snprintf prints with enough digits is read back as the same number (up to the 9 digits strtof uses)
	for ( i = 0; i < 400; i++ ) {
		b.f = ( (float)( Random() >> 8 ) / 16777216.0f - 0.5f ) * 1000.0f;
		snprintf( text, sizeof( text ), "%.6f", b.f );
		back = strtof( text, NULL );
		CHECKNEAR( back, b.f, fabsf( b.f ) * 4e-7f + 1e-6f );
	}
}

int OnRun( int x )
{
	checks = 0;
	TestStrings();
	TestMemory();
	TestCtype();
	TestStdlib();
	TestMath();
	TestFormat();
	FuzzFormat();
	FuzzInt();
	TestStrtof();
	TestRoundTrip();
	return checks;
}
