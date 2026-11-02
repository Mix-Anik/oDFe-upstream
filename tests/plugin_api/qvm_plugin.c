/*
A QVM program that calls every kind of engine function of test.api, the way the scenarios of harness.c say (ping( scenario )).
*/

#define ODF_PLUGIN_VERSION "1"
#define EXPORTS( X ) X(OnPing) X(OnSay) X(OnWithStruct)
#include "odft_entry.h"

static char buf[64];
static char saved[32];
static int pairSum;

int OnPing( int scenario )
{
	float v[3], w[3];
	int i;

	v[0] = 1; v[1] = 2; v[2] = 3;
	switch ( scenario ) {
		case 0:  return Plain( 5 );
		case 1:  return NeedsAlpha( 6 );
		case 2:  return CheatOnly( 7 );
		case 3:  return CheatAndBeta( 8 );
		case 4:
			MemSet( buf, 0, sizeof( buf ) );
			return FillBuf( buf, 16 ) * 1000 + buf[0];
		case 5:  return FillBuf( (char *)0x7ffffff0, 16 );
		case 6:  return FillBuf( buf, 0 );
		case 7:  return FillBuf( buf, 5000000 );
		case 8:  return FillBuf( NULL, 4 );
		case 9:  return ReadBuf( "abc", 3 );
		case 10: return TwoStrings( "hello", NULL );
		case 11: return TwoStrings( NULL, "x" );
		case 12: return (int)( HalfOf( 3.0f, 5.0f ) * 10.0f );
		case 13: return Wide( 5, 2, 1 );
		case 14: return Vec( v, w ) * 1000 + (int)( w[0] + w[1] + w[2] );
		case 15: return Vec( v, (float *)2 );
		case 16: Nothing(); return 77;
		case 17: return (int)( Sqrt( 16.0f ) * 10.0f );
		case 18:
			MemSet( buf, 7, 8 );
			MemCpy( buf + 8, buf, 8 );
			return buf[15];
		case 19: MemCpy( (char *)0x7ffffff0, buf, 8 ); return 1;
		case 20:
			for ( i = 0; i < 31 && saved[i]; i++ ) {
			}
			return i * 100 + pairSum;
		case 21: return (int)( Sin( 0.0f ) * 10.0f + Cos( 0.0f ) * 10.0f + Atan2( 0.0f, 1.0f ) * 10.0f );
	}
	return -1;
}

void OnSay( const char *text )
{
	int i;

	for ( i = 0; i < 31 && text[i]; i++ ) {
		saved[i] = text[i];
	}
	saved[i] = 0;
}

int OnWithStruct( const odftPair_t *p )
{
	pairSum = p->a + (int)p->b;
	return pairSum;
}
