/*
===========================================================================
cl_demoroute.c -- demos of a game that is put back in time, see cl_demoroute.h
===========================================================================
*/

#include "cl_demoroute.h"

#define STORE_NAME		"demos/temp/segments.store"
#define STORE_MAX		0x7ff00000		// offsets are ints

typedef struct {
	qboolean	used;
	int			parent;			// the segment before this one, -1 for the first
	int			children;		// segments that come after this one
	int			handle;			// the game snapshot that ends this segment, 0 if there is none
	int			off, len;		// where the messages are in the store
	int			msgs;
	int			firstSeq, lastSeq;		// message numbers
	int			firstPre, lastPost;		// server command numbers
} seg_t;

static struct {
	seg_t			*seg;
	int				num, cap;
	fileHandle_t	store;
	int				storePos;		// where the write handle is
	int				storeEnd;		// the end of the data, and where the next message goes
	int				cur;			// the segment that is written, -1 when nothing is recorded
	qboolean		failed;			// the store could not be written: the recording is lost
} dr = { NULL, 0, 0, FS_INVALID_HANDLE, 0, 0, -1, qfalse };


// ---------------------------------------------------------------------------
// the segments
// ---------------------------------------------------------------------------

static seg_t *Seg( int i )
{
	return i >= 0 && i < dr.num && dr.seg[i].used ? &dr.seg[i] : NULL;
}

static int NewSeg( int parent )
{
	seg_t *s;
	int i;

	for ( i = 0; i < dr.num; i++ ) {
		if ( !dr.seg[i].used ) {
			break;
		}
	}
	if ( i == dr.num ) {
		if ( dr.num == dr.cap ) {
			const int cap = dr.cap ? dr.cap * 2 : 64;
			seg_t *grown = Z_Malloc( cap * (int)sizeof( seg_t ) );

			if ( dr.seg ) {
				Com_Memcpy( grown, dr.seg, dr.num * sizeof( seg_t ) );
				Z_Free( dr.seg );
			}
			dr.seg = grown;
			dr.cap = cap;
		}
		dr.num++;
	}
	s = &dr.seg[i];
	Com_Memset( s, 0, sizeof( *s ) );
	s->used = qtrue;
	s->parent = parent;
	s->off = dr.storeEnd;
	if ( Seg( parent ) ) {
		dr.seg[parent].children++;
	}
	return i;
}

static int FindHandle( int handle )
{
	int i;

	if ( handle <= 0 ) {
		return -1;
	}
	for ( i = 0; i < dr.num; i++ ) {
		if ( dr.seg[i].used && dr.seg[i].handle == handle ) {
			return i;
		}
	}
	return -1;
}

// a segment is needed as long as a snapshot ends it, another segment follows it, or it is the one that is written
static qboolean Needed( int i )
{
	return dr.seg[i].handle || dr.seg[i].children || i == dr.cur;
}

static void RecomputeEnd( void )
{
	int i, end = 0;

	for ( i = 0; i < dr.num; i++ ) {
		if ( dr.seg[i].used && dr.seg[i].off + dr.seg[i].len > end ) {
			end = dr.seg[i].off + dr.seg[i].len;
		}
	}
	dr.storeEnd = end;
}

// removes a segment that nothing needs, and the ones before it that are not needed then; the store is cut back where the data ends
static void Prune( int i )
{
	qboolean removed = qfalse;

	while ( Seg( i ) && !Needed( i ) ) {
		const int parent = dr.seg[i].parent;

		dr.seg[i].used = qfalse;
		if ( Seg( parent ) ) {
			dr.seg[parent].children--;
		}
		removed = qtrue;
		i = parent;
	}
	if ( removed ) {
		RecomputeEnd();
	}
}

