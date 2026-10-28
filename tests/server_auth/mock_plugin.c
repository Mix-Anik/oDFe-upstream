/*
A stand-in for the records plugin, controlled by the test program through the exported Mock_* functions.
It answers logins like the real plugin would (synchronously or on a later Frame) and can crash on request.
*/

#include <stdio.h>
#include <string.h>

#include "odf_plugin_api.h"

static const odfPluginImport_t *eng;

#define MAX_LOG		32
static char logName[MAX_LOG][32];
static char logData[MAX_LOG][1100];
static int logCount;

static int mode;						// 0 answer in OnEvent, 1 answer in the next Frame, 2 never answer
static int resultCode = ODF_AUTHRES_OK;
static int64_t resultID = 42;
static char resultName[40] = "mix";
static char resultReason[40];
static int pendingClient = -1, pendingSerial;
static int frames;
static volatile int *volatile nullPtr;

ODF_EXPORT void Mock_Setup( int newMode, int result, long long id, const char *name, const char *reason )
{
	mode = newMode;
	resultCode = result;
	resultID = id;
	snprintf( resultName, sizeof( resultName ), "%s", name ? name : "" );
	snprintf( resultReason, sizeof( resultReason ), "%s", reason ? reason : "" );
}

ODF_EXPORT int Mock_EventCount( void ) { return logCount; }
ODF_EXPORT const char *Mock_EventName( int i ) { return i >= 0 && i < logCount ? logName[i] : ""; }
ODF_EXPORT const char *Mock_EventData( int i ) { return i >= 0 && i < logCount ? logData[i] : ""; }
ODF_EXPORT int Mock_Frames( void ) { return frames; }
ODF_EXPORT int Mock_Client( int clientNum, odfClientInfo_t *out ) { return eng->GetClientInfo( clientNum, out ); }
ODF_EXPORT void Mock_ClearLog( void ) { logCount = 0; }

static void Answer( int client, int serial )
{
	eng->AuthResult( client, serial, resultCode, resultID, resultName, resultReason[0] ? resultReason : NULL );
}

static void Info( const char *data, const char *key, char *out, int size )
{
	// tiny info string reader, enough for the test
	char pattern[48];
	const char *p;

	out[0] = '\0';
	snprintf( pattern, sizeof( pattern ), "\\%s\\", key );
	p = strstr( data, pattern );
	if ( p ) {
		int i = 0;

		p += strlen( pattern );
		while ( *p && *p != '\\' && i < size - 1 ) {
			out[i++] = *p++;
		}
		out[i] = '\0';
	}
}

static void OnEvent( const char *name, const char *data )
{
	if ( logCount < MAX_LOG ) {
		snprintf( logName[logCount], sizeof( logName[0] ), "%s", name );
		snprintf( logData[logCount], sizeof( logData[0] ), "%s", data );
		logCount++;
	}

	if ( !strcmp( name, "crash" ) ) {
		*nullPtr = 1;
	} else if ( !strcmp( name, "auth_response" ) ) {
		char client[16], serial[16];

		Info( data, "client", client, sizeof( client ) );
		Info( data, "serial", serial, sizeof( serial ) );
		if ( mode == 0 ) {
			Answer( atoi( client ), atoi( serial ) );
		} else if ( mode == 1 ) {
			pendingClient = atoi( client );
			pendingSerial = atoi( serial );
		}
	}
}

static void Frame( int serverTimeMs )
{
	(void)serverTimeMs;
	frames++;
	if ( pendingClient >= 0 ) {
		int c = pendingClient;

		pendingClient = -1;
		Answer( c, pendingSerial );
	}
}

static int Init( void )
{
	eng->Print( "mock plugin ready" );
	return 0;
}

static const odfPluginExport_t exports = {
	ODF_PLUGIN_API_VERSION, "mock", "mock 1", ODF_PLUGIN_FLAG_AUTH, Init, 0, Frame, OnEvent
};

ODF_EXPORT const odfPluginExport_t *ODF_PluginEntry( const odfPluginImport_t *imp )
{
	eng = imp;
	return &exports;
}
