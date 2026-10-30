/*
A stand-in for a client plugin, controlled by the test program through the exported Mock_State.
It records what the engine hands it, and tries the drawing calls in phases where they must be ignored.
*/

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "mock_state.h"

static const odfcPluginImport_t *eng;
static mockState_t st;
static int shader;
static volatile int *volatile nullPtr;

ODFC_EXPORT mockState_t *Mock_State( void ) { return &st; }

static const float lineA[3] = { 0, 0, 0 }, lineB[3] = { 100, 0, 0 };

static void TryDrawing2D( void )
{
	static const float white[4] = { 1, 1, 1, 1 };

	eng->SetColor( white );
	eng->DrawStretchPic( 1, 2, 3, 4, 0, 0, 1, 1, shader );
}

static void TryDrawing3D( void )
{
	odfcPolyVert_t v[3];
	static const unsigned char rgba[4] = { 255, 0, 0, 255 };

	memset( v, 0, sizeof( v ) );
	v[0].xyz[0] = 5;
	eng->AddPoly( shader, 3, v );
	eng->AddBeam( shader, lineA, lineB, rgba );
}

static const float modelOrigin[3] = { 70, 0, 0 }, modelAngles[3] = { 0, 90, 0 };

static void Command( const char *name )
{
	char buf[32];

	st.commandCalls++;
	snprintf( st.commandName, sizeof( st.commandName ), "%s", name );
	st.commandArgc = eng->CommandArgc();
	eng->CommandArgv( 1, buf, sizeof( buf ) );
	snprintf( st.commandArg1, sizeof( st.commandArg1 ), "%s", buf );
	if ( st.crashInCommand ) {
		*nullPtr = 1;
	}
}

static int FilterSound( int entityNum, int isLoop, int entityClientNum, int ownClientNum )
{
	st.filterCalls++;
	st.filterArgs[0] = entityNum;
	st.filterArgs[1] = isLoop;
	st.filterArgs[2] = entityClientNum;
	st.filterArgs[3] = ownClientNum;
	return st.filterResult;
}

static int Init( void )
{
	char buf[64];

	eng->CvarRegister( "mock_rom", "fixed", ODFC_CVAR_ROM );
	st.cmdRegistered = eng->CommandRegister( "mock_cmd" );
	st.cmdBadName = eng->CommandRegister( "Bad Name" );
	st.cmdTaken = eng->CommandRegister( "quit" );
	st.cmdSecond = eng->CommandRegister( "mock_cmd" );

	st.initCalls++;
	eng->CvarRegister( "mock_setting", "5", ODFC_CVAR_ARCHIVE );
	st.cvarDefault = eng->CvarInteger( "mock_setting" );
	st.cvarValue = eng->CvarValue( "mock_float" );
	eng->CvarString( "mock_secret", buf, sizeof( buf ) );
	snprintf( st.secretSeen, sizeof( st.secretSeen ), "%s", buf );
	eng->CvarString( "mock_visible", buf, sizeof( buf ) );
	snprintf( st.visibleSeen, sizeof( st.visibleSeen ), "%s", buf );
	return 0;
}

static void Shutdown( void )
{
	st.shutdownCalls++;
	eng->Print( "bye" );
}

static void MapLoad( const char *mapName )
{
	st.mapLoads++;
	snprintf( st.lastMap, sizeof( st.lastMap ), "%s", mapName );
	shader = eng->RegisterShader( "railCore" );
	st.mapLoadShader = shader;
	st.modelHandle = eng->RegisterModel( "models/test.md3" );
	TryDrawing3D();		// not allowed here: must not reach the renderer
	TryDrawing2D();
}

static void CGameShutdown( void )
{
	st.cgameShutdowns++;
}

static void Draw3D( const odfcView_t *view )
{
	volatile float zero = 0.0f;
	float nan = zero / zero, bad[3], mins[3] = { -1, -1, -1 }, maxs[3] = { 1, 1, 1 };

	st.draw3d++;
	st.view = *view;
	if ( st.crashInDraw3D ) {
		*nullPtr = 1;
	}
	st.snapOk = eng->GetSnapshot( &st.snap );
	st.cheatsFlag = eng->CheatsEnabled();
	st.psOk = eng->GetPlayerState( &st.ps );
	st.msOk = eng->GetMoveState( &st.ms );
	{
		float low[3] = { 0, 0, -50 }, high[3] = { 0, 0, 50 }, nanPoint[3];

		nanPoint[0] = nan; nanPoint[1] = 0; nanPoint[2] = -50;
		st.contentsLow = eng->PointContents( low );
		st.contentsHigh = eng->PointContents( high );
		st.contentsNaN = eng->PointContents( nanPoint );
	}

	eng->Trace( &st.traceGood, lineA, mins, maxs, lineB, ODFC_MASK_SHOT );
	bad[0] = nan; bad[1] = 0; bad[2] = 0;
	eng->Trace( &st.traceNaN, bad, NULL, NULL, lineB, ODFC_MASK_SHOT );
	eng->Trace( &st.traceNull, NULL, NULL, NULL, lineB, ODFC_MASK_SHOT );

	TryDrawing3D();
	TryDrawing2D();		// not allowed here
	st.weaponOk = eng->GetWeaponState( &st.weapons );
	eng->DrawModel( 1, 2, 3, 4, st.modelHandle, modelOrigin, modelAngles, 100 );		// 2D only: dropped here
}

static void Draw2D( int phase )
{
	st.draw2d++;
	if ( st.numPhases < 8 ) {
		st.phases[st.numPhases++] = phase;
	}
	TryDrawing2D();
	TryDrawing3D();		// not allowed here
	if ( st.modelHandle ) {
		eng->DrawModel( 10, 20, 30, 40, st.modelHandle, modelOrigin, modelAngles, 100 );
	}
}

static odfcPluginExport_t exports = {
	sizeof( odfcPluginExport_t ), ODFC_API_VERSION, "mock", "mock 1.0",
	Init, Shutdown, MapLoad, CGameShutdown, Draw3D, Draw2D, Command, FilterSound
};

ODFC_EXPORT const odfcPluginExport_t *ODF_ClientPluginEntry( const odfcPluginImport_t *imp )
{
	eng = imp;
#ifdef BAD_VERSION
	exports.apiVersion = ODFC_API_VERSION + 1;
#endif
#ifdef BAD_SIZE
	exports.size = sizeof( int );
#endif
#ifdef OLD_EXPORTS
	// built for version 3: the table ends with Draw2D
	exports.size = (int)offsetof( odfcPluginExport_t, Command );
	exports.apiVersion = 3;
#endif
#ifdef REFUSE
	return NULL;
#endif
	return &exports;
}
