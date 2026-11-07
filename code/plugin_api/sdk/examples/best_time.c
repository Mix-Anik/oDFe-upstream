/* A server plugin that announces a new best time and answers "!best" in the chat. The best time is kept in the plugin's own
   storage, so it survives a restart of the server.
   It shows: events (player_finish), chat commands, server commands to players and the private storage of a plugin.
   Build:  odf-build --server -o best_time.qvm best_time.c
   The operator gives what it needs:  +set sv_plugins "best_time:chat,storage" */
#define ODF_PLUGIN_VERSION     "1.0"
#define ODF_PLUGIN_CAPS        "chat,storage"
#define ODF_PLUGIN_DESCRIPTION "announces the best time"
#define EXPORTS( X ) X(OnInit)
#include "odfs_entry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORE_FILE "best.txt"            /* "<ms> <name>" */

static int  bestTime;                    /* ms, 0 until a run was finished */
static char bestName[33];

/* The value of a key of an info string ("\key\value\key\value"), "" when it is not there. */
static const char *InfoValue( const char *info, const char *key, char *out, int size )
{
	char pattern[40];
	const char *p;
	int n;

	out[0] = '\0';
	snprintf( pattern, sizeof( pattern ), "\\%s\\", key );
	p = strstr( info, pattern );
	if ( p ) {
		p += strlen( pattern );
		n = (int)strcspn( p, "\\" );
		if ( n >= size ) {
			n = size - 1;
		}
		memcpy( out, p, n );
		out[n] = '\0';
	}
	return out;
}

static void Save( void )
{
	char text[64];

	snprintf( text, sizeof( text ), "%d %s", bestTime, bestName );
	StoreWrite( STORE_FILE, text, (int)strlen( text ), 0 );        /* 0: replace the file */
}

static void Load( void )
{
	char text[64];
	char *space;
	int n;

	n = StoreRead( STORE_FILE, 0, text, sizeof( text ) - 1 );      /* -1: no such file yet */
	if ( n <= 0 ) {
		return;
	}
	text[n] = '\0';
	space = strchr( text, ' ' );
	if ( space ) {
		*space = '\0';
		bestTime = atoi( text );
		snprintf( bestName, sizeof( bestName ), "%s", space + 1 );
	}
}

/* "player_finish": the game module says a player finished a run (the keys are listed with the event in the reference) */
static void OnFinish( const char *data )
{
	char value[16], text[128];
	odfClientInfo_t info;
	int time;

	time = atoi( InfoValue( data, "time", value, sizeof( value ) ) );
	if ( time <= 0 || ( bestTime && time >= bestTime ) ) {
		return;
	}
	if ( !GetClientInfo( atoi( InfoValue( data, "client", value, sizeof( value ) ) ), &info ) ) {
		return;                  /* the slot is empty: the player left */
	}

	bestTime = time;
	snprintf( bestName, sizeof( bestName ), "%s", info.name );
	Save();

	/* clientNum -1: everybody */
	snprintf( text, sizeof( text ), "chat \"^2New best time: ^7%s ^2%d.%03d\"", bestName, bestTime / 1000, bestTime % 1000 );
	SendServerCommand( -1, text );
}

/* "!best" in the chat */
static void CmdBest( int clientNum, int serial, const char *args )
{
	char text[128];

	if ( !bestTime ) {
		snprintf( text, sizeof( text ), "print \"Nobody has finished a run yet.\n\"" );
	} else {
		snprintf( text, sizeof( text ), "print \"Best time: %s ^7%d.%03d\n\"", bestName, bestTime / 1000, bestTime % 1000 );
	}
	SendServerCommand( clientNum, text );
}

int OnInit( void )
{
	Load();
	/* each returns 0 when the engine refuses (a capability the operator did not give, ...): then there is nothing to run */
	if ( !EventSubscribe( "player_finish", OnFinish ) || !ChatCommandRegister( "best", CmdBest ) ) {
		return 1;
	}
	return 0;
}
