/*
===========================================================================
HTTP requests for the server plugins, see sv_http.h. libcurl's multi interface, polled every frame.
===========================================================================
*/

#include "server.h"
#include "sv_http.h"

#ifdef USE_CURL

#ifdef USE_LOCAL_HEADERS
#include "curl/curl.h"
#else
#include <curl/curl.h>
#endif

#ifdef USE_CURL_DLOPEN

#ifdef _WIN32
#define SV_CURL_LIB		"libcurl-3.dll"
#elif defined( __APPLE__ )
#define SV_CURL_LIB		"libcurl.dylib"
#else
#define SV_CURL_LIB		"libcurl.so.4"
#endif

static CURL *(*hc_easy_init)( void );
static CURLcode (*hc_easy_setopt)( CURL *curl, CURLoption option, ... );
static void (*hc_easy_cleanup)( CURL *curl );
static CURLcode (*hc_easy_getinfo)( CURL *curl, CURLINFO info, ... );
static const char *(*hc_easy_strerror)( CURLcode );
static CURLM *(*hc_multi_init)( void );
static CURLMcode (*hc_multi_add_handle)( CURLM *multi, CURL *curl );
static CURLMcode (*hc_multi_remove_handle)( CURLM *multi, CURL *curl );
static CURLMcode (*hc_multi_perform)( CURLM *multi, int *running );
static CURLMcode (*hc_multi_cleanup)( CURLM *multi );
static CURLMsg *(*hc_multi_info_read)( CURLM *multi, int *msgsInQueue );
static struct curl_slist *(*hc_slist_append)( struct curl_slist *list, const char *string );
static void (*hc_slist_free_all)( struct curl_slist *list );

static void *curlLib;

static qboolean LoadCurl( void )
{
	if ( curlLib ) {
		return qtrue;
	}
	curlLib = Sys_LoadLibrary( SV_CURL_LIB );
#ifdef ALTERNATE_CURL_LIB
	if ( !curlLib ) {
		curlLib = Sys_LoadLibrary( ALTERNATE_CURL_LIB );
	}
#endif
	if ( !curlLib ) {
		return qfalse;
	}
#define GPA( name_ ) \
	if ( !( hc_##name_ = Sys_LoadFunction( curlLib, "curl_" #name_ ) ) ) { \
		Sys_UnloadLibrary( curlLib ); \
		curlLib = NULL; \
		return qfalse; \
	}
	GPA( easy_init ) GPA( easy_setopt ) GPA( easy_cleanup ) GPA( easy_getinfo ) GPA( easy_strerror )
	GPA( multi_init ) GPA( multi_add_handle ) GPA( multi_remove_handle ) GPA( multi_perform ) GPA( multi_cleanup )
	GPA( multi_info_read ) GPA( slist_append ) GPA( slist_free_all )
#undef GPA
	return qtrue;
}

static void UnloadCurl( void )
{
	if ( curlLib ) {
		Sys_UnloadLibrary( curlLib );
		curlLib = NULL;
	}
}

#else // statically linked

#define hc_easy_init curl_easy_init
#define hc_easy_setopt curl_easy_setopt
#define hc_easy_cleanup curl_easy_cleanup
#define hc_easy_getinfo curl_easy_getinfo
#define hc_easy_strerror curl_easy_strerror
#define hc_multi_init curl_multi_init
#define hc_multi_add_handle curl_multi_add_handle
#define hc_multi_remove_handle curl_multi_remove_handle
#define hc_multi_perform curl_multi_perform
#define hc_multi_cleanup curl_multi_cleanup
#define hc_multi_info_read curl_multi_info_read
#define hc_slist_append curl_slist_append
#define hc_slist_free_all curl_slist_free_all

static qboolean LoadCurl( void ) { return qtrue; }
static void UnloadCurl( void ) {}

#endif // USE_CURL_DLOPEN

typedef enum {
	R_FREE,
	R_RUNNING,
	R_FINISHED
} reqState_t;

typedef struct {
	reqState_t			state;
	int					id;
	void				*owner;
	CURL				*easy;
	struct curl_slist	*headers;
	char				*postData;
	char				*answer;
	int					answerLen;
	int					answerCap;
	qboolean			tooLarge;
	qboolean			told;		// the plugin has been told it finished
	int					status;
	char				error[CURL_ERROR_SIZE];
} request_t;

static request_t	requests[SV_HTTP_MAX_RUNNING + SV_HTTP_MAX_FINISHED];
static CURLM		*multi;
static int			nextId;

static size_t WriteAnswer( char *data, size_t size, size_t count, void *user )
{
	request_t *r = (request_t *)user;
	size_t n = size * count;

	if ( n > (size_t)SV_HTTP_MAX_ANSWER || r->answerLen + (int)n > SV_HTTP_MAX_ANSWER ) {
		r->tooLarge = qtrue;
		return 0;		// curl stops the transfer
	}
	if ( r->answerLen + (int)n + 1 > r->answerCap ) {
		int cap = r->answerCap ? r->answerCap * 2 : 4096;
		char *bigger;

		while ( cap < r->answerLen + (int)n + 1 ) {
			cap *= 2;
		}
		bigger = (char *)realloc( r->answer, cap );
		if ( !bigger ) {
			r->tooLarge = qtrue;
			return 0;
		}
		r->answer = bigger;
		r->answerCap = cap;
	}
	memcpy( r->answer + r->answerLen, data, n );
	r->answerLen += (int)n;
	r->answer[r->answerLen] = '\0';
	return n;
}

static void FreeRequest( request_t *r )
{
	if ( r->easy ) {
		if ( multi && r->state == R_RUNNING ) {
			hc_multi_remove_handle( multi, r->easy );
		}
		hc_easy_cleanup( r->easy );
	}
	if ( r->headers ) {
		hc_slist_free_all( r->headers );
	}
	free( r->postData );
	free( r->answer );
	Com_Memset( r, 0, sizeof( *r ) );
}

// Only these headers are sent, one line each, without control characters: the plugin cannot make the engine send anything else.
static struct curl_slist *BuildHeaders( const char *headers )
{
	static const char *const allowed[] = { "authorization", "content-type", "accept" };
	struct curl_slist *list = NULL;
	const char *s = headers;
	int lines = 0;

	while ( s && *s && lines < 8 ) {
		char line[600];
		const char *end = s;
		int len, i, colon = -1, ok = 0;

		while ( *end && *end != '\n' ) {
			end++;
		}
		len = (int)( end - s );
		if ( len > 0 && s[len - 1] == '\r' ) {
			len--;
		}
		if ( len > 0 && len < (int)sizeof( line ) ) {
			Com_Memcpy( line, s, len );
			line[len] = '\0';
			for ( i = 0; i < len; i++ ) {
				if ( (unsigned char)line[i] < 0x20 || line[i] == 0x7F ) {
					len = 0;
					break;
				}
				if ( line[i] == ':' && colon < 0 ) {
					colon = i;
				}
			}
			if ( len > 0 && colon > 0 ) {
				for ( i = 0; i < (int)ARRAY_LEN( allowed ); i++ ) {
					if ( (int)strlen( allowed[i] ) == colon && !Q_stricmpn( line, allowed[i], colon ) ) {
						ok = 1;
					}
				}
			}
			if ( ok ) {
				list = hc_slist_append( list, line );
				lines++;
			}
		}
		s = *end ? end + 1 : end;
	}
	return hc_slist_append( list, "Expect:" );		// do not wait for a 100 Continue
}

qboolean SV_Http_Init( void )
{
	if ( multi ) {
		return qtrue;
	}
	if ( !LoadCurl() ) {
		return qfalse;
	}
	multi = hc_multi_init();
	if ( !multi ) {
		UnloadCurl();
		return qfalse;
	}
	return qtrue;
}

void SV_Http_Shutdown( void )
{
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( requests ); i++ ) {
		FreeRequest( &requests[i] );
	}
	if ( multi ) {
		hc_multi_cleanup( multi );
		multi = NULL;
	}
	UnloadCurl();
}