static qboolean StoreWrite( const void *data, int len )
{
	if ( dr.failed || dr.store == FS_INVALID_HANDLE || (unsigned)dr.storeEnd + len > STORE_MAX ) {
		dr.failed = qtrue;
		return qfalse;
	}
	if ( dr.storePos != dr.storeEnd ) {
		FS_Seek( dr.store, dr.storeEnd, FS_SEEK_SET );
		dr.storePos = dr.storeEnd;
	}
	if ( FS_Write( data, len, dr.store ) != len ) {
		Com_Printf( S_COLOR_YELLOW "Demo route: could not write %s.\n", STORE_NAME );
		dr.failed = qtrue;
		return qfalse;
	}
	dr.storePos += len;
	dr.storeEnd += len;
	return qtrue;
}


// ---------------------------------------------------------------------------
// recording
// ---------------------------------------------------------------------------

qboolean DemoRoute_Start( void )
{
	if ( dr.store == FS_INVALID_HANDLE ) {
		dr.store = FS_FOpenFileWrite( STORE_NAME );
		if ( dr.store == FS_INVALID_HANDLE ) {
			Com_Printf( S_COLOR_YELLOW "Demo route: could not open %s.\n", STORE_NAME );
			return qfalse;
		}
		dr.storePos = dr.storeEnd = 0;
		dr.num = 0;
	}
	if ( dr.failed ) {
		dr.num = 0;				// the store is not whole: nothing in it can be used
		dr.storeEnd = 0;
		dr.failed = qfalse;
	}
	if ( Seg( dr.cur ) ) {
		const int old = dr.cur;

		dr.cur = -1;
		Prune( old );
	}
	dr.cur = NewSeg( -1 );
	return qtrue;
}

static void WriteMessage( int seq, const byte *data, int len, int preCmd, int postCmd )
{
	seg_t *s = Seg( dr.cur );
	int head[2];

	if ( !s ) {
		return;
	}
	head[0] = LittleLong( seq );
	head[1] = LittleLong( len );
	if ( !StoreWrite( head, sizeof( head ) ) || !StoreWrite( data, len ) ) {
		return;
	}
	if ( s->msgs == 0 ) {
		s->firstSeq = seq;
		s->firstPre = preCmd;
	}
	s->lastSeq = seq;
	s->lastPost = postCmd;
	s->msgs++;
	s->len += (int)sizeof( head ) + len;
}

void DemoRoute_Header( int seq, const byte *data, int len, int cmdSeq )
{
	WriteMessage( seq, data, len, cmdSeq, cmdSeq );
}

void DemoRoute_Frame( int seq, const byte *data, int len, int preCmd, int postCmd )
{
	WriteMessage( seq, data, len, preCmd, postCmd );
}

void DemoRoute_Created( int handle )
{
	const int old = dr.cur;

	if ( !Seg( old ) || handle <= 0 ) {
		return;
	}
	dr.seg[old].handle = handle;
	dr.cur = NewSeg( old );
}

qboolean DemoRoute_Applied( int handle )
{
	const int target = FindHandle( handle );
	const int old = dr.cur;

	if ( target < 0 || !Seg( old ) ) {
		return qfalse;
	}
	// what was written since the checkpoint is given up; the messages that come now are the first ones of the new timeline
	dr.cur = -1;
	Prune( old );
	dr.cur = NewSeg( target );
	return qtrue;
}

void DemoRoute_Restart( void )
{
	const int old = dr.cur;

	if ( !Seg( old ) ) {
		return;
	}
	dr.cur = -1;
	Prune( old );
	dr.cur = NewSeg( -1 );
}

void DemoRoute_Freed( int handle )
{
	const int i = FindHandle( handle );

	if ( i >= 0 ) {
		dr.seg[i].handle = 0;
		Prune( i );
	}
}

void DemoRoute_Stopped( void )
{
	const int old = dr.cur;

	dr.cur = -1;
	if ( Seg( old ) ) {
		Prune( old );
	}
	if ( dr.store != FS_INVALID_HANDLE ) {
		FS_Flush( dr.store );
	}
}

void DemoRoute_Shutdown( void )
{
	if ( dr.store != FS_INVALID_HANDLE ) {
		FS_FCloseFile( dr.store );
		FS_HomeRemove( STORE_NAME );
		dr.store = FS_INVALID_HANDLE;
	}
	if ( dr.seg ) {
		Z_Free( dr.seg );
	}
	dr.seg = NULL;
	dr.num = dr.cap = 0;
	dr.storePos = dr.storeEnd = 0;
	dr.cur = -1;
	dr.failed = qfalse;
}

