/*
A stand-in for a client plugin (a QVM program), controlled by the test program through mock_state.h.
It records what the engine hands it, and tries the drawing calls in phases where they must be ignored.
*/

// the cvars and commands of a plugin are its own: each variant of this plugin gets its own names
#ifndef PFX
#define PFX "mock"
#endif

#if defined( BAD_VERSION )
#define ODF_PLUGIN_VERSION "future"
#define ODF_PLUGIN_API "99"
#elif defined( SECOND )
#define ODF_PLUGIN_VERSION "second 1.0"
#else
#define ODF_PLUGIN_VERSION "mock 1.0"
#endif

#define EXPORTS( X ) X(OnInit) X(OnShutdown) X(OnMapLoad) X(OnCgameShutdown) X(OnDraw3D) X(OnDraw2D) X(OnFilterSound)

// the entry point has to come first
#include "odfc_entry.h"
#include "mock_state.h"

// the two calls only the test build of the engine has (the asm file is made by run.sh)
extern void TestPull( void *state, int size );
extern void TestPush( const void *state, int size );

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

static void Begin( void ) { TestPull( &st, (int)sizeof( st ) ); }
static void End( void ) { TestPush( &st, (int)sizeof( st ) ); }

static const float lineA[3] = { 0, 0, 0 }, lineB[3] = { 100, 0, 0 };

static void TryDrawing2D( void )
{
	static const float white[4] = { 1, 1, 1, 1 };

	SetColor( white );
	DrawStretchPic( 1, 2, 3, 4, 0, 0, 1, 1, shader );
}

static void TryDrawing3D( void )
{
	odfcPolyVert_t v[3];
	static const unsigned char rgba[4] = { 255, 0, 0, 255 };

	MemSet( v, 0, sizeof( v ) );
	v[0].xyz[0] = 5;
	AddPoly( shader, 3, v );
	AddBeam( shader, lineA, lineB, rgba );
}

static const float modelOrigin[3] = { 70, 0, 0 }, modelAngles[3] = { 0, 90, 0 };

static void MockCommand( int argc, const char **argv )
{
	int n = 0;

	Begin();
	st.commandCalls++;
	Copy( st.commandName, argv[0], sizeof( st.commandName ) );
	st.commandArgc = argc;
	Copy( st.commandArg1, argc > 1 ? argv[1] : "", sizeof( st.commandArg1 ) );
	while ( argc > 1 && argv[1][n] ) {
		n++;
	}
	st.commandArg1Len = n;
	if ( st.loopInCommand ) {
		volatile int spin = 0;

		for ( ;; ) {
			spin++;
		}
	}
	End();
}

int OnFilterSound( int entityNum, int isLoop, int entityClientNum, int ownClientNum )
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

int OnInit( void )
{
	char buf[64];

	Begin();
	CvarRegister( PFX "_rom", "fixed", ODFC_CVAR_ROM );
	st.cmdRegistered = CommandRegister( PFX "_cmd", MockCommand );
	st.cmdBadName = CommandRegister( "Bad Name", MockCommand );
	st.cmdTaken = CommandRegister( "quit", MockCommand );
	st.cmdSecond = CommandRegister( PFX "_cmd", MockCommand );
	st.cmdNoHandler = CommandRegister( PFX "_nohandler", 0 );

	st.initCalls++;
	CvarRegister( PFX "_setting", "5", ODFC_CVAR_ARCHIVE );
	st.cvarDefault = CvarInteger( PFX "_setting" );
	// the test program set these before the plugin started, as a config file would
	CvarRegister( PFX "_float", "0", 0 );
	CvarRegister( PFX "_visible", "no", 0 );
	CvarRegister( PFX "_secret", "open", 0 );		// is the engine's private cvar now: it must stay out of reach
#ifdef REFUSE
	// the wrong way round: no flag for a shared cvar, the flag for one that its first plugin did not share. Both are refused
	CvarRegister( "shared_view", "7", ODFC_CVAR_ARCHIVE );
	CvarRegister( "plain_view", "7", ODFC_CVAR_ARCHIVE | ODFC_CVAR_SHARED );
#else
	CvarRegister( "shared_view", "7", ODFC_CVAR_ARCHIVE | ODFC_CVAR_SHARED );
	CvarRegister( "plain_view", "7", ODFC_CVAR_ARCHIVE );
#endif
	CvarString( "shared_view", buf, sizeof( buf ) );
	Copy( st.sharedSeen, buf, sizeof( st.sharedSeen ) );
	CvarString( "plain_view", buf, sizeof( buf ) );
	Copy( st.plainSeen, buf, sizeof( st.plainSeen ) );
	st.cvarValue = CvarValue( PFX "_float" );
	CvarString( PFX "_secret", buf, sizeof( buf ) );
	Copy( st.secretSeen, buf, sizeof( st.secretSeen ) );
	CvarString( PFX "_visible", buf, sizeof( buf ) );
	Copy( st.visibleSeen, buf, sizeof( st.visibleSeen ) );
	// not the plugin's and not on the list of readable settings
	CvarString( "rconpassword", buf, sizeof( buf ) );
	Copy( st.notOwnedSeen, buf, sizeof( st.notOwnedSeen ) );
	CvarString( "cg_draw2D", buf, sizeof( buf ) );
	Copy( st.readableSeen, buf, sizeof( st.readableSeen ) );
	// names of the game cannot be taken over
	CvarRegister( "cg_mock_evil", "1", 0 );
	CvarString( "cg_mock_evil", buf, sizeof( buf ) );
	st.cvarReserved = buf[0] != '\0';
	End();
#ifdef REFUSE
	return 1;
#else
	return 0;
#endif
}