int SV_Http_Start( int method, const char *url, const char *headers, const void *body, int bodyLen, int timeoutMs, void *owner )
{
	request_t *r = NULL;
	int i, running = 0;

	if ( !multi || !url || !*url || ( method != SV_HTTP_GET && method != SV_HTTP_POST ) || bodyLen < 0 || bodyLen > SV_HTTP_MAX_BODY ||
		( bodyLen > 0 && !body ) || ( method == SV_HTTP_GET && bodyLen > 0 ) ) {
		return 0;
	}
	for ( i = 0; i < (int)ARRAY_LEN( requests ); i++ ) {
		if ( requests[i].state == R_RUNNING ) {
			running++;
		} else if ( requests[i].state == R_FREE && !r ) {
			r = &requests[i];
		}
	}
	if ( !r || running >= SV_HTTP_MAX_RUNNING ) {
		return 0;
	}
	if ( timeoutMs < 1000 ) {
		timeoutMs = 1000;
	} else if ( timeoutMs > 30000 ) {
		timeoutMs = 30000;
	}

	Com_Memset( r, 0, sizeof( *r ) );
	r->easy = hc_easy_init();
	if ( !r->easy ) {
		return 0;
	}
	r->owner = owner;
	r->headers = BuildHeaders( headers );
	if ( method == SV_HTTP_POST ) {
		r->postData = (char *)malloc( bodyLen + 1 );
		if ( !r->postData ) {
			FreeRequest( r );
			return 0;
		}
		if ( bodyLen ) {
			memcpy( r->postData, body, bodyLen );
		}
		r->postData[bodyLen] = '\0';
		hc_easy_setopt( r->easy, CURLOPT_POST, 1L );
		hc_easy_setopt( r->easy, CURLOPT_POSTFIELDS, r->postData );
		hc_easy_setopt( r->easy, CURLOPT_POSTFIELDSIZE, (long)bodyLen );
	}
	hc_easy_setopt( r->easy, CURLOPT_URL, url );
	hc_easy_setopt( r->easy, CURLOPT_HTTPHEADER, r->headers );
	hc_easy_setopt( r->easy, CURLOPT_WRITEFUNCTION, WriteAnswer );
	hc_easy_setopt( r->easy, CURLOPT_WRITEDATA, r );
	hc_easy_setopt( r->easy, CURLOPT_ERRORBUFFER, r->error );
	hc_easy_setopt( r->easy, CURLOPT_TIMEOUT_MS, (long)timeoutMs );
	hc_easy_setopt( r->easy, CURLOPT_CONNECTTIMEOUT_MS, (long)( timeoutMs < 10000 ? timeoutMs : 10000 ) );
	hc_easy_setopt( r->easy, CURLOPT_NOSIGNAL, 1L );
	hc_easy_setopt( r->easy, CURLOPT_FOLLOWLOCATION, 0L );		// an answer cannot send the engine to another host
#if LIBCURL_VERSION_NUM >= 0x075500
	hc_easy_setopt( r->easy, CURLOPT_PROTOCOLS_STR, "http,https" );
#else
	hc_easy_setopt( r->easy, CURLOPT_PROTOCOLS, (long)( CURLPROTO_HTTP | CURLPROTO_HTTPS ) );
#endif
	hc_easy_setopt( r->easy, CURLOPT_USERAGENT, "oDFe-plugin" );
	if ( hc_multi_add_handle( multi, r->easy ) != CURLM_OK ) {
		FreeRequest( r );
		return 0;
	}
	r->state = R_RUNNING;
	r->id = ++nextId > 0 ? nextId : ( nextId = 1 );
	return r->id;
}

