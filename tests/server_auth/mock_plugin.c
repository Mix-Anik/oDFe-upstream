/*
A stand-in for the records plugin (built for WebAssembly), controlled by the test program through mock_state.h. It answers
logins like the real plugin would (synchronously or on a later frame), logs the events it gets, and does what the events
"query" and "act" ask for, so the test can see what the engine lets a plugin do.

With -DLITE it does not import the functions that need capabilities: it is a plugin that asks for nothing.
*/

#include <string.h>

#include "odfs.h"
#include "mock_state.h"

ODF_IMPORT( "odfs", "TestPull" ) void odfs_TestPull( void *state, int size );
ODF_IMPORT( "odfs", "TestPush" ) void odfs_TestPush( const void *state, int size );

ODF_DEFINE_IO()

#ifndef PFX
#define PFX "mock"
#endif

#ifdef WANTS
ODF_MANIFEST( "version=wants 1\ncaps=chat,http" )
#elif defined( LITE )
ODF_MANIFEST( "version=lite 1" )
#else
ODF_MANIFEST( "version=mock 1" )
#endif

static mockState_t st;
static int pendingClient = -1, pendingSerial;

static void Begin( void ) { odfs_TestPull( &st, (int)sizeof( st ) ); }
static void End( void ) { odfs_TestPush( &st, (int)sizeof( st ) ); }

static void Copy( char *dst, const char *src, int size )
{
	int i;

	for ( i = 0; i < size - 1 && src[i]; i++ ) {
		dst[i] = src[i];
	}
	dst[i] = '\0';
}

static int ToInt( const char *s )
{
	int v = 0, neg = 0;

	if ( *s == '-' ) {
		neg = 1;
		s++;
	}
	while ( *s >= '0' && *s <= '9' ) {
		v = v * 10 + ( *s++ - '0' );
	}
	return neg ? -v : v;
}

// tiny info string reader, enough for the test
static void Info( const char *data, const char *key, char *out, int size )
{
	char pattern[48];
	int i = 0, k = 0;
	const char *p = data;

	out[0] = '\0';
	pattern[k++] = '\\';
	while ( *key && k < 44 ) {
		pattern[k++] = *key++;
	}
	pattern[k++] = '\\';
	pattern[k] = '\0';
	while ( *p ) {
		if ( !strncmp( p, pattern, k ) ) {
			p += k;
			while ( *p && *p != '\\' && i < size - 1 ) {
				out[i++] = *p++;
			}
			out[i] = '\0';
			return;
		}
		p++;
	}
}

#ifndef LITE
static void Answer( int client, int serial )
{
	odfs_AuthResult( client, serial, st.resultCode, st.resultID, st.resultName, st.resultReason[0] ? st.resultReason : NULL );
}
#endif

