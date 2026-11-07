/*
Loads a real client plugin (a .qvm) with the real host code of the engine (cl_plugin.c) and runs it against the fake engine of
the other tests: starts it, loads a map, draws frames, and says how long a frame of the plugin takes. Not a test of what the
plugin draws (its own tests do that), but of whether it runs at all as a QVM, compiled and interpreted.

    plugin_smoke <home dir> <plugin name> <jit|interp> [frames] [cvar=value ...]

The plugin is <home dir>/plugins/<plugin name>.qvm.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "client.h"
#include "stubs.h"
#include "plugin_vm.h"
#include "odfc_types.h"

#define OWN_CLIENT 1

extern int numTestPrints;
void Stubs_InitRenderer( void );

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
	VectorSet( s->ps.velocity, 320, 100, 0 );
	s->numEntities = 4;
	s->parseEntitiesNum = first;

	for ( i = 0; i < 4; i++ ) {
		memset( &cl.parseEntities[first + i], 0, sizeof( entityState_t ) );
	}
	e = &cl.parseEntities[first];
	e[0].number = 3;  e[0].eType = ET_PLAYER;
	e[1].number = 40; e[1].eType = ET_MISSILE; e[1].clientNum = OWN_CLIENT; e[1].weapon = WP_ROCKET_LAUNCHER;
	e[1].pos.trType = TR_LINEAR; e[1].pos.trTime = serverTime; VectorSet( e[1].pos.trDelta, 900, 0, 0 );
	e[2].number = 41; e[2].eType = ET_MISSILE; e[2].clientNum = OWN_CLIENT; e[2].weapon = WP_GRENADE_LAUNCHER;
	e[2].pos.trType = TR_GRAVITY; e[2].pos.trTime = serverTime; VectorSet( e[2].pos.trDelta, 300, 100, 200 );
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
	cls.glconfig.vidWidth = 1920;
	cls.glconfig.vidHeight = 1080;
	cl_connectedToCheatServer = 0;
	clc.demoplaying = qfalse;
}

static refdef_t Refdef( void )
{
	refdef_t fd;

	memset( &fd, 0, sizeof( fd ) );
	fd.width = 1280; fd.height = 720;
	fd.fov_x = 90; fd.fov_y = 58.5f;
	VectorSet( fd.vieworg, 100, 200, 300 );
	VectorSet( fd.viewaxis[0], 1, 0, 0 );
	VectorSet( fd.viewaxis[1], 0, 1, 0 );
	VectorSet( fd.viewaxis[2], 0, 0, 1 );
	return fd;
}

// every shader exists, so that a plugin that needs one draws
static qhandle_t SmokeShader( const char *name )
{
	return 9;
}

static double Now( void )
{
	struct timespec ts;

	clock_gettime( CLOCK_MONOTONIC, &ts );
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// one run in one mode; returns qfalse when the plugin did not start or got stopped
static qboolean Run( const char *home, const char *name, int frames, int interpret, char **settings, int numSettings )
{
	refdef_t fd;
	double start, ms;
	int i, j, prints;

	pluginVMInterpret = interpret;
	printf( "-- %s\n", interpret ? "interpreter" : "compiled" );
	Stubs_InitRenderer();
	re.RegisterShader = SmokeShader;
	re.RegisterShaderNoMip = SmokeShader;
	Test_SetCvar( "fs_homepath", home, 0 );
	Test_SetCvar( "cg_draw2D", "1", 0 );
	SetUpGame();
	numTestPrints = 0;
	CL_Plugins_Init();
	CL_Plugins_MapLoad( "maps/test.bsp" );
	for ( i = 0; i < numTestPrints; i++ ) {
		printf( "  %s", testPrints[i] );
		if ( !strchr( testPrints[i], '\n' ) ) {
			printf( "\n" );
		}
	}
	// after the start: the plugin can only read the cvars it made itself
	for ( i = 0; i < numSettings; i++ ) {
		char key[64];
		char *eq = strchr( settings[i], '=' );

		if ( eq ) {
			Q_strncpyz( key, settings[i], (int)( eq - settings[i] ) + 1 );
			Test_SetCvar( key, eq + 1, 0 );
		}
	}
	if ( CL_Plugins_Count() < 1 ) {
		printf( "no plugin was listed\n" );
		return qfalse;
	}
	{
		char text[256];

		CL_Plugins_Info( 0, 2, text, sizeof( text ) );
		printf( "  state %d %s\n", CL_Plugins_Info( 0, 0, text, sizeof( text ) ), text );
	}

	// ODF_SMOKE_COMMANDS=1: every console command the plugin registered is run twice, with the arguments "one two"
	if ( getenv( "ODF_SMOKE_COMMANDS" ) ) {
		for ( i = 0; i < numAddedCommands; i++ ) {
			if ( !addedCommandFn[i] ) {
				continue;
			}
			Q_strncpyz( stCmdArgs[0], addedCommands[i], sizeof( stCmdArgs[0] ) );
			Q_strncpyz( stCmdArgs[1], "one", sizeof( stCmdArgs[1] ) );
			Q_strncpyz( stCmdArgs[2], "two", sizeof( stCmdArgs[2] ) );
			stCmdArgc = 3;
			for ( j = 0; j < 2; j++ ) {
				numTestPrints = 0;
				addedCommandFn[i]();
				if ( numTestPrints > 0 ) {
					printf( "  command %s -> %s", addedCommands[i], testPrints[0] );
				}
			}
		}
		stCmdArgc = 0;
	}

	// ODF_SMOKE_GHOST=n: a ghost of n points in slot 0 (50 ms apart, near where the player runs, or 600 units away with a "far" after the
	// number) and a race that runs; exercises the plugins that follow a ghost
	if ( getenv( "ODF_SMOKE_GHOST" ) ) {
		const int n = atoi( getenv( "ODF_SMOKE_GHOST" ) );
		const float yOff = strstr( getenv( "ODF_SMOKE_GHOST" ), "far" ) ? 600.0f : 0.0f;
		odfcGhostPoint_t pts[256];
		int sent = 0, c, k;

		while ( sent < n ) {
			c = n - sent < 256 ? n - sent : 256;
			for ( k = 0; k < c; k++ ) {
				memset( &pts[k], 0, sizeof( pts[k] ) );
				pts[k].ms = ( sent + k ) * 50;
				VectorSet( pts[k].origin, 100.0f + ( ( sent + k ) % 120 ), 200.0f + yOff + ( ( sent + k ) / 120 ) * 3.0f, 300.0f );
			}
			CL_Plugins_SetGhostPath( 0, sent, c, pts, sent + c == n );
			sent += c;
		}
	}

	stPolyCalls = stPolyVerts = stBeamCalls = stSetColorCalls = stStretchCalls = stTraceCalls = stTraceBoxCalls = 0;
	numTestPrints = 0;
	start = Now();
	for ( j = 0; j < frames; j++ ) {
		// what the game module of oDFs publishes each frame: the state the next frame of the player is predicted from
		{
			playerState_t ps;

			memset( &ps, 0, sizeof( ps ) );
			ps.clientNum = OWN_CLIENT;
			ps.speed = 320;
			ps.gravity = 800;
			ps.weapon = WP_ROCKET_LAUNCHER;
			ps.viewheight = 26;
			ps.groundEntityNum = ENTITYNUM_NONE;
			ps.stats[STAT_HEALTH] = 100;
			ps.stats[STAT_WEAPONS] = ( 1 << WP_ROCKET_LAUNCHER ) | ( 1 << WP_GRENADE_LAUNCHER );
			ps.ammo[WP_ROCKET_LAUNCHER] = 10;
			ps.ammo[WP_GRENADE_LAUNCHER] = 5;
			if ( getenv( "ODF_SMOKE_GHOST" ) ) {		// DeFRaG's race: the timer runs, ms in stats[7] / [8]
				ps.stats[12] = 2 | 4;
				ps.stats[7] = ( j * 8 ) >> 16;
				ps.stats[8] = ( j * 8 ) & 0xffff;
				ps.commandTime = cl.snap.serverTime + 8;
			}
			VectorSet( ps.origin, 100 + j % 100, 200, 300 );
			VectorSet( ps.velocity, 320, 100 + j % 50, -50 );
			VectorSet( ps.viewangles, 10, ( j * 3 ) % 360, 0 );
			CL_Plugins_SetPlayerState( &ps, 0 );
		}
		fd = Refdef();
		CL_Plugins_BeforeScene( &fd );
		CL_Plugins_AfterScene( &fd );
		CL_Plugins_AfterFrame();
	}
	ms = Now() - start;
	printf( "  %d frames in %.1f ms: %.1f us a frame\n", frames, ms, ms * 1000.0 / frames );
	printf( "  per frame: %.1f beams, %.1f polys, %.1f stretch pics, %.1f traces\n", (double)stBeamCalls / frames,
		(double)stPolyCalls / frames, (double)stStretchCalls / frames, (double)stTraceCalls / frames );
	prints = numTestPrints;
	for ( i = 0; i < prints && i < MAX_TEST_PRINTS; i++ ) {
		if ( strstr( testPrints[i], "stopped" ) || strstr( testPrints[i], "error" ) ) {
			printf( "  PRINT: %s\n", testPrints[i] );
		}
	}
	CL_Plugins_CGameShutdown();
	CL_Plugins_Shutdown();
	return qtrue;
}

int main( int argc, char **argv )
{
	int frames = 5000, interpret;

	if ( argc < 4 ) {
		fprintf( stderr, "usage: plugin_smoke <home dir> <plugin name> <jit|interp> [frames] [cvar=value ...]\n" );
		return 2;
	}
	interpret = !strcmp( argv[3], "interp" );
	if ( argc > 4 ) {
		frames = atoi( argv[4] );
	}
	return Run( argv[1], argv[2], frames, interpret, argv + 5, argc > 5 ? argc - 5 : 0 ) ? 0 : 1;
}
