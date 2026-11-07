/*
Tests the real client plugin host (cl_plugin.c, plugin_vm.c and the QVM code of the engine) against mock plugins built for
WebAssembly, with the engine functions it calls stubbed out (stubs.c). Not part of the engine build, see run.sh.
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "stubs.h"
#include "mock_state.h"

void Stubs_InitRenderer( void );

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

// The state of the mock plugins lives here, outside of their sandboxes (see mock_state.h).
static mockState_t mirrorMock, mirrorSecond;
static mockState_t *mock = &mirrorMock, *oldMock = &mirrorSecond;
static const char *homeDir;

static mockState_t *MirrorOf( const char *plugin )
{
	if ( !strcmp( plugin, "mock" ) ) {
		return &mirrorMock;
	}
	if ( !strcmp( plugin, "second" ) ) {
		return &mirrorSecond;
	}
	return NULL;
}

void Test_MirrorPull( const char *plugin, void *dst, int size )
{
	mockState_t *m = MirrorOf( plugin );

	if ( m && size == (int)sizeof( *m ) ) {
		memcpy( dst, m, size );
	}
}

void Test_MirrorPush( const char *plugin, const void *src, int size )
{
	mockState_t *m = MirrorOf( plugin );

	if ( m && size == (int)sizeof( *m ) ) {
		memcpy( m, src, size );
	}
}

static int FindPrint( const char *text )
{
	int i;

	for ( i = 0; i < numTestPrints; i++ ) {
		if ( strstr( testPrints[i], text ) ) {
			return i;
		}
	}
	return -1;
}

// ---- the disabled file and the list the menu sees ----

static void WriteDisabled( const char *text )
{
	char path[600];
	FILE *f;

	snprintf( path, sizeof( path ), "%s/plugins/disabled", homeDir );
	f = fopen( path, "wb" );
	if ( f ) {
		fputs( text, f );
		fclose( f );
	}
}

static void ReadDisabled( char *out, int size )
{
	char path[600];
	FILE *f;
	size_t n = 0;

	snprintf( path, sizeof( path ), "%s/plugins/disabled", homeDir );
	out[0] = '\0';
	f = fopen( path, "rb" );
	if ( f ) {
		n = fread( out, 1, size - 1, f );
		fclose( f );
	}
	out[n] = '\0';
}

// the state of a plugin as the menu gets it (-1: not in the list), and one of its fields
static int ListedState( const char *name, int field, char *text, int size )
{
	char buf[256];
	int i, count = CL_Plugins_Count();

	for ( i = 0; i < count; i++ ) {
		CL_Plugins_Info( i, 0, buf, sizeof( buf ) );
		if ( !strcmp( buf, name ) ) {
			return CL_Plugins_Info( i, field, text, size );
		}
	}
	return -1;
}

static int CommandRegistered( const char *name );

static int StateOf( const char *name )
{
	char text[256];

	return ListedState( name, 0, text, sizeof( text ) );
}

static int FileExists( const char *name )
{
	char path[600];
	FILE *f;

	snprintf( path, sizeof( path ), "%s/plugins/%s", homeDir, name );
	f = fopen( path, "rb" );
	if ( f ) {
		fclose( f );
	}
	return f != NULL;
}

static void ResetCounters( void )
{
	stPolyCalls = stPolyVerts = stBeamCalls = stSetColorCalls = stStretchCalls = stTraceCalls = stTraceBoxCalls = 0;
	numTestPrints = 0;
}

// ---- a game with three snapshots ----
//
// snapshot 10 at 1000 ms, 11 at 1050, 12 at 1100 (the newest, cl.snap). The player is client 1.
// Each has four entities: another player, a missile of client 1 (its trTime tells the snapshot),
// a missile of client 2 and an item.

#define OWN_CLIENT 1

static void AddSnapshot( int num, int serverTime )
{
	clSnapshot_t *s = &cl.snapshots[num & PACKET_MASK];
	int first = ( num - 10 ) * 4, i;
	entityState_t *e;

	memset( s, 0, sizeof( *s ) );
	s->valid = qtrue;
	s->messageNum = num;
	s->serverTime = serverTime;
	s->ps.clientNum = OWN_CLIENT;
	s->ps.weapon = 5;
	VectorSet( s->ps.origin, 10, 20, 30 );
	VectorSet( s->ps.velocity, 1, 2, 3 );
	s->numEntities = 4;
	s->parseEntitiesNum = first;

	for ( i = 0; i < 4; i++ ) {
		memset( &cl.parseEntities[first + i], 0, sizeof( entityState_t ) );
	}
	e = &cl.parseEntities[first];
	e[0].number = 3;  e[0].eType = ET_PLAYER;
	e[1].number = 40; e[1].eType = ET_MISSILE; e[1].clientNum = OWN_CLIENT; e[1].weapon = WP_ROCKET_LAUNCHER;
	e[1].pos.trType = TR_LINEAR; e[1].pos.trTime = serverTime; VectorSet( e[1].pos.trDelta, 900, 0, 0 );
	e[2].number = 41; e[2].eType = ET_MISSILE; e[2].clientNum = 2; e[2].weapon = WP_GRENADE_LAUNCHER;
	e[3].number = 5;  e[3].eType = ET_ITEM;
}

static void SetUpGame( void )
{
	memset( &cl, 0, sizeof( cl ) );
	AddSnapshot( 10, 1000 );
	AddSnapshot( 11, 1050 );
	AddSnapshot( 12, 1100 );
	cl.snap = cl.snapshots[12 & PACKET_MASK];
	cl.parseEntitiesNum = 12;
	cl.serverTime = 1060;
	cls.state = CA_ACTIVE;
	cl_connectedToCheatServer = 0;
	clc.demoplaying = qfalse;
}

static refdef_t TestRefdef( void )
{
	refdef_t fd;

	memset( &fd, 0, sizeof( fd ) );
	fd.x = 0; fd.y = 0; fd.width = 1280; fd.height = 720;
	fd.fov_x = 90; fd.fov_y = 58.5f;
	VectorSet( fd.vieworg, 100, 200, 300 );
	VectorSet( fd.viewaxis[0], 1, 0, 0 );
	VectorSet( fd.viewaxis[1], 0, 1, 0 );
	VectorSet( fd.viewaxis[2], 0, 0, 1 );
	return fd;
}

// draws a 3D frame and returns what the plugin saw
static void Frame3D( void )
{
	refdef_t fd = TestRefdef();

	CL_Plugins_BeforeScene( &fd );
}

// the 2D part of a frame is where the mock does what the test asked for (snapOp)
static void Frame2D( void )
{
	refdef_t fd = TestRefdef();

	CL_Plugins_AfterScene( &fd );
}

// snapshots of the game: who may call them, that applying waits for the next frame, and what the client does with the first
// snapshot after it
static void TestStateApi( void )
{
	int owner;

	printf( "game snapshots\n" );
	stStateCreates = stStateApplies = stStateFrees = 0;

	// no cheats: the dispatcher refuses before the engine hears of it
	cl_connectedToCheatServer = 0;
	mock->snapOp = 1;
	Frame2D();
	CHECK( mock->snapResult == 0 && stStateCreates == 0, "SnapshotCreate without cheats: %d, calls %d", mock->snapResult, stStateCreates );

	cl_connectedToCheatServer = 1;
	mock->snapOp = 1;
	mock->snapFlags = 1;
	Frame2D();
	CHECK( mock->snapResult == 0 && stStateCreates == 0, "flags other than 0 must be refused (%d)", mock->snapResult );

	mock->snapOp = 1;
	mock->snapFlags = 0;
	Frame2D();
	CHECK( mock->snapResult == stStateHandle && stStateCreates == 1, "SnapshotCreate with cheats: %d", mock->snapResult );
	owner = stStateCreateOwner;
	CHECK( owner > 0, "the plugin has to be known as an owner (%d)", owner );

	// a demo: the engine says no
	clc.demoplaying = qtrue;
	mock->snapOp = 1;
	Frame2D();
	CHECK( mock->snapResult == 0 && stStateCreates == 1, "SnapshotCreate in a demo" );
	mock->snapOp = 2;
	mock->snapArg = stStateHandle;
	Frame2D();
	CHECK( mock->snapResult == 0, "SnapshotApply in a demo" );
	clc.demoplaying = qfalse;

	// apply: only a handle of the plugin, only a cgame that takes time changes; nothing happens before the next frame
	mock->snapOp = 2;
	mock->snapArg = stStateHandle + 1;
	Frame2D();
	CHECK( mock->snapResult == 0, "a handle that does not exist" );
	stStateOwnerOf = owner + 1;
	mock->snapOp = 2;
	mock->snapArg = stStateHandle;
	Frame2D();
	CHECK( mock->snapResult == 0, "the snapshot of another plugin" );
	stStateOwnerOf = owner;
	stSeekable = 0;
	mock->snapOp = 2;
	Frame2D();
	CHECK( mock->snapResult == 0, "a cgame that does not take time changes" );
	stSeekable = 1;
	mock->snapOp = 2;
	Frame2D();
	CHECK( mock->snapResult == 1 && stStateApplies == 0, "SnapshotApply is accepted but not made yet (%d, applies %d)", mock->snapResult, stStateApplies );

	// the next frame makes it
	cls.state = CA_ACTIVE;
	clc.serverMessageSequence = 40;
	CL_Plugins_BeforeFrame();
	CHECK( stStateApplies == 1, "applied at the start of the frame: %d", stStateApplies );
	CL_Plugins_BeforeFrame();
	CHECK( stStateApplies == 1, "applied twice" );

	// a snapshot of the old timeline arrives: nothing changes
	cl.serverTime = 5000;
	cl.snap.messageNum = 40;
	cl.snap.serverTime = 4990;
	CL_Plugins_StateSnapshot();
	CHECK( cl.serverTime == 5000, "an old snapshot must not reset the clock" );

	// the first one after it: the clock and the view are set to it, the cgame and the plugin hear of it later
	cl.snap.messageNum = 41;
	cl.snap.serverTime = 3000;
	cl.snap.ps.viewangles[YAW] = 90;
	cl.snap.ps.delta_angles[YAW] = ANGLE2SHORT( 30 );
	cls.realtime = 777;
	cl.cmds[3].serverTime = 4900;
	cl.cmds[CMD_BACKUP - 1].serverTime = 5000;
	stTimeChangeCalls = 0;
	mock->seekCalls = 0;
	CL_Plugins_StateSnapshot();
	CHECK( cl.serverTime == 3000 && cl.oldServerTime == 3000 && cl.oldFrameServerTime == 3000, "clock %d / %d / %d",
		cl.serverTime, cl.oldServerTime, cl.oldFrameServerTime );
	CHECK( cl.serverTimeDelta == 3000 - 777, "serverTimeDelta %d", cl.serverTimeDelta );
	CHECK( cl.cmds[3].serverTime == 0 && cl.cmds[CMD_BACKUP - 1].serverTime == 0, "the old usercmds must not look newer than the server's" );
	CHECK( fabs( cl.viewangles[YAW] - 60.0f ) < 0.01f, "view angle %g, wanted 60", cl.viewangles[YAW] );
	CHECK( stTimeChangeCalls == 0 && mock->seekCalls == 0, "the cgame is told in SetCGameTime, not while the packet is read" );
	CL_Plugins_StateSnapshot();
	cl.snap.serverTime = 3008;
	cl.serverTime = 3100;
	CL_Plugins_StateSnapshot();
	CHECK( cl.serverTime == 3100, "only the first snapshot resets the clock" );

	CL_Plugins_StateNotify();
	CHECK( stTimeChangeCalls == 1 && stTimeChangeArgs[0] == 3008 && stTimeChangeArgs[1] == 41, "cgame time change: %d calls (%d, %d)",
		stTimeChangeCalls, stTimeChangeArgs[0], stTimeChangeArgs[1] );
	CHECK( mock->seekCalls == 1 && mock->seekFrom == 5000 && mock->seekTo == 3008, "OnDemoSeek %d: %d -> %d", mock->seekCalls,
		mock->seekFrom, mock->seekTo );
	CL_Plugins_StateNotify();
	CHECK( stTimeChangeCalls == 1 && mock->seekCalls == 1, "told twice" );

	// free: only the owner
	stStateOwnerOf = owner + 1;
	mock->snapOp = 3;
	mock->snapArg = stStateHandle;
	Frame2D();
	CHECK( stStateFrees == 0, "freed the snapshot of another plugin" );
	stStateOwnerOf = owner;
	mock->snapOp = 3;
	Frame2D();
	CHECK( stStateFrees == 1, "SnapshotFree" );

	cl_connectedToCheatServer = 0;
}

// a demo that is recorded in segments (cl_demoroute.c): the snapshots of the game cut it
static void TestDemoRoute( void )
{
	printf( "demo route\n" );
	stStateCreates = stStateApplies = 0;
	stRouteCreated = stRouteApplied = stRouteRestarts = 0;
	stStateOwnerOf = stStateCreateOwner;
	cl_connectedToCheatServer = 1;
	clc.demoRoute = qfalse;
	clc.demowaiting = qfalse;

	// a normal recording is not told about the snapshots
	mock->snapOp = 1;
	Frame2D();
	CHECK( mock->snapResult == stStateHandle && stRouteCreated == 0, "no route: no cut (%d)", stRouteCreated );

	clc.demoRoute = qtrue;
	mock->snapOp = 1;
	Frame2D();
	CHECK( stRouteCreated == 1 && stRouteCreatedHandle == stStateHandle, "a snapshot cuts the demo that is recorded in segments (%d, handle %d)", stRouteCreated, stRouteCreatedHandle );
	stStateOwnerOf = stStateCreateOwner;
	stStateHandle = 7;

	// the apply: the route goes on from the snapshot, the messages of the old timeline are not recorded
	mock->snapOp = 2;
	mock->snapArg = stStateHandle;
	Frame2D();
	CHECK( stRouteApplied == 0, "nothing before the next frame" );
	cls.state = CA_ACTIVE;
	clc.serverMessageSequence = 60;
	CL_Plugins_BeforeFrame();
	CHECK( stStateApplies == 1 && stRouteApplied == 1 && stRouteAppliedHandle == stStateHandle && clc.demowaiting && stRouteRestarts == 0,
		"applied: route told %d, waiting %d, restarts %d", stRouteApplied, clc.demowaiting, stRouteRestarts );

	// a snapshot the demo has no route to: the demo starts again
	clc.demowaiting = qfalse;
	stRouteKnown = 0;
	mock->snapOp = 2;
	Frame2D();
	CL_Plugins_BeforeFrame();
	CHECK( stRouteApplied == 2 && stRouteRestarts == 1, "no route: the demo restarts (%d)", stRouteRestarts );
	stRouteKnown = 1;

	// without a recording in segments nothing of this happens
	clc.demoRoute = qfalse;
	mock->snapOp = 2;
	Frame2D();
	CL_Plugins_BeforeFrame();
	CHECK( stRouteApplied == 2 && stStateApplies == 3, "no route: not told" );

	// the client gets the first snapshot of the new timeline
	cl.snap.messageNum = 99;
	cl.snap.serverTime = 3000;
	CL_Plugins_StateSnapshot();
	CL_Plugins_StateNotify();
	cl_connectedToCheatServer = 0;
}

// the demo calls of the API: only with cheats, a name goes through as it is, the state is a plain read
static void TestDemoApi( void )
{
	printf( "demo calls\n" );
	stDemoRecords = stDemoStops = stDemoSaves = 0;

	cl_connectedToCheatServer = 0;
	mock->demoOp = 1;
	Frame2D();
	CHECK( mock->demoResult == 0 && stDemoRecords == 0, "DemoRecord without cheats (%d, calls %d)", mock->demoResult, stDemoRecords );
	mock->demoOp = 2;
	mock->demoArg = 1;
	Frame2D();
	CHECK( mock->demoResult == 0 && stDemoStops == 0, "DemoStop without cheats" );
	mock->demoOp = 3;
	strcpy( mock->demoName, "st1[tas.cpm]0.07.468" );
	Frame2D();
	CHECK( mock->demoResult == 0 && stDemoSaves == 0, "DemoSave without cheats" );
	mock->demoOp = 4;
	memset( &mock->demoState, 0, sizeof( mock->demoState ) );
	Frame2D();
	CHECK( mock->demoResult == 1 && mock->demoState.recording == 1 && mock->demoState.segments == 1 && mock->demoState.mine == 1 && mock->demoState.bytes == 4242,
		"DemoState is not limited by cheats (%d, bytes %d)", mock->demoResult, mock->demoState.bytes );

	cl_connectedToCheatServer = 1;
	mock->demoOp = 1;
	Frame2D();
	CHECK( mock->demoResult == 1 && stDemoRecords == 1, "DemoRecord with cheats (%d, calls %d)", mock->demoResult, stDemoRecords );
	stDemoRecordResult = 0;
	mock->demoOp = 1;
	Frame2D();
	CHECK( mock->demoResult == 0 && stDemoRecords == 2, "DemoRecord that could not start" );
	stDemoRecordResult = 1;
	mock->demoOp = 2;
	mock->demoArg = 1;
	Frame2D();
	CHECK( mock->demoResult == 1 && stDemoStops == 1 && stDemoStopDiscard == 1, "DemoStop( discard ): %d, %d", stDemoStops, stDemoStopDiscard );
	mock->demoOp = 2;
	mock->demoArg = 0;
	Frame2D();
	CHECK( stDemoStops == 2 && stDemoStopDiscard == 0, "DemoStop( keep )" );
	mock->demoOp = 3;
	strcpy( mock->demoName, "st1[tas.cpm]0.07.468" );
	Frame2D();
	CHECK( mock->demoResult == 1 && stDemoSaves == 1 && !strcmp( stDemoSavedName, "st1[tas.cpm]0.07.468" ), "DemoSave: '%s'", stDemoSavedName );
	cl_connectedToCheatServer = 0;
}

static void TestLoading( void )
{
	printf( "loading\n" );
	CHECK( FindPrint( "Client plugin 'mock' mock 1.0 loaded" ) >= 0, "mock was not loaded" );
	CHECK( FindPrint( "built for client plugin API" ) >= 0, "a plugin of a newer API must be refused" );
	CHECK( FindPrint( "called a function the engine does not have" ) >= 0, "a plugin that calls what does not exist must be stopped" );
	CHECK( FindPrint( "init returned 1" ) >= 0, "a plugin whose init fails is not loaded" );
	CHECK( FindPrint( "has no io buffer" ) >= 0, "a plugin without an io buffer must be refused" );
	CHECK( FindPrint( "odfs" ) < 0, "a server plugin must not be reported" );

	// what the menu is shown: six plugins, in the order of their names; neither the module that is a server
	// plugin nor the one with a name that cannot be used is in the list
	CHECK( CL_Plugins_Count() == 6, "the list has %d plugins", CL_Plugins_Count() );
	CHECK( StateOf( "mock" ) == 0 && StateOf( "badver" ) == 2 && StateOf( "badimport" ) == 3 && StateOf( "refuse" ) == 2 && StateOf( "noio" ) == 2, "states" );
	CHECK( StateOf( "second" ) == 4, "a plugin in the disabled file is not started (state %d)", StateOf( "second" ) );
	CHECK( StateOf( "noentry" ) == -1 && StateOf( "bad name" ) == -1, "modules that are no client plugins are listed" );
	{
		char text[256];

		CHECK( ListedState( "mock", 1, text, sizeof( text ) ) == 0 && !strcmp( text, "mock 1.0" ), "version '%s'", text );
		ListedState( "badver", 2, text, sizeof( text ) );
		CHECK( strstr( text, "built for client plugin API" ) != NULL, "reason '%s'", text );
		ListedState( "second", 3, text, sizeof( text ) );
		CHECK( !strcmp( text, "1" ), "disabled flag '%s'", text );
		ListedState( "mock", 3, text, sizeof( text ) );
		CHECK( !strcmp( text, "0" ), "disabled flag of a running plugin '%s'", text );
		CHECK( CL_Plugins_Info( 99, 0, text, sizeof( text ) ) == -1 && text[0] == '\0', "an index out of the list" );
	}
	CHECK( oldMock && oldMock->initCalls == 0, "a disabled plugin was started" );
	CHECK( mock->notOwnedSeen[0] == '\0', "a cvar nobody listed was readable: '%s'", mock->notOwnedSeen );
	CHECK( !strcmp( mock->readableSeen, "1" ), "a setting of the game was not readable: '%s'", mock->readableSeen );
	CHECK( !mock->cvarReserved, "a plugin created a cvar with a name of the game" );
	CHECK( FindPrint( "cannot create the cvar cg_mock_evil" ) >= 0, "the refusal was not reported" );
	CHECK( FindPrint( "the cvar mock_secret already exists" ) >= 0, "a private cvar was taken over" );
	// plain_view is registered by every mock without ODFC_CVAR_SHARED, shared_view with it; mock is the first to start
	CHECK( !strcmp( mock->plainSeen, "7" ) && !strcmp( mock->sharedSeen, "7" ), "the first plugin reads what it made: '%s' '%s'", mock->plainSeen, mock->sharedSeen );
	CHECK( FindPrint( "Plugin 'refuse': the cvar plain_view already exists" ) >= 0, "a cvar of another plugin without the shared flag was taken over" );
	CHECK( FindPrint( "Plugin 'refuse': the cvar shared_view already exists" ) >= 0, "a shared cvar was joined without the flag" );
	CHECK( FindPrint( "Plugin 'mock': the cvar shared_view already exists" ) < 0, "a shared cvar was refused" );

	CHECK( mock->initCalls == 1, "Init calls %d", mock->initCalls );
	CHECK( mock->cvarDefault == 5, "a registered cvar has its default, got %d", mock->cvarDefault );
	CHECK( fabs( mock->cvarValue - 2.5 ) < 1e-6, "CvarValue gave %f", mock->cvarValue );
	CHECK( !strcmp( mock->visibleSeen, "yes" ), "CvarString gave '%s'", mock->visibleSeen );
	CHECK( mock->secretSeen[0] == '\0', "a private cvar must not be readable, got '%s'", mock->secretSeen );
}

static void TestDrawingRules( void )
{
	refdef_t fd = TestRefdef();

	printf( "drawing is only allowed in the right phase\n" );

	// no map yet: nothing is called
	ResetCounters();
	Frame3D();
	CHECK( mock->draw3d == 0, "Draw3D before MapLoad" );

	CL_Plugins_MapLoad( "maps/test.bsp" );
	CHECK( mock->mapLoads == 1, "MapLoad calls %d", mock->mapLoads );
	CHECK( !strcmp( mock->lastMap, "maps/test.bsp" ), "map '%s'", mock->lastMap );
	CHECK( mock->mapLoadShader == 7, "RegisterShader in MapLoad gave %d", mock->mapLoadShader );
	CHECK( stPolyCalls == 0 && stBeamCalls == 0 && stSetColorCalls == 0 && stStretchCalls == 0,
		"drawing inside MapLoad reached the renderer (%d %d %d %d)", stPolyCalls, stBeamCalls, stSetColorCalls, stStretchCalls );

	ResetCounters();
	Frame3D();
	CHECK( mock->draw3d == 1, "Draw3D calls %d", mock->draw3d );
	CHECK( stPolyCalls == 1 && stPolyVerts == 3, "polys %d verts %d", stPolyCalls, stPolyVerts );
	CHECK( stBeamCalls == 1, "beams %d", stBeamCalls );
	CHECK( stBeamShader == 7, "beam shader %d", stBeamShader );
	CHECK( stBeamFrom[0] == 0 && stBeamTo[0] == 100, "beam from %f to %f", stBeamFrom[0], stBeamTo[0] );
	CHECK( stBeamRGBA[0] == 255 && stBeamRGBA[1] == 0 && stBeamRGBA[3] == 255, "beam colour" );
	CHECK( stSetColorCalls == 0 && stStretchCalls == 0, "2D drawing inside Draw3D reached the renderer" );

	CHECK( mock->view.time == 1060, "view time %d", mock->view.time );
	CHECK( mock->view.width == 1280 && mock->view.height == 720, "view size" );
	CHECK( mock->view.fovX == 90.0f && mock->view.origin[2] == 300.0f && mock->view.axis[1][1] == 1.0f, "view" );

	// a menu model or the like is not the game's world scene
	ResetCounters();
	fd.rdflags = RDF_NOWORLDMODEL;
	CL_Plugins_BeforeScene( &fd );
	CL_Plugins_AfterScene( &fd );
	CHECK( mock->draw3d == 1 && mock->draw2d == 0, "a scene without a world must not call the plugin" );

	// 2D: after the scene, then after the frame
	cls.glconfig.vidWidth = 1920;
	cls.glconfig.vidHeight = 1080;
	ResetCounters();
	fd = TestRefdef();
	CL_Plugins_AfterScene( &fd );
	CHECK( mock->draw2d == 1 && mock->phases[0] == ODFC_DRAW2D_SCENE, "Draw2D after the scene" );
	CHECK( stSetColorCalls == 2 && stSetColorLastNull, "the engine must reset the colour (calls %d, last null %d)", stSetColorCalls, stSetColorLastNull );
	CHECK( stStretchCalls == 1, "stretch pics %d", stStretchCalls );
	// the plugin draws in 640x480 (the mock: 1 2 3 4); the renderer wants pixels (1920x1080 here)
	CHECK( stStretchLast[0] == 3 && stStretchLast[1] == 4.5f && stStretchLast[2] == 9 && stStretchLast[3] == 9,
		"the stretch pic is not scaled to the screen: %g %g %g %g", stStretchLast[0], stStretchLast[1], stStretchLast[2], stStretchLast[3] );
	CHECK( stPolyCalls == 0 && stBeamCalls == 0, "3D drawing inside Draw2D reached the renderer" );

	cgvm = NULL;
	CL_Plugins_AfterFrame();
	CHECK( mock->draw2d == 1, "no frame end without the game module" );
	cgvm = (vm_t *)&failures;		// any non-NULL pointer: the game module is running
	CL_Plugins_AfterFrame();
	CHECK( mock->draw2d == 2 && mock->phases[1] == ODFC_DRAW2D_FRAME, "Draw2D after the frame" );
}

static void TestTrace( void )
{
	printf( "trace\n" );
	ResetCounters();
	Frame3D();
	CHECK( stTraceCalls == 1, "NaN or NULL arguments must not reach the collision code (%d traces)", stTraceCalls );
	CHECK( stTraceBoxCalls == 1, "a box trace must pass its box on" );
	CHECK( mock->traceGood.fraction == 0.5f && mock->traceGood.endpos[0] == 50.0f && mock->traceGood.normal[2] == 1.0f, "trace result" );
	CHECK( mock->traceNaN.fraction == 1.0f, "NaN trace fraction %f", mock->traceNaN.fraction );
}

static void TestSnapshots( void )
{
	printf( "snapshots and cheats\n" );

	// view time 1060: snapshot 11 (1050) is the one the game shows, 12 (1100) is still ahead
	Frame3D();
	CHECK( mock->snapOk, "no snapshot" );
	CHECK( mock->snap.serverTime == 1050, "snapshot time %d, wanted 1050", mock->snap.serverTime );
	CHECK( mock->snap.player.clientNum == OWN_CLIENT && mock->snap.player.weapon == 5, "player" );
	CHECK( mock->snap.player.origin[1] == 20.0f && mock->snap.player.velocity[2] == 3.0f, "player state" );
	CHECK( mock->snap.cheats == 0 && mock->cheatsFlag == 0, "cheats must be off" );

	// no cheats: only the missiles of the watched player
	CHECK( mock->snap.numEntities == 1, "entities without cheats: %d, wanted 1", mock->snap.numEntities );
	CHECK( mock->snap.entities[0].number == 40 && mock->snap.entities[0].weapon == WP_ROCKET_LAUNCHER
		&& mock->snap.entities[0].pos.trTime == 1050 && mock->snap.entities[0].pos.trDelta[0] == 900.0f, "the own missile" );

	// sv_cheats on the server
	cl_connectedToCheatServer = 1;
	Frame3D();
	CHECK( mock->snap.numEntities == 4 && mock->snap.cheats == 1 && mock->cheatsFlag == 1,
		"with cheats: %d entities, cheats %d / %d", mock->snap.numEntities, mock->snap.cheats, mock->cheatsFlag );
	cl_connectedToCheatServer = 0;

	// a demo
	clc.demoplaying = qtrue;
	Frame3D();
	CHECK( mock->snap.numEntities == 4 && mock->cheatsFlag == 1, "demo: %d entities", mock->snap.numEntities );
	clc.demoplaying = qfalse;

	// back to normal: the flag is not sticky
	Frame3D();
	CHECK( mock->snap.numEntities == 1 && mock->cheatsFlag == 0, "cheats stayed on" );

	// which snapshot is current
	cl.serverTime = 1050;
	Frame3D();
	CHECK( mock->snap.serverTime == 1050, "view time 1050 gave %d", mock->snap.serverTime );
	cl.serverTime = 1049;
	Frame3D();
	CHECK( mock->snap.serverTime == 1000, "view time 1049 gave %d", mock->snap.serverTime );
	cl.serverTime = 500;
	Frame3D();
	CHECK( mock->snap.serverTime == 1100, "a view time before every snapshot gives the newest, got %d", mock->snap.serverTime );
	cl.serverTime = 5000;
	Frame3D();
	CHECK( mock->snap.serverTime == 1100, "view time 5000 gave %d", mock->snap.serverTime );

	// a snapshot that was dropped is skipped
	cl.serverTime = 1060;
	cl.snapshots[11 & PACKET_MASK].valid = qfalse;
	Frame3D();
	CHECK( mock->snap.serverTime == 1000, "dropped snapshot: got %d", mock->snap.serverTime );
	cl.snapshots[11 & PACKET_MASK].valid = qtrue;

	// not in a game
	cls.state = CA_CONNECTED;
	Frame3D();
	CHECK( !mock->snapOk, "a snapshot outside of a game" );
	cls.state = CA_ACTIVE;
}

static void TestPlayerState( void )
{
	playerState_t ps;

	printf( "predicted player state\n" );
	cl.serverTime = 1060;

	// the game module has published nothing
	Frame3D();
	CHECK( !mock->psOk, "a player state nobody published" );

	memset( &ps, 0, sizeof( ps ) );
	ps.clientNum = 1;
	ps.pm_type = PM_DEAD;
	ps.weapon = WP_GRENADE_LAUNCHER;
	ps.viewheight = 26;
	VectorSet( ps.origin, 100, 200, 300 );
	VectorSet( ps.velocity, 1, 2, 3 );
	VectorSet( ps.viewangles, -30, 90, 0 );
	CL_Plugins_SetPlayerState( &ps, 0 );
	Frame3D();
	CHECK( mock->psOk, "no player state after it was published" );
	CHECK( mock->ps.clientNum == 1 && mock->ps.pmType == PM_DEAD && mock->ps.weapon == WP_GRENADE_LAUNCHER && mock->ps.viewheight == 26, "player state fields" );
	CHECK( mock->ps.origin[0] == 100 && mock->ps.origin[2] == 300 && mock->ps.velocity[1] == 2 && mock->ps.viewangles[0] == -30 && mock->ps.viewangles[1] == 90, "player state vectors" );

	// it is for one frame: the next one, with the game module silent, has none
	cl.serverTime = 1070;
	Frame3D();
	CHECK( !mock->psOk, "a stale player state was handed out" );
	cl.serverTime = 1060;

	// and it does not survive a map change
	CL_Plugins_CGameShutdown();
	CL_Plugins_MapLoad( "maps/test.bsp" );
	Frame3D();
	CHECK( !mock->psOk, "a player state from the previous map" );

	// outside of a game
	CL_Plugins_SetPlayerState( &ps, 0 );
	cls.state = CA_CONNECTED;
	Frame3D();
	CHECK( !mock->psOk, "a player state outside of a game" );
	cls.state = CA_ACTIVE;
	CL_Plugins_CGameShutdown();
	CL_Plugins_MapLoad( "maps/test.bsp" );
}

static void TestMoveState( void )
{
	playerState_t ps;
	usercmd_t *cmd;

	printf( "move state\n" );
	cl.serverTime = 1060;

	// nothing published: nothing to move
	CL_Plugins_CGameShutdown();
	CL_Plugins_MapLoad( "maps/test.bsp" );
	Frame3D();
	CHECK( !mock->msOk, "a move state nobody published" );

	memset( &ps, 0, sizeof( ps ) );
	ps.pm_flags = PMF_DUCKED | PMF_JUMP_HELD | PMF_TIME_KNOCKBACK;
	ps.pm_time = 150;
	ps.groundEntityNum = 1022;
	ps.speed = 320;
	ps.gravity = 800;
	ps.powerups[PW_FLIGHT] = 12345;
	ps.stats[13] = 1 | 16 | 32;
	memset( &cl.cmds, 0, sizeof( cl.cmds ) );
	cl.cmdNumber = 40;
	cmd = &cl.cmds[40 & CMD_MASK];
	cmd->forwardmove = 127; cmd->rightmove = -64; cmd->upmove = 127; cmd->buttons = 1;
	cmd = &cl.cmds[39 & CMD_MASK];
	cmd->forwardmove = 5;		// an older one: the latest counts
	CL_Plugins_SetPlayerState( &ps, 1 | 2 | 4 | 256 );
	Frame3D();
	CHECK( mock->msOk, "no move state after it was published" );
	CHECK( mock->ms.pmFlags == ( PMF_DUCKED | PMF_JUMP_HELD | PMF_TIME_KNOCKBACK ) && mock->ms.pmTime == 150, "flags %d time %d", mock->ms.pmFlags, mock->ms.pmTime );
	CHECK( mock->ms.groundEntityNum == 1022 && mock->ms.speed == 320 && mock->ms.gravity == 800, "ground %d speed %d gravity %d", mock->ms.groundEntityNum, mock->ms.speed, mock->ms.gravity );
	CHECK( mock->ms.flight == 1, "the flight powerup" );
	CHECK( mock->ms.userInput == ( 1 | 16 | 32 ), "keys %d", mock->ms.userInput );
	CHECK( mock->ms.viewFlags == ( ODFC_VIEW_SCOREBOARD | ODFC_VIEW_NOT_FIRST_PERSON ), "unknown view flags must be dropped, got %d", mock->ms.viewFlags );
	CHECK( !mock->ms.demoPlayback, "demo flag" );
	CHECK( mock->ms.cmdValid && mock->ms.forwardmove == 127 && mock->ms.rightmove == -64 && mock->ms.upmove == 127 && mock->ms.buttons == 1,
		"latest command %d %d %d", mock->ms.forwardmove, mock->ms.rightmove, mock->ms.upmove );

	// a demo has no commands of its own
	clc.demoplaying = qtrue;
	Frame3D();
	CHECK( mock->msOk && mock->ms.demoPlayback && !mock->ms.cmdValid && mock->ms.forwardmove == 0, "demo: valid %d forward %d", mock->ms.cmdValid, mock->ms.forwardmove );
	clc.demoplaying = qfalse;

	// no command made yet
	cl.cmdNumber = 0;
	Frame3D();
	CHECK( mock->msOk && !mock->ms.cmdValid, "a command before any was made" );
	cl.cmdNumber = 40;

	// stale, and outside of a game
	cl.serverTime = 1070;
	Frame3D();
	CHECK( !mock->msOk, "a stale move state" );
	cl.serverTime = 1060;
	cls.state = CA_CONNECTED;
	Frame3D();
	CHECK( !mock->msOk, "a move state outside of a game" );
	cls.state = CA_ACTIVE;

	// the contents of the map: the stub has water below z = 0
	Frame3D();
	CHECK( mock->contentsLow == CONTENTS_WATER && mock->contentsHigh == 0, "contents %d / %d", mock->contentsLow, mock->contentsHigh );
	CHECK( mock->contentsNaN == 0, "a NaN point reached the collision code" );

	// the screen size reaches the plugin
	cls.glconfig.vidWidth = 1920;
	cls.glconfig.vidHeight = 1080;
	Frame3D();
	CHECK( mock->view.screenWidth == 1920 && mock->view.screenHeight == 1080, "screen %d x %d", mock->view.screenWidth, mock->view.screenHeight );

	CL_Plugins_CGameShutdown();
	CL_Plugins_MapLoad( "maps/test.bsp" );
}

static void TestMapChange( void )
{
	int draws = mock->draw3d, shutdowns = mock->cgameShutdowns, loads = mock->mapLoads;

	printf( "map change\n" );
	CL_Plugins_CGameShutdown();
	CHECK( mock->cgameShutdowns == shutdowns + 1, "CGameShutdown calls %d", mock->cgameShutdowns );
	Frame3D();
	CHECK( mock->draw3d == draws, "Draw3D without a map" );
	CL_Plugins_CGameShutdown();
	CHECK( mock->cgameShutdowns == shutdowns + 1, "CGameShutdown twice" );

	CL_Plugins_MapLoad( "maps/other.bsp" );
	CHECK( mock->mapLoads == loads + 1 && !strcmp( mock->lastMap, "maps/other.bsp" ), "second MapLoad" );
	Frame3D();
	CHECK( mock->draw3d == draws + 1, "Draw3D after the second MapLoad" );
}



static void TestGhostPath( void )
{
	odfcGhostPoint_t pts[6];
	int i, serial;

	printf( "ghost paths\n" );
	for ( i = 0; i < 6; i++ ) {
		memset( &pts[i], 0, sizeof( pts[i] ) );
		pts[i].ms = i * 50;
		pts[i].origin[0] = (float)( i * 40 );
		pts[i].flags = i == 4 ? ODFC_GHOST_BREAK : 0;
	}
	mock->ghostSlot = 1;
	mock->ghostFirst = 0;
	Frame3D();
	CHECK( !mock->ghostInfoOk && mock->ghostGot == 0, "a slot without a path: info %d got %d", mock->ghostInfoOk, mock->ghostGot );

	// the game module sends the path in parts
	CL_Plugins_SetGhostPath( 1, 0, 3, pts, 0 );
	Frame3D();
	CHECK( mock->ghostInfoOk && mock->ghostInfo.numPoints == 3 && mock->ghostInfo.durationMs == 100 && !mock->ghostInfo.complete && mock->ghostGot == 3
		&& mock->ghostPts[2].origin[0] == 80.0f, "first part: info %d points %d duration %d got %d", mock->ghostInfoOk,
		mock->ghostInfo.numPoints, mock->ghostInfo.durationMs, mock->ghostGot );
	CHECK( mock->ghostInfo.raceMs == 0, "no time known yet: %d", mock->ghostInfo.raceMs );
	serial = mock->ghostInfo.serial;

	CL_Plugins_SetGhostPath( 1, 5, 1, pts + 5, 0 );		// does not continue: dropped
	CL_Plugins_SetGhostTime( 1, 7648 );
	CL_Plugins_SetGhostPath( 1, 3, 3, pts + 3, 1 );
	mock->ghostFirst = 4;
	Frame3D();
	CHECK( mock->ghostInfo.raceMs == 7648, "the time of the run: %d", mock->ghostInfo.raceMs );
	CHECK( mock->ghostInfo.numPoints == 6 && mock->ghostInfo.complete && mock->ghostInfo.serial == serial && mock->ghostGot == 2
		&& mock->ghostPts[0].flags == ODFC_GHOST_BREAK && mock->ghostPts[1].ms == 250, "second part: points %d complete %d got %d",
		mock->ghostInfo.numPoints, mock->ghostInfo.complete, mock->ghostGot );
	mock->ghostFirst = 6;
	Frame3D();
	CHECK( mock->ghostGot == 0, "reading behind the end" );

	// points that go back in time are dropped, a new path replaces the old one
	pts[0].ms = 10;
	CL_Plugins_SetGhostPath( 1, 6, 1, pts, 1 );
	mock->ghostFirst = 0;
	Frame3D();
	CHECK( mock->ghostInfo.numPoints == 6, "a point from the past was added" );
	CL_Plugins_SetGhostPath( 1, 0, 2, pts + 1, 0 );
	Frame3D();
	CHECK( mock->ghostInfo.raceMs == 0, "a new path forgets the time" );
	CHECK( mock->ghostInfo.numPoints == 2 && !mock->ghostInfo.complete && mock->ghostInfo.serial != serial, "a new path: points %d serial %d",
		mock->ghostInfo.numPoints, mock->ghostInfo.serial );

	// a long path (the store grows): 20000 points in parts of 4000
	{
		static odfcGhostPoint_t big[4000];
		int part, k;

		for ( part = 0; part < 5; part++ ) {
			for ( k = 0; k < 4000; k++ ) {
				memset( &big[k], 0, sizeof( big[k] ) );
				big[k].ms = ( part * 4000 + k ) * 8;
				big[k].origin[0] = (float)( part * 4000 + k );
			}
			CL_Plugins_SetGhostPath( 2, part * 4000, 4000, big, part == 4 );
		}
		mock->ghostSlot = 2;
		mock->ghostFirst = 19998;
		Frame3D();
		CHECK( mock->ghostInfoOk && mock->ghostInfo.numPoints == 20000 && mock->ghostInfo.complete && mock->ghostInfo.durationMs == 19999 * 8
			&& mock->ghostGot == 2 && mock->ghostPts[1].origin[0] == 19999.0f, "a long path: points %d got %d", mock->ghostInfo.numPoints, mock->ghostGot );
		mock->ghostFirst = 8190;		// across the first growth
		Frame3D();
		CHECK( mock->ghostGot == 4 && mock->ghostPts[3].origin[0] == 8193.0f, "across a growth of the store: got %d", mock->ghostGot );
		mock->ghostSlot = 1;
		mock->ghostFirst = 0;
	}

	// other slots, bad slots
	CL_Plugins_SetGhostPath( 7, 0, 2, pts, 0 );
	CL_Plugins_SetGhostTime( 7, 100 );
	CL_Plugins_SetGhostTime( -1, 100 );
	CL_Plugins_SetGhostPath( -1, 0, 2, pts, 0 );
	mock->ghostSlot = 0;
	Frame3D();
	CHECK( !mock->ghostInfoOk, "slot 0 has nothing" );
	mock->ghostSlot = 9;
	Frame3D();
	CHECK( !mock->ghostInfoOk && mock->ghostGot == 0, "a slot that does not exist" );
	mock->ghostSlot = 1;
	mock->ghostFirst = 0;
}

static void TestV4( void )
{
	refdef_t fd = TestRefdef();
	int i;

	printf( "API version 4: weapons, models, commands, sounds\n" );

	// weapons and ammunition of the published state
	memset( &cl.snap.ps, 0, sizeof( cl.snap.ps ) );
	{
		playerState_t ps;

		memset( &ps, 0, sizeof( ps ) );
		ps.stats[STAT_WEAPONS] = ( 1 << 2 ) | ( 1 << 5 );
		ps.ammo[2] = 100;
		ps.ammo[5] = 7;
		ps.ammo[15] = 99;
		ps.commandTime = 4321;
		ps.stats[7] = 3;
		ps.stats[8] = 0x1234;
		ps.stats[12] = 6;
		ps.persistant[5] = 77;
		CL_Plugins_SetStatsTime( 1032 );
		CL_Plugins_SetPlayerState( &ps, 0 );
	}
	Frame3D();
	CHECK( mock->statsOk && mock->pstats.commandTime == 4321 && mock->pstats.stats[7] == 3 && mock->pstats.stats[8] == 0x1234
		&& mock->pstats.stats[12] == 6 && mock->pstats.persistant[5] == 77 && mock->pstats.stats[15] == 0 && mock->pstats.snapshotTime == 1032,
		"player stats: ok %d time %d stats %d %d %d persistant %d", mock->statsOk, mock->pstats.commandTime, mock->pstats.stats[7],
		mock->pstats.stats[8], mock->pstats.stats[12], mock->pstats.persistant[5] );
	CHECK( mock->weaponOk && mock->weapons.weapons == ( ( 1 << 2 ) | ( 1 << 5 ) ) && mock->weapons.ammo[2] == 100 && mock->weapons.ammo[5] == 7 && mock->weapons.ammo[15] == 99,
		"weapon state: ok %d weapons %x ammo %d %d", mock->weaponOk, mock->weapons.weapons, mock->weapons.ammo[2], mock->weapons.ammo[5] );
	cl.serverTime = 1070;
	Frame3D();
	CHECK( !mock->weaponOk && !mock->statsOk, "a stale weapon state or stats" );
	cl.serverTime = 1060;

	// models: registered in MapLoad, drawn in Draw2D only, scaled like the pictures
	CHECK( mock->modelHandle == 9, "RegisterModel gave %d", mock->modelHandle );
	cls.glconfig.vidWidth = 1920;
	cls.glconfig.vidHeight = 1080;
	ResetCounters();
	stModelCalls = stClearScenes = stRenderScenes = 0;
	Frame3D();
	CHECK( stModelCalls == 0 && stRenderScenes == 0, "a model was drawn in Draw3D" );
	CL_Plugins_AfterScene( &fd );
	CHECK( stModelCalls == 1 && stModelHandle == 9 && stClearScenes == 1 && stRenderScenes == 1, "model: %d entities, handle %d, %d scenes", stModelCalls, stModelHandle, stRenderScenes );
	CHECK( stRenderRect[0] == 30 && stRenderRect[1] == 45 && stRenderRect[2] == 90 && stRenderRect[3] == 90 && ( stRenderFlags & RDF_NOWORLDMODEL ),
		"model rectangle %d %d %d %d flags %d", stRenderRect[0], stRenderRect[1], stRenderRect[2], stRenderRect[3], stRenderFlags );

	// ROM cvars
	CHECK( !strcmp( Cvar_VariableString( "mock_rom" ), "fixed" ), "the ROM cvar has '%s'", Cvar_VariableString( "mock_rom" ) );

	// commands
	CHECK( mock->cmdRegistered == 1, "mock_cmd was not registered" );
	CHECK( mock->cmdBadName == 0, "a bad command name was accepted" );
	CHECK( mock->cmdTaken == 0, "an engine command was taken over" );
	CHECK( mock->cmdSecond == 0, "a command was registered twice" );
	CHECK( mock->cmdNoHandler == 0 && !CommandRegistered( "mock_nohandler" ), "a command without a handler was registered" );
	{
		int idx = -1;

		for ( i = 0; i < numAddedCommands; i++ ) {
			if ( !strcmp( addedCommands[i], "mock_cmd" ) ) idx = i;
		}
		CHECK( idx >= 0, "the engine did not get the command" );
		if ( idx >= 0 ) {
			snprintf( stCmdArgs[0], sizeof( stCmdArgs[0] ), "MOCK_CMD" );
			snprintf( stCmdArgs[1], sizeof( stCmdArgs[1] ), "hello" );
			stCmdArgc = 2;
			addedCommandFn[idx]();
			// a command that is not the plugin's is not passed on
			snprintf( stCmdArgs[0], sizeof( stCmdArgs[0] ), "nosuchcmd" );
			addedCommandFn[idx]();
			snprintf( stCmdArgs[0], sizeof( stCmdArgs[0] ), "MOCK_CMD" );
			CHECK( mock->commandCalls == 1, "a command of another name reached the plugin" );
			addedCommandFn[idx]();
			CHECK( mock->commandCalls == 2 && !strcmp( mock->commandName, "MOCK_CMD" ) && mock->commandArgc == 2 && !strcmp( mock->commandArg1, "hello" ),
				"command: calls %d name '%s' argc %d arg '%s'", mock->commandCalls, mock->commandName, mock->commandArgc, mock->commandArg1 );

			// more words than the plugin is given, and a word that is too long: cut, and the plugin is fine
			snprintf( stCmdArgs[0], sizeof( stCmdArgs[0] ), "MOCK_CMD" );
			memset( stCmdArgs[1], 'x', 250 );
			stCmdArgs[1][250] = '\0';
			for ( i = 2; i < 20; i++ ) {
				snprintf( stCmdArgs[i], sizeof( stCmdArgs[i] ), "w%d", i );
			}
			stCmdArgc = 20;
			addedCommandFn[idx]();
			CHECK( mock->commandCalls == 3 && mock->commandArgc == 16 && mock->commandArg1Len == 199 && StateOf( "mock" ) == 0,
				"20 words: calls %d argc %d length of the first %d state %d", mock->commandCalls, mock->commandArgc, mock->commandArg1Len, StateOf( "mock" ) );
			stCmdArgc = 1;
			addedCommandFn[idx]();
			CHECK( mock->commandCalls == 4 && mock->commandArgc == 1 && mock->commandArg1[0] == '\0', "a command without arguments: argc %d", mock->commandArgc );
		}
	}

	// sounds: the owner of an entity is what the snapshots said; the plugin decides
	mock->filterCalls = 0;
	mock->filterResult = 0;
	CHECK( !CL_Plugins_FilterSound( 40, qfalse ), "the sound was dropped although the plugin said no" );
	CHECK( mock->filterCalls >= 1 && mock->filterArgs[0] == 40 && mock->filterArgs[1] == 0 && mock->filterArgs[2] == OWN_CLIENT && mock->filterArgs[3] == OWN_CLIENT,
		"filter args %d %d %d %d", mock->filterArgs[0], mock->filterArgs[1], mock->filterArgs[2], mock->filterArgs[3] );
	CL_Plugins_FilterSound( 41, qtrue );
	CHECK( mock->filterArgs[0] == 41 && mock->filterArgs[1] == 1 && mock->filterArgs[2] == 2, "the missile of client 2: owner %d loop %d", mock->filterArgs[2], mock->filterArgs[1] );
	CL_Plugins_FilterSound( 3, qfalse );
	CHECK( mock->filterArgs[2] == 0, "a player entity without a clientNum field set: %d", mock->filterArgs[2] );
	CL_Plugins_FilterSound( 900, qfalse );
	CHECK( mock->filterArgs[2] == -1, "an entity that was never in a snapshot: %d", mock->filterArgs[2] );
	CL_Plugins_FilterSound( 1023, qfalse );
	CHECK( mock->filterArgs[0] == 1023 && mock->filterArgs[2] == -1, "the world" );
	CL_Plugins_FilterSound( -5, qfalse );
	CL_Plugins_FilterSound( 5000, qfalse );
	CHECK( mock->filterArgs[2] == -1, "out of range entity numbers" );
	mock->filterResult = 1;
	CHECK( CL_Plugins_FilterSound( 41, qfalse ), "the sound was not dropped although the plugin said yes" );
	mock->filterResult = 0;

	// an entity stays known after it left the snapshots
	cl.snap.numEntities = 0;
	CL_Plugins_FilterSound( 41, qfalse );
	CHECK( mock->filterArgs[2] == 2, "owner forgotten after the entity left the snapshot: %d", mock->filterArgs[2] );
	cl.snap = cl.snapshots[12 & PACKET_MASK];
	cls.state = CA_CONNECTED;
	mock->filterCalls = 0;
	CHECK( !CL_Plugins_FilterSound( 41, qfalse ) && mock->filterCalls == 0, "sounds are not filtered outside of a game" );
	cls.state = CA_ACTIVE;
}

static int CommandRegistered( const char *name )
{
	int i;

	for ( i = 0; i < numAddedCommands; i++ ) {
		if ( !strcmp( addedCommands[i], name ) ) {
			return 1;
		}
	}
	return 0;
}

// switching plugins on and off while the game runs
static void TestToggle( void )
{
	char file[256], late[600], src[600];
	int inits, shutdowns, mapLoads, cgameShutdowns, draws;
	FILE *in, *out;

	printf( "switching plugins on and off\n" );

	// mock is off from the start
	WriteDisabled( "mock\nsecond\nbadver\nbadimport\nrefuse\nnoio\n" );
	memset( mock, 0, sizeof( *mock ) );
	numAddedCommands = 0;
	SetUpGame();
	CL_Plugins_Init();
	CL_Plugins_MapLoad( "maps/switch.bsp" );
	CHECK( StateOf( "mock" ) == 4 && mock->initCalls == 0 && !CommandRegistered( "mock_cmd" ), "mock was started although it is off" );
	Frame3D();
	CHECK( mock->draw3d == 0, "an off plugin is drawn" );

	// on: it starts, and gets the map that is loaded
	CHECK( CL_Plugins_SetEnabled( "mock", 1 ) == 1, "SetEnabled gave 0" );
	CHECK( StateOf( "mock" ) == 0 && mock->initCalls == 1, "mock did not start (state %d, init %d)", StateOf( "mock" ), mock->initCalls );
	CHECK( mock->mapLoads == 1 && !strcmp( mock->lastMap, "maps/switch.bsp" ), "the loaded map was not given (%d '%s')", mock->mapLoads, mock->lastMap );
	CHECK( CommandRegistered( "mock_cmd" ), "its command was not registered" );
	Frame3D();
	CHECK( mock->draw3d == 1, "a plugin that was switched on is not drawn" );
	ReadDisabled( file, sizeof( file ) );
	CHECK( strstr( file, "mock" ) == NULL && strstr( file, "second" ) != NULL, "file after switching on: '%s'", file );

	// off: it is told, its commands go, it is not called again
	inits = mock->initCalls; shutdowns = mock->shutdownCalls; mapLoads = mock->mapLoads; cgameShutdowns = mock->cgameShutdowns;
	CHECK( CL_Plugins_SetEnabled( "mock", 0 ) == 1, "SetEnabled gave 0" );
	CHECK( StateOf( "mock" ) == 4, "state %d after switching off", StateOf( "mock" ) );
	CHECK( mock->shutdownCalls == shutdowns + 1 && mock->cgameShutdowns == cgameShutdowns + 1, "stop calls %d %d", mock->shutdownCalls - shutdowns, mock->cgameShutdowns - cgameShutdowns );
	CHECK( !CommandRegistered( "mock_cmd" ), "the command stayed" );
	draws = mock->draw3d;
	Frame3D();
	CHECK( mock->draw3d == draws, "a plugin that was switched off is drawn" );
	ReadDisabled( file, sizeof( file ) );
	CHECK( strstr( file, "mock" ) != NULL, "file after switching off: '%s'", file );

	// again, and the same again changes nothing
	CHECK( CL_Plugins_SetEnabled( "mock", 0 ) == 1 && mock->shutdownCalls == shutdowns + 1, "switching off twice" );
	CHECK( CL_Plugins_SetEnabled( "mock", 1 ) == 1 && mock->initCalls == inits + 1 && mock->mapLoads == mapLoads + 1, "switching on again" );
	CHECK( CL_Plugins_SetEnabled( "mock", 1 ) == 1 && mock->initCalls == inits + 1, "switching on twice" );
	CHECK( CommandRegistered( "mock_cmd" ), "the command is back" );

	// names that are not plugins
	CHECK( CL_Plugins_SetEnabled( "nosuch", 1 ) == 0 && CL_Plugins_SetEnabled( "noentry", 0 ) == 0 && CL_Plugins_SetEnabled( NULL, 0 ) == 0, "unknown names" );
	CHECK( CL_Plugins_SetEnabled( "MOCK", 1 ) == 1, "names are not case sensitive" );

	// a plugin that failed to start: off and on is another try
	CHECK( StateOf( "refuse" ) == 4, "refuse is off" );
	CHECK( CL_Plugins_SetEnabled( "refuse", 1 ) == 1 && StateOf( "refuse" ) == 2, "a plugin that refuses is failed again (%d)", StateOf( "refuse" ) );
	CHECK( CL_Plugins_SetEnabled( "refuse", 0 ) == 1 && StateOf( "refuse" ) == 4, "a failed plugin can be switched off" );
	CHECK( CL_Plugins_SetEnabled( "badimport", 1 ) == 1 && StateOf( "badimport" ) == 3, "badimport after switching on" );
	ResetCounters();
	CHECK( CL_Plugins_SetEnabled( "badimport", 1 ) == 1 && StateOf( "badimport" ) == 3, "badimport after switching on again" );
	CHECK( FindPrint( "called a function the engine does not have" ) >= 0, "a plugin that failed is tried again when it is switched on" );

	// a library that appears in the folder is picked up, and started
	snprintf( src, sizeof( src ), "%s/plugins/mock.qvm", homeDir );
	snprintf( late, sizeof( late ), "%s/plugins/late.qvm", homeDir );
	CHECK( CL_Plugins_Count() == 6, "count %d before the new module", CL_Plugins_Count() );
	in = fopen( src, "rb" );
	out = fopen( late, "wb" );
	if ( in && out ) {
		char chunk[4096];
		size_t n;

		while ( ( n = fread( chunk, 1, sizeof( chunk ), in ) ) > 0 ) {
			fwrite( chunk, 1, n, out );
		}
	}
	if ( in ) fclose( in );
	if ( out ) fclose( out );
	ResetCounters();
	CL_Plugins_Rescan();
	CHECK( CL_Plugins_Count() == 7 && StateOf( "late" ) == 0, "the new library is not listed and running (%d, state %d)", CL_Plugins_Count(), StateOf( "late" ) );
	CHECK( FindPrint( "Client plugin 'late'" ) >= 0, "the new library was not started" );
	ResetCounters();
	CL_Plugins_Rescan();
	CHECK( CL_Plugins_Count() == 7 && numTestPrints == 0, "a second scan did something (%d prints)", numTestPrints );

	CL_Plugins_CGameShutdown();
	CL_Plugins_Shutdown();
	remove( late );
}

// a plugin that trapped is stopped; switching it off and on is another try
static void TestStoppedToggle( void )
{
	char file[256], text[256];

	printf( "switching a plugin that was stopped off and on\n" );
	WriteDisabled( "second\nbadver\nbadimport\nrefuse\nnoio\n" );
	memset( mock, 0, sizeof( *mock ) );
	SetUpGame();
	CL_Plugins_Init();
	CL_Plugins_MapLoad( "maps/test.bsp" );
	mock->crashInDraw3D = 1;
	Frame3D();
	mock->crashInDraw3D = 0;
	CHECK( StateOf( "mock" ) == 3, "state %d after the trap", StateOf( "mock" ) );
	ListedState( "mock", 2, text, sizeof( text ) );
	CHECK( strstr( text, "stopped in OnDraw3D" ) != NULL && strstr( text, "division" ) != NULL, "reason of the trap: '%s'", text );
	ListedState( "mock", 1, text, sizeof( text ) );
	CHECK( text[0] == '\0', "a stopped plugin shows a version: '%s'", text );

	CHECK( CL_Plugins_SetEnabled( "mock", 0 ) == 1, "SetEnabled gave 0" );
	CHECK( StateOf( "mock" ) == 4 && mock->shutdownCalls == 0, "a stopped plugin was called (state %d)", StateOf( "mock" ) );
	ListedState( "mock", 3, text, sizeof( text ) );
	CHECK( !strcmp( text, "1" ), "the choice is not shown" );
	ReadDisabled( file, sizeof( file ) );
	CHECK( strstr( file, "mock" ) != NULL, "the choice was not saved: '%s'", file );

	CHECK( CL_Plugins_SetEnabled( "mock", 1 ) == 1 && StateOf( "mock" ) == 0 && mock->initCalls == 2, "the plugin did not start again (state %d, init %d)", StateOf( "mock" ), mock->initCalls );
	Frame3D();
	CHECK( mock->draw3d == 1, "a plugin that was started again is not drawn (%d)", mock->draw3d );
	ReadDisabled( file, sizeof( file ) );
	CHECK( strstr( file, "mock" ) == NULL, "switching on was not saved: '%s'", file );

	// a plugin that is stopped while it is on can be switched on again without going through off
	mock->crashInDraw3D = 1;
	Frame3D();
	mock->crashInDraw3D = 0;
	CHECK( StateOf( "mock" ) == 3, "second trap" );
	CHECK( CL_Plugins_SetEnabled( "mock", 1 ) == 1 && StateOf( "mock" ) == 0, "switching on a stopped plugin is another try (state %d)", StateOf( "mock" ) );

	CL_Plugins_CGameShutdown();
	CL_Plugins_Shutdown();
}

// the file is read as people edit it
static void TestDisabledFile( void )
{
	char file[256];

	printf( "the disabled file\n" );
	// a byte order mark, other cases, windows line ends, comments, blank lines, a name with a path, a name that is not there
	WriteDisabled( "\xEF\xBB\xBFMOCK\r\n\r\n# note\r\n  second  \r\n../evil\r\nnosuch\r\nSECOND\r\n" );
	SetUpGame();
	CL_Plugins_Init();
	CHECK( StateOf( "mock" ) == 4 && StateOf( "second" ) == 4, "names were not read (%d %d)", StateOf( "mock" ), StateOf( "second" ) );
	CHECK( StateOf( "badver" ) == 2, "a plugin that is not in the file is off" );
	ReadDisabled( file, sizeof( file ) );
	CHECK( !strcmp( file, "mock\nsecond\n" ), "names of libraries that are gone are dropped, got '%s'", file );
	CL_Plugins_Shutdown();

	// nothing to clean up: the file is left alone
	WriteDisabled( "second\nmock\n" );
	CL_Plugins_Init();
	ReadDisabled( file, sizeof( file ) );
	CHECK( !strcmp( file, "second\nmock\n" ), "a clean file was rewritten: '%s'", file );
	CL_Plugins_Shutdown();

	// no file
	{
		char path[600];

		snprintf( path, sizeof( path ), "%s/plugins/disabled", homeDir );
		remove( path );
		CL_Plugins_Init();
		CHECK( StateOf( "mock" ) == 0 && StateOf( "second" ) == 0, "without the file everything starts" );
		// every mock registers shared_view with the shared flag: both read it
		CHECK( !strcmp( mock->sharedSeen, "7" ) && !strcmp( oldMock->sharedSeen, "7" ), "a shared cvar must be readable by both: '%s' '%s'", mock->sharedSeen, oldMock->sharedSeen );
		CL_Plugins_Shutdown();
		CHECK( !FileExists( "disabled" ), "the file was made without a reason" );
	}
}

static void TestTraps( void )
{
	int draws, draws2d, secondDraws;
	refdef_t fd = TestRefdef();

	printf( "plugins that trap\n" );
	// the second plugin is on in this session; it must not be affected by the first one going down
	CHECK( StateOf( "second" ) == 0, "second is not running (%d)", StateOf( "second" ) );
	secondDraws = oldMock->draw3d;

	mock->crashInDraw3D = 1;
	ResetCounters();
	Frame3D();
	CHECK( FindPrint( "stopped in OnDraw3D" ) >= 0, "the trap was not reported" );
	CHECK( FindPrint( "division" ) >= 0, "the reason was not reported" );
	CHECK( oldMock->draw3d == secondDraws + 1, "the plugin after the trapped one was not called (%d)", oldMock->draw3d - secondDraws );
	draws = mock->draw3d;
	draws2d = mock->draw2d;

	mock->crashInDraw3D = 0;
	Frame3D();
	CHECK( mock->draw3d == draws, "draw3d was called again after the trap" );
	CL_Plugins_AfterScene( &fd );
	CHECK( mock->draw2d == draws2d, "draw2d was called after the trap" );
	CHECK( oldMock->draw3d == secondDraws + 2 && oldMock->draw2d >= 1, "the other plugin stopped too" );

	numTestPrints = 0;
	CL_Plugins_CGameShutdown();
	CL_Plugins_Shutdown();
	CHECK( FindPrint( "bye" ) >= 0, "shutdown was not called on the plugin that is fine" );
	CHECK( mock->shutdownCalls == 0, "shutdown was called on a plugin that trapped" );
}

// a pointer outside of the memory of the plugin, and a plugin that never returns
static void TestBadBehaviour( void )
{
	int calls;

	printf( "a bad pointer and an endless loop\n" );
	WriteDisabled( "second\nbadver\nbadimport\nrefuse\nnoio\n" );
	memset( mock, 0, sizeof( *mock ) );
	numAddedCommands = 0;
	SetUpGame();
	CL_Plugins_Init();
	CL_Plugins_MapLoad( "maps/test.bsp" );

	// the engine does not touch a pointer outside of the memory of the plugin, and stops the plugin
	mock->badPointerInDraw3D = 1;
	ResetCounters();
	Frame3D();
	CHECK( StateOf( "mock" ) == 3, "state %d after a bad pointer", StateOf( "mock" ) );
	CHECK( FindPrint( "stopped in OnDraw3D" ) >= 0, "no report for a bad pointer" );

	// an endless loop runs out of its budget instead of hanging the game
	mock->badPointerInDraw3D = 0;
	CHECK( CL_Plugins_SetEnabled( "mock", 1 ) == 1 && StateOf( "mock" ) == 0, "restart (%d)", StateOf( "mock" ) );
	{
		int idx = -1, i;

		for ( i = 0; i < numAddedCommands; i++ ) {
			if ( !strcmp( addedCommands[i], "mock_cmd" ) ) idx = i;
		}
		CHECK( idx >= 0, "the command of the restarted plugin" );
		mock->loopInCommand = 1;
		calls = mock->commandCalls;
		ResetCounters();
		if ( idx >= 0 ) {
			snprintf( stCmdArgs[0], sizeof( stCmdArgs[0] ), "mock_cmd" );
			stCmdArgc = 1;
			addedCommandFn[idx]();
		}
		CHECK( StateOf( "mock" ) == 3, "the endless loop was not stopped (state %d, calls %d -> %d)", StateOf( "mock" ), calls, mock->commandCalls );
		CHECK( FindPrint( "instruction budget" ) >= 0, "the reason was not the budget" );
		CHECK( !CommandRegistered( "mock_cmd" ), "the command of a stopped plugin stayed" );
	}
	CL_Plugins_CGameShutdown();
	CL_Plugins_Shutdown();
}

int main( int argc, char **argv )
{
	if ( argc < 2 ) {
		fprintf( stderr, "usage: client_plugin_test <home dir>\n" );
		return 2;
	}
	homeDir = argv[1];

	Stubs_InitRenderer();
	Test_SetCvar( "fs_homepath", homeDir, 0 );
	Test_SetCvar( "mock_float", "2.5", 0 );
	Test_SetCvar( "mock_secret", "hush", CVAR_PRIVATE );
	Test_SetCvar( "mock_visible", "yes", 0 );
	Test_SetCvar( "mock_rom", "from an old config", 0 );
	Test_SetCvar( "rconpassword", "topsecret", 0 );
	Test_SetCvar( "cg_draw2D", "1", 0 );

	// ---- a session that ends normally ----
	WriteDisabled( "second\n" );
	SetUpGame();
	CL_Plugins_Init();
	TestLoading();
	TestDrawingRules();
	TestTrace();
	TestSnapshots();
	TestPlayerState();
	TestMoveState();
	TestV4();
	TestGhostPath();
	TestStateApi();
	TestDemoRoute();
	TestDemoApi();
	TestMapChange();
	Frame3D();
	CHECK( !mock->ghostInfoOk, "a ghost path survived the end of the game module" );

	printf( "shutdown\n" );
	numTestPrints = 0;
	CL_Plugins_CGameShutdown();
	CL_Plugins_Shutdown();
	CHECK( FindPrint( "bye" ) >= 0, "Shutdown of the plugin was not called" );
	{
		int draws = mock->draw3d;

		Frame3D();
		CHECK( mock->draw3d == draws, "a plugin was called after Shutdown" );
	}

	// ---- a session where one plugin traps and another one does not ----
	memset( mock, 0, sizeof( *mock ) );
	memset( oldMock, 0, sizeof( *oldMock ) );
	WriteDisabled( "badver\nbadimport\nrefuse\nnoio\n" );
	SetUpGame();
	CL_Plugins_Init();
	CL_Plugins_MapLoad( "maps/test.bsp" );
	TestTraps();

	TestBadBehaviour();
	TestToggle();
	TestStoppedToggle();
	TestDisabledFile();

	if ( failures ) {
		printf( "%d FAILED\n", failures );
		return 1;
	}
	printf( "all client plugin tests passed\n" );
	return 0;
}
