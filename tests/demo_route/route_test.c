/*
Tests code/client/cl_demoroute.c natively: the route of a recording that is put back in time becomes one demo whose message numbers,
server command numbers and times have no gap. The real message code of the engine (msg.c, huffman) writes and reads the messages.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../code/client/cl_demoroute.h"

static int failures, checks;
#define CHECK( cond, ... ) do { checks++; if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

// ---- the engine around it: files on the disk, memory ----

#define MAX_FILES 8
static FILE *files[MAX_FILES + 1];
static char names[MAX_FILES + 1][128];

static fileHandle_t NewHandle( FILE *f, const char *name )
{
	int i;

	for ( i = 1; i <= MAX_FILES; i++ ) {
		if ( !files[i] ) {
			files[i] = f;
			strncpy( names[i], name, 127 );
			return i;
		}
	}
	return 0;
}

static void MakeDirs( void )
{
	// run.sh makes build/demos/temp
}

fileHandle_t FS_FOpenFileWrite( const char *qpath )
{
	char path[256];
	FILE *f;

	MakeDirs();
	snprintf( path, sizeof( path ), "build/%s", qpath );
	f = fopen( path, "wb" );
	return f ? NewHandle( f, path ) : 0;
}

int FS_FOpenFileRead( const char *qpath, fileHandle_t *file, qboolean unique )
{
	char path[256];
	FILE *f;

	(void)unique;
	snprintf( path, sizeof( path ), "build/%s", qpath );
	f = fopen( path, "rb" );
	if ( !f ) {
		*file = 0;
		return -1;
	}
	*file = NewHandle( f, path );
	return 1;
}

int FS_Write( const void *buffer, int len, fileHandle_t f ) { return (int)fwrite( buffer, 1, len, files[f] ); }
int FS_Read( void *buffer, int len, fileHandle_t f ) { return (int)fread( buffer, 1, len, files[f] ); }
int FS_Seek( fileHandle_t f, long offset, fsOrigin_t origin ) { return fseek( files[f], offset, origin == FS_SEEK_SET ? SEEK_SET : origin == FS_SEEK_CUR ? SEEK_CUR : SEEK_END ); }
void FS_Flush( fileHandle_t f ) { fflush( files[f] ); }
void FS_FCloseFile( fileHandle_t f ) { fclose( files[f] ); files[f] = NULL; }
void FS_HomeRemove( const char *homePath ) { char path[256]; snprintf( path, sizeof( path ), "build/%s", homePath ); remove( path ); }

cvar_t *cl_shownet;
void *Z_Malloc( size_t size ) { return calloc( 1, size ); }
void Z_Free( void *p ) { free( p ); }
void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_Error( errorParm_t code, const char *fmt, ... ) { (void)code; printf( "Com_Error: %s\n", fmt ); exit( 2 ); }
void QDECL Com_DPrintf( const char *fmt, ... ) { (void)fmt; }

// ---- messages ----

typedef struct {
	int		seq;			// number of the command
	const char *text;
} cmd_t;

// a message of the server: the acknowledge, the commands, a snapshot (time, delta, some payload) and the end
static int MakeMessage( byte *buf, int ack, const cmd_t *cmds, int numCmds, int time, int deltaNum, int payloadSeed )
{
	msg_t m;
	int i;

	MSG_Init( &m, buf, MAX_MSGLEN );
	MSG_Bitstream( &m );
	MSG_WriteLong( &m, ack );
	for ( i = 0; i < numCmds; i++ ) {
		MSG_WriteByte( &m, svc_serverCommand );
		MSG_WriteLong( &m, cmds[i].seq );
		{
			const char *t = cmds[i].text;

			do {
				MSG_WriteByte( &m, (byte)*t );		// MSG_WriteString would change the percent and the high characters
			} while ( *t++ );
		}
	}
	MSG_WriteByte( &m, svc_snapshot );
	MSG_WriteLong( &m, time );
	MSG_WriteByte( &m, deltaNum );
	MSG_WriteLong( &m, payloadSeed );
	for ( i = 0; i < 20; i++ ) {
		MSG_WriteByte( &m, ( payloadSeed * 7 + i * 13 ) & 0xff );
	}
	MSG_WriteByte( &m, svc_EOF );
	return m.cursize;
}

// what the player of the demo gets out of it
typedef struct {
	int		seq, time, delta, payload;
	int		numCmds;
	cmd_t	cmds[8];
	char	text[8][64];
} frame_t;

static int ReadDemo( const char *path, frame_t *out, int max )
{
	FILE *f = fopen( path, "rb" );
	int n = 0;

	if ( !f ) {
		return -1;
	}
	for ( ;; ) {
		int head[2];
		static byte body[MAX_MSGLEN_BUF];
		msg_t m;
		frame_t *fr;
		int c;

		if ( fread( head, 4, 2, f ) != 2 || head[1] < 0 || n >= max ) {
			break;
		}
		if ( fread( body, 1, head[1], f ) != (size_t)head[1] ) {
			break;
		}
		fr = &out[n++];
		memset( fr, 0, sizeof( *fr ) );
		fr->seq = head[0];
		MSG_Init( &m, body, MAX_MSGLEN );
		m.cursize = head[1];
		MSG_Bitstream( &m );
		MSG_ReadLong( &m );
		for ( ;; ) {
			c = MSG_ReadByte( &m );
			if ( c == svc_serverCommand ) {
				fr->cmds[fr->numCmds].seq = MSG_ReadLong( &m );
				{
					int k = 0;

					while ( ( c = MSG_ReadByte( &m ) ) > 0 && k < 63 ) {
						fr->text[fr->numCmds][k++] = (char)c;
					}
				}
				fr->cmds[fr->numCmds].text = fr->text[fr->numCmds];
				fr->numCmds++;
			} else if ( c == svc_snapshot ) {
				fr->time = MSG_ReadLong( &m );
				fr->delta = MSG_ReadByte( &m );
				fr->payload = MSG_ReadLong( &m );
				break;
			} else {
				break;		// the header message
			}
		}
	}
	fclose( f );
	return n;
}

// The state of the live game: message and command numbers as the client had them
static int liveSeq, liveCmd, liveTime;

// a message of the live game with n new commands (and the commands that were not acknowledged yet, as the server sends them)
static void Live( int newCmds, const char *prefix, int resend, int deltaNum )
{
	static byte buf[MAX_MSGLEN_BUF];
	cmd_t cmds[8];
	int n = 0, i, pre = liveCmd, len;
	char texts[8][32];

	for ( i = 0; i < resend; i++ ) {
		snprintf( texts[n], 32, "old %d", liveCmd - resend + 1 + i );
		cmds[n].seq = liveCmd - resend + 1 + i;
		cmds[n].text = texts[n];
		n++;
	}
	for ( i = 0; i < newCmds; i++ ) {
		liveCmd++;
		snprintf( texts[n], 32, "%s%d %%s\x90", prefix, liveCmd );		// a percent and a high character: must stay as they are
		cmds[n].seq = liveCmd;
		cmds[n].text = texts[n];
		n++;
	}
	liveSeq++;
	liveTime += 32;
	len = MakeMessage( buf, 1, cmds, n, liveTime, deltaNum, liveSeq );
	DemoRoute_Frame( liveSeq, buf, len, pre, liveCmd );
}

int main( void )
{
	static frame_t demo[200];
	static byte buf[MAX_MSGLEN_BUF], out[MAX_MSGLEN_BUF];
	fileHandle_t f;
	int i, n, len, handle = 0;
	int cpA, cpB, cpC;

	// ---- Renumber on its own ----
	{
		cmd_t cmds[2] = { { 40, "print \"hello\"" }, { 41, "cs 5 \"100% \x90\"" } };
		msg_t m;
		int newLen, c;

		len = MakeMessage( buf, 7, cmds, 2, 1234, 1, 9 );
		newLen = DemoRoute_Renumber( buf, len, 30, out, MAX_MSGLEN );
		CHECK( newLen > 0 && newLen <= len + 2, "renumbered length %d (was %d)", newLen, len );
		MSG_Init( &m, out, MAX_MSGLEN );
		m.cursize = newLen;
		MSG_Bitstream( &m );
		CHECK( MSG_ReadLong( &m ) == 7, "acknowledge" );
		CHECK( MSG_ReadByte( &m ) == svc_serverCommand && MSG_ReadLong( &m ) == 10 && !strcmp( MSG_ReadString( &m ), "print \"hello\"" ), "first command" );
		CHECK( MSG_ReadByte( &m ) == svc_serverCommand && MSG_ReadLong( &m ) == 11, "second command" );
		{
			const char *want = cmds[1].text;		// read byte by byte: MSG_ReadString would change the percent and the high character

			for ( c = 0; c <= (int)strlen( want ); c++ ) {
				if ( MSG_ReadByte( &m ) != (byte)want[c] ) {
					CHECK( 0, "the text of the command changed at %d", c );
					break;
				}
			}
		}
		c = MSG_ReadByte( &m );
		CHECK( c == svc_snapshot && MSG_ReadLong( &m ) == 1234 && MSG_ReadByte( &m ) == 1 && MSG_ReadLong( &m ) == 9, "the snapshot is as it was" );
		// without commands
		len = MakeMessage( buf, 3, NULL, 0, 99, 0, 4 );
		newLen = DemoRoute_Renumber( buf, len, 5, out, MAX_MSGLEN );
		MSG_Init( &m, out, MAX_MSGLEN );
		m.cursize = newLen;
		MSG_Bitstream( &m );
		MSG_ReadLong( &m );
		CHECK( MSG_ReadByte( &m ) == svc_snapshot && MSG_ReadLong( &m ) == 99, "no commands" );
	}

	// ---- a recording with checkpoints and loads ----
	liveSeq = 100; liveCmd = 50; liveTime = 8000;
	CHECK( DemoRoute_Start(), "start" );
	{
		cmd_t gs = { 0, "" };
		int l = MakeMessage( buf, 1, &gs, 0, 0, 0, 0 );		// the gamestate stands in for a message without a snapshot
		DemoRoute_Header( liveSeq, buf, l, liveCmd );
	}
	// a first message that comes after dropped messages (the recording waited for a whole snapshot): the numbers jump
	liveSeq += 3; liveCmd += 2;
	Live( 1, "a", 0, 0 );
	for ( i = 0; i < 9; i++ ) {
		Live( 1, "a", 0, 1 );
	}
	// checkpoint A, the game goes on
	DemoRoute_Created( cpA = 11 );
	for ( i = 0; i < 5; i++ ) {
		Live( 1, "b", 0, 1 );
	}
	// checkpoint B and a try that is given up
	DemoRoute_Created( cpB = 12 );
	for ( i = 0; i < 6; i++ ) {
		Live( 1, "x", 0, 1 );
	}
	// load A: the game is put back, the first message of the new timeline is whole and has the commands that were not acknowledged
	{
		const int sizeBefore = DemoRoute_Size();

		CHECK( DemoRoute_Applied( cpA ), "apply A" );
		CHECK( DemoRoute_Size() < sizeBefore, "the segment that was given up is not part of the route (%d, was %d)", DemoRoute_Size(), sizeBefore );
	}
	liveSeq += 5;			// messages of the old timeline that were dropped
	liveTime = 8000 + 32 * 10 + 8;
	liveCmd += 3;			// commands the old timeline sent that the client has not seen
	Live( 2, "marker", 2, 0 );
	for ( i = 0; i < 4; i++ ) {
		Live( 1, "c", 0, 1 );
	}
	DemoRoute_Created( cpC = 13 );
	for ( i = 0; i < 3; i++ ) {
		Live( 1, "d", 0, 1 );
	}
	// a snapshot that was made when nothing was recorded is not known
	CHECK( !DemoRoute_Applied( 77 ), "an unknown snapshot" );

	f = FS_FOpenFileWrite( "demos/temp/out.dm_68" );
	n = DemoRoute_Export( f );
	FS_FCloseFile( f );
	CHECK( n == 1 + 10 + 5 + 3, "messages written: %d", n );
	n = ReadDemo( "build/demos/temp/out.dm_68", demo, 200 );
	CHECK( n == 1 + 10 + 5 + 3, "messages read: %d", n );

	// the messages: numbers go up by one from the header on, the time never goes back, every command is new and one more than the one before
	{
		int cmdNo = demo[0].cmds[0].seq, bad = 0, prevTime = -1, texts = 0;
		const char *marker = NULL;

		for ( i = 1; i < n; i++ ) {
			int k;

			if ( demo[i].seq != demo[i - 1].seq + ( i == 1 ? 3 : 1 ) ) {
				// the first message after the header may have a gap (dropped messages)
				if ( i != 1 ) {
					bad++;
					printf( "    message %d: number %d after %d\n", i, demo[i].seq, demo[i - 1].seq );
				}
			}
			if ( demo[i].time < prevTime ) {
				bad++;
				printf( "    message %d: time %d after %d\n", i, demo[i].time, prevTime );
			}
			prevTime = demo[i].time;
			for ( k = 0; k < demo[i].numCmds; k++ ) {
				if ( demo[i].cmds[k].seq > cmdNo ) {
					if ( demo[i].cmds[k].seq != cmdNo + 1 && i > 1 ) {
						bad++;
						printf( "    message %d: command %d after %d\n", i, demo[i].cmds[k].seq, cmdNo );
					}
					cmdNo = demo[i].cmds[k].seq;
					texts++;
					if ( !strncmp( demo[i].text[k], "marker", 6 ) ) {
						marker = demo[i].text[k];
						if ( !strstr( marker, "%s\x90" ) ) {
							bad++;		// the text of a renumbered command is not what it was
							printf( "    text '%s'\n", marker );
						}
					}
				}
			}
		}
		CHECK( bad == 0, "numbers and time are continuous (%d problems)", bad );
		CHECK( marker != NULL, "the command of the new timeline is in the demo" );
		(void)texts;
	}
	// the order of the parts: a, b, then c, d (x was given up)
	{
		int seenX = 0, seenB = 0, seenC = 0;

		for ( i = 0; i < n; i++ ) {
			int k;

			for ( k = 0; k < demo[i].numCmds; k++ ) {
				seenX += demo[i].text[k][0] == 'x';
				if ( demo[i].text[k][0] == 'b' ) { seenB++; }
				if ( demo[i].text[k][0] == 'c' || !strncmp( demo[i].text[k], "marker", 6 ) ) { seenC++;  }
			}
		}
		CHECK( seenX == 0, "the attempt that was given up is not in the demo" );
		CHECK( seenB == 0 && seenC == 4 + 2, "route: a, then the new timeline without the way to B (%d, %d)", seenB, seenC );
	}
	// the delta number of the first message of the new timeline is 0 (whole)
	{
		int whole = 0;

		for ( i = 1; i < n; i++ ) {
			if ( !strncmp( demo[i].numCmds ? demo[i].text[demo[i].numCmds - 1] : "", "marker", 6 ) ) {
				whole = demo[i].delta == 0;
			}
		}
		CHECK( whole, "the new timeline starts with a whole snapshot" );
	}

	// ---- the checkpoint B is on a branch that is still needed; load it later ----
	{
		const int sizeA = DemoRoute_Size();

		CHECK( DemoRoute_Applied( cpB ), "apply B (made on the branch that was left)" );
		CHECK( DemoRoute_Size() > 0 && DemoRoute_Size() < sizeA + 5000, "route to B has a size: %d", DemoRoute_Size() );
		f = FS_FOpenFileWrite( "demos/temp/out2.dm_68" );
		n = DemoRoute_Export( f );
		FS_FCloseFile( f );
		CHECK( n == 1 + 10 + 5, "route to B: %d messages (header, a, b)", n );
		n = ReadDemo( "build/demos/temp/out2.dm_68", demo, 200 );
		CHECK( n == 16 && demo[15].time > demo[14].time, "route to B reads back" );
	}

	// ---- snapshots that are freed: what only they needed goes ----
	DemoRoute_Freed( cpC );			// C: the segment that ended with it has no snapshot and nothing after it now
	DemoRoute_Freed( cpB );			// B is the one that the recording goes on from: the segment before it stays while it is written
	{
		const int s = DemoRoute_Size();

		CHECK( s > 0, "the route that is written stays (%d)", s );
	}
	DemoRoute_Freed( cpA );
	f = FS_FOpenFileWrite( "demos/temp/out3.dm_68" );
	n = DemoRoute_Export( f );
	FS_FCloseFile( f );
	CHECK( n == 16, "route to B after A and C were freed: %d messages", n );

	// ---- a restart: a new recording on its own ----
	DemoRoute_Restart();
	{
		cmd_t gs = { 0, "" };
		int l = MakeMessage( buf, 1, &gs, 0, 0, 0, 0 );

		DemoRoute_Header( 500, buf, l, 70 );
		liveSeq = 502; liveCmd = 70; liveTime = 40000;
		Live( 1, "r", 0, 0 );
		Live( 1, "r", 0, 1 );
	}
	f = FS_FOpenFileWrite( "demos/temp/out4.dm_68" );
	n = DemoRoute_Export( f );
	FS_FCloseFile( f );
	CHECK( n == 3, "restarted recording: %d messages", n );

	DemoRoute_Stopped();
	CHECK( !DemoRoute_Active(), "stopped" );
	DemoRoute_Shutdown();
	{
		FILE *g = fopen( "build/demos/temp/segments.store", "rb" );

		CHECK( g == NULL, "the store file is gone" );
		if ( g ) {
			fclose( g );
		}
	}
	(void)handle;

	// ---- the names a plugin gives ----
	{
		char out[200];
		static const char *bad[] = { "", "/abs", ".hidden", "a/../b", "a..b", "a//b", "a/./b", "dir/", "has space", "c:x", "back\\slash", "a*b", "a?b", "a|b", "q\"x" };
		int i;

		for ( i = 0; i < (int)( sizeof( bad ) / sizeof( bad[0] ) ); i++ ) {
			CHECK( !DemoRoute_CleanName( bad[i], out, sizeof( out ) ), "refused: '%s'", bad[i] );
		}
		CHECK( DemoRoute_CleanName( "st1[tas.cpm]0.07.468", out, sizeof( out ) ) && !strcmp( out, "st1[tas.cpm]0.07.468" ), "tas as a word stays: '%s'", out );
		CHECK( DemoRoute_CleanName( "st1[mdf.cpm]0.07.468", out, sizeof( out ) ) && !strcmp( out, "st1[mdf.cpm]0.07.468[tas]" ), "tas is added: '%s'", out );
		CHECK( DemoRoute_CleanName( "st1[mdf.cpm]0.07.468.dm_68", out, sizeof( out ) ) && !strcmp( out, "st1[mdf.cpm]0.07.468[tas]" ), "the extension goes: '%s'", out );
		CHECK( DemoRoute_CleanName( "strategy[mdf.cpm]", out, sizeof( out ) ) && !strcmp( out, "strategy[mdf.cpm][tas]" ), "tas inside a word is no tag: '%s'", out );
		CHECK( DemoRoute_CleanName( "stas_run", out, sizeof( out ) ) && strstr( out, "[tas]" ), "tas at the end of a word: '%s'", out );
		CHECK( DemoRoute_CleanName( "TAS-run", out, sizeof( out ) ) && !strcmp( out, "TAS-run" ), "upper case: '%s'", out );
		CHECK( DemoRoute_CleanName( "practice/st1[tas.cpm]1", out, sizeof( out ) ) && !strcmp( out, "practice/st1[tas.cpm]1" ), "a folder: '%s'", out );
		CHECK( DemoRoute_CleanName( "tasfolder/run", out, sizeof( out ) ) && !strcmp( out, "tasfolder/run[tas]" ), "tas in the folder does not count: '%s'", out );
		CHECK( !DemoRoute_CleanName( "0123456789012345678901234567890123456789", out, 40 ), "too long for the buffer" );
	}

	printf( "demo_route: %s (%d checks, %d failed)\n", failures ? "FAILED" : "ok", checks, failures );
	return failures ? 1 : 0;
}