qboolean DemoRoute_Active( void )
{
	return Seg( dr.cur ) != NULL;
}

int DemoRoute_Size( void )
{
	int i = dr.cur, bytes = 0;

	while ( Seg( i ) ) {
		bytes += dr.seg[i].len;
		i = dr.seg[i].parent;
	}
	return bytes;
}


// ---------------------------------------------------------------------------
// the demo
// ---------------------------------------------------------------------------

// copies the bits [from, to) of in to out at *bit
static void CopyBits( byte *out, int *bit, const byte *in, int from, int to )
{
	for ( ; from < to; from++ ) {
		HuffmanPutBit( out, (*bit)++, HuffmanGetBit( in, from ) );
	}
}

/*
===============
DemoRoute_Renumber

A message is the acknowledge number, the server commands (command byte, number, text) and then the rest (the snapshot ...). Only
the numbers of the commands change; their text and the rest are copied as the bits they are, so nothing else is touched.
===============
*/
int DemoRoute_Renumber( const byte *in, int inLen, int cmdOff, byte *out, int outMax )
{
	msg_t r, w;
	const int endBit = inLen * 8;
	int tail, newBits, c;

	MSG_Init( &r, (byte *)in, inLen );
	r.cursize = inLen;
	MSG_Bitstream( &r );
	MSG_Init( &w, out, outMax );
	MSG_Bitstream( &w );

	MSG_WriteLong( &w, MSG_ReadLong( &r ) );
	for ( ;; ) {
		const int q0 = r.bit;
		int q1, q2, seq;

		tail = q0;
		if ( q0 >= endBit ) {
			break;
		}
		c = MSG_ReadByte( &r );
		if ( c != svc_serverCommand ) {
			break;
		}
		q1 = r.bit;
		seq = MSG_ReadLong( &r );
		q2 = r.bit;
		do {
			c = MSG_ReadByte( &r );
		} while ( c > 0 && r.bit < endBit );
		if ( c != 0 ) {
			return -1;		// the text does not end
		}
		if ( w.bit + ( r.bit - q0 ) + 64 > outMax * 8 ) {
			return -1;
		}
		CopyBits( out, &w.bit, in, q0, q1 );
		MSG_WriteLong( &w, seq - cmdOff );
		CopyBits( out, &w.bit, in, q2, r.bit );
	}
	newBits = w.bit + ( endBit - tail );
	if ( w.overflowed || newBits > outMax * 8 ) {
		return -1;
	}
	CopyBits( out, &w.bit, in, tail, endBit );
	return ( newBits + 7 ) >> 3;
}

static qboolean WriteOut( fileHandle_t out, int seq, const byte *data, int len )
{
	const int head[2] = { LittleLong( seq ), LittleLong( len ) };

	return FS_Write( head, sizeof( head ), out ) == (int)sizeof( head ) && FS_Write( data, len, out ) == len;
}

