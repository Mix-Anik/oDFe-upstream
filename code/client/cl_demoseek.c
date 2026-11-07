/*
===========================================================================
cl_demoseek.c - demo playback controls: pause, speed, seek, timeline bar

Commands
	demo_pause [0|1]	toggle (or set) pause
	demo_speed [x]		playback speed (sets timescale, which is cheat protected otherwise)
	demo_seek <pos>		<pos> is seconds from the demo start: "83.5", "1:23", "+5" / "-5"
						(relative to the current position) or "50%"

Cvars
	cl_demoKeys		1: SPACE pause, LEFT/RIGHT seek +-5 s, DOWN/UP speed down/up, HOME restart
					(only while a demo is playing and no menu / console is open)
	cl_demoBar		0: never draw the timeline, 1: while paused / seeking / mouse moved, 2: always
	cl_demoCursor	1: a mouse cursor is shown over the demo; click or drag on the timeline to seek

The timeline cursor is virtual, like the one of the menu: the mouse keeps sending relative motion
(CL_MouseEvent) and the engine moves and draws the cursor itself. While a demo plays the mouse has no
other use. The cursor and the bar fade out 2.5 s after the last mouse move.

Dragging only previews the position; the seek is made when the button is released, because a
backward seek restarts the demo.

Smooth seeking (cgame with CG_ODF_TIME_CHANGE, e.g. oDFs)
---------------------------------------------------------
A cgame that registers with trap_ODF_DemoSeekSupport can be told that the position changed. The engine then
keeps keyframes of the demo parser (the part of cl / clc that a delta parse needs: the snapshot ring, the entities
it points to, the configstrings, the file position; about 40 KB + the entities, one per cl_demoKeyInterval)
while the demo plays or is fast forwarded. A seek restores the nearest earlier keyframe, parses forward to the
target (running the server commands in the engine only), and calls CG_ODF_TIME_CHANGE; no restart, no reload,
no cgame init. The configstrings that differ from what the cgame knew are sent to it as ordinary "cs" commands.

+mouseseek  hold it and move the mouse: the demo is paused and the position follows the mouse (cl_demoMouseSeek
            milliseconds per mouse count, a negative value reverses it). LEFT / RIGHT then step one snapshot
            (one server frame) back / forward, and keep stepping while held.

Fallback seeking (any other cgame)
----------------------------------
The DeFRaG cgame cannot be skipped over: it decodes the race time, run state, checkpoints and
stats from the CHAIN of snapshots it reads (CG_DF_ReadSnapshot), and it errors out when snapshot
numbers or server time go backwards. So the engine never jumps; it feeds the skipped snapshots
through the live cgame as fast as possible instead (catch-up), with all rendering, sound and
printing syscalls of the cgame turned off (CLDemo_Seeking, see CL_CgameSystemCalls):

	forward seek   catch-up from the current position
	backward seek  the demo is restarted ("demo <name>", cgame and all) and caught up from the start

Each catch-up step parses a few demo messages (at most 24 message numbers, so the cgame can still
read all of them from the client's 32 slot snapshot ring) and runs one cgame frame at the newest
snapshot's time. The cgame then transitions through all of them in order, exactly like after a
lag spike.
===========================================================================
*/

#include "client.h"

#define SEEK_STEP		5000
#define BAR_SHOW_TIME	2500	/* real ms the bar / cursor stay after the last use */
#define CATCHUP_BUDGET	30		/* ms of real time spent in catch-up per client frame */
#define CATCHUP_MSGS	16		/* demo messages parsed per cgame frame */
#define CATCHUP_RING	24		/* max span of message numbers per cgame frame (ring is PACKET_BACKUP = 32) */
#define CATCHUP_CMDS	40		/* max server commands per cgame frame (ring is MAX_RELIABLE_COMMANDS = 64) */

/* layout in 640x480 virtual coordinates */
#define PANEL_X		30.0f
#define PANEL_Y		440.0f
#define PANEL_W		580.0f
#define PANEL_H		38.0f
#define TRACK_X		40.0f
#define TRACK_Y		466.0f
#define TRACK_W		560.0f
#define TRACK_H		4.0f
#define CURSOR_SIZE	32.0f		/* as the menu cursor */

#define KEY_MAX			2048	/* keyframes per demo */
#define KEY_INTERVAL	1000	/* ms between keyframes (longer for very long demos) */
#define KEY_BUDGET		( 192 * 1024 * 1024 )	/* bytes of keyframes; after that the last one is the nearest */

typedef struct {
	int				serverTime;
	int				filePos;				// where the next message starts
	clSnapshot_t	snap;
	clSnapshot_t	snapshots[PACKET_BACKUP];
	gameState_t		gameState;
	char			mapname[MAX_QPATH];
	int				parseEntitiesNum, serverId;
	int				entBase, entCount;		// the entities the snapshots of the ring point to
	entityState_t	*ents;
	int				messageSeq, cmdSeq, cmdExec;
	int				numPending;				// commands received but not yet run: cmdExec + 1 .. cmdSeq
	char			( *pending )[ MAX_STRING_CHARS ];
	int				size;
} demoKey_t;

static const float speedSteps[] = { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f };

static struct {
	char		arg[MAX_OSPATH];	/* the argument of the demo command, to restart it */
	qboolean	indexed;			/* first / last / snapshots are valid and seeking is allowed */
	int			firstTime;			/* serverTime of the first snapshot */
	int			lastTime;			/* serverTime of the last snapshot */
	int			snapshots;
	int			gamestates;

