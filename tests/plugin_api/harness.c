/*
Tests of the dispatcher gen_api.py generates (test.api): argument checks, capabilities, the cheat rule, 64 bit numbers and floats.
*/

#include "q_shared.h"
#include "qcommon.h"
#include "plugin_vm.h"
#include "stubs.h"
#include "odft_types.h"

static int checks, failures;

#define CHECK( cond, ... ) do { \
	checks++; \
	if ( !( cond ) ) { \
		failures++; \
		printf( "FAIL %s:%d: %s   ", __FILE__, __LINE__, #cond ); \
		printf( __VA_ARGS__ ); \
		printf( "\n" ); \
	} \
} while ( 0 )

static int cheatsOn, nothingCalls;

static qboolean CheatsAllowedT( void )
{
	return cheatsOn != 0;
}
#define ODF_CheatsAllowed CheatsAllowedT
#include "test_api.inc"
#include "plugin_common_imp.h"

static int Imp_Plain( int x ) { return x + 1; }
static int Imp_NeedsAlpha( int x ) { return x + 2; }
static int Imp_CheatOnly( int x ) { return x * 10; }
static int Imp_CheatAndBeta( int x ) { return x * 100; }
static int Imp_FillBuf( char *buf, int n ) { if ( buf && n > 0 ) memset( buf, 'A', n ); return n; }
static int Imp_ReadBuf( const char *buf, int n ) { int i, sum = 0; for ( i = 0; i < n; i++ ) sum += (unsigned char)buf[i]; return sum; }
static int Imp_TwoStrings( const char *a, const char *b ) { return (int)strlen( a ) * 100 + ( b ? (int)strlen( b ) : 99 ); }
static float Imp_HalfOf( float a, float b ) { return ( a + b ) / 2; }
static int Imp_Wide( int64_t id, int tag ) { return (int)( id >> 32 ) * 1000 + (int)( id & 0xFFFF ) + tag; }
static int Imp_Vec( const float *v, float *w ) { w[0] = v[0] * 2; w[1] = v[1] * 2; w[2] = v[2] * 2; return 3; }
static void Imp_Nothing( void ) { nothingCalls++; }

static void *image;
static int imageSize;

static pluginVM_t *Load( unsigned caps )
{
	pluginVMConfig_t cfg;
	char err[256];
	pluginVM_t *vm;

	memset( &cfg, 0, sizeof( cfg ) );
	cfg.name = "test";
	cfg.module = "odft";
	cfg.dispatch = Odft_Dispatch;
	cfg.exportNames = odft_exportNames;
	cfg.numExports = ODFT_NUM_EXPORTS;
	cfg.grantedCaps = caps;
	cfg.capNames = odft_capNames;
	cfg.numCaps = ODFT_NUM_CAPS;
	cfg.maxDataBytes = 16u << 20;
	vm = PluginVM_Load( image, imageSize, &cfg, NULL, err, sizeof( err ) );
	if ( !vm ) {
		printf( "load failed: %s\n", err );
	}
	return vm;
}

// runs scenario n of the plugin; returns qfalse if the plugin was stopped (and says why in why)
static qboolean Run( unsigned caps, int n, int *result, const char **why )
{
	pluginVM_t *vm = Load( caps );
	static char reason[256];
	qboolean ok;

	*result = 0;
	*why = "";
	if ( !vm ) {
		failures++;
		return qfalse;
	}
	ok = ODFT_Call_OnPing( vm, n, result );
	Q_strncpyz( reason, PluginVM_Error( vm ), sizeof( reason ) );
	*why = reason;
	PluginVM_Free( vm );
	return ok;
}

static void Returns( unsigned caps, int n, int expected )
{
	int r;
	const char *why;
	qboolean ok = Run( caps, n, &r, &why );

	CHECK( ok && r == expected, "scenario %d (caps %u cheats %d): ok %d, result %d, wanted %d (%s)", n, caps, cheatsOn, ok, r, expected, why );
}

static void Stops( unsigned caps, int n, const char *text )
{
	int r;
	const char *why;
	qboolean ok = Run( caps, n, &r, &why );

	CHECK( !ok && strstr( why, text ) != NULL, "scenario %d (caps %u): ok %d, \"%s\", wanted a stop with \"%s\"", n, caps, ok, why, text );
}

int main( int argc, char **argv )
{
	enum { ALPHA = 1, BETA = 2 };
	FILE *f;
	pluginVM_t *vm;
	int r, mode;

	setvbuf( stdout, NULL, _IONBF, 0 );
	f = fopen( argv[1], "rb" );
	if ( !f ) {
		printf( "cannot open %s\n", argv[1] );
		return 2;
	}
	fseek( f, 0, SEEK_END );
	imageSize = (int)ftell( f );
	fseek( f, 0, SEEK_SET );
	image = malloc( imageSize );
	if ( fread( image, 1, imageSize, f ) != (size_t)imageSize ) {
		return 2;
	}
	fclose( f );
	Stubs_Init();

	for ( mode = 0; mode < 2; mode++ ) {
		pluginVMInterpret = mode;
		printf( "-- %s\n", mode ? "interpreter" : "compiled" );

		cheatsOn = 0;
		Returns( 0, 0, 6 );										// a function without rules
		Stops( 0, 1, "without the capability alpha" );			// a capability the plugin does not have
		Returns( ALPHA, 1, 8 );
		Returns( 0, 2, 0 );										// cheats off: it does nothing and returns 0
		Stops( 0, 3, "without the capability beta" );			// the capability is checked first
		Stops( ALPHA, 3, "without the capability beta" );
		Returns( BETA, 3, 0 );
		cheatsOn = 1;
		Returns( 0, 2, 70 );									// cheats on: it works
		Stops( 0, 3, "without the capability beta" );			// still needs its capability
		Returns( BETA, 3, 800 );
		cheatsOn = 0;

		Returns( 0, 4, 16 * 1000 + 'A' );						// a buffer the engine fills
		Stops( 0, 5, "outside the memory" );
		Returns( 0, 6, 0 );										// no bytes: the pointer is not used
		Stops( 0, 7, "bad size" );
		Stops( 0, 8, "null pointer" );
		Returns( 0, 9, 294 );									// a buffer the engine reads
		Returns( 0, 10, 599 );									// a string that may be NULL
		Stops( 0, 11, "null string" );							// one that may not
		Returns( 0, 12, 40 );									// floats in and out
		Returns( 0, 13, 2006 );									// a 64 bit number as two halves
		Returns( 0, 14, 3012 );									// pointers to arrays of floats
		Stops( 0, 15, "misaligned" );
		nothingCalls = 0;
		Returns( 0, 16, 77 );
		CHECK( nothingCalls == 1, "a function without arguments: %d calls", nothingCalls );
		Returns( 0, 17, 40 );									// math of the engine
		Returns( 0, 18, 7 );									// bulk memory of the engine
		Stops( 0, 19, "outside the memory" );
		Returns( 0, 21, 10 );

		// exports with a string and a struct
		vm = Load( 0 );
		if ( vm ) {
			odftPair_t pair;

			pair.a = 40;
			pair.b = 2.0f;
			CHECK( ODFT_Call_OnSay( vm, "hello" ), "say" );
			CHECK( ODFT_Call_OnWithStruct( vm, &pair, &r ) && r == 42, "with_struct: %d", r );
			CHECK( ODFT_Call_OnPing( vm, 20, &r ) && r == 542, "the plugin kept the string and the sum: %d", r );
			PluginVM_Free( vm );
		}
	}

	printf( "%d checks, %d failed\n", checks, failures );
	return failures ? 1 : 0;
}
