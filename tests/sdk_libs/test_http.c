/* Native test of odf_http.c with a made-up engine: what it asks of HttpRequest, and what the callbacks get. */

#include <stdio.h>
#include <string.h>

#include "odfs.h"
#include "odf_http.h"

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

/* ---- the engine ---- */

#define MAX_FAKE	16

static struct {
	int					id;
	int					method;
	char				url[200], headers[400], body[200];
	odfsHttpHandler_t	handler;
	int					userData;
} fake[MAX_FAKE];
static int numFake, refuse;
static const char *answer = "";

int HttpRequest( int method, const char *url, const char *headers, const void *body, int bodyLen, int timeoutMs, odfsHttpHandler_t handler, int userData )
{
	if ( refuse || numFake >= MAX_FAKE || !handler ) {
		return 0;
	}
	fake[numFake].id = 100 + numFake;
	fake[numFake].method = method;
	snprintf( fake[numFake].url, sizeof( fake[0].url ), "%s", url );
	snprintf( fake[numFake].headers, sizeof( fake[0].headers ), "%s", headers ? headers : "" );
	snprintf( fake[numFake].body, sizeof( fake[0].body ), "%.*s", bodyLen, body ? (const char *)body : "" );
	fake[numFake].handler = handler;
	fake[numFake].userData = userData;
	(void)timeoutMs;
	return fake[numFake++].id;
}

int HttpBody( int id, int offset, void *out, int outSize )
{
	int len = (int)strlen( answer ) - offset;

	(void)id;
	if ( len <= 0 ) {
		return 0;
	}
	if ( len > outSize ) {
		len = outSize;
	}
	memcpy( out, answer + offset, len );
	return len;
}

/* the engine says request n is done */
static void Finish( int n, int status, const char *text )
{
	answer = text;
	fake[n].handler( fake[n].id, status, (int)strlen( text ), fake[n].userData );
}

/* ---- the plugin ---- */

static int calls, lastStatus, lastLen;
static char lastBody[70000], lastError[64];
static void *lastUser;
static char logged[200];

static void Callback( const httpResult_t *r, void *user )
{
	calls++;
	lastStatus = r->status;
	lastLen = r->bodyLen;
	snprintf( lastBody, sizeof( lastBody ), "%s", r->body );
	snprintf( lastError, sizeof( lastError ), "%s", r->error ? r->error : "" );
	lastUser = user;
}

static void Log( const char *text )
{
	snprintf( logged, sizeof( logged ), "%s", text );
}

int main( void )
{
	static char big[70000];
	int token = 0, i;

	Http_Init( Log );

	/* a GET */
	CHECK( Http_Get( "https://api.test/x", Callback, &token ) == 1, "get" );
	CHECK( numFake == 1 && fake[0].method == ODF_HTTP_GET && !strcmp( fake[0].url, "https://api.test/x" ) &&
		strstr( fake[0].headers, "Accept: application/json" ) && !fake[0].body[0], "the request: %s %s", fake[0].url, fake[0].headers );
	CHECK( Http_InFlight() == 1, "in flight %d", Http_InFlight() );
	Finish( 0, 200, "{\"a\":1}" );
	CHECK( calls == 1 && lastStatus == 200 && lastLen == 7 && !strcmp( lastBody, "{\"a\":1}" ) && lastUser == &token && !lastError[0],
		"answer: %d %d '%s'", calls, lastStatus, lastBody );
	CHECK( Http_InFlight() == 0, "in flight after the answer %d", Http_InFlight() );

	/* a POST with a key */
	CHECK( Http_PostJSON( "https://api.test/p", "secret", "{\"b\":2}", Callback, NULL ) == 1, "post" );
	CHECK( fake[1].method == ODF_HTTP_POST && !strcmp( fake[1].body, "{\"b\":2}" ) && strstr( fake[1].headers, "Content-Type: application/json" ) &&
		strstr( fake[1].headers, "Authorization: Bearer secret" ), "post: %s", fake[1].headers );
	CHECK( Http_PostJSON( "https://api.test/p", NULL, "{}", Callback, NULL ) == 1 && !strstr( fake[2].headers, "Authorization" ), "post without a key" );
	Finish( 2, 200, "{}" );
	Finish( 1, 404, "{\"error\":{\"code\":\"nope\"}}" );
	CHECK( lastStatus == 404 && strstr( lastBody, "nope" ), "an answer to an older request: %d", lastStatus );

	/* a request that failed before an answer */
	calls = 0;
	CHECK( Http_Get( "https://api.test/f", Callback, NULL ) == 1, "get" );
	Finish( 3, 0, "" );
	CHECK( calls == 1 && lastStatus == 0 && !strcmp( lastError, "no answer" ), "failed request: %d '%s'", lastStatus, lastError );

	/* an answer that is too large for the buffer */
	memset( big, 'x', sizeof( big ) - 1 );
	CHECK( Http_Get( "https://api.test/big", Callback, NULL ) == 1, "get" );
	answer = big;
	fake[4].handler( fake[4].id, 200, (int)strlen( big ), fake[4].userData );
	CHECK( lastStatus == 0 && !strcmp( lastError, "the answer is too large" ), "large answer: %d '%s'", lastStatus, lastError );

	/* the answer of a request nobody knows is ignored */
	calls = 0;
	fake[0].handler( 555, 200, 0, 0 );
	fake[0].handler( 100, 200, 0, 99 );
	fake[0].handler( 100, 200, 0, -1 );
	CHECK( calls == 0, "answers to unknown requests reached the plugin" );

	/* too many at once */
	numFake = 0;
	for ( i = 0; i < 8; i++ ) {
		CHECK( Http_Get( "https://api.test/many", Callback, NULL ) == 1, "request %d", i );
	}
	CHECK( Http_Get( "https://api.test/many", Callback, NULL ) == 0 && Http_InFlight() == 8, "a ninth request was started" );

	/* the plugin shuts down with requests running: their callbacks hear about it */
	calls = 0;
	Http_Shutdown();
	CHECK( calls == 8 && lastStatus == 0 && !strcmp( lastError, "shutdown" ) && Http_InFlight() == 0, "shutdown: %d calls, '%s'", calls, lastError );

	/* the engine refuses (host not allowed) */
	logged[0] = '\0';
	refuse = 1;
	CHECK( Http_Get( "https://evil.test/", Callback, NULL ) == 0 && logged[0], "a refused request" );
	refuse = 0;
	CHECK( Http_Get( "https://api.test/x", NULL, NULL ) == 0, "a request without a callback" );
	CHECK( Http_PostJSON( "https://api.test/x", "k", "{}", Callback, NULL ) == 1 || numFake >= MAX_FAKE, "post after a refusal" );

	if ( failures ) {
		printf( "FAILED: %d check(s)\n", failures );
		return 1;
	}
	printf( "all odf_http checks passed\n" );
	return 0;
}
