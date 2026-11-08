/*
HTTP requests with a JSON body for server plugins, on top of HttpRequest / HttpBody: a callback for each request and the answer
in one zero terminated buffer. Nothing blocks: a request is started with Http_PostJSON or Http_Get and the callback is called in
a later frame when the request is done. The operator has to allow the host (sv_plugins "name:http=<host>", capability http).

Using it: #include "odf_http.h" in the plugin. odf-build sees the include and adds odf_http.c to the program. To read the
answer or write the body, include "odf_json.h" as well.
*/

#ifndef ODF_HTTP_H
#define ODF_HTTP_H

#ifndef ODF_MODULE_SERVER
#error odf_http.h is for server plugins (odfs.h)
#endif

typedef struct {
	int			status;		/* HTTP status, 0 when the request failed before an answer (see error) */
	const char	*body;		/* zero terminated, valid during the callback only */
	int			bodyLen;
	const char	*error;		/* text for status 0, else NULL */
} httpResult_t;

typedef void ( *httpCallback_t )( const httpResult_t *result, void *user );
typedef void ( *httpLog_t )( const char *text );

/* Call once before the first request, from OnInit. log (may be NULL) gets the reasons why a request was not started. */
void	Http_Init( httpLog_t log );

/* Forgets all requests; their callbacks get status 0 and the error "shutdown". Call from OnShutdown. */
void	Http_Shutdown( void );

/* Starts a POST request with a JSON body. bearer (may be NULL) is sent as "Authorization: Bearer ...". Returns 0 if it could not
   be started (too many running, the host is not allowed, bad arguments); the callback is not called then. */
int		Http_PostJSON( const char *url, const char *bearer, const char *json, httpCallback_t callback, void *user );

/* Starts a GET request (no body, no key). Same rules as above. */
int		Http_Get( const char *url, httpCallback_t callback, void *user );

/* The number of requests that are not done yet. */
int		Http_InFlight( void );

#endif
