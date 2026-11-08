/*
Tests of plugin_vm.c with a real program (qvm_plugin.c, built by q3lcc/q3asm), in the compiler of the engine and in the
interpreter: what a plugin can and cannot do.
*/

#include "q_shared.h"
#include "qcommon.h"
#include "plugin_vm.h"
#include "stubs.h"
#include <setjmp.h>

static int checks, failures;

#define TRACEF( text ) do { if ( getenv( "TRACE" ) ) printf( "%s\n", text ); } while ( 0 )

#define CHECK( cond, ... ) do { \
	checks++; \
	if ( !( cond ) ) { \
		failures++; \
		printf( "FAIL %s:%d: %s   ", __FILE__, __LINE__, #cond ); \
		printf( __VA_ARGS__ ); \
		printf( "\n" ); \
	} \
} while ( 0 )

static void *image;
static int imageSize;
static int cheatsOn;
static jmp_buf abortJmp;
static const char *lastPrint;

enum { CAP_RESTRICTED = 1, CAP_OTHER = 2 };
static const char *const capNames[] = { "restricted", "other" };
static const char *const exportNames[] = { "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7", "e8", "e9", "e10", "e11", "e12", "e13", "e14", "e15", "e16" };

static ID_INLINE int FloatAsIntT( float f ) { union { float f; int i; } u; u.f = f; return u.i; }
static ID_INLINE float IntAsFloatT( int i ) { union { float f; int i; } u; u.i = i; return u.f; }

// the engine side of the test API: what generated code would look like
static intptr_t Dispatch( pluginVM_t *vm, intptr_t *args )
{
	pluginVMImportCalls++;
	switch ( args[0] ) {
		case 0: {
			const char *s;

			PluginVM_String( vm, args[1], qfalse, &s );
			lastPrint = s;
			return 0;
		}
		case 1:
			return (int)args[1] + (int)args[2];
		case 2:
			return FloatAsIntT( IntAsFloatT( (int)args[1] ) / 2.0f );
		case 3: {
			void *p;

			if ( (uint32_t)args[2] > 1000000000u ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
			}
			PluginVM_Ptr( vm, args[1], (unsigned)args[2], 1, qfalse, &p );
			memset( p, (int)args[3], (size_t)(unsigned)args[2] );
			return 0;
		}
		case 4:
			if ( !( PluginVM_Caps( vm ) & CAP_OTHER ) ) {
				PluginVM_Denied( vm, "Restricted", "other" );
			}
			return 42;
		case 5:
			if ( !cheatsOn ) {
				return 0;
			}
			return 1;
		case 6: {
			void *p;
			const int *v;
			int n = (int)args[2], i, sum = 0;

			if ( (unsigned)n > 1000000u ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
			}
			PluginVM_Ptr( vm, args[1], (unsigned)n * sizeof( int ), 4, qfalse, &p );
			v = (const int *)p;
			for ( i = 0; i < n; i++ ) {
				sum += v[i];
			}
			return sum;
		}
		case 7:			// an engine function that does a lot of work (a megabyte copied, a file written ...)
			PluginVM_Charge( vm, 100000 );
			return 0;
		case 8:			// Com_Error: the engine leaves the call without returning to the plugin
			PluginVM_Abort( "test error" );
			Q_longjmp( abortJmp, 1 );
			return 0;
		default:
			PluginVM_Trap( vm, "unknown engine function" );
			return 0;
	}
}

static pluginVMConfig_t Config( unsigned caps )
{
	pluginVMConfig_t cfg;

	memset( &cfg, 0, sizeof( cfg ) );
	cfg.name = "test";
	cfg.module = "test";
	cfg.dispatch = Dispatch;
	cfg.exportNames = exportNames;
	cfg.numExports = (int)( sizeof( exportNames ) / sizeof( exportNames[0] ) );
	cfg.grantedCaps = caps;
	cfg.capNames = capNames;
	cfg.numCaps = 2;
	cfg.maxDataBytes = 16u << 20;
	return cfg;
}

