/* HTTP requests with a JSON body for server plugins (see odf_http.h). odf-build adds this file to a program that includes odf_http.h. */

#include <stdio.h>
#include <string.h>

#include "odfs.h"
#include "odf_http.h"

#define HTTP_MAX_IN_FLIGHT	8
#define HTTP_MAX_RESPONSE	65536		/* a bigger answer is an error */
#define HTTP_TIMEOUT_MS		15000

typedef struct {
	int				id;				/* 0: the slot is free */
	httpCallback_t	callback;
	void			*user;
} httpRequest_t;

static httpRequest_t http_requests[HTTP_MAX_IN_FLIGHT];
static int http_inFlight;
static httpLog_t http_log;
static char http_response[HTTP_MAX_RESPONSE + 1];

void Http_Init( httpLog_t log )
{
	http_log = log;
	memset( http_requests, 0, sizeof( http_requests ) );
	http_inFlight = 0;
}

void Http_Shutdown( void )
{
	httpResult_t result;
	int i;

	result.status = 0;
	result.body = "";
	result.bodyLen = 0;
	result.error = "shutdown";
	for ( i = 0; i < HTTP_MAX_IN_FLIGHT; i++ ) {
		if ( http_requests[i].id ) {
			httpCallback_t callback = http_requests[i].callback;
			void *user = http_requests[i].user;

			http_requests[i].id = 0;
			http_inFlight--;
			callback( &result, user );
		}
	}
}

/* the engine calls this when a request is done; slot is the userData the request was started with */
static void Http_Finished( int id, int status, int bodyLen, int slot )
{
	httpResult_t result;
	httpCallback_t callback;
	void *user;
	int got = 0;

	if ( slot < 0 || slot >= HTTP_MAX_IN_FLIGHT || http_requests[slot].id != id ) {
		return;		/* a request that was given up */
	}
	callback = http_requests[slot].callback;
	user = http_requests[slot].user;
	http_requests[slot].id = 0;
	http_inFlight--;

	result.error = status ? NULL : "no answer";
	if ( bodyLen > HTTP_MAX_RESPONSE ) {
		status = 0;
		result.error = "the answer is too large";
		bodyLen = 0;
	}
	while ( got < bodyLen ) {
		int n = HttpBody( id, got, http_response + got, bodyLen - got );

		if ( n <= 0 ) {
			break;
		}
		got += n;
	}
	http_response[got] = '\0';
	result.status = status;
	result.body = http_response;
	result.bodyLen = got;
	callback( &result, user );
}

static int Http_Start( int method, const char *url, const char *headers, const char *body, httpCallback_t callback, void *user )
{
	int i, id;

	for ( i = 0; i < HTTP_MAX_IN_FLIGHT && http_requests[i].id; i++ ) {
	}
	if ( i == HTTP_MAX_IN_FLIGHT || !callback ) {
		return 0;
	}
	id = HttpRequest( method, url, headers, body, body ? (int)strlen( body ) : 0, HTTP_TIMEOUT_MS, Http_Finished, i );
	if ( id <= 0 ) {
		if ( http_log ) {
			http_log( "the engine did not start a request (is the host of the address listed after http= in sv_plugins?)" );
		}
		return 0;
	}
	http_requests[i].id = id;
	http_requests[i].callback = callback;
	http_requests[i].user = user;
	http_inFlight++;
	return 1;
}

int Http_PostJSON( const char *url, const char *bearer, const char *json, httpCallback_t callback, void *user )
{
	char headers[400];

	if ( bearer && strlen( bearer ) > 300 ) {
		return 0;
	}
	if ( bearer ) {
		snprintf( headers, sizeof( headers ), "Content-Type: application/json\nAccept: application/json\nAuthorization: Bearer %s", bearer );
	} else {
		snprintf( headers, sizeof( headers ), "Content-Type: application/json\nAccept: application/json" );
	}
	return Http_Start( ODF_HTTP_POST, url, headers, json, callback, user );
}

int Http_Get( const char *url, httpCallback_t callback, void *user )
{
	return Http_Start( ODF_HTTP_GET, url, "Accept: application/json", NULL, callback, user );
}

int Http_InFlight( void )
{
	return http_inFlight;
}
