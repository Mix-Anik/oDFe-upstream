/*
A stand-in for the records plugin (a QVM program), controlled by the test program through mock_state.h. It answers
logins like the real plugin would (synchronously or on a later frame), logs the events it gets, and does what the events
"query" and "act" ask for, so the test can see what the engine lets a plugin do.

With -DLITE it does not call the functions that need capabilities: it is a plugin that asks for nothing.
*/

#ifndef PFX
#define PFX "mock"
#endif

// what the plugin asks for: the capabilities of the functions it calls
#ifdef WANTS
#define ODF_PLUGIN_VERSION "wants 1"
#define ODF_PLUGIN_CAPS "chat,http"
#elif defined( STOREONLY )
#define ODF_PLUGIN_VERSION "store 1"
#define ODF_PLUGIN_CAPS "storage"
#elif defined( LITE )
#define ODF_PLUGIN_VERSION "lite 1"
#else
#define ODF_PLUGIN_VERSION "mock 1"
#define ODF_PLUGIN_CAPS "auth,http,storage"
#endif

#define EXPORTS( X ) X(OnInit) X(OnFrame)

// the entry point has to come first
#include "odfs_entry.h"
#include "mock_state.h"

// the two calls only the test build of the engine has (the asm file is made by run.sh)
extern void TestPull( void *state, int size );
extern void TestPush( const void *state, int size );

// there is no C library
static int Strlen( const char *s )
{
	int n = 0;

	while ( s[n] ) {
		n++;
	}
	return n;
}

static int Strcmp( const char *a, const char *b )
{
	while ( *a && *a == *b ) {
		a++;
		b++;
	}
	return *(const unsigned char *)a - *(const unsigned char *)b;
}

static int Strncmp( const char *a, const char *b, int n )
{
	while ( n > 0 && *a && *a == *b ) {
		a++;
		b++;
		n--;
	}
	return n > 0 ? *(const unsigned char *)a - *(const unsigned char *)b : 0;
}

static mockState_t st;
static int pendingClient = -1, pendingSerial;

