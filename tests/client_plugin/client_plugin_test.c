/*
Tests the real client plugin host (cl_plugin.c, sv_plugin_guard.c) against a mock plugin, with the engine
functions it calls stubbed out (stubs.c). Not part of the engine build, see run.sh.
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "client.h"
#include "stubs.h"
#include "mock_state.h"

void Stubs_InitRenderer( void );

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

static mockState_t *mock;
static const char *homeDir;

static void *OpenModule( const char *path )
{
#ifdef _WIN32
	return (void *)LoadLibraryA( path );
#else
	return dlopen( path, RTLD_NOW );
#endif
}

static void *LoadSym( void *lib, const char *name )
{
#ifdef _WIN32
	return (void *)GetProcAddress( (HMODULE)lib, name );
#else
	return dlsym( lib, name );
#endif
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

static void TestLoading( void )
{
	printf( "loading\n" );
	CHECK( FindPrint( "Client plugin 'mock' mock 1.0 loaded" ) >= 0, "mock was not loaded" );
	CHECK( FindPrint( "built for client plugin API" ) >= 0, "a plugin of a newer API must be refused" );
	CHECK( FindPrint( "export table is too small" ) >= 0, "a small export table must be refused" );
	CHECK( FindPrint( "refused to load" ) >= 0, "a plugin that returns NULL is not loaded" );
	CHECK( FindPrint( "has no ODF_ClientPluginEntry" ) >= 0, "a server plugin must not load on the client" );
	CHECK( FindPrint( "invalid name" ) >= 0, "path tricks in a name must be refused" );
	CHECK( FindPrint( "not found" ) >= 0, "a missing plugin must be reported" );

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
	CHECK( mock->traceNull.fraction == 1.0f, "NULL trace fraction %f", mock->traceNull.fraction );
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


static mockState_t *oldMock;

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
		CL_Plugins_SetPlayerState( &ps, 0 );
	}
	Frame3D();
	CHECK( mock->weaponOk && mock->weapons.weapons == ( ( 1 << 2 ) | ( 1 << 5 ) ) && mock->weapons.ammo[2] == 100 && mock->weapons.ammo[5] == 7 && mock->weapons.ammo[15] == 99,
		"weapon state: ok %d weapons %x ammo %d %d", mock->weaponOk, mock->weapons.weapons, mock->weapons.ammo[2], mock->weapons.ammo[5] );
	cl.serverTime = 1070;
	Frame3D();
	CHECK( !mock->weaponOk, "a stale weapon state" );
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
			CHECK( mock->commandCalls == 2 && !strcmp( mock->commandName, "mock_cmd" ) && mock->commandArgc == 2 && !strcmp( mock->commandArg1, "hello" ),
				"command: calls %d name '%s' argc %d arg '%s'", mock->commandCalls, mock->commandName, mock->commandArgc, mock->commandArg1 );
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

// a plugin built for version 3 has a shorter export table: it keeps working and gets none of the new calls
static void TestOldExports( void )
{
	refdef_t fd = TestRefdef();
	int i;

	printf( "a plugin with the export table of version 3\n" );
	numAddedCommands = 0;
	stCmdArgc = 0;
	Test_SetCvar( "cl_plugins", "oldexp", 0 );
	SetUpGame();
	CL_Plugins_Init();
	CL_Plugins_MapLoad( "maps/test.bsp" );
	CHECK( oldMock && oldMock->initCalls == 1, "the plugin with the old export table was not started" );
	if ( oldMock ) {
		CHECK( oldMock->cmdRegistered == 1, "its command was not registered" );
		Frame3D();
		CL_Plugins_AfterScene( &fd );
		CHECK( oldMock->draw3d == 1 && oldMock->draw2d == 1, "the old plugin is not drawn (%d %d)", oldMock->draw3d, oldMock->draw2d );
		for ( i = 0; i < numAddedCommands; i++ ) {
			if ( addedCommandFn[i] ) {
				snprintf( stCmdArgs[0], sizeof( stCmdArgs[0] ), "%.31s", addedCommands[i] );
				stCmdArgc = 1;
				addedCommandFn[i]();
			}
		}
		CHECK( !CL_Plugins_FilterSound( 41, qfalse ), "a sound was dropped by a plugin that cannot filter" );
		CHECK( oldMock->filterCalls == 0 && oldMock->commandCalls == 0, "new calls reached a plugin that cannot have them (%d %d)", oldMock->filterCalls, oldMock->commandCalls );
	}
	CL_Plugins_CGameShutdown();
	CL_Plugins_Shutdown();
}

static void TestCrash( void )
{
	int draws, draws2d;

	printf( "a plugin that crashes\n" );
	mock->crashInDraw3D = 1;
	ResetCounters();
	Frame3D();
	CHECK( FindPrint( "crashed in Draw3D" ) >= 0, "the crash was not reported" );
	draws = mock->draw3d;
	draws2d = mock->draw2d;

	mock->crashInDraw3D = 0;
	Frame3D();
	CHECK( mock->draw3d == draws, "Draw3D was called again after the crash" );
	{
		refdef_t fd = TestRefdef();

		CL_Plugins_AfterScene( &fd );
	}
	CHECK( mock->draw2d == draws2d, "Draw2D was called after the crash" );

	numTestPrints = 0;
	CL_Plugins_CGameShutdown();
	CL_Plugins_Shutdown();
	CHECK( FindPrint( "bye" ) < 0, "Shutdown was called on a crashed plugin" );
}

int main( int argc, char **argv )
{
	void *lib;
	char path[512];

	if ( argc < 2 ) {
		fprintf( stderr, "usage: client_plugin_test <home dir>\n" );
		return 2;
	}
	homeDir = argv[1];

	snprintf( path, sizeof( path ), "%s/plugins/mock%s", homeDir,
#ifdef _WIN32
		".dll"
#else
		".so"
#endif
	);
	lib = OpenModule( path );
	if ( !lib ) {
		fprintf( stderr, "cannot load %s\n", path );
		return 2;
	}
	mock = ( (mockState_t *( * )( void ))LoadSym( lib, "Mock_State" ) )();
	{
		void *oldLib;

		snprintf( path, sizeof( path ), "%s/plugins/oldexp%s", homeDir,
#ifdef _WIN32
			".dll"
#else
			".so"
#endif
		);
		oldLib = OpenModule( path );
		if ( oldLib ) {
			oldMock = ( (mockState_t *( * )( void ))LoadSym( oldLib, "Mock_State" ) )();
		}
	}

	Stubs_InitRenderer();
	Test_SetCvar( "fs_homepath", homeDir, 0 );
	Test_SetCvar( "mock_float", "2.5", 0 );
	Test_SetCvar( "mock_secret", "hush", CVAR_PRIVATE );
	Test_SetCvar( "mock_visible", "yes", 0 );
	Test_SetCvar( "mock_rom", "from an old config", 0 );

	// ---- a session that ends normally ----
	Test_SetCvar( "cl_plugins", "mock badver badsize refuse noentry ../evil nosuch", 0 );
	SetUpGame();
	CL_Plugins_Init();
	TestLoading();
	TestDrawingRules();
	TestTrace();
	TestSnapshots();
	TestPlayerState();
	TestMoveState();
	TestV4();
	TestMapChange();

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

	TestOldExports();

	// ---- a session where the plugin crashes ----
	memset( mock, 0, sizeof( *mock ) );
	Test_SetCvar( "cl_plugins", "mock", 0 );
	SetUpGame();
	CL_Plugins_Init();
	CL_Plugins_MapLoad( "maps/test.bsp" );
	TestCrash();

	if ( failures ) {
		printf( "%d FAILED\n", failures );
		return 1;
	}
	printf( "all client plugin tests passed\n" );
	return 0;
}