	qboolean	paused;
	qboolean	seeking;			/* catch-up to target is running */
	int			target;				/* serverTime */
	int			startTime;			/* serverTime the catch-up started from (for the progress display) */
	qboolean	restartPending;		/* "demo" command queued, the catch-up starts when it is up */
	int			restartFrame;
	float		speed;				/* timescale to restore after a restart (the gamestate resets it) */
	qboolean	restoreSpeed;
	int			barUntil;			/* Sys_Milliseconds() */

	qboolean	cursorInit;
	float		curX, curY;			/* pixels */
	int			cursorTime;			/* Sys_Milliseconds() of the last mouse move */
	qboolean	dragging;
	float		dragFrac;			/* 0..1 along the timeline */

	demoKey_t	**keys;				/* smooth seeking */
	int			numKeys, keyBytes, keyInterval;
	int			quietFrame;			/* cls.framecount of the frame that draws the first snapshot after a seek */
	qboolean	scrubbing;			/* +mouseseek is held */
	int			scrubDelta;			/* mouse counts since the last seek */
	int			step;				/* single snapshot steps asked for with LEFT / RIGHT while scrubbing, one is made per frame */
	float		scrubTime;			/* where the mouse says the demo is (serverTime); the playhead snaps to snapshots, this does not */
	qboolean	scrubWarned;
} demo;

static qboolean cgameSeekable;		/* the cgame registered CG_ODF_TIME_CHANGE */

static cvar_t *cl_demoKeys;
static cvar_t *cl_demoBar;
static cvar_t *cl_demoCursor;
static cvar_t *cl_demoMouseSeek;


/*
=================
Demo_Scan

Walks the demo file once and records the time range. Only the message headers are read: the first
snapshot's serverTime is a plain long in front of the delta data, so there is nothing to decode.
=================
*/
static void Demo_Scan( const char *path ) {
	fileHandle_t	f;
	byte			data[ MAX_MSGLEN_BUF ];
	msg_t			buf;
	int				seq, len, cmd;

	demo.indexed = qfalse;
	demo.snapshots = demo.gamestates = 0;
	demo.firstTime = demo.lastTime = 0;

	if ( FS_FOpenFileRead( path, &f, qtrue ) < 0 || f == FS_INVALID_HANDLE ) {
		return;
	}

	for ( ;; ) {
		if ( FS_Read( &seq, 4, f ) != 4 || FS_Read( &len, 4, f ) != 4 ) {
			break;
		}
		len = LittleLong( len );
		if ( len < 0 || len > MAX_MSGLEN || FS_Read( data, len, f ) != len ) {
			break;
		}

		MSG_Init( &buf, data, MAX_MSGLEN );
		buf.cursize = len;
		buf.readcount = 0;
		MSG_Bitstream( &buf );
		MSG_ReadLong( &buf );	// reliable acknowledge

		while ( buf.readcount <= buf.cursize ) {
			cmd = MSG_ReadByte( &buf );
			if ( cmd == svc_serverCommand ) {
				MSG_ReadLong( &buf );
				MSG_ReadString( &buf );
				continue;
			}
			if ( cmd == svc_nop ) {
				continue;
			}
			if ( cmd == svc_gamestate ) {
				demo.gamestates++;
			} else if ( cmd == svc_snapshot ) {
				const int t = MSG_ReadLong( &buf );
				if ( demo.snapshots == 0 ) {
					demo.firstTime = t;
				}
				demo.lastTime = t;
				demo.snapshots++;
			}
			break;	// the rest of the message is not needed
		}
	}

	FS_FCloseFile( f );

	// several gamestates = map change inside the demo, the time line is not continuous
	demo.indexed = demo.snapshots > 1 && demo.gamestates == 1 && demo.lastTime - demo.firstTime > 0;
}


/*
=================
CLDemo_Start

Called by the demo command once the file is open. A restart issued by a backward seek keeps the
index and the target and goes on seeking as soon as the demo is up again.
=================
*/
void CLDemo_Start( const char *arg, const char *path ) {
	if ( demo.restartPending && !Q_stricmp( arg, demo.arg ) ) {
		demo.restartPending = qfalse;
		demo.seeking = demo.indexed;
		demo.startTime = demo.firstTime;
		demo.paused = qfalse;
		demo.dragging = qfalse;
		return;
	}

	CLDemo_Free();
	Com_Memset( &demo, 0, sizeof( demo ) );
	Q_strncpyz( demo.arg, arg, sizeof( demo.arg ) );
	Demo_Scan( path );
	demo.keyInterval = KEY_INTERVAL;
	if ( demo.indexed && ( demo.lastTime - demo.firstTime ) / KEY_MAX + 1 > demo.keyInterval ) {
		demo.keyInterval = ( demo.lastTime - demo.firstTime ) / KEY_MAX + 1;
	}
}


void CLDemo_CgameSupport( int version ) {
	cgameSeekable = version == CG_DEMOSEEK_VERSION;
}


qboolean CLDemo_CgameSeekable( void ) {
	return cgameSeekable;
}


void CLDemo_CgameShutdown( void ) {
	cgameSeekable = qfalse;
}


qboolean CLDemo_Quiet( void ) {
	return clc.demoplaying && demo.quietFrame && cls.framecount == demo.quietFrame;
}


qboolean CLDemo_Paused( void ) {
	return clc.demoplaying && ( demo.paused || demo.scrubbing );
}


// a seek is under way (catch-up, restart, +mouseseek held): the demo must not be recorded now, see CL_Record_f
qboolean CLDemo_SeekBusy( void ) {
	return clc.demoplaying && ( demo.seeking || demo.restartPending || demo.scrubbing );
}


qboolean CLDemo_Seeking( void ) {
	return clc.demoplaying && demo.seeking;
}


static int Demo_Playhead( void ) {
	return demo.seeking ? demo.target : cl.serverTime;
}


