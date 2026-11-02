/*
A stand-in for a client plugin (built for WebAssembly), controlled by the test program through mock_state.h.
It records what the engine hands it, and tries the drawing calls in phases where they must be ignored.
*/

#include <string.h>

#include "odfc.h"
#include "mock_state.h"

// the two imports only the test build of the engine has
ODF_IMPORT( "odfc", "TestPull" ) void odfc_TestPull( void *state, int size );
ODF_IMPORT( "odfc", "TestPush" ) void odfc_TestPush( const void *state, int size );

ODF_DEFINE_IO()

// the cvars and commands of a plugin are its own: each variant of this plugin gets its own names
#ifndef PFX
#define PFX "mock"
#endif

#if defined( BAD_VERSION )
ODF_MANIFEST( "version=future\napi=99" )
#elif defined( SECOND )
ODF_MANIFEST( "version=second 1.0" )
#else
ODF_MANIFEST( "version=mock 1.0" )
#endif

static mockState_t st;
static int shader;

static void Copy( char *dst, const char *src, int size )
{
	int i;

	for ( i = 0; i < size - 1 && src[i]; i++ ) {
		dst[i] = src[i];
	}
	dst[i] = '\0';
}

static void Begin( void ) { odfc_TestPull( &st, (int)sizeof( st ) ); }
static void End( void ) { odfc_TestPush( &st, (int)sizeof( st ) ); }

static const float lineA[3] = { 0, 0, 0 }, lineB[3] = { 100, 0, 0 };

static void TryDrawing2D( void )
{
	static const float white[4] = { 1, 1, 1, 1 };

	odfc_SetColor( white );
	odfc_DrawStretchPic( 1, 2, 3, 4, 0, 0, 1, 1, shader );
}

static void TryDrawing3D( void )
{
	odfcPolyVert_t v[3];
	static const unsigned char rgba[4] = { 255, 0, 0, 255 };

	memset( v, 0, sizeof( v ) );
	v[0].xyz[0] = 5;
	odfc_AddPoly( shader, 3, v );
	odfc_AddBeam( shader, lineA, lineB, rgba );
}

static const float modelOrigin[3] = { 70, 0, 0 }, modelAngles[3] = { 0, 90, 0 };

void odfc_command( const char *name )
{
	char buf[32];

	Begin();
	st.commandCalls++;
	Copy( st.commandName, name, sizeof( st.commandName ) );
	st.commandArgc = odfc_CommandArgc();
	odfc_CommandArgv( 1, buf, sizeof( buf ) );
	Copy( st.commandArg1, buf, sizeof( st.commandArg1 ) );
	if ( st.loopInCommand ) {
		volatile int spin = 0;

		for ( ;; ) {
			spin++;
		}
	}
	End();
}

int odfc_filter_sound( int entityNum, int isLoop, int entityClientNum, int ownClientNum )
{
	int result;

	Begin();
	st.filterCalls++;
	st.filterArgs[0] = entityNum;
	st.filterArgs[1] = isLoop;
	st.filterArgs[2] = entityClientNum;
	st.filterArgs[3] = ownClientNum;
	result = st.filterResult;
	End();
	return result;
}