int DemoRoute_Export( fileHandle_t out )
{
	static byte	body[MAX_MSGLEN_BUF], renumbered[MAX_MSGLEN_BUF];
	fileHandle_t rd = FS_INVALID_HANDLE;
	int *chain, n = 0, i, count = 0;
	int msgOff = 0, cmdOff = 0, prevLastSeq = 0, prevLastPost = 0, outLastSeq = 0, outLastPost = 0;
	qboolean first = qtrue, ok = qtrue;

	if ( !Seg( dr.cur ) || dr.failed || dr.store == FS_INVALID_HANDLE || out == FS_INVALID_HANDLE ) {
		return -1;
	}
	chain = Z_Malloc( ( dr.num + 1 ) * (int)sizeof( int ) );
	for ( i = dr.cur; Seg( i ); i = dr.seg[i].parent ) {
		chain[n++] = i;
	}

	FS_Flush( dr.store );
	if ( FS_FOpenFileRead( STORE_NAME, &rd, qtrue ) < 0 || rd == FS_INVALID_HANDLE ) {
		Com_Printf( S_COLOR_YELLOW "Demo route: could not read %s.\n", STORE_NAME );
		Z_Free( chain );
		return -1;
	}

	while ( n > 0 && ok ) {
		const seg_t *s = &dr.seg[chain[--n]];
		int left = s->len;

		if ( s->msgs == 0 ) {
			continue;
		}
		if ( first ) {
			first = qfalse;
		} else if ( s->firstSeq != prevLastSeq + 1 || s->firstPre != prevLastPost ) {
			// not the next message of the one before (the game was put back): the numbers go on from where the demo is
			msgOff = s->firstSeq - ( outLastSeq + 1 );
			cmdOff = s->firstPre - outLastPost;
		}
		FS_Seek( rd, s->off, FS_SEEK_SET );
		while ( left > 0 ) {
			int head[2], seq, len;

			if ( left < (int)sizeof( head ) || FS_Read( head, sizeof( head ), rd ) != (int)sizeof( head ) ) {
				ok = qfalse;
				break;
			}
			seq = LittleLong( head[0] );
			len = LittleLong( head[1] );
			if ( len < 0 || len > MAX_MSGLEN || (int)sizeof( head ) + len > left || FS_Read( body, len, rd ) != len ) {
				ok = qfalse;
				break;
			}
			left -= (int)sizeof( head ) + len;
			if ( cmdOff ) {
				const int newLen = DemoRoute_Renumber( body, len, cmdOff, renumbered, MAX_MSGLEN );

				if ( newLen < 0 ) {
					ok = qfalse;
					break;
				}
				ok = WriteOut( out, seq - msgOff, renumbered, newLen );
			} else {
				ok = WriteOut( out, seq - msgOff, body, len );
			}
			if ( !ok ) {
				break;
			}
			count++;
		}
		prevLastSeq = s->lastSeq;
		prevLastPost = s->lastPost;
		outLastSeq = s->lastSeq - msgOff;
		outLastPost = s->lastPost - cmdOff;
	}
	FS_FCloseFile( rd );
	Z_Free( chain );
	if ( !ok ) {
		Com_Printf( S_COLOR_YELLOW "Demo route: the demo could not be written.\n" );
		return -1;
	}
	return count;
}


// ---------------------------------------------------------------------------
// names of demos given by a plugin
// ---------------------------------------------------------------------------

static qboolean CharIsAlnum( char c ) {
	return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' );
}

// the word tas as a word of its own (not inside another one) in the name
static qboolean NameHasTas( const char *base ) {
	const char *s;

	for ( s = base; *s; s++ ) {
		if ( !Q_stricmpn( s, "tas", 3 ) && !CharIsAlnum( s[3] ) && ( s == base || !CharIsAlnum( s[-1] ) ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// Checks and cleans the name a plugin gave: out is what goes after demos/ (without the extension). qfalse when it cannot be used.
qboolean DemoRoute_CleanName( const char *in, char *out, int size ) {
	int		n, len = (int)strlen( in );
	const char *slash;

	if ( len > 6 && !Q_stricmp( in + len - 6, ".dm_68" ) ) {
		len -= 6;		// the extension is made here
	}
	if ( len <= 0 || len > 150 || in[0] == '/' || in[0] == '.' ) {
		return qfalse;
	}
	for ( n = 0; n < len; n++ ) {
		const char c = in[n];

		if ( !( CharIsAlnum( c ) || strchr( "_-.()[]+,#/", c ) ) || ( c == '.' && n + 1 < len && in[n + 1] == '.' )
			|| ( c == '/' && n + 1 < len && ( in[n + 1] == '/' || in[n + 1] == '.' ) ) ) {
			return qfalse;
		}
	}
	if ( len + 8 > size ) {
		return qfalse;
	}
	Com_Memcpy( out, in, len );
	out[len] = '\0';
	slash = strrchr( out, '/' );
	if ( slash && !slash[1] ) {
		return qfalse;		// a folder, not a file
	}
	if ( !NameHasTas( slash ? slash + 1 : out ) ) {
		Q_strcat( out, size, "[tas]" );
	}
	return qtrue;
}
