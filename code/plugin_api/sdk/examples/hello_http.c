/* A server plugin that fetches a JSON document when it starts and prints one field of it. The libraries odf_http and odf_json are
   added to the program by odf-build because the plugin includes their headers.
   Build:  odf-build --server -o hello_http.qvm hello_http.c
   The operator allows the host:  +set sv_plugins "hello_http:http=api.example.com" */
#define ODF_PLUGIN_VERSION     "1.0"
#define ODF_PLUGIN_CAPS        "http"
#define ODF_PLUGIN_DESCRIPTION "fetches a JSON document"
#define EXPORTS( X ) X(OnInit) X(OnShutdown)
#include "odfs_entry.h"
#include "odf_http.h"
#include "odf_json.h"

#include <stdio.h>

static void Log( const char *text )
{
	Print( text );
}

/* called in a later frame, when the answer is there */
static void Answer( const httpResult_t *result, void *user )
{
	char name[64], text[128];

	if ( result->status != 200 ) {
		snprintf( text, sizeof( text ), "request failed: status %d %s", result->status, result->error ? result->error : "" );
	} else if ( Json_String( Json_Find( result->body, "server.name" ), name, sizeof( name ) ) < 0 ) {
		snprintf( text, sizeof( text ), "no server.name in the answer" );
	} else {
		snprintf( text, sizeof( text ), "the server is called %s", name );
	}
	Print( text );
}

int OnInit( void )
{
	Http_Init( Log );
	Http_Get( "https://api.example.com/info", Answer, NULL );
	return 0;
}

void OnShutdown( void )
{
	Http_Shutdown();
}