void SV_Http_Pump( void )
{
	int running = 0, queued = 0, i;
	CURLMsg *msg;

	if ( !multi ) {
		return;
	}
	hc_multi_perform( multi, &running );
	while ( ( msg = hc_multi_info_read( multi, &queued ) ) != NULL ) {
		if ( msg->msg != CURLMSG_DONE ) {
			continue;
		}
		for ( i = 0; i < (int)ARRAY_LEN( requests ); i++ ) {
			request_t *r = &requests[i];

			if ( r->state == R_RUNNING && r->easy == msg->easy_handle ) {
				long code = 0;

				if ( msg->data.result == CURLE_OK && !r->tooLarge ) {
					hc_easy_getinfo( r->easy, CURLINFO_RESPONSE_CODE, &code );
				}
				r->status = (int)code;
				hc_multi_remove_handle( multi, r->easy );
				hc_easy_cleanup( r->easy );
				r->easy = NULL;
				if ( r->headers ) {
					hc_slist_free_all( r->headers );
					r->headers = NULL;
				}
				free( r->postData );
				r->postData = NULL;
				r->state = R_FINISHED;
				break;
			}
		}
	}
}

qboolean SV_Http_Finished( int *id, int *status, int *bodyLen, void **owner )
{
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( requests ); i++ ) {
		request_t *r = &requests[i];

		if ( r->state == R_FINISHED && !r->told ) {
			*id = r->id;
			*status = r->status;
			*bodyLen = r->answerLen;
			*owner = r->owner;
			r->told = qtrue;
			return qtrue;
		}
	}
	return qfalse;
}

int SV_Http_Body( int id, int offset, char *out, int outSize )
{
	int i, n;

	if ( id <= 0 || offset < 0 || !out || outSize < 1 ) {
		return 0;
	}
	for ( i = 0; i < (int)ARRAY_LEN( requests ); i++ ) {
		request_t *r = &requests[i];

		if ( r->state == R_FINISHED && r->id == id ) {
			if ( offset >= r->answerLen ) {
				return 0;
			}
			n = r->answerLen - offset;
			if ( n > outSize ) {
				n = outSize;
			}
			memcpy( out, r->answer + offset, n );
			return n;
		}
	}
	return 0;
}

void SV_Http_Release( int id )
{
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( requests ); i++ ) {
		if ( requests[i].state == R_FINISHED && requests[i].id == id ) {
			FreeRequest( &requests[i] );
			return;
		}
	}
}

void SV_Http_Abort( void *owner )
{
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( requests ); i++ ) {
		if ( requests[i].state != R_FREE && requests[i].owner == owner ) {
			FreeRequest( &requests[i] );
		}
	}
}

#else // !USE_CURL

qboolean SV_Http_Init( void ) { return qfalse; }
void SV_Http_Shutdown( void ) {}
int SV_Http_Start( int method, const char *url, const char *headers, const void *body, int bodyLen, int timeoutMs, void *owner ) { return 0; }
void SV_Http_Pump( void ) {}
qboolean SV_Http_Finished( int *id, int *status, int *bodyLen, void **owner ) { return qfalse; }
int SV_Http_Body( int id, int offset, char *out, int outSize ) { return 0; }
void SV_Http_Release( int id ) {}
void SV_Http_Abort( void *owner ) {}

#endif