static void Demo_FormatTime( char *out, int size, int ms ) {
	if ( ms < 0 ) {
		ms = 0;
	}
	Com_sprintf( out, size, "%i:%02i.%i", ms / 60000, ( ms / 1000 ) % 60, ( ms % 1000 ) / 100 );
}


static qboolean Demo_CanControl( void ) {
	return clc.demoplaying && cls.state == CA_ACTIVE;
}


static void Demo_Touch( void ) {
	demo.barUntil = Sys_Milliseconds() + BAR_SHOW_TIME;
}



/*
=================
Keyframes

A keyframe is what the delta parser of the client needs to go on from a message: the snapshot ring with the
entities it points to, the configstrings, the sequence numbers, the file position, and the server commands that
were received but not run yet. They are taken at the frontier of the demo (the furthest point reached), so
every position behind it can be restored.
=================
*/
void CLDemo_Free( void ) {
	int i;

	for ( i = 0; i < demo.numKeys; i++ ) {
		if ( demo.keys[i]->ents ) {
			Z_Free( demo.keys[i]->ents );
		}
		if ( demo.keys[i]->pending ) {
			Z_Free( demo.keys[i]->pending );
		}
		Z_Free( demo.keys[i] );
	}
	if ( demo.keys ) {
		Z_Free( demo.keys );
	}
	demo.keys = NULL;
	demo.numKeys = demo.keyBytes = 0;
}


static void Demo_CaptureKey( void ) {
	demoKey_t	*k;
	int			i, minNum, n;

	if ( demo.numKeys >= KEY_MAX || demo.keyBytes > KEY_BUDGET ) {
		return;
	}
	if ( !demo.keys ) {
		demo.keys = Z_Malloc( KEY_MAX * sizeof( demo.keys[0] ) );
	}

	k = Z_Malloc( sizeof( *k ) );
	k->serverTime = cl.snap.serverTime;
	k->filePos = FS_FTell( clc.demofile );
	k->snap = cl.snap;
	Com_Memcpy( k->snapshots, cl.snapshots, sizeof( k->snapshots ) );
	k->gameState = cl.gameState;
	Q_strncpyz( k->mapname, cl.mapname, sizeof( k->mapname ) );
	k->parseEntitiesNum = cl.parseEntitiesNum;
	k->serverId = cl.serverId;
	k->messageSeq = clc.serverMessageSequence;
	k->cmdSeq = clc.serverCommandSequence;
	k->cmdExec = clc.lastExecutedServerCommand;

	// the entities of every snapshot the parser would still accept as a delta base
	minNum = cl.parseEntitiesNum;
	for ( i = 0; i < PACKET_BACKUP; i++ ) {
		const clSnapshot_t *s = &cl.snapshots[i];

		if ( !s->valid || cl.snap.messageNum - s->messageNum < 0 || cl.snap.messageNum - s->messageNum >= PACKET_BACKUP ) {
			continue;
		}
		if ( cl.parseEntitiesNum - s->parseEntitiesNum > MAX_PARSE_ENTITIES - MAX_SNAPSHOT_ENTITIES ) {
			continue;
		}
		if ( s->parseEntitiesNum - minNum < 0 ) {
			minNum = s->parseEntitiesNum;
		}
	}
	k->entBase = minNum;
	k->entCount = cl.parseEntitiesNum - minNum;
	if ( k->entCount > 0 ) {
		k->ents = Z_Malloc( k->entCount * sizeof( entityState_t ) );
		for ( i = 0; i < k->entCount; i++ ) {
			k->ents[i] = cl.parseEntities[ ( k->entBase + i ) % MAX_PARSE_ENTITIES ];
		}
	}

	n = k->cmdSeq - k->cmdExec;
	if ( n > 0 && n < MAX_RELIABLE_COMMANDS ) {
		k->numPending = n;
		k->pending = Z_Malloc( n * MAX_STRING_CHARS );
		for ( i = 0; i < n; i++ ) {
			Q_strncpyz( k->pending[i], clc.serverCommands[ ( k->cmdExec + 1 + i ) & ( MAX_RELIABLE_COMMANDS - 1 ) ], MAX_STRING_CHARS );
		}
	}

	k->size = sizeof( *k ) + k->entCount * sizeof( entityState_t ) + k->numPending * MAX_STRING_CHARS;
	demo.keyBytes += k->size;
	demo.keys[ demo.numKeys++ ] = k;
}


/*
=================
CLDemo_MessageParsed

After every demo message. Takes a keyframe when the demo reached a new part.
=================
*/
void CLDemo_MessageParsed( void ) {
	if ( !cgameSeekable || !clc.demoplaying || !demo.indexed || clc.demorecording ) {
		return;
	}
	if ( !( clc.eventMask & EM_SNAPSHOT ) || !cl.snap.valid || ( cls.state != CA_ACTIVE && cls.state != CA_PRIMED ) ) {
		return;
	}
	if ( demo.numKeys && cl.snap.serverTime - demo.keys[ demo.numKeys - 1 ]->serverTime < demo.keyInterval ) {
		return;
	}
	Demo_CaptureKey();
}


// the last keyframe at or before the time
static const demoKey_t *Demo_FindKey( int time ) {
	int lo = 0, hi = demo.numKeys - 1;

	if ( hi < 0 ) {
		return NULL;
	}
	while ( lo < hi ) {
		const int mid = ( lo + hi + 1 ) / 2;

		if ( demo.keys[mid]->serverTime - time <= 0 ) {
			lo = mid;
		} else {
			hi = mid - 1;
		}
	}
	return demo.keys[lo];
}