// what the test asks the plugin to try: \a\<action>\n\<name>\d\<data>\c\<client>
static void Act( const char *data )
{
	char action[24], name[64], text[200], client[8];

	Info( data, "a", action, sizeof( action ) );
	Info( data, "n", name, sizeof( name ) );
	Info( data, "d", text, sizeof( text ) );
	Info( data, "c", client, sizeof( client ) );
	st.actResult = -99;
	st.actText[0] = '\0';

	if ( !strcmp( action, "cmd" ) ) {
		odfs_SendServerCommand( ToInt( client ), text );
		st.actResult = 1;
	} else if ( !strcmp( action, "cvar" ) ) {
		char buf[64];

		odfs_CvarRegister( name, text, ODF_CVAR_PRIVATE );
		odfs_CvarString( name, buf, sizeof( buf ) );
		Copy( st.actText, buf, sizeof( st.actText ) );
		st.actResult = 1;
	} else if ( !strcmp( action, "cvarread" ) ) {
		char buf[64];

		odfs_CvarString( name, buf, sizeof( buf ) );
		Copy( st.actText, buf, sizeof( st.actText ) );
		st.actResult = odfs_CvarInteger( name );
	} else if ( !strcmp( action, "spin" ) ) {
		volatile int spin = 0;

		for ( ;; ) {
			spin++;
		}
	} else if ( !strcmp( action, "badptr" ) ) {
		odfs_GetClientInfo( 0, (odfClientInfo_t *)0x7ffffff0 );
	}
#ifndef LITE
	else if ( !strcmp( action, "storew" ) ) {
		st.actResult = odfs_StoreWrite( name, text, (int)strlen( text ), 0 );
	} else if ( !strcmp( action, "storea" ) ) {
		st.actResult = odfs_StoreWrite( name, text, (int)strlen( text ), 1 );
	} else if ( !strcmp( action, "storer" ) ) {
		st.readLen = odfs_StoreRead( name, ToInt( text ), st.readBuf, sizeof( st.readBuf ) - 1 );
		if ( st.readLen >= 0 ) {
			st.readBuf[st.readLen] = '\0';
		}
		st.actResult = st.readLen;
	} else if ( !strcmp( action, "stores" ) ) {
		st.actResult = odfs_StoreSize( name );
	} else if ( !strcmp( action, "stored" ) ) {
		st.actResult = odfs_StoreDelete( name );
	} else if ( !strcmp( action, "storefill" ) ) {
		// writes 1 MB pieces until the engine says no: how much fits
		static char piece[1024];
		int i, pieces = 0;

		memset( piece, 'x', sizeof( piece ) );
		for ( i = 0; i < 20; i++ ) {
			int k, ok = 1;

			for ( k = 0; k < 1024 && ok; k++ ) {
				ok = odfs_StoreWrite( name, piece, (int)sizeof( piece ), 1 );
			}
			if ( !ok ) {
				break;
			}
			pieces++;
		}
		st.actResult = pieces;
	} else if ( !strcmp( action, "http" ) ) {
		st.actResult = odfs_HttpRequest( ToInt( client ), text,
			"Authorization: Bearer secret\nContent-Type: application/json\nX-Evil: 1\nHost: elsewhere", "{}", 2, 5000 );
	} else if ( !strcmp( action, "httpbody" ) ) {
		char buf[64];

		st.actResult = odfs_HttpBody( ToInt( text ), 0, buf, sizeof( buf ) );		// outside of on_http: nothing
	}
#endif
}

int odfs_init( void )
{
	Begin();
	st.initCalls++;
	odfs_Print( "mock plugin ready" );
	End();
	return 0;
}

void odfs_frame( int serverTime )
{
	Begin();
	st.frames++;
#ifndef LITE
	if ( pendingClient >= 0 ) {
		int c = pendingClient;

		pendingClient = -1;
		Answer( c, pendingSerial );
	}
#endif
	End();
}

void odfs_on_event( const char *name, const char *data )
{
	char target[32];

	// events with a 	\<plugin> in them are for that plugin only (the test has several copies of this one)
	Info( data, "t", target, sizeof( target ) );
	if ( target[0] && strcmp( target, PFX ) ) {
		return;
	}
	Begin();
	if ( st.eventCount < MOCK_MAX_LOG ) {
		Copy( st.eventName[st.eventCount], name, sizeof( st.eventName[0] ) );
		Copy( st.eventData[st.eventCount], data, sizeof( st.eventData[0] ) );
		st.eventCount++;
	}

	if ( !strcmp( name, "crash" ) ) {
		*(volatile int *)0x7ffffff0 = 1;
	} else if ( !strcmp( name, "query" ) ) {
		char client[8];

		Info( data, "client", client, sizeof( client ) );
		st.infoOk = odfs_GetClientInfo( ToInt( client ), &st.info );
	} else if ( !strcmp( name, "act" ) ) {
		Act( data );
	}
#ifndef LITE
	else if ( !strcmp( name, "auth_response" ) ) {
		char client[16], serial[16];

		Info( data, "client", client, sizeof( client ) );
		Info( data, "serial", serial, sizeof( serial ) );
		if ( st.mode == 0 ) {
			Answer( ToInt( client ), ToInt( serial ) );
		} else if ( st.mode == 1 ) {
			pendingClient = ToInt( client );
			pendingSerial = ToInt( serial );
		}
	}
#endif
	End();
}

#ifndef LITE
void odfs_on_http( int id, int status, int bodyLen )
{
	Begin();
	if ( st.httpDone < 4 ) {
		int n = st.httpDone++;

		st.httpId[n] = id;
		st.httpStatus[n] = status;
		st.httpBodyLen[n] = bodyLen;
		{
			char buf[64];
			int got = odfs_HttpBody( id, 0, buf, sizeof( buf ) - 1 );

			buf[got > 0 ? got : 0] = '\0';
			Copy( st.httpBody[n], buf, sizeof( st.httpBody[0] ) );
		}
	}
	End();
}
#endif