static pluginVM_t *Load( unsigned caps )
{
	pluginVMConfig_t cfg = Config( caps );
	char err[256];
	pluginVM_t *vm = PluginVM_Load( image, imageSize, &cfg, NULL, err, sizeof( err ) );

	if ( !vm ) {
		printf( "load failed: %s\n", err );
	}
	return vm;
}

// one call with up to two integer arguments
static qboolean Call2( pluginVM_t *vm, int exp, int a, int b, int budget, int *result )
{
	int args[2];

	PluginVM_IOBegin( vm );
	args[0] = a;
	args[1] = b;
	return PluginVM_Call( vm, exp, args, 2, result, budget );
}

static qboolean ErrHas( pluginVM_t *vm, const char *text )
{
	return strstr( PluginVM_Error( vm ), text ) != NULL;
}

// a call that has to stop the plugin, with a reason that contains text
static unsigned stopCaps = CAP_RESTRICTED | CAP_OTHER;

static void MustStop( int exp, int a, int b, int budget, const char *text )
{
	pluginVM_t *vm = Load( stopCaps );
	int r = 0;
	qboolean ok;

	if ( getenv( "TRACE" ) ) {
		printf( "stop %d(%d,%d)\n", exp, a, b );
	}

	if ( !vm ) {
		failures++;
		return;
	}
	ok = Call2( vm, exp, a, b, budget, &r );
	CHECK( !ok, "export %d(%d,%d) should have been stopped, returned %d", exp, a, b, r );
	CHECK( ErrHas( vm, text ), "export %d(%d,%d): error is \"%s\", wanted \"%s\"", exp, a, b, PluginVM_Error( vm ), text );
	// a stopped plugin is not called again, and the game goes on
	CHECK( !Call2( vm, 0, 1, 2, PLUGIN_VM_BUDGET_FRAME, &r ), "a stopped plugin was called" );
	PluginVM_Free( vm );
}

static void MustReturn( int exp, int a, int b, int expected )
{
	pluginVM_t *vm = Load( CAP_RESTRICTED | CAP_OTHER );
	int r = 0;
	qboolean ok;

	if ( getenv( "TRACE" ) ) {
		printf( "return %d(%d,%d)\n", exp, a, b );
	}

	if ( !vm ) {
		failures++;
		return;
	}
	ok = Call2( vm, exp, a, b, PLUGIN_VM_BUDGET_FRAME, &r );
	CHECK( ok, "export %d(%d,%d) was stopped: %s", exp, a, b, PluginVM_Error( vm ) );
	CHECK( r == expected, "export %d(%d,%d) returned %d, wanted %d", exp, a, b, r, expected );
	PluginVM_Free( vm );
}