void OnShutdown( void )
{
	Begin();
	st.shutdownCalls++;
	Print( "bye" );
	End();
}

void OnMapLoad( const char *mapName )
{
	Begin();
	st.mapLoads++;
	Copy( st.lastMap, mapName, sizeof( st.lastMap ) );
	shader = RegisterShader( "railCore" );
	st.mapLoadShader = shader;
	st.modelHandle = RegisterModel( "models/test.md3" );
	TryDrawing3D();		// not allowed here: must not reach the renderer
	TryDrawing2D();
	End();
}

void OnCgameShutdown( void )
{
	Begin();
	st.cgameShutdowns++;
	End();
}

void OnDraw3D( const odfcView_t *view )
{
	volatile float zero = 0.0f;
	float nan = zero / zero, bad[3], mins[3] = { -1, -1, -1 }, maxs[3] = { 1, 1, 1 };

	Begin();
	st.draw3d++;
	st.view = *view;
	if ( st.crashInDraw3D ) {
		{
			volatile int zero2 = 0;

			st.cmdTaken = 1 / zero2;		// a division the processor traps on: the plugin is stopped
		}
	}
	if ( st.badPointerInDraw3D ) {
		GetSnapshot( (odfcSnapshot_t *)0x7ffffff0 );		// the engine must not touch it
	}
	st.snapOk = GetSnapshot( &st.snap );
	st.cheatsFlag = CheatsEnabled();
	st.psOk = GetPlayerState( &st.ps );
	st.msOk = GetMoveState( &st.ms );
	{
		float low[3] = { 0, 0, -50 }, high[3] = { 0, 0, 50 }, nanPoint[3];

		nanPoint[0] = nan; nanPoint[1] = 0; nanPoint[2] = -50;
		st.contentsLow = PointContents( low );
		st.contentsHigh = PointContents( high );
		st.contentsNaN = PointContents( nanPoint );
	}

	Trace( &st.traceGood, lineA, mins, maxs, lineB, ODFC_MASK_SHOT );
	bad[0] = nan; bad[1] = 0; bad[2] = 0;
	Trace( &st.traceNaN, bad, NULL, NULL, lineB, ODFC_MASK_SHOT );

	TryDrawing3D();
	TryDrawing2D();		// not allowed here
	st.weaponOk = GetWeaponState( &st.weapons );
	DrawModel( 1, 2, 3, 4, st.modelHandle, modelOrigin, modelAngles, 100 );		// 2D only: dropped here
	End();
}

void OnDraw2D( int phase )
{
	Begin();
	st.draw2d++;
	if ( st.numPhases < 8 ) {
		st.phases[st.numPhases++] = phase;
	}
	TryDrawing2D();
	TryDrawing3D();		// not allowed here
	if ( st.modelHandle ) {
		DrawModel( 10, 20, 30, 40, st.modelHandle, modelOrigin, modelAngles, 100 );
	}
	End();
}
