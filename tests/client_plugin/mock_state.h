#ifndef MOCK_STATE_H
#define MOCK_STATE_H

#include "odfc_types.h"

// What the mock plugin saw and got back, and what the test program tells it to do. The plugin runs in its sandbox: it copies
// this struct to the test program at the end of every call (Test_MirrorPush) and takes it back at the start of the next
// (Test_MirrorPull), so both sides see the same values and a restart of the plugin does not lose them.
typedef struct {
	int				crashInDraw3D;		// set by the test: the next draw3d reads memory outside of the sandbox
	int				loopInCommand;		// the next command never returns
	int				badPointerInDraw3D;	// the next draw3d hands a pointer outside of its memory to the engine

	int				initCalls, shutdownCalls;
	int				cvarDefault;		// CvarInteger of a cvar the mock registered with a default
	float			cvarValue;
	char			secretSeen[64];		// what CvarString returned for a private cvar
	char			visibleSeen[64];
	char			notOwnedSeen[64];	// a cvar of the game that is not on the list of readable ones
	char			readableSeen[64];	// one that is
	char			sharedSeen[64];		// CvarString of "shared_view", registered by every mock with ODFC_CVAR_SHARED
	char			plainSeen[64];		// the same for "plain_view", registered without the flag: only the first mock gets it
	int				cvarReserved;		// 1 when the engine let the mock create a reserved name

	int				mapLoads;
	char			lastMap[64];
	int				mapLoadShader;
	int				cgameShutdowns;

	int				draw3d;
	odfcView_t		view;
	int				snapOk;
	odfcSnapshot_t	snap;
	odfcTrace_t		traceGood, traceNaN;
	int				cheatsFlag;
	int				psOk;
	odfcPlayerState_t	ps;
	int				msOk;
	odfcMoveState_t	ms;
	int				contentsLow, contentsHigh, contentsNaN;

	int				weaponOk;
	odfcWeaponState_t	weapons;
	int				statsOk;
	odfcPlayerStats_t	pstats;
	int				ghostSlot, ghostFirst;		// set by the test: the slot and first point the mock asks for in Draw3D
	int				ghostInfoOk, ghostGot;
	odfcGhostInfo_t	ghostInfo;
	odfcGhostPoint_t	ghostPts[4];
	int				modelHandle;
	int				cmdRegistered, cmdBadName, cmdTaken, cmdSecond, cmdNoHandler;
	int				commandCalls, commandArgc;
	char			commandName[32], commandArg1[32];
	int				commandArg1Len;		/* the real length of argument 1 */
	int				filterCalls, filterArgs[4], filterResult;	// set filterResult for what filter_sound returns
	int				romSeen;
	int				drawModelFrom3D;

	int				draw2d;
	int				phases[8];
	int				numPhases;

	int				snapOp, snapArg, snapFlags;	// set by the test: 1 SnapshotCreate, 2 SnapshotApply( snapArg ), 3 SnapshotFree( snapArg ), made in the next Draw2D
	int				snapResult;
	int				demoOp, demoArg, demoResult;	// set by the test: 1 DemoRecord, 2 DemoStop( demoArg ), 3 DemoSave( demoName ), 4 DemoState, made in the next Draw2D
	char			demoName[160];
	odfcDemoState_t	demoState;
	int				seekCalls, seekFrom, seekTo;	// OnDemoSeek
} mockState_t;

#endif