static void Demo_RestoreKey( const demoKey_t *k ) {
	int i;

	FS_Seek( clc.demofile, k->filePos, FS_SEEK_SET );

	cl.snap = k->snap;
	Com_Memcpy( cl.snapshots, k->snapshots, sizeof( cl.snapshots ) );
	cl.gameState = k->gameState;
	Q_strncpyz( cl.mapname, k->mapname, sizeof( cl.mapname ) );
	cl.parseEntitiesNum = k->parseEntitiesNum;
	cl.serverId = k->serverId;
	for ( i = 0; i < k->entCount; i++ ) {
		cl.parseEntities[ ( k->entBase + i ) % MAX_PARSE_ENTITIES ] = k->ents[i];
	}

	clc.serverMessageSequence = k->messageSeq;
	clc.serverCommandSequence = k->cmdSeq;
	clc.lastExecutedServerCommand = k->cmdExec;
	for ( i = 0; i < k->numPending; i++ ) {
		const int index = ( k->cmdExec + 1 + i ) & ( MAX_RELIABLE_COMMANDS - 1 );

		Q_strncpyz( clc.serverCommands[index], k->pending[i], sizeof( clc.serverCommands[index] ) );
		clc.serverCommandsIgnore[index] = qfalse;
	}
	cl.newSnapshots = qfalse;
}


// a server command the cgame will run when it makes the snapshot current
static void Demo_QueueCommand( const char *s ) {
	const int index = ( ++clc.serverCommandSequence ) & ( MAX_RELIABLE_COMMANDS - 1 );

	Q_strncpyz( clc.serverCommands[index], s, sizeof( clc.serverCommands[index] ) );
	clc.serverCommandsIgnore[index] = qfalse;
}


// like SV_SendConfigstring: long strings go out in "bcs" chunks
static void Demo_QueueConfigstring( int index, const char *value ) {
	const int maxChunk = MAX_STRING_CHARS - 24;
	const int len = strlen( value );

	if ( len >= maxChunk ) {
		int sent = 0, remaining = len;

		while ( remaining > 0 ) {
			const char *cmd = sent == 0 ? "bcs0" : remaining < maxChunk ? "bcs2" : "bcs1";
			char buf[ MAX_STRING_CHARS ];

			Q_strncpyz( buf, value + sent, maxChunk );
			Demo_QueueCommand( va( "%s %i \"%s\"", cmd, index, buf ) );
			sent += maxChunk - 1;
			remaining -= maxChunk - 1;
		}
	} else {
		Demo_QueueCommand( va( "cs %i \"%s\"", index, value ) );
	}
}


/*
=================
Demo_SmoothSeek

Restore a keyframe if the target is behind us, parse forward to it, tell the cgame. Returns qfalse if the demo
ended on the way.
=================
*/
static qboolean Demo_SmoothSeek( int target ) {
	static gameState_t	view;
	const demoKey_t		*k;
	const int			oldTime = cl.serverTime;
	int					i, before;

	// what the cgame knows: the configstrings of the commands it has run
	view = cl.gameState;

	if ( target - cl.serverTime < 0 ) {
		k = Demo_FindKey( target );
		if ( !k ) {
			return qfalse;
		}
		Demo_RestoreKey( k );
	}

	CL_DemoRunServerCommands();
	while ( cl.snap.serverTime - target < 0 ) {
		CL_ReadDemoMessage();
		if ( cls.state != CA_ACTIVE ) {
			return qfalse;		// end of the demo
		}
		CL_DemoRunServerCommands();
	}

	// the cgame is behind or ahead by an unknown amount of configstring changes: give it the difference
	before = clc.serverCommandSequence;
	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		const char *now = cl.gameState.stringData + cl.gameState.stringOffsets[i];
		const char *was = view.stringData + view.stringOffsets[i];

		if ( strcmp( now, was ) ) {
			Demo_QueueConfigstring( i, now );
		}
	}
	// the cgame runs them as it makes the snapshot current; the engine already did
	clc.lastExecutedServerCommand = clc.serverCommandSequence;
	cl.snap.serverCommandNum = clc.serverCommandSequence;
	cl.snapshots[ cl.snap.messageNum & PACKET_MASK ].serverCommandNum = clc.serverCommandSequence;

	cl.serverTime = cl.oldServerTime = cl.oldFrameServerTime = cl.snap.serverTime;
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
	cl.extrapolatedSnapshot = qfalse;
	cl.newSnapshots = qfalse;

	CL_CGameTimeChange( cl.snap.serverTime, cl.snap.messageNum, before );
	CL_Plugins_DemoSeek( oldTime, cl.snap.serverTime );
	demo.quietFrame = cls.framecount + 1;
	Com_DPrintf( "demo seek %i -> %i (wanted %i), %i configstrings\n", oldTime, cl.snap.serverTime, target, clc.serverCommandSequence - before );
	return qtrue;
}


// serverTime of the snapshot before the one shown (the ring keeps the last PACKET_BACKUP); a bit earlier if it is gone
static int Demo_PrevSnapshotTime( void ) {
	int i, best = cl.serverTime - 1;
	qboolean found = qfalse;

	for ( i = 0; i < PACKET_BACKUP; i++ ) {
		const clSnapshot_t *s = &cl.snapshots[i];

		if ( !s->valid || cl.snap.messageNum - s->messageNum < 0 || cl.snap.messageNum - s->messageNum >= PACKET_BACKUP ) {
			continue;
		}
		if ( s->serverTime - cl.serverTime < 0 && ( !found || s->serverTime - best > 0 ) ) {
			best = s->serverTime;
			found = qtrue;
		}
	}
	return best;
}


