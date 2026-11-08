/*
===========================================================================
cl_demoroute.h -- demos of a game that is put back in time (game snapshots of client plugins)

A plugin that saves checkpoints (SnapshotCreate) and goes back to one (SnapshotApply) makes the server time jump back. A demo
that records everything has that jump in it, and no demo player can follow it. What the player wants is the route: the way to the
checkpoint, then the way on from it, without the attempts that were given up. The server time of that route has no gaps, because a
checkpoint brings the time back to where it was made.

While a demo is recorded in a local game with cheats, the messages do not go to the demo file but into segments of a store file
(demos/temp/segments.store). A segment is what was recorded between two checkpoints; the segments form a tree like the checkpoints
do. SnapshotCreate ends the segment that is being written and starts the next one, SnapshotApply drops the segment that is being
written and starts a new one after the checkpoint that is loaded. When the recording stops (or "savedemo" is used) the chain of
segments from the start to the segment that is being written becomes the demo file: the message numbers and the numbers of the
server commands are made continuous where a segment does not follow the one before it.

A segment lives as long as a checkpoint that needs it, or the recording that is being written. Nothing is held in memory but the
small description of each segment; the data is in the store file, which is deleted when the game ends.
===========================================================================
*/

#ifndef CL_DEMOROUTE_H
#define CL_DEMOROUTE_H

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

// The gamestate at the start of such a demo has one more configstring, a fixed place that the game never writes to: it says the demo was
// recorded in a game with cheats. sv_cheats can be changed again later in the demo (the system info is updated by a command), this stays.
#define CS_ODF_DEMO			1023
#define CS_ODF_DEMO_VALUE	"\\cheats\\1\\route\\1"

// A recording starts. Returns qfalse when it could not be started (the demo is then written the usual way).
qboolean	DemoRoute_Start( void );

// The first message of the demo (the gamestate): seq is its message number, cmdSeq the number of the last server command the client has.
void		DemoRoute_Header( int seq, const byte *data, int len, int cmdSeq );

// A message of the server. seq is its number, preCmd and postCmd are the number of the last server command the client had before and
// after it read the message.
void		DemoRoute_Frame( int seq, const byte *data, int len, int preCmd, int postCmd );

// A game snapshot was made: the segment that is written ends here and belongs to it.
void		DemoRoute_Created( int handle );

// A game snapshot was applied. Returns qfalse when the route to it is not known (it was made when nothing was recorded); the caller
// then restarts the recording (DemoRoute_Restart).
qboolean	DemoRoute_Applied( int handle );

// What was recorded is dropped, a new recording starts at the current position (the caller writes the header again).
void		DemoRoute_Restart( void );

// A game snapshot is gone.
void		DemoRoute_Freed( int handle );

// Writes the demo (the messages of the chain to the segment that is being written) to out, without the terminator. Returns the number
// of messages written, -1 when something went wrong.
int			DemoRoute_Export( fileHandle_t out );

// The recording ended (the file is written).
void		DemoRoute_Stopped( void );

// The game is over: everything goes, the store file is deleted.
void		DemoRoute_Shutdown( void );

// Whether a recording that uses segments is going, and how many bytes the chain of it has.
qboolean	DemoRoute_Active( void );
int			DemoRoute_Size( void );

// The message numbers and the server commands of one message are renumbered: the server commands get their number minus cmdOff.
// Returns the length of the new message (written to out, which holds outMax bytes), -1 if it does not fit. Public for the tests.
int			DemoRoute_Renumber( const byte *in, int inLen, int cmdOff, byte *out, int outMax );

// Checks and cleans the name of a demo a plugin gave (what goes after demos/, without the extension): letters, digits and _ - . ( ) [ ] + , # /
// only, no .. and no leading dot or slash, not a folder. Adds [tas] when the name has no word tas in it. Returns qfalse when it cannot be used.
qboolean	DemoRoute_CleanName( const char *in, char *out, int size );

#endif
