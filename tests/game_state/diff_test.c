/*
Tests the runs of differences the game snapshots are made of (code/server/sv_diff.h): applying the runs of
Diff_Make( base, cur ) to base must give cur again, for every size and every kind of change.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../code/server/sv_diff.h"

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAILED: " __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

static unsigned int seed = 12345;

static unsigned int Rand( void )
{
	seed = seed * 1103515245u + 12345u;
	return ( seed >> 8 ) & 0xffffff;
}

// makes the runs, applies them to a copy of base and compares; returns the size of the runs
static int RoundTrip( const unsigned char *base, const unsigned char *cur, int size, const char *what )
{
	unsigned char *runs, *out;
	int n, m;

	n = Diff_Make( base, cur, size, NULL );
	runs = malloc( n + 1 );
	m = Diff_Make( base, cur, size, runs );
	CHECK( n == m, "%s: the size counted (%d) is not the size written (%d)", what, n, m );

	out = malloc( size + 1 );
	CHECK( Diff_Apply( runs, n, base, out, size ) == 0, "%s: Diff_Apply refused its own runs", what );
	CHECK( size == 0 || !memcmp( out, cur, size ), "%s: size %d, the result is not what was saved", what, size );
	free( runs );
	free( out );
	return n;
}

int main( void )
{
	enum { MAX = 5000 };
	unsigned char base[MAX], cur[MAX];
	int size, i, round;

	// nothing changed: no runs at all
	for ( i = 0; i < MAX; i++ ) {
		base[i] = (unsigned char)Rand();
	}
	memcpy( cur, base, MAX );
	CHECK( RoundTrip( base, cur, MAX, "equal" ) == 0, "equal blocks give runs" );
	CHECK( RoundTrip( base, cur, 0, "empty" ) == 0, "empty block" );

	// one byte at every position of small blocks (before, inside and after the 64 byte blocks of the comparison)
	for ( size = 1; size < 200; size++ ) {
		for ( i = 0; i < size; i++ ) {
			int n;

			memcpy( cur, base, size );
			cur[i] ^= 0x55;
			n = RoundTrip( base, cur, size, "one byte" );
			CHECK( n == 9, "one changed byte gives %d bytes of runs (wanted 9)", n );
		}
	}

	// the first and the last byte of a big block
	memcpy( cur, base, MAX );
	cur[0] ^= 1;
	cur[MAX - 1] ^= 1;
	CHECK( RoundTrip( base, cur, MAX, "ends" ) == 18, "the two ends are two runs" );

	// changes closer than DIFF_GAP are one run, farther apart two
	memcpy( cur, base, MAX );
	cur[100] ^= 1;
	cur[100 + DIFF_GAP] ^= 1;
	CHECK( RoundTrip( base, cur, MAX, "near" ) == 8 + DIFF_GAP + 1, "changes %d apart are one run", DIFF_GAP );
	memcpy( cur, base, MAX );
	cur[100] ^= 1;
	cur[100 + DIFF_GAP + 2] ^= 1;
	CHECK( RoundTrip( base, cur, MAX, "far" ) == 18, "changes %d apart are two runs", DIFF_GAP + 2 );

	// everything changed
	for ( i = 0; i < MAX; i++ ) {
		cur[i] = (unsigned char)( base[i] ^ 0xff );
	}
	CHECK( RoundTrip( base, cur, MAX, "all" ) == MAX + 8, "all bytes changed" );

	// random changes of every density
	for ( round = 0; round < 3000; round++ ) {
		const int density = 1 + (int)( Rand() % 300 );
		const int n = (int)( Rand() % ( MAX + 1 ) );

		for ( i = 0; i < n; i++ ) {
			base[i] = (unsigned char)Rand();
		}
		memcpy( cur, base, n );
		for ( i = 0; i < n; i++ ) {
			if ( Rand() % 1000 < (unsigned int)( 1000 / density ) ) {
				cur[i] = (unsigned char)Rand();
			}
		}
		RoundTrip( base, cur, n, "random" );
	}

	// runs that do not fit the block are refused
	{
		unsigned char runs[16], out[10];
		int off = 5, len = 8;

		memcpy( runs, &off, 4 );
		memcpy( runs + 4, &len, 4 );
		memset( runs + 8, 1, 8 );
		CHECK( Diff_Apply( runs, 16, base, out, 10 ) == -1, "a run past the end of the block was accepted" );
		len = 100;
		memcpy( runs + 4, &len, 4 );
		CHECK( Diff_Apply( runs, 16, base, out, 100 ) == -1, "a run longer than the data was accepted" );
		off = -1;
		len = 1;
		memcpy( runs, &off, 4 );
		memcpy( runs + 4, &len, 4 );
		CHECK( Diff_Apply( runs, 9, base, out, 10 ) == -1, "a negative offset was accepted" );
	}

	if ( failures ) {
		printf( "%d FAILED\n", failures );
		return 1;
	}
	printf( "state diff tests passed\n" );
	return 0;
}