/*
=================
Demo_SeekTo

serverTime target (clamped to the demo)
=================
*/
static void Demo_SeekTo( int target ) {
	if ( !demo.indexed ) {
		Com_Printf( "demo_seek: this demo cannot be seeked (%i gamestates)\n", demo.gamestates );
		return;
	}
	if ( clc.demorecording ) {
		Com_Printf( "demo_seek: not while recording\n" );
		return;
	}

	if ( target < demo.firstTime ) {
		target = demo.firstTime;
	}
	// stay away from the very end: running out of messages ends the demo
	if ( target > demo.lastTime - 50 ) {
		target = demo.lastTime - 50;
	}

	Demo_Touch();
	demo.target = target;

	if ( cgameSeekable && ( demo.numKeys || target - cl.serverTime >= 0 ) ) {
		// no restart: the cgame takes the jump
		demo.seeking = qfalse;
		if ( !Demo_SmoothSeek( target ) ) {
			Com_Printf( "demo_seek: the demo ended\n" );
		}
		return;
	}

	if ( target < cl.serverTime ) {
		// the cgame cannot go back in time: play the demo again from the start
		demo.restartPending = qtrue;
		demo.restartFrame = cls.framecount;
		demo.seeking = qfalse;
		demo.speed = com_timescale->value;
		demo.restoreSpeed = qtrue;
		Cbuf_AddText( va( "demo \"%s\"\n", demo.arg ) );
		return;
	}

	demo.startTime = cl.snap.serverTime;
	demo.seeking = qtrue;
}


/*
=================
Demo_ParsePos

"83.5", "1:23.5", "50%"; a leading + or - makes it relative to the current position
=================
*/
static qboolean Demo_ParsePos( const char *s, int *target ) {
	qboolean relative = qfalse, negative = qfalse, percent;
	float value;
	const char *colon;

	if ( *s == '+' || *s == '-' ) {
		relative = qtrue;
		negative = *s == '-';
		s++;
	}
	if ( !*s ) {
		return qfalse;
	}

	percent = s[ strlen( s ) - 1 ] == '%';
	colon = strchr( s, ':' );
	if ( colon ) {
		value = atof( s ) * 60.0f + atof( colon + 1 );
	} else {
		value = atof( s );
	}

	if ( percent ) {
		*target = demo.firstTime + (int)( ( demo.lastTime - demo.firstTime ) * value / 100.0f );
		return qtrue;
	}

	{
		const int ms = (int)( value * 1000.0f );
		if ( relative ) {
			*target = Demo_Playhead() + ( negative ? -ms : ms );
		} else {
			*target = demo.firstTime + ms;
		}
	}
	return qtrue;
}


static void Demo_Seek_f( void ) {
	int target;

	if ( !Demo_CanControl() ) {
		Com_Printf( "demo_seek: no demo is playing\n" );
		return;
	}
	if ( Cmd_Argc() != 2 || !Demo_ParsePos( Cmd_Argv( 1 ), &target ) ) {
		Com_Printf( "demo_seek <seconds | m:ss | +/-seconds | percent%%>\n" );
		return;
	}
	Demo_SeekTo( target );
}


static void Demo_SetPause( int mode ) {	// 0/1, -1 = toggle
	demo.paused = mode < 0 ? !demo.paused : mode != 0;
	Demo_Touch();
}


static void Demo_Pause_f( void ) {
	if ( !Demo_CanControl() ) {
		Com_Printf( "demo_pause: no demo is playing\n" );
		return;
	}
	Demo_SetPause( Cmd_Argc() > 1 ? atoi( Cmd_Argv( 1 ) ) : -1 );
}


/*
=================
Demo_SetSpeed

timescale is a cheat cvar and a systeminfo one (every gamestate resets it), so it is forced here.
=================
*/
static void Demo_SetSpeed( float speed ) {
	if ( speed < 0.05f ) {
		speed = 0.05f;
	} else if ( speed > 16.0f ) {
		speed = 16.0f;
	}
	Cvar_Set2( "timescale", va( "%g", speed ), qtrue );
	Demo_Touch();
}


static void Demo_StepSpeed( int dir ) {
	const int count = ARRAY_LEN( speedSteps );
	float cur = com_timescale->value;
	int i;

	if ( cur <= 0.0f ) {
		cur = 1.0f;
	}

	if ( dir > 0 ) {
		for ( i = 0; i < count; i++ ) {
			if ( speedSteps[i] > cur * 1.001f ) {
				break;
			}
		}
		Demo_SetSpeed( speedSteps[ i < count ? i : count - 1 ] );
	} else {
		for ( i = count - 1; i >= 0; i-- ) {
			if ( speedSteps[i] < cur * 0.999f ) {
				break;
			}
		}
		Demo_SetSpeed( speedSteps[ i >= 0 ? i : 0 ] );
	}
}


static void Demo_Speed_f( void ) {
	if ( !Demo_CanControl() ) {
		Com_Printf( "demo_speed: no demo is playing\n" );
		return;
	}
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "demo_speed <factor>, now %g\n", com_timescale->value );
		return;
	}
	Demo_SetSpeed( atof( Cmd_Argv( 1 ) ) );
}


static void Demo_MouseSeekDown_f( void ) {
	if ( !Demo_CanControl() ) {
		return;
	}
	if ( clc.demorecording ) {
		Com_Printf( "+mouseseek: not while recording\n" );
		return;
	}
	if ( !cgameSeekable ) {
		if ( !demo.scrubWarned ) {
			demo.scrubWarned = qtrue;
			Com_Printf( "+mouseseek needs a cgame that takes CG_ODF_TIME_CHANGE (oDFs); use demo_seek instead\n" );
		}
		return;
	}
	demo.scrubbing = qtrue;
	demo.scrubDelta = 0;
	demo.step = 0;
	demo.scrubTime = cl.serverTime;
	Demo_Touch();
}


static void Demo_MouseSeekUp_f( void ) {
	demo.scrubbing = qfalse;
	demo.scrubDelta = 0;
	demo.step = 0;
}