static void Begin( void ) { TestPull( &st, (int)sizeof( st ) ); }
static void End( void ) { TestPush( &st, (int)sizeof( st ) ); }

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
		if ( !Strncmp( p, pattern, k ) ) {
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

// ---- what the plugin gives the engine to call ----

static void Log( const char *name, const char *data )
{
	if ( st.eventCount < MOCK_MAX_LOG ) {
		Copy( st.eventName[st.eventCount], name, sizeof( st.eventName[0] ) );
		Copy( st.eventData[st.eventCount], data, sizeof( st.eventData[0] ) );
		st.eventCount++;
	}
}

static void Handle( const char *name, const char *data );
static void EvSub( const char *data ) { Handle( "sub", data ); }
#ifndef LITE
static void HttpDone( int id, int status, int bodyLen, int userData );
#endif

#ifndef LITE
static void Answer( int client, int serial )
{
	AuthResult( client, serial, st.resultCode, st.resultIDLo, st.resultIDHi, st.resultName, st.resultReason[0] ? st.resultReason : NULL );
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

	if ( !Strcmp( action, "cmd" ) ) {
		SendServerCommand( ToInt( client ), text );
		st.actResult = 1;
	} else if ( !Strcmp( action, "cvar" ) ) {
		char buf[64];

		CvarRegister( name, text, ODF_CVAR_PRIVATE );
		CvarString( name, buf, sizeof( buf ) );
		Copy( st.actText, buf, sizeof( st.actText ) );
		st.actResult = 1;
	} else if ( !Strcmp( action, "cvarread" ) ) {
		char buf[64];

		CvarString( name, buf, sizeof( buf ) );
		Copy( st.actText, buf, sizeof( st.actText ) );
		st.actResult = CvarInteger( name );
	} else if ( !Strcmp( action, "spin" ) ) {
		volatile int spin = 0;

		for ( ;; ) {
			spin++;
		}
	} else if ( !Strcmp( action, "badptr" ) ) {
		GetClientInfo( 0, (odfClientInfo_t *)0x7ffffff0 );
	} else if ( !Strcmp( action, "sub" ) ) {
		st.actResult = EventSubscribe( name, EvSub );
	} else if ( !Strcmp( action, "subnohandler" ) ) {
		st.actResult = EventSubscribe( name, 0 );
	}
#ifndef LITE
	else if ( !Strcmp( action, "storew" ) ) {
		st.actResult = StoreWrite( name, text, (int)Strlen( text ), 0 );
	} else if ( !Strcmp( action, "storea" ) ) {
		st.actResult = StoreWrite( name, text, (int)Strlen( text ), 1 );
	} else if ( !Strcmp( action, "storer" ) ) {
		st.readLen = StoreRead( name, ToInt( text ), st.readBuf, sizeof( st.readBuf ) - 1 );
		if ( st.readLen >= 0 ) {
			st.readBuf[st.readLen] = '\0';
		}
		st.actResult = st.readLen;
	} else if ( !Strcmp( action, "stores" ) ) {
		st.actResult = StoreSize( name );
	} else if ( !Strcmp( action, "stored" ) ) {
		st.actResult = StoreDelete( name );
	} else if ( !Strcmp( action, "storefill" ) ) {
		// appends pieces of text KB (1 to 16) until the engine says no (or 40 MB are in): how many KB fit. (Every append is a file
		// operation, which costs the call a good part of its allowance of engine work: not thousands of them in one call.)
		static char piece[16 * 1024];
		int i, kb = ToInt( text ), pieces = 0;

		if ( kb < 1 || kb > 16 ) {
			kb = 1;
		}
		MemSet( piece, 'x', sizeof( piece ) );
		for ( i = 0; i < 40 * 1024 / kb; i++ ) {
			if ( !StoreWrite( name, piece, kb * 1024, 1 ) ) {
				break;
			}
			pieces++;
		}
		st.actResult = pieces * kb;
	} else if ( !Strcmp( action, "http" ) ) {
		st.actResult = HttpRequest( ToInt( client ), text,
			"Authorization: Bearer secret\nContent-Type: application/json\nX-Evil: 1\nHost: elsewhere", "{}", 2, 5000,
			HttpDone, 7000 + ToInt( client ) );
	} else if ( !Strcmp( action, "httpnohandler" ) ) {
		st.actResult = HttpRequest( 0, text, NULL, NULL, 0, 5000, 0, 0 );
	} else if ( !Strcmp( action, "httpbody" ) ) {
		char buf[64];

		st.actResult = HttpBody( ToInt( text ), 0, buf, sizeof( buf ) );		// outside of the handler of the request: nothing
	}
#endif
}

// one handler for each event the test raises; the log gets the name of the event
#define EVENT_HANDLER( n ) static void Ev_##n( const char *data ) { Handle( #n, data ); }
EVENT_HANDLER( player_finish )
EVENT_HANDLER( client_disconnect )
EVENT_HANDLER( map_begin )
EVENT_HANDLER( auth_response )
EVENT_HANDLER( query )
EVENT_HANDLER( act )
EVENT_HANDLER( crash )

#ifndef LITE
static void Append( char *dst, int size, const char *s )
{
	Copy( dst + Strlen( dst ), s, size - Strlen( dst ) );
}

static void AppendInt( char *dst, int size, int value )
{
	char digits[12], one[2];
	int n = 0;
	unsigned v = (unsigned)value;

	do {
		digits[n++] = (char)( '0' + v % 10 );
		v /= 10;
	} while ( v );
	one[1] = '\0';
	while ( n ) {
		one[0] = digits[--n];
		Append( dst, size, one );
	}
}

// "!top arguments": the log gets it as an info string
static void ChatTop( int clientNum, int serial, const char *args )
{
	char data[300];

	Begin();
	data[0] = '\0';
	Append( data, sizeof( data ), "\\client\\" );
	AppendInt( data, sizeof( data ), clientNum );
	Append( data, sizeof( data ), "\\serial\\" );
	AppendInt( data, sizeof( data ), serial );
	Append( data, sizeof( data ), "\\args\\" );
	Append( data, sizeof( data ), args );
	Log( "chat_top", data );
	End();
}
#endif

int OnInit( void )
{
	Begin();
	st.initCalls++;
	Print( "mock plugin ready" );
	EventSubscribe( "player_finish", Ev_player_finish );
	EventSubscribe( "client_disconnect", Ev_client_disconnect );
	EventSubscribe( "map_begin", Ev_map_begin );
	EventSubscribe( "auth_response", Ev_auth_response );		// refused for a plugin without the capability auth
	EventSubscribe( "query", Ev_query );
	EventSubscribe( "act", Ev_act );
	EventSubscribe( "crash", Ev_crash );
#if !defined( LITE ) && !defined( STOREONLY )
	ChatCommandRegister( "top", ChatTop );		// needs the capability chat, which only the full mock was given
#endif
	End();
	return 0;
}

void OnFrame( int serverTime )
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

static void Handle( const char *name, const char *data )
{
	char target[32];

	// events with a 	\<plugin> in them are for that plugin only (the test has several copies of this one)
	Info( data, "t", target, sizeof( target ) );
	if ( target[0] && Strcmp( target, PFX ) ) {
		return;
	}
	Begin();
	Log( name, data );

	if ( !Strcmp( name, "crash" ) ) {
		volatile int zero = 0;

		st.actResult = 1 / zero;		// a division the processor traps on: the plugin is stopped
	} else if ( !Strcmp( name, "query" ) ) {
		char client[8];

		Info( data, "client", client, sizeof( client ) );
		st.infoOk = GetClientInfo( ToInt( client ), &st.info );
	} else if ( !Strcmp( name, "act" ) ) {
		Act( data );
	}
#ifndef LITE
	else if ( !Strcmp( name, "auth_response" ) ) {
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
static void HttpDone( int id, int status, int bodyLen, int userData )
{
	Begin();
	if ( st.httpDone < 4 ) {
		int n = st.httpDone++;

		st.httpUser[n] = userData;
		st.httpId[n] = id;
		st.httpStatus[n] = status;
		st.httpBodyLen[n] = bodyLen;
		{
			char buf[64];
			int got = HttpBody( id, 0, buf, sizeof( buf ) - 1 );

			buf[got > 0 ? got : 0] = '\0';
			Copy( st.httpBody[n], buf, sizeof( st.httpBody[0] ) );
		}
	}
	End();
}
#endif