int odfc_init( void )
{
	char buf[64];

	Begin();
	odfc_CvarRegister( PFX "_rom", "fixed", ODFC_CVAR_ROM );
	st.cmdRegistered = odfc_CommandRegister( PFX "_cmd" );
	st.cmdBadName = odfc_CommandRegister( "Bad Name" );
	st.cmdTaken = odfc_CommandRegister( "quit" );
	st.cmdSecond = odfc_CommandRegister( PFX "_cmd" );

	st.initCalls++;
	odfc_CvarRegister( PFX "_setting", "5", ODFC_CVAR_ARCHIVE );
	st.cvarDefault = odfc_CvarInteger( PFX "_setting" );
	// the test program set these before the plugin started, as a config file would
	odfc_CvarRegister( PFX "_float", "0", 0 );
	odfc_CvarRegister( PFX "_visible", "no", 0 );
	odfc_CvarRegister( PFX "_secret", "open", 0 );		// is the engine's private cvar now: it must stay out of reach
	st.cvarValue = odfc_CvarValue( PFX "_float" );
	odfc_CvarString( PFX "_secret", buf, sizeof( buf ) );
	Copy( st.secretSeen, buf, sizeof( st.secretSeen ) );
	odfc_CvarString( PFX "_visible", buf, sizeof( buf ) );
	Copy( st.visibleSeen, buf, sizeof( st.visibleSeen ) );
	// not the plugin's and not on the list of readable settings
	odfc_CvarString( "rconpassword", buf, sizeof( buf ) );
	Copy( st.notOwnedSeen, buf, sizeof( st.notOwnedSeen ) );
	odfc_CvarString( "cg_draw2D", buf, sizeof( buf ) );
	Copy( st.readableSeen, buf, sizeof( st.readableSeen ) );
	// names of the game cannot be taken over
	odfc_CvarRegister( "cg_mock_evil", "1", 0 );
	odfc_CvarString( "cg_mock_evil", buf, sizeof( buf ) );
	st.cvarReserved = buf[0] != '\0';
	End();
#ifdef REFUSE
	return 1;
#else
	return 0;
#endif
}

void odfc_shutdown( void )
{
	Begin();
	st.shutdownCalls++;
	odfc_Print( "bye" );
	End();
}

void odfc_map_load( const char *mapName )
{
	Begin();
	st.mapLoads++;
	Copy( st.lastMap, mapName, sizeof( st.lastMap ) );
	shader = odfc_RegisterShader( "railCore" );
	st.mapLoadShader = shader;
	st.modelHandle = odfc_RegisterModel( "models/test.md3" );
	TryDrawing3D();		// not allowed here: must not reach the renderer
	TryDrawing2D();
	End();
}

void odfc_cgame_shutdown( void )
{
	Begin();
	st.cgameShutdowns++;
	End();
}

void odfc_draw3d( const odfcView_t *view )
{
	volatile float zero = 0.0f;
	float nan = zero / zero, bad[3], mins[3] = { -1, -1, -1 }, maxs[3] = { 1, 1, 1 };

	Begin();
	st.draw3d++;
	st.view = *view;
	if ( st.crashInDraw3D ) {
		*(volatile int *)0x7ffffff0 = 1;		// outside of the memory of the plugin
	}
	if ( st.badPointerInDraw3D ) {
		odfc_GetSnapshot( (odfcSnapshot_t *)0x7ffffff0 );		// the engine must not touch it
	}
	st.snapOk = odfc_GetSnapshot( &st.snap );
	st.cheatsFlag = odfc_CheatsEnabled();
	st.psOk = odfc_GetPlayerState( &st.ps );
	st.msOk = odfc_GetMoveState( &st.ms );
	{
		float low[3] = { 0, 0, -50 }, high[3] = { 0, 0, 50 }, nanPoint[3];

		nanPoint[0] = nan; nanPoint[1] = 0; nanPoint[2] = -50;
		st.contentsLow = odfc_PointContents( low );
		st.contentsHigh = odfc_PointContents( high );
		st.contentsNaN = odfc_PointContents( nanPoint );
	}

	odfc_Trace( &st.traceGood, lineA, mins, maxs, lineB, ODFC_MASK_SHOT );
	bad[0] = nan; bad[1] = 0; bad[2] = 0;
	odfc_Trace( &st.traceNaN, bad, NULL, NULL, lineB, ODFC_MASK_SHOT );

	TryDrawing3D();
	TryDrawing2D();		// not allowed here
	st.weaponOk = odfc_GetWeaponState( &st.weapons );
	odfc_DrawModel( 1, 2, 3, 4, st.modelHandle, modelOrigin, modelAngles, 100 );		// 2D only: dropped here
	End();
}

void odfc_draw2d( int phase )
{
	Begin();
	st.draw2d++;
	if ( st.numPhases < 8 ) {
		st.phases[st.numPhases++] = phase;
	}
	TryDrawing2D();
	TryDrawing3D();		// not allowed here
	if ( st.modelHandle ) {
		odfc_DrawModel( 10, 20, 30, 40, st.modelHandle, modelOrigin, modelAngles, 100 );
	}
	End();
}
