/*
Runs the QVM program sdk_test.c, which tests the library of the SDK (code/plugin_api/sdk/odf_lib.c).
*/

#include "q_shared.h"
#include "qcommon.h"
#include "plugin_vm.h"
#include "stubs.h"
#include "odft_types.h"

static int reports;

static qboolean NoCheats( void )
{
	return qfalse;
}
#define ODF_CheatsAllowed NoCheats
#include "sdk_api.inc"
#include "plugin_common_imp.h"

static void Imp_Report( const char *text )
{
	reports++;
	printf( "FAIL %s\n", text );
}

static void Imp_HostFormatFloat( char *out, int size, const char *format, float value )
{
	snprintf( out, size, format, (double)value );
}

static void Imp_HostFormatInt( char *out, int size, const char *format, int value )
{
	snprintf( out, size, format, value );
}

static float Imp_HostStrtof( const char *text )
{
	return strtof( text, NULL );
}

int main( int argc, char **argv )
{
	pluginVMConfig_t cfg;
	char err[256];
	pluginVM_t *vm;
	void *image;
	int imageSize, mode, result, failed = 0;
	FILE *f;

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
		memset( &cfg, 0, sizeof( cfg ) );
		cfg.name = "sdktest";
		cfg.module = "odft";
		cfg.dispatch = Odft_Dispatch;
		cfg.exportNames = odft_exportNames;
		cfg.numExports = ODFT_NUM_EXPORTS;
		cfg.capNames = odft_capNames;
		cfg.numCaps = ODFT_NUM_CAPS;
		cfg.maxDataBytes = 16u << 20;
		vm = PluginVM_Load( image, imageSize, &cfg, NULL, err, sizeof( err ) );
		if ( !vm ) {
			printf( "load failed: %s\n", err );
			return 1;
		}
		reports = 0;
		if ( !ODFT_Call_OnRun( vm, 0, &result ) ) {
			printf( "the program was stopped: %s\n", PluginVM_Error( vm ) );
			failed++;
		} else {
			printf( "%d checks, %d reported\n", result, reports );
		}
		failed += reports;
		PluginVM_Free( vm );
	}
	return failed ? 1 : 0;
}