/*
=================
Timeline layout and cursor

Everything is kept in pixels; the virtual 640x480 rectangles go through SCR_AdjustFrom640 like the
drawing does, so the hit test matches what is on the screen on any aspect ratio.
=================
*/
static void Demo_Rect( float x, float y, float w, float h, float *out ) {
	SCR_AdjustFrom640( &x, &y, &w, &h );
	out[0] = x;
	out[1] = y;
	out[2] = w;
	out[3] = h;
}


static qboolean Demo_CursorEnabled( void ) {
	return cl_demoCursor->integer && cl_demoBar->integer && demo.indexed && !demo.scrubbing
		&& Demo_CanControl() && Key_GetCatcher() == 0;
}


static qboolean Demo_CursorAwake( void ) {
	return Demo_CursorEnabled() && ( demo.dragging || Sys_Milliseconds() - demo.cursorTime < BAR_SHOW_TIME );
}


static void Demo_UpdateDrag( void ) {
	float track[4];

	Demo_Rect( TRACK_X, TRACK_Y, TRACK_W, TRACK_H, track );
	demo.dragFrac = ( demo.curX - track[0] ) / track[2];
	if ( demo.dragFrac < 0.0f ) {
		demo.dragFrac = 0.0f;
	} else if ( demo.dragFrac > 1.0f ) {
		demo.dragFrac = 1.0f;
	}
}


/*
=================
CLDemo_MouseEvent

Relative mouse motion from CL_MouseEvent. Returns qtrue if it moved the timeline cursor.
=================
*/
qboolean CLDemo_MouseEvent( int dx, int dy ) {
	const float scale = cls.glconfig.vidHeight / 480.0f;	// same feel as the menu cursor

	if ( demo.scrubbing ) {
		demo.scrubDelta += dx;
		return qtrue;
	}

	if ( !Demo_CursorEnabled() ) {
		return qfalse;
	}

	if ( !demo.cursorInit ) {
		demo.curX = cls.glconfig.vidWidth * 0.5f;
		demo.curY = cls.glconfig.vidHeight * 0.5f;
		demo.cursorInit = qtrue;
	}

	demo.curX += dx * scale;
	demo.curY += dy * scale;
	if ( demo.curX < 0.0f ) demo.curX = 0.0f;
	if ( demo.curX > cls.glconfig.vidWidth - 1 ) demo.curX = cls.glconfig.vidWidth - 1;
	if ( demo.curY < 0.0f ) demo.curY = 0.0f;
	if ( demo.curY > cls.glconfig.vidHeight - 1 ) demo.curY = cls.glconfig.vidHeight - 1;

	demo.cursorTime = Sys_Milliseconds();
	if ( demo.dragging ) {
		Demo_UpdateDrag();
	}
	return qtrue;
}


/*
=================
CLDemo_KeyDown

Returns qtrue if the key was used. Only called while a demo plays and nothing else wants keys.
=================
*/
qboolean CLDemo_KeyDown( int key ) {
	if ( !Demo_CanControl() ) {
		return qfalse;
	}

	if ( key == K_MOUSE1 ) {
		float panel[4];

		if ( !Demo_CursorAwake() ) {
			return qfalse;
		}
		Demo_Rect( PANEL_X, PANEL_Y, PANEL_W, PANEL_H, panel );
		if ( demo.curX < panel[0] || demo.curX >= panel[0] + panel[2]
			|| demo.curY < panel[1] || demo.curY >= panel[1] + panel[3] ) {
			return qfalse;
		}
		demo.dragging = qtrue;
		Demo_UpdateDrag();
		return qtrue;
	}

	if ( !cl_demoKeys->integer ) {
		return qfalse;
	}

	switch ( key ) {
	case K_SPACE:
	case K_PAUSE:
		if ( keys[key].repeats == 1 ) {
			Demo_SetPause( -1 );
		}
		return qtrue;
	case K_LEFTARROW:
		if ( demo.scrubbing ) {
			// +mouseseek held: one snapshot back (the key repeat keeps stepping)
			demo.step = demo.step > -8 ? demo.step - 1 : demo.step;
			Demo_Touch();
			return qtrue;
		}
		// backward seeks restart the demo: no key repeat
		if ( keys[key].repeats == 1 ) {
			Demo_SeekTo( Demo_Playhead() - SEEK_STEP );
		}
		return qtrue;
	case K_RIGHTARROW:
		if ( demo.scrubbing ) {
			demo.step = demo.step < 8 ? demo.step + 1 : demo.step;
			Demo_Touch();
			return qtrue;
		}
		Demo_SeekTo( Demo_Playhead() + SEEK_STEP );
		return qtrue;
	case K_DOWNARROW:
		if ( keys[key].repeats == 1 ) {
			Demo_StepSpeed( -1 );
		}
		return qtrue;
	case K_UPARROW:
		if ( keys[key].repeats == 1 ) {
			Demo_StepSpeed( 1 );
		}
		return qtrue;
	case K_HOME:
		if ( keys[key].repeats == 1 ) {
			Demo_SeekTo( demo.firstTime );
		}
		return qtrue;
	}
	return qfalse;
}


/*
=================
CLDemo_KeyUp

Releasing the button ends a timeline drag and makes the seek.
=================
*/
qboolean CLDemo_KeyUp( int key ) {
	if ( key != K_MOUSE1 || !demo.dragging ) {
		return qfalse;
	}

	demo.dragging = qfalse;
	if ( Demo_CanControl() ) {
		Demo_SeekTo( demo.firstTime + (int)( ( demo.lastTime - demo.firstTime ) * demo.dragFrac ) );
	}
	return qtrue;
}


