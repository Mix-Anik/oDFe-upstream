/*
===========================================================================
HTTP requests for the server plugins (the HttpRequest import). The engine does the networking
with libcurl; a plugin only sees an id and, later, the status and the answer. Nothing blocks.
===========================================================================
*/

#ifndef SV_HTTP_H
#define SV_HTTP_H

#include "../qcommon/q_shared.h"

#define SV_HTTP_GET			0
#define SV_HTTP_POST		1

#define SV_HTTP_MAX_BODY		( 64 * 1024 )		// of a request
#define SV_HTTP_MAX_ANSWER		( 256 * 1024 )		// of an answer: more is an error
#define SV_HTTP_MAX_RUNNING		4
#define SV_HTTP_MAX_FINISHED	8

// Starts using libcurl (loads it where it is a library of the system). Returns qfalse when it is not there.
qboolean	SV_Http_Init( void );
void		SV_Http_Shutdown( void );

// Starts a request and returns its id (1 or more), or 0 (not available, too many requests, bad arguments). headers are
// "Name: value" lines separated by newlines; only Authorization, Content-Type and Accept are sent. owner is given back
// when the request finishes.
int			SV_Http_Start( int method, const char *url, const char *headers, const void *body, int bodyLen, int timeoutMs, void *owner );

// Moves the transfers on. Call every server frame.
void		SV_Http_Pump( void );

// Takes the next finished request: returns qfalse when there is none. status is 0 when it failed before an answer.
qboolean	SV_Http_Finished( int *id, int *status, int *bodyLen, void **owner );

// Copies a piece of the answer of a finished request, from byte offset on; returns the number of bytes copied.
int			SV_Http_Body( int id, int offset, char *out, int outSize );

// Forgets a finished request.
void		SV_Http_Release( int id );

// Ends the requests of a plugin that stopped; their answers are dropped.
void		SV_Http_Abort( void *owner );

#endif // SV_HTTP_H
