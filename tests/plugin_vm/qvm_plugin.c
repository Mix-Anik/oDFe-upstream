/*
A QVM program for the tests of plugin_vm.c. Each command is something a plugin (a hostile one, mostly) could do.
Built with q3lcc and q3asm, see run.sh.
*/

// the engine functions, see syscalls.asm and the dispatcher in harness.c
extern void test_Print( const char *text );
extern int test_Add( int a, int b );
extern float test_Half( float f );
extern void test_Fill( void *dst, int count, int value );
extern int test_Restricted( void );
extern int test_Cheat( void );
extern int test_Sum( const int *values, int count );
extern void test_Slow( void );
extern void test_Abort( void );

extern int Hostile_JumpLoop( void );
extern int Hostile_CallMid( void );

static char io[4096];
static int counter;
static char big[8000];

static int Recurse( int n );
static int Switchy( int n );

// vmMain has to be the first function of the program
int vmMain( int command, int ioAddress, int unused1, int unused2 )
{
	int *a = (int *)ioAddress;
	int i, sum;

	switch ( command ) {
		case 1000:		// where the io buffer is
			return (int)io;
		case 1001:		// manifest
			return (int)"module=test\nversion=1.0\napi=1\ncaps=restricted";
		case 1002:		// exports: all of the ones the test has
			return 0x7FFFF;

		case 0:			// a plain call with a result
			return test_Add( a[0], a[1] );
		case 1:			// never ends: a loop
			for ( ;; ) {
				counter++;
			}
		case 2:			// never ends: a call of itself
			return Recurse( 0 );
		case 3:			// integer division
			return a[0] / a[1];
		case 4:			// integer remainder
			return a[0] % a[1];
		case 5:			// a store to any address
			*(int *)a[0] = 0x12345678;
			return *(int *)a[0];
		case 6:			// the engine writes into memory the plugin names: a count that does not fit
			test_Fill( (void *)a[0], a[1], 7 );
			return 0;
		case 7:			// floating point in and out
			return (int)( test_Half( 3.0f ) * 4.0f );
		case 8:			// a string the engine reads, address and bytes from the plugin
			test_Print( (const char *)a[0] );
			return 0;
		case 9:			// needs a capability
			return test_Restricted();
		case 10:		// cheat only
			return test_Cheat();
		case 11:		// jump to an address the plugin makes up
			return ( (int (*)( int ))a[0] )( 1 );
		case 12:		// a call the engine does not have
			return ( (int (*)( int ))-200 )( 1 );
		case 13:		// switch: many jump table targets
			sum = 0;
			for ( i = 0; i < 8; i++ ) {
				sum += Switchy( i );
			}
			return sum;
		case 14:		// a pointer to values in its own memory, engine reads count of them
			for ( i = 0; i < 8; i++ ) {
				big[i * 4] = (char)( i + 1 );
			}
			return test_Sum( (const int *)big, a[0] );
		case 15:		// long loop that still finishes
			sum = 0;
			for ( i = 0; i < a[0]; i++ ) {
				sum += i & 3;
			}
			return sum;
		case 16:		// copies a big structure over itself (block copy) from addresses the plugin makes up
			{
				struct { int v[1000]; } *dst = (void *)a[0], *src = (void *)a[1];
				*dst = *src;
			}
			return 1;
		case 17:		// a loop with a jump to an address in a variable (hand written code)
			return Hostile_JumpLoop();
		case 18:		// a call of an address in the middle of a function (hand written code)
			return Hostile_CallMid();
		case 19:		// engine functions that take long, as often as the plugin likes (the loop itself is cheap for the budget)
			for ( i = 0; i < a[0]; i++ ) {
				test_Slow();
			}
			return a[0];
		case 20:		// an engine function that leaves the call with an error (Com_Error)
			test_Abort();
			return 0;
		default:
			return -12345;
	}
}

static int Recurse( int n )
{
	char pad[64];

	pad[0] = (char)n;
	return Recurse( n + 1 ) + pad[0];
}

static int Switchy( int n )
{
	switch ( n ) {
		case 0: return 10;
		case 1: return 11;
		case 2: return 12;
		case 3: return 13;
		case 4: return 14;
		case 5: return 15;
		case 6: return 16;
		default: return -1;
	}
}