/*
=================
Demo_CatchUpStep

One cgame frame worth of messages. Returns qfalse if the demo is no longer active
(end of the file, new gamestate).
=================
*/
static qboolean Demo_CatchUpStep( void ) {
	const int startMsg = cl.snap.messageNum;
	const int startCmd = clc.serverCommandSequence;
	int n = 0;

	while ( cl.snap.serverTime - demo.target < 0 && n < CATCHUP_MSGS
		&& clc.serverMessageSequence - startMsg < CATCHUP_RING
		&& clc.serverCommandSequence - startCmd < CATCHUP_CMDS ) {
		CL_ReadDemoMessage();
		if ( cls.state != CA_ACTIVE ) {
			return qfalse;
		}
		n++;
	}

	// the cgame reads everything between its last snapshot and this one, in order
	cl.serverTime = cl.oldServerTime = cl.oldFrameServerTime = cl.snap.serverTime;
	cl.newSnapshots = qfalse;
	CL_CGameCatchUp();
	return qtrue;
}


/*
=================
CLDemo_Frame

Called by CL_SetCGameTime for every frame of an active demo. Returns qtrue if the frame was
used up by a seek (nothing else may touch the demo time).
=================
*/
qboolean CLDemo_Frame( void ) {
	int start;

	if ( demo.restoreSpeed ) {
		// the new gamestate reset timescale
		demo.restoreSpeed = qfalse;
		if ( demo.speed != com_timescale->value ) {
			Cvar_Set2( "timescale", va( "%g", demo.speed ), qtrue );
		}
	}

	if ( demo.scrubbing && clc.demorecording ) {
		demo.scrubbing = qfalse;
		demo.scrubDelta = 0;
		demo.step = 0;
	}

	if ( demo.scrubbing && demo.step ) {
		const int dir = demo.step > 0 ? 1 : -1;

		demo.step -= dir;
		if ( dir > 0 ) {
			// the snapshot after the one shown has been read already: just let the time reach it; the normal part of this
			// frame reads the one after. No time change for the cgame, so the effects of the moment go on
			if ( cl.snap.serverTime - cl.serverTime <= 0 ) {
				// the next snapshot is not read yet (right after a seek): the normal part of this frame reads it, step then
				demo.step += dir;
				return qfalse;
			}
			if ( cl.snap.serverTime - ( demo.lastTime - 50 ) < 0 ) {
				cl.serverTime = cl.oldServerTime = cl.snap.serverTime;
				cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
				cl.extrapolatedSnapshot = qfalse;
				demo.quietFrame = cls.framecount + 1;
				Demo_Touch();
			}
			demo.scrubTime = cl.serverTime;
			return qfalse;
		}
		Demo_SeekTo( Demo_PrevSnapshotTime() );
		demo.scrubTime = cl.serverTime;
		return qtrue;
	}

	if ( demo.scrubbing && demo.scrubDelta ) {
		const int delta = demo.scrubDelta;

		demo.scrubDelta = 0;
		if ( delta ) {
			// accumulate here: the playhead lands on a snapshot, which can be further apart than a mouse count is long
			demo.scrubTime += delta * cl_demoMouseSeek->value;
			if ( demo.scrubTime < demo.firstTime ) {
				demo.scrubTime = demo.firstTime;
			} else if ( demo.scrubTime > demo.lastTime - 50 ) {
				demo.scrubTime = demo.lastTime - 50;
			}
			Demo_SeekTo( (int)demo.scrubTime );
		}
		return qtrue;
	}

	if ( demo.restartPending ) {
		if ( cls.framecount != demo.restartFrame ) {
			// the "demo" command did not start (file gone?), keep playing
			demo.restartPending = qfalse;
			return qfalse;
		}
		return qtrue;
	}

	if ( !demo.seeking ) {
		return qfalse;
	}

	start = Sys_Milliseconds();
	do {
		if ( cl.snap.serverTime - demo.target >= 0 ) {
			break;
		}
		if ( !Demo_CatchUpStep() ) {
			// demo ended or changed: nothing left to seek in
			demo.seeking = qfalse;
			return qtrue;
		}
	} while ( Sys_Milliseconds() - start < CATCHUP_BUDGET );

	if ( cl.snap.serverTime - demo.target >= 0 ) {
		// arrived: continue normal playback from this snapshot
		demo.seeking = qfalse;
		CL_Plugins_DemoSeek( demo.startTime, cl.snap.serverTime );
		cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
		cl.extrapolatedSnapshot = qfalse;
		S_StopAllSounds();
	}
	return qtrue;
}