static void RunSuite( const char *label )
{
	pluginVMConfig_t cfg;
	pluginVM_t *vm, *vm2;
	char err[256];
	int r = 0, args[4], off, i;
	qboolean ok;

	printf( "-- %s\n", label );

	// loading
	cfg = Config( 0 );
	vm = PluginVM_Load( image, imageSize, &cfg, NULL, err, sizeof( err ) );
	CHECK( vm == NULL, "a plugin that needs a capability it did not get was loaded" );
	CHECK( strstr( err, "restricted" ) != NULL, "err: %s", err );

	vm = Load( CAP_RESTRICTED );
	CHECK( vm != NULL, "load" );
	if ( !vm ) {
		return;
	}
	CHECK( strstr( PluginVM_Manifest( vm ), "version=1.0" ) != NULL, "manifest: %s", PluginVM_Manifest( vm ) );
	CHECK( PluginVM_Has( vm, 0 ) && PluginVM_Has( vm, 16 ) && !PluginVM_Has( vm, 17 ), "export mask" );

	// the memory limit
	cfg = Config( CAP_RESTRICTED );
	cfg.maxDataBytes = 64 * 1024;
	vm2 = PluginVM_Load( image, imageSize, &cfg, NULL, err, sizeof( err ) );
	CHECK( vm2 == NULL && strstr( err, "memory" ) != NULL, "memory limit: %s", err );

	// plain calls
	CHECK( Call2( vm, 0, 2, 3, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 5, "add: %d", r );
	CHECK( Call2( vm, 7, 0, 0, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 6, "float: %d", r );
	CHECK( Call2( vm, 13, 0, 0, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 90, "switch: %d", r );
	CHECK( Call2( vm, 15, 100000, 0, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 150000, "long loop: %d", r );
	CHECK( Call2( vm, 14, 8, 0, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 36, "engine reads memory of the plugin: %d", r );
	CHECK( Call2( vm, 3, 10, 3, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 3, "division: %d", r );
	CHECK( Call2( vm, 3, -7, 2, PLUGIN_VM_BUDGET_FRAME, &r ) && r == -3, "division: %d", r );
	CHECK( Call2( vm, 4, 7, 3, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 1, "remainder: %d", r );
	CHECK( Call2( vm, 4, -7, 2, PLUGIN_VM_BUDGET_FRAME, &r ) && r == -1, "remainder: %d", r );
	CHECK( Call2( vm, 0, 1, 1, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 2, "still alive" );

	// strings through the io buffer
	PluginVM_IOBegin( vm );
	args[0] = (int)PluginVM_IOPutString( vm, "hello" );
	CHECK( args[0] != 0, "io put" );
	lastPrint = NULL;
	CHECK( PluginVM_Call( vm, 8, args, 1, &r, PLUGIN_VM_BUDGET_FRAME ) && lastPrint && !strcmp( lastPrint, "hello" ), "string" );

	// the engine writes into memory the plugin names, inside it
	PluginVM_IOBegin( vm );
	off = (int)PluginVM_IOPut( vm, "................", 16, 1 );
	CHECK( Call2( vm, 6, off, 16, PLUGIN_VM_BUDGET_FRAME, &r ), "fill inside" );
	for ( i = 0; i < 1; i++ ) {
		PluginVM_Free( vm );
	}

	// a plugin that does something wrong is stopped
	MustStop( 1, 0, 0, 100000, "budget" );					// a loop that never ends
	MustStop( 15, 100000, 0, 1000, "budget" );				// a loop that is too long for its budget
	MustStop( 17, 0, 0, 100000, "budget" );					// a loop made of a jump to an address in a variable
	MustStop( 18, 0, 0, 100000, "bad location" );					// a call into the middle of a function
	MustStop( 2, 0, 0, PLUGIN_VM_BUDGET_FRAME, "stack" );	// a function that calls itself
	MustStop( 3, 10, 0, PLUGIN_VM_BUDGET_FRAME, "division" );
	// a divisor of -1 is fine, also for INT_MIN, which the processor would fault on
	MustReturn( 3, 12, -1, -12 );
	MustReturn( 3, (int)0x80000000, -1, (int)0x80000000 );
	MustReturn( 4, 12, -1, 0 );
	MustReturn( 4, (int)0x80000000, -1, 0 );
	MustStop( 4, 10, 0, PLUGIN_VM_BUDGET_FRAME, "division" );
	MustStop( 11, 0x12345, 0, PLUGIN_VM_BUDGET_FRAME, "VM" );	// a jump to an address that is not code
	MustStop( 11, 0x7FFFFFF0, 0, PLUGIN_VM_BUDGET_FRAME, "VM" );
	MustStop( 12, 0, 0, PLUGIN_VM_BUDGET_FRAME, "unknown engine function" );
	stopCaps = CAP_RESTRICTED;
	MustStop( 9, 0, 0, PLUGIN_VM_BUDGET_FRAME, "without the capability" );
	stopCaps = CAP_RESTRICTED | CAP_OTHER;
	MustStop( 6, 0x1FFF0, 4096, PLUGIN_VM_BUDGET_FRAME, "outside the memory" );		// the engine would write past the end
	MustStop( 6, (int)0xFFFFFFF0, 16, PLUGIN_VM_BUDGET_FRAME, "outside the memory" );
	MustStop( 6, 0, 16, PLUGIN_VM_BUDGET_FRAME, "null pointer" );
	MustStop( 6, 0x400, 2000000000, PLUGIN_VM_BUDGET_FRAME, "bad size" );
	MustStop( 14, 100000, 0, PLUGIN_VM_BUDGET_FRAME, "outside the memory" );
	MustStop( 8, 0x7FFFFFF0, 0, PLUGIN_VM_BUDGET_FRAME, "outside the memory" );

	// the budget counts the steps of the plugin, not the work of the engine: that has an allowance of its own
	MustReturn( 19, 40, 0, 40 );
	MustStop( 19, 100000, 0, PLUGIN_VM_BUDGET_FRAME, "allowance" );

	// an error that leaves the call through Com_Error: the plugin is stopped, nothing points into the stack that is gone
	vm = Load( CAP_RESTRICTED | CAP_OTHER );
	vm2 = Load( CAP_RESTRICTED | CAP_OTHER );
	if ( vm && vm2 ) {
		volatile int aborted = 0;

		if ( Q_setjmp( abortJmp ) == 0 ) {
			Call2( vm, 20, 0, 0, PLUGIN_VM_BUDGET_FRAME, &r );
		} else {
			aborted = 1;
		}
		CHECK( aborted, "the error did not leave the call" );
		CHECK( ErrHas( vm, "engine stopped" ) && ErrHas( vm, "test error" ), "error: %s", PluginVM_Error( vm ) );
		CHECK( !Call2( vm, 0, 1, 2, PLUGIN_VM_BUDGET_FRAME, &r ), "a plugin that was left by an error was called" );
		CHECK( Call2( vm2, 0, 20, 22, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 42, "the other plugin" );
		PluginVM_Free( vm );
		PluginVM_Free( vm2 );
	}

	// stores and block copies to any address stay in the memory of the plugin
	MustReturn( 5, 0x7FFFFFF0, 0, 0x12345678 );
	MustReturn( 5, (int)0xFFFFFFF8, 0, 0x12345678 );
	MustReturn( 5, -4, 0, 0x12345678 );
	MustStop( 16, 0x7FFFFFF0, 0x7FFFFFF8, PLUGIN_VM_BUDGET_FRAME, "copy memory" );		// a copy that wraps around the end of the memory
	MustStop( 16, (int)0xFFFFF000, 0x1000, PLUGIN_VM_BUDGET_FRAME, "copy memory" );
	MustStop( 16, 0x1FF00, 0x1000, PLUGIN_VM_BUDGET_FRAME, "copy memory" );			// 4000 bytes from near the end: past it
	MustReturn( 16, 0x2000, 0x4000, 1 );											// the same copy inside the memory

	// cheats
	cheatsOn = 0;
	MustReturn( 10, 0, 0, 0 );
	cheatsOn = 1;
	MustReturn( 10, 0, 0, 1 );
	cheatsOn = 0;

	// one plugin stopped does not touch another
	TRACEF( "two plugins: load" );
	vm = Load( CAP_RESTRICTED | CAP_OTHER );
	vm2 = Load( CAP_RESTRICTED | CAP_OTHER );
	TRACEF( "two plugins: loaded" );
	CHECK( vm && vm2, "two plugins" );
	if ( vm && vm2 ) {
		CHECK( !Call2( vm, 1, 0, 0, 10000, &r ), "loop" );
		TRACEF( "two plugins: stopped the first" );
		CHECK( Call2( vm2, 0, 20, 22, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 42, "the other plugin" );
		PluginVM_Free( vm );
		PluginVM_Free( vm2 );
	}

	// stats
	vm = Load( CAP_RESTRICTED );
	if ( vm ) {
		pluginVMStats_t st;

		Call2( vm, 0, 1, 2, PLUGIN_VM_BUDGET_FRAME, &r );
		Call2( vm, 0, 1, 2, PLUGIN_VM_BUDGET_FRAME, &r );
		PluginVM_GetStats( vm, 0, &st );
		CHECK( st.calls == 2 && st.importCalls == 2, "stats: %u calls, %u imports", st.calls, st.importCalls );
		CHECK( st.maxSteps > 0 && st.maxSteps < 1000 && st.maxWork == 0, "stats: %d steps, %d work for a small call", st.maxSteps, st.maxWork );
		PluginVM_Free( vm );
	}
	// the peak of steps and of engine work (40 calls of 100000 units each)
	vm = Load( CAP_RESTRICTED | CAP_OTHER );
	if ( vm ) {
		pluginVMStats_t st;

		CHECK( Call2( vm, 19, 40, 0, PLUGIN_VM_BUDGET_FRAME, &r ) && r == 40, "slow calls" );
		Call2( vm, 19, 10, 0, PLUGIN_VM_BUDGET_FRAME, &r );
		PluginVM_GetStats( vm, 19, &st );
		CHECK( st.calls == 2 && st.maxWork == 4000000, "stats: %u calls, peak of work %d, wanted 4000000", st.calls, st.maxWork );
		CHECK( st.maxSteps > 0 && st.maxSteps < 1000, "stats: peak of steps %d", st.maxSteps );
		PluginVM_Free( vm );
	}
	ok = qtrue;
	(void)ok;
}

// garbage and damaged programs must not be loaded, or must run harmlessly
static void Fuzz( void )
{
	pluginVMConfig_t cfg = Config( CAP_RESTRICTED | CAP_OTHER );
	unsigned seed = 12345;
	char err[256];
	int n, loaded = 0, ran = 0;
	byte *copy = (byte *)malloc( imageSize );

#define RND() ( seed = seed * 1103515245u + 12345u, ( seed >> 8 ) )
	// truncations
	for ( n = 0; n < imageSize; n += 7 ) {
		memcpy( copy, image, imageSize );
		CHECK( PluginVM_Load( copy, n, &cfg, NULL, err, sizeof( err ) ) == NULL || n > imageSize - 4, "truncated to %d bytes was loaded", n );
	}
	// changed bytes anywhere
	const int fuzzCount = getenv( "FUZZ_N" ) ? atoi( getenv( "FUZZ_N" ) ) : 3000;

	for ( n = 0; n < fuzzCount; n++ ) {
		pluginVM_t *vm;
		int k, changes = 1 + RND() % 4;

		memcpy( copy, image, imageSize );
		for ( k = 0; k < changes; k++ ) {
			copy[ RND() % imageSize ] = (byte)RND();
		}
		vm = PluginVM_Load( copy, imageSize, &cfg, NULL, err, sizeof( err ) );
		if ( vm ) {
			int r, e;

			loaded++;
			for ( e = 0; e < 17 && !PluginVM_Error( vm )[0]; e++ ) {
				int a = (int)RND(), b = (int)RND();

				Call2( vm, e, a, b, 20000, &r );
				ran++;
			}
			PluginVM_Free( vm );
		}
	}
	free( copy );
	printf( "   fuzz: %d changed programs loaded, %d calls, nothing escaped\n", loaded, ran );
}

int main( int argc, char **argv )
{
	FILE *f;

	setvbuf( stdout, NULL, _IONBF, 0 );
	if ( argc < 2 ) {
		printf( "usage: harness plugin.qvm\n" );
		return 2;
	}
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

	pluginVMInterpret = 0;
	RunSuite( "compiled" );
	Fuzz();
	pluginVMInterpret = 1;
	RunSuite( "interpreter" );
	Fuzz();

	printf( "%d checks, %d failed\n", checks, failures );
	return failures ? 1 : 0;
}