/*
=================
CLDemo_DrawBar
=================
*/
void CLDemo_DrawBar( void ) {
	static const vec4_t colBack = { 0.0f, 0.0f, 0.0f, 0.55f };
	static const vec4_t colTrack = { 0.35f, 0.35f, 0.35f, 0.9f };
	static const vec4_t colFill = { 0.2f, 0.75f, 1.0f, 0.95f };
	static const vec4_t colWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
	static const vec4_t colBlack = { 0.0f, 0.0f, 0.0f, 1.0f };
	static qhandle_t cursorShader, cursorBack, cursorFront;
	static int cursorShaderRestart = -1;
	const qboolean awake = Demo_CursorAwake();
	char cur[ 32 ], total[ 32 ], line[ 96 ];
	const char *state;
	float frac;
	int shown;

	if ( !demo.indexed || !clc.demoplaying || cls.state != CA_ACTIVE ) {
		return;
	}
	if ( cl_demoBar->integer == 0 ) {
		return;
	}
	if ( cl_demoBar->integer < 2 && !awake && !demo.paused && !demo.scrubbing && !demo.seeking && !demo.restartPending
		&& Sys_Milliseconds() - demo.barUntil > 0 ) {
		return;
	}

	// while dragging show the drag position; while catching up the cgame shows the old position
	// until it arrives, so show the target
	if ( demo.dragging ) {
		frac = demo.dragFrac;
		shown = demo.firstTime + (int)( ( demo.lastTime - demo.firstTime ) * frac );
	} else {
		shown = demo.seeking ? demo.target : cl.serverTime;
		frac = (float)( shown - demo.firstTime ) / (float)( demo.lastTime - demo.firstTime );
		if ( frac < 0.0f ) frac = 0.0f;
		if ( frac > 1.0f ) frac = 1.0f;
	}

	Demo_FormatTime( cur, sizeof( cur ), shown - demo.firstTime );
	Demo_FormatTime( total, sizeof( total ), demo.lastTime - demo.firstTime );

	if ( demo.seeking ) {
		const int span = demo.target - demo.startTime;
		const int done = span > 0 ? ( cl.snap.serverTime - demo.startTime ) * 100 / span : 100;
		state = va( "SEEKING %i%%", done < 0 ? 0 : done > 100 ? 100 : done );
	} else if ( demo.paused ) {
		state = "PAUSED";
	} else {
		state = "";
	}
	if ( fabs( com_timescale->value - 1.0f ) > 0.001f ) {
		Com_sprintf( line, sizeof( line ), "%s / %s   x%g   %s", cur, total, com_timescale->value, state );
	} else {
		Com_sprintf( line, sizeof( line ), "%s / %s   %s", cur, total, state );
	}

	SCR_FillRect( PANEL_X, PANEL_Y, PANEL_W, PANEL_H, colBack );
	SCR_FillRect( TRACK_X, TRACK_Y, TRACK_W, TRACK_H, colTrack );
	SCR_FillRect( TRACK_X, TRACK_Y, TRACK_W * frac, TRACK_H, colFill );
	SCR_FillRect( TRACK_X + TRACK_W * frac - 2, TRACK_Y - 3, 4, TRACK_H + 6, colWhite );
	SCR_DrawStringExt( 40, 449, 8, line, colWhite, qtrue, qtrue );

	if ( awake ) {
		const float size = CURSOR_SIZE * ( cls.glconfig.vidHeight / 480.0f );
		const float x = demo.curX - size * 0.5f, y = demo.curY - size * 0.5f;

		if ( cursorShaderRestart != cls.lastVidRestart ) {
			cursorBack = re.RegisterShaderNoMip( "df_gfx/menu/cursor_back" );
			cursorFront = re.RegisterShaderNoMip( "df_gfx/menu/cursor_front" );
			cursorShader = re.RegisterShaderNoMip( "menu/art/3_cursor2" );
			cursorShaderRestart = cls.lastVidRestart;
		}
		if ( cursorBack && cursorFront ) {
			// the cursor of the DeFRaG menus (UI_DF_DrawCursor): two layers tinted with df_ui_CursorColor, the back one
			// white while the pointer is over something usable
			vec4_t color = { 0.50f, 0.80f, 1.00f, 1.00f };
			const char *p = Cvar_VariableString( "df_ui_CursorColor" );
			float panel[4];
			int i;

			for ( i = 0; i < 4; i++ ) {
				const char *token = COM_Parse( &p );

				if ( !token[0] ) {
					break;
				}
				color[i] = atof( token );
			}
			Demo_Rect( PANEL_X, PANEL_Y, PANEL_W, PANEL_H, panel );
			if ( demo.curX >= panel[0] && demo.curX < panel[0] + panel[2] && demo.curY >= panel[1] && demo.curY < panel[1] + panel[3] ) {
				re.SetColor( colWhite );
			} else {
				re.SetColor( color );
			}
			re.DrawStretchPic( x, y, size, size, 0, 0, 1, 1, cursorBack );
			re.SetColor( color );
			re.DrawStretchPic( x, y, size, size, 0, 0, 1, 1, cursorFront );
			re.SetColor( NULL );
		} else if ( cursorShader ) {
			// the stock cursor, as the menus fall back to
			re.SetColor( NULL );
			re.DrawStretchPic( x, y, size, size, 0, 0, 1, 1, cursorShader );
		} else {
			// no menu art: a white block with an outline
			re.SetColor( colBlack );
			re.DrawStretchPic( demo.curX - 1, demo.curY - 1, 8, 8, 0, 0, 0, 0, cls.whiteShader );
			re.SetColor( colWhite );
			re.DrawStretchPic( demo.curX, demo.curY, 6, 6, 0, 0, 0, 0, cls.whiteShader );
			re.SetColor( NULL );
		}
	}
}


void CLDemo_Init( void ) {
	cl_demoKeys = Cvar_Get( "cl_demoKeys", "1", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_demoKeys, "Demo playback keys: SPACE pause, LEFT/RIGHT seek +-5 s, DOWN/UP playback speed, HOME restart." );
	cl_demoBar = Cvar_Get( "cl_demoBar", "1", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_demoBar, "Demo timeline: 0 - never, 1 - while paused / seeking / mouse moved, 2 - always." );
	cl_demoCursor = Cvar_Get( "cl_demoCursor", "1", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_demoCursor, "Show a mouse cursor during demo playback; click or drag the timeline to seek." );

	cl_demoMouseSeek = Cvar_Get( "cl_demoMouseSeek", "10", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_demoMouseSeek, "+mouseseek: milliseconds of demo time per mouse count (negative reverses the direction). LEFT / RIGHT while it is held step one snapshot." );

	Cmd_AddCommand( "+mouseseek", Demo_MouseSeekDown_f );
	Cmd_AddCommand( "-mouseseek", Demo_MouseSeekUp_f );
	Cmd_AddCommand( "demo_seek", Demo_Seek_f );
	Cmd_AddCommand( "demo_pause", Demo_Pause_f );
	Cmd_AddCommand( "demo_speed", Demo_Speed_f );
}
