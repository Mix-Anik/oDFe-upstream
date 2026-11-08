/*
===========================================================================
Client plugins: QVM programs that run in a sandbox and add HUD elements and helpers
to the player's game. A plugin can only do what the functions it imports do, and those show
nothing but what the player's own game shows (the interface is in code/plugin_api/client.api,
the sandbox in plugin_vm.c). A plugin has no files, no network and no way to touch engine memory.
===========================================================================
*/

#include "client.h"
#include "cl_demoroute.h"
#include "../qcommon/cm_public.h"
#include "../qcommon/plugin_vm.h"
#include "../plugin_api/include/odfc_types.h"

#define PLUGIN_EXT			".qvm"
#define MAX_PLUGINS			64
#define MAX_PLUGIN_NAME		256
#define DISABLED_FILE		"disabled"		// in the plugins folder: the names of the plugins that are switched off
#define MAX_POLY_VERTS		64
#define MAX_PLUGIN_CVARS	256
#define MAX_CVAR_NAME		48
#define MAX_MODULE_FILE		( 8 * 1024 * 1024 )

// the API structs are copied to renderer structs byte by byte
typedef char odfcPolyVertSize_t[ sizeof( odfcPolyVert_t ) == sizeof( polyVert_t ) ? 1 : -1 ];
typedef char odfcPolyVertSt_t[ offsetof( odfcPolyVert_t, st ) == offsetof( polyVert_t, st ) ? 1 : -1 ];
typedef char odfcPolyVertRgba_t[ offsetof( odfcPolyVert_t, rgba ) == offsetof( polyVert_t, modulate ) ? 1 : -1 ];
typedef char odfcEntitiesMatch_t[ ODFC_MAX_SNAPSHOT_ENTITIES == MAX_ENTITIES_IN_SNAPSHOT ? 1 : -1 ];

typedef enum {
	PS_RUNNING,
	PS_STOPPED,		// shut down normally
	PS_FAILED,		// did not load or refused to start
	PS_FAULTED,		// trapped or ran out of its budget, stopped
	PS_DISABLED		// switched off, not loaded
} pluginState_t;

typedef struct {
	char						name[MAX_PLUGIN_NAME];
	char						path[MAX_OSPATH];
	pluginVM_t				*vm;
	pluginState_t				state;
	char						note[192];		// why it is not running
	char						version[64];	// from its manifest
	qboolean					disabled;		// switched off by the player (kept in the disabled file)
	qboolean					hidden;			// a module that is not a client plugin (a server plugin): not shown
	qboolean					notClient;		// set while loading when the module is not a client plugin
	char						cvars[MAX_PLUGIN_CVARS][MAX_CVAR_NAME];		// the cvars it created
	int							numCvars;
} plugin_t;

// what the plugin is allowed to draw right now
typedef enum {
	DRAW_NONE,
	DRAW_3D,
	DRAW_2D
} drawPhase_t;

static plugin_t				plugins[MAX_PLUGINS];
static int					numPlugins;
static plugin_t				*current;		// the plugin whose code is running, for log prefixes and the cvars it may use
static char					loadedMap[MAX_QPATH];		// the map of the game module that is running
static char					disabledNames[MAX_PLUGINS][MAX_PLUGIN_NAME];		// what the disabled file said when it was read or written
static int					numDisabledNames;
static qboolean				mapLoaded;		// between MapLoad and CGameShutdown
static drawPhase_t			drawPhase;

// the predicted player state the game module published (trap_ODF_SetPlayerState), and the view time it was published at
static playerState_t		publishedPS;
static int					publishedTime;
static qboolean				published;
static int					publishedViewFlags;
static int					publishedStatsTime;		// the server time of the snapshot the stats of the published state are from, 0: not known

// the paths of the ghosts the game module plays (trap_ODF_GhostPath), by slot
typedef struct {
	odfcGhostPoint_t	*points;		// grows by doubling as the path comes in, up to ODFC_GHOST_MAX_POINTS
	int					capacity;
	int					num;
	int					serial;
	qboolean			complete;
	int					raceMs;			// the exact time of the run the path comes from, 0: not known
} ghostPath_t;
static ghostPath_t			ghostPath[ODFC_GHOST_SLOTS];
static int					ghostSerial;
static void GhostPaths_Free( void );

static qboolean CheatsAllowed( void );
#define ODF_CheatsAllowed CheatsAllowed
// What an engine function costs the allowance of the call that is running, besides the step it is (see plugin_vm.h). The units are
// about 10 ns: loading from disk is 10000, a snapshot of the game 100000, a byte copied 1/128.
#define ODF_CHARGE( units_ ) 	do { 		if ( current && current->vm ) { 			PluginVM_Charge( current->vm, (unsigned)( units_ ) ); 		} 	} while ( 0 )

#include "../plugin_api/generated/cl_plugin_api.inc"
#include "../plugin_api/plugin_common_imp.h"

// the player's own plugins get every capability there is
#define ODFC_CAPS_ALL		( ( 1u << ODFC_NUM_CAPS ) - 1 )

#ifdef ODF_PLUGIN_TESTS
// Two calls that only the test build has: the mock plugin keeps its state in the test program, outside of its sandbox.
extern void Test_MirrorPull( const char *plugin, void *dst, int size );
extern void Test_MirrorPush( const char *plugin, const void *src, int size );
#define ODFC_TEST_PULL	ODFC_NUM_IMPORTS
#define ODFC_TEST_PUSH	( ODFC_NUM_IMPORTS + 1 )
#endif

// what a plugin calls goes through here
static intptr_t Plugin_Dispatch( pluginVM_t *vm, intptr_t *args )
{
#ifdef ODF_PLUGIN_TESTS
	if ( args[0] == ODFC_TEST_PULL || args[0] == ODFC_TEST_PUSH ) {
		void *p = NULL;

		if ( args[2] > 0 ) {
			PluginVM_Ptr( vm, args[1], (unsigned)args[2], 1, qfalse, &p );
			if ( args[0] == ODFC_TEST_PULL ) {
				Test_MirrorPull( current->name, p, (int)args[2] );
			} else {
				Test_MirrorPush( current->name, p, (int)args[2] );
			}
		}
		return 0;
	}
#endif
	return Odfc_Dispatch( vm, args );
}


// ---------------------------------------------------------------------------
// calls into a plugin
// ---------------------------------------------------------------------------

static void Plugin_Fault( plugin_t *p, const char *what );

// Runs one call of a plugin. A plugin that traps (bad memory access, bad pointer, out of budget) is stopped.
#define PLUGIN_RUN( p_, what_, call_ ) \
	do { \
		plugin_t *prev_ = current; \
		qboolean ok_; \
		current = (p_); \
		ok_ = (call_); \
		current = prev_; \
		drawPhase = DRAW_NONE; \
		if ( !ok_ ) { \
			Plugin_Fault( (p_), (what_) ); \
		} \
	} while ( 0 )

// the same, and ok_ says whether the call was made without a trap
#define PLUGIN_RUN_OK( ok_, p_, what_, call_ ) \
	do { \
		plugin_t *prev_ = current; \
		current = (p_); \
		(ok_) = (call_); \
		current = prev_; \
		drawPhase = DRAW_NONE; \
		if ( !(ok_) ) { \
			Plugin_Fault( (p_), (what_) ); \
		} \
	} while ( 0 )

#define PLUGIN_HAS( p_, fn_ ) \
	( (p_)->vm && PluginVM_Has( (p_)->vm, ODFC_EXP_##fn_ ) )


// ---------------------------------------------------------------------------
// the rules for what a plugin sees
// ---------------------------------------------------------------------------

static qboolean CheatsAllowed( void )
{
	return ( clc.demoplaying || cl_connectedToCheatServer ) ? qtrue : qfalse;
}

static qboolean IsFinite3( const float *v )
{
	int i;

	for ( i = 0; i < 3; i++ ) {
		if ( !( v[i] > -1e9f && v[i] < 1e9f ) ) {		// false for NaN as well
			return qfalse;
		}
	}
	return qtrue;
}


// ---------------------------------------------------------------------------
// what the plugins can call
// ---------------------------------------------------------------------------

static void Imp_Print( const char *text )
{
	if ( !text ) {
		return;
	}
	Com_Printf( "[%s] %s\n", current ? current->name : "plugin", text );
}

static int Imp_Milliseconds( void )
{
	return Sys_Milliseconds();
}

// The cvars a plugin may create. The names the engine and the game use are theirs: a plugin cannot take them over, and
// cannot mark them read-only or archive them.
static const char *const reservedPrefixes[] = {
	"cg_", "cl_", "com_", "df_", "fs_", "g_", "in_", "m_", "net_", "r_", "s_", "sv_", "ui_", "vm_", "j_", "snd_", "con_", "rcon", "cvar_", "bot_", "sensitivity"
};

static qboolean ValidCvarName( const char *name )
{
	int i, len = (int)strlen( name );
	qboolean underscore = qfalse;

	if ( len < 3 || len >= MAX_CVAR_NAME ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		char c = name[i];

		if ( c == '_' && i > 0 ) {
			underscore = qtrue;
		} else if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ) ) {
			return qfalse;
		}
	}
	if ( !underscore ) {
		return qfalse;
	}
	for ( i = 0; i < (int)ARRAY_LEN( reservedPrefixes ); i++ ) {
		if ( !Q_stricmpn( name, reservedPrefixes[i], (int)strlen( reservedPrefixes[i] ) ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

static qboolean Plugin_OwnsCvar( const plugin_t *p, const char *name )
{
	int i;

	for ( i = 0; i < p->numCvars; i++ ) {
		if ( !Q_stricmp( p->cvars[i], name ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// The cvars a plugin created with ODFC_CVAR_SHARED. Another plugin may register such a cvar too, if it gives the flag as well;
// every other cvar belongs to the one plugin that created it.
static char				sharedCvars[MAX_PLUGIN_CVARS][MAX_CVAR_NAME];
static int				numSharedCvars;

static qboolean Cvar_IsShared( const char *name )
{
	int i;

	for ( i = 0; i < numSharedCvars; i++ ) {
		if ( !Q_stricmp( sharedCvars[i], name ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void Imp_CvarRegister( const char *name, const char *defaultValue, int flags )
{
	unsigned existing;

	if ( !current || !name || !defaultValue || !ValidCvarName( name ) ) {
		if ( current && name && *name ) {
			Com_Printf( S_COLOR_YELLOW "Plugin '%s': cannot create the cvar %s (invalid or reserved name)\n", current->name, name );
		}
		return;
	}
	if ( !Plugin_OwnsCvar( current, name ) ) {
		// a value the player set in a config before the plugin started is fine, a cvar of anyone else is not
		existing = Cvar_Flags( name );
		if ( existing != CVAR_NONEXISTENT && !( existing & CVAR_USER_CREATED ) && !( ( flags & ODFC_CVAR_SHARED ) && Cvar_IsShared( name ) ) ) {
			Com_Printf( S_COLOR_YELLOW "Plugin '%s': the cvar %s already exists\n", current->name, name );
			return;
		}
		if ( current->numCvars >= MAX_PLUGIN_CVARS ) {
			Com_Printf( S_COLOR_YELLOW "Plugin '%s': too many cvars, %s is not created\n", current->name, name );
			return;
		}
		Q_strncpyz( current->cvars[current->numCvars++], name, MAX_CVAR_NAME );
	}
	if ( ( flags & ODFC_CVAR_SHARED ) && !Cvar_IsShared( name ) && numSharedCvars < MAX_PLUGIN_CVARS ) {
		Q_strncpyz( sharedCvars[numSharedCvars++], name, MAX_CVAR_NAME );
	}
	if ( flags & ODFC_CVAR_ROM ) {
		Cvar_Get( name, defaultValue, CVAR_ROM );		// a ROM cvar always has the value given
		return;
	}
	Cvar_Get( name, defaultValue, ( flags & ODFC_CVAR_ARCHIVE ) ? CVAR_ARCHIVE_ND : 0 );
}

// A plugin reads the cvars it created and settings of the game that show how the player has it set up; nothing else
// (not passwords, not the cvars of other plugins).
static const char *const readablePrefixes[] = {
	"cg_", "r_", "m_", "df_", "pmove_", "cl_yawspeed", "cl_pitchspeed", "cl_run", "cl_freelook", "cl_timenudge", "com_maxfps",
	"sensitivity", "timescale", "sv_fps", "sv_cheats", "g_speed", "g_gravity", "snaps", "mapname"
};

static qboolean Plugin_CanRead( const plugin_t *p, const char *name )
{
	int i;

	if ( !p || !name || !*name ) {
		return qfalse;
	}
	if ( Plugin_OwnsCvar( p, name ) ) {
		return qtrue;
	}
	for ( i = 0; i < (int)ARRAY_LEN( readablePrefixes ); i++ ) {
		if ( !Q_stricmpn( name, readablePrefixes[i], (int)strlen( readablePrefixes[i] ) ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void Imp_CvarString( const char *name, char *out, int outSize )
{
	if ( !out || outSize < 1 ) {
		return;
	}
	out[0] = '\0';
	if ( !Plugin_CanRead( current, name ) ) {
		return;
	}
	Cvar_VariableStringBufferSafe( name, out, outSize, CVAR_PRIVATE );
}

static int Imp_CvarInteger( const char *name )
{
	char buf[MAX_CVAR_VALUE_STRING];

	Imp_CvarString( name, buf, sizeof( buf ) );
	return atoi( buf );
}

static float Imp_CvarValue( const char *name )
{
	char buf[MAX_CVAR_VALUE_STRING];

	Imp_CvarString( name, buf, sizeof( buf ) );
	return (float)atof( buf );
}

static int Imp_CheatsEnabled( void )
{
	return CheatsAllowed();
}


// ---------------------------------------------------------------------------
// snapshots of the game (the work is in sv_state.c)
//
// Creating one is a read and can be done any time. Applying one changes the server and the client, so it is only requested
// here and made at the start of the next client frame (CL_Plugins_BeforeFrame), when no module is running. The client then
// waits for the first snapshot of the new timeline and sets its clock to it (CL_Plugins_StateResync), which also tells the
// plugins (OnDemoSeek) and the cgame (CG_ODF_TIME_CHANGE).
// ---------------------------------------------------------------------------

static int			stateApplyHandle;		// requested by a plugin, not made yet
static qboolean		stateWait;				// applied, the first snapshot after it is not here yet
static int			stateWaitMsg;			// the message number the client had received when it was applied
static qboolean		stateNotify;			// the clock was reset, the cgame and the plugins do not know yet
static int			stateOldTime;			// the client clock before it was reset

// the number SV_State* knows a plugin by
static int PluginOwner( void )
{
	return current ? (int)( current - plugins ) + 1 : 0;
}

static int Imp_SnapshotCreate( int flags )
{
	if ( flags || clc.demoplaying || cls.state != CA_ACTIVE ) {
		return 0;
	}
	ODF_CHARGE( 100000 );
	{
		const int handle = SV_StateCreate( PluginOwner() );

		if ( handle && clc.demoRoute ) {
			DemoRoute_Created( handle );		// the demo being recorded is cut here
		}
		return handle;
	}
}

static int Imp_SnapshotApply( int handle, int flags )
{
	if ( flags || clc.demoplaying || cls.state != CA_ACTIVE || !CLDemo_CgameSeekable() ) {
		return 0;
	}
	if ( SV_StateOwner( handle ) != PluginOwner() || !SV_StateCanApply( handle ) ) {
		return 0;
	}
	stateApplyHandle = handle;
	return 1;
}

static void Imp_SnapshotFree( int handle )
{
	if ( handle > 0 && SV_StateOwner( handle ) == PluginOwner() ) {
		SV_StateFree( handle );
	}
}

/*
===============
CurrentSnapshot

The snapshot the game is showing: the newest one that is not ahead of the view time. That is what the game module
works with too, and not cl.snap, which can be one or more snapshots ahead.
===============
*/
static const clSnapshot_t *CurrentSnapshot( void )
{
	int num;

	if ( !cl.snap.valid ) {
		return NULL;
	}
	for ( num = cl.snap.messageNum; cl.snap.messageNum - num < PACKET_BACKUP; num-- ) {
		const clSnapshot_t *s = &cl.snapshots[num & PACKET_MASK];

		if ( !s->valid || s->messageNum != num ) {
			continue;
		}
		if ( cl.parseEntitiesNum - s->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
			break;		// the entities of this one and of all older ones are gone
		}
		if ( s->serverTime <= cl.serverTime ) {
			return s;
		}
	}
	return &cl.snap;
}

static int Imp_GetSnapshot( odfcSnapshot_t *out )
{
	const clSnapshot_t *s;
	qboolean cheats;
	int i, count;

	if ( !out || !mapLoaded || cls.state != CA_ACTIVE ) {
		return 0;
	}
	s = CurrentSnapshot();
	if ( !s ) {
		return 0;
	}
	cheats = CheatsAllowed();

	out->serverTime = s->serverTime;
	out->cheats = cheats;
	out->player.clientNum = s->ps.clientNum;
	out->player.weapon = s->ps.weapon;
	VectorCopy( s->ps.origin, out->player.origin );
	VectorCopy( s->ps.velocity, out->player.velocity );
	VectorCopy( s->ps.viewangles, out->player.viewangles );

	count = s->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	out->numEntities = 0;
	for ( i = 0; i < count; i++ ) {
		const entityState_t *es = &cl.parseEntities[ ( s->parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 ) ];
		odfcEntity_t *e;

		// without cheats a plugin sees the missiles of the watched player and nothing else
		if ( !cheats && ( es->eType != ET_MISSILE || es->clientNum != s->ps.clientNum ) ) {
			continue;
		}
		e = &out->entities[out->numEntities++];
		e->number = es->number;
		e->eType = es->eType;
		e->eFlags = es->eFlags;
		e->weapon = es->weapon;
		e->clientNum = es->clientNum;
		e->pos.trType = es->pos.trType;
		e->pos.trTime = es->pos.trTime;
		e->pos.trDuration = es->pos.trDuration;
		VectorCopy( es->pos.trBase, e->pos.trBase );
		VectorCopy( es->pos.trDelta, e->pos.trDelta );
	}
	return 1;
}

static void Imp_Trace( odfcTrace_t *out, const float *start, const float *mins, const float *maxs, const float *end, int contentMask )
{
	trace_t t;

	if ( !out ) {
		return;
	}
	Com_Memset( out, 0, sizeof( *out ) );
	out->fraction = 1.0f;
	if ( !start || !end ) {
		return;
	}
	VectorCopy( end, out->endpos );
	if ( !mapLoaded || !IsFinite3( start ) || !IsFinite3( end ) ||
		( mins && !IsFinite3( mins ) ) || ( maxs && !IsFinite3( maxs ) ) ) {
		return;
	}

	CM_BoxTrace( &t, start, end, mins ? mins : vec3_origin, maxs ? maxs : vec3_origin, 0, contentMask, qfalse );

	out->allSolid = t.allsolid;
	out->startSolid = t.startsolid;
	out->fraction = t.fraction;
	VectorCopy( t.endpos, out->endpos );
	VectorCopy( t.plane.normal, out->normal );
	out->surfaceFlags = t.surfaceFlags;
	out->contents = t.contents;
}

static int Imp_GetPlayerState( odfcPlayerState_t *out )
{
	if ( !out || !mapLoaded || cls.state != CA_ACTIVE ) {
		return 0;
	}
	// only what was published for the frame that is being drawn: an older cgame, or one that stopped, gives nothing
	if ( !published || publishedTime != cl.serverTime ) {
		return 0;
	}
	out->clientNum = publishedPS.clientNum;
	out->pmType = publishedPS.pm_type;
	out->weapon = publishedPS.weapon;
	out->viewheight = publishedPS.viewheight;
	VectorCopy( publishedPS.origin, out->origin );
	VectorCopy( publishedPS.velocity, out->velocity );
	VectorCopy( publishedPS.viewangles, out->viewangles );
	return 1;
}

static int Imp_GetMoveState( odfcMoveState_t *out )
{
	if ( !out || !mapLoaded || cls.state != CA_ACTIVE || !published || publishedTime != cl.serverTime ) {
		return 0;
	}
	out->pmFlags = publishedPS.pm_flags;
	out->pmTime = publishedPS.pm_time;
	out->groundEntityNum = publishedPS.groundEntityNum;
	out->speed = publishedPS.speed;
	out->gravity = publishedPS.gravity;
	out->flight = publishedPS.powerups[PW_FLIGHT] != 0;
	out->userInput = publishedPS.stats[13];		// DeFRaG's key mask of the watched player
	out->viewFlags = publishedViewFlags & ( ODFC_VIEW_SCOREBOARD | ODFC_VIEW_NOT_FIRST_PERSON );
	out->demoPlayback = clc.demoplaying ? 1 : 0;

	// your own latest command, as the game module reads it for the prediction
	out->cmdValid = 0;
	out->forwardmove = out->rightmove = out->upmove = out->buttons = 0;
	if ( !clc.demoplaying && cl.cmdNumber > 0 ) {
		const usercmd_t *cmd = &cl.cmds[ cl.cmdNumber & CMD_MASK ];

		out->cmdValid = 1;
		out->forwardmove = cmd->forwardmove;
		out->rightmove = cmd->rightmove;
		out->upmove = cmd->upmove;
		out->buttons = cmd->buttons;
	}
	return 1;
}

static int Imp_PointContents( const float *point )
{
	if ( !point || !mapLoaded || !IsFinite3( point ) ) {
		return 0;
	}
	return CM_PointContents( point, 0 );
}

static int Imp_RegisterShader( const char *name )
{
	if ( !name || !*name || !mapLoaded ) {
		return 0;
	}
	ODF_CHARGE( 10000 );
	return re.RegisterShader( name );
}

static void Imp_AddPoly( int shader, int numVerts, const odfcPolyVert_t *verts )
{
	if ( drawPhase != DRAW_3D || !verts || numVerts < 3 || numVerts > MAX_POLY_VERTS ) {
		return;
	}
	re.AddPolyToScene( shader, numVerts, (const polyVert_t *)verts, 1 );
}

static void Imp_AddBeam( int shader, const float *from, const float *to, const unsigned char *rgba )
{
	refEntity_t beam;

	if ( drawPhase != DRAW_3D || !from || !to || !rgba || !IsFinite3( from ) || !IsFinite3( to ) ) {
		return;
	}
	Com_Memset( &beam, 0, sizeof( beam ) );
	beam.reType = RT_RAIL_CORE;
	beam.customShader = shader;
	VectorCopy( from, beam.oldorigin );
	VectorCopy( to, beam.origin );
	AxisClear( beam.axis );
	Com_Memcpy( beam.shader.rgba, rgba, 4 );		// the shader colour (shaderRGBA in the original engine)
	re.AddRefEntityToScene( &beam, qfalse );
}

static void Imp_SetColor( const float *rgba )
{
	if ( drawPhase != DRAW_2D ) {
		return;
	}
	re.SetColor( rgba );
}

static void Imp_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader )
{
	if ( drawPhase != DRAW_2D ) {
		return;
	}
	// the plugin draws in the 640x480 screen, like the game module, which scales it itself
	SCR_AdjustFrom640( &x, &y, &w, &h );
	re.DrawStretchPic( x, y, w, h, s1, t1, s2, t2, shader );
}

static int Imp_GetWeaponState( odfcWeaponState_t *out )
{
	int i;

	if ( !out || !mapLoaded || cls.state != CA_ACTIVE || !published || publishedTime != cl.serverTime ) {
		return 0;
	}
	out->weapons = publishedPS.stats[STAT_WEAPONS];
	for ( i = 0; i < ODFC_MAX_WEAPONS; i++ ) {
		out->ammo[i] = publishedPS.ammo[i];
	}
	return 1;
}

static int Imp_GetPlayerStats( odfcPlayerStats_t *out )
{
	int i;

	if ( !out || !mapLoaded || cls.state != CA_ACTIVE || !published || publishedTime != cl.serverTime ) {
		return 0;
	}
	out->commandTime = publishedPS.commandTime;
	out->snapshotTime = publishedStatsTime;
	for ( i = 0; i < ODFC_MAX_STATS; i++ ) {
		out->stats[i] = publishedPS.stats[i];
	}
	for ( i = 0; i < ODFC_MAX_PERSISTANT; i++ ) {
		out->persistant[i] = publishedPS.persistant[i];
	}
	return 1;
}

// the demo of the plugin (cl_main.c)
static int Imp_DemoRecord( void )
{
	ODF_CHARGE( 50000 );
	return CL_PluginDemoRecord() ? 1 : 0;
}

static int Imp_DemoStop( int discard )
{
	ODF_CHARGE( 50000 );
	return CL_PluginDemoStop( discard ? qtrue : qfalse ) ? 1 : 0;
}

static int Imp_DemoSave( const char *name )
{
	ODF_CHARGE( 1000000 );		// writes the whole demo
	return name && CL_PluginDemoSave( name ) ? 1 : 0;
}

static int Imp_DemoState( odfcDemoState_t *out )
{
	if ( !out ) {
		return 0;
	}
	CL_PluginDemoState( &out->recording, &out->segments, &out->mine, &out->bytes );
	return 1;
}

static int Imp_GetGhostInfo( int slot, odfcGhostInfo_t *out )
{
	const ghostPath_t *g;

	if ( !out || !mapLoaded || slot < 0 || slot >= ODFC_GHOST_SLOTS ) {
		return 0;
	}
	g = &ghostPath[slot];
	if ( !g->points || g->num <= 0 ) {
		return 0;
	}
	out->serial = g->serial;
	out->numPoints = g->num;
	out->durationMs = g->points[g->num - 1].ms;
	out->complete = g->complete ? 1 : 0;
	out->raceMs = g->raceMs;
	return 1;
}

static int Imp_GetGhostPath( int slot, int first, int count, odfcGhostPoint_t *out )
{
	const ghostPath_t *g;

	if ( !out || !mapLoaded || slot < 0 || slot >= ODFC_GHOST_SLOTS || first < 0 || count <= 0 ) {
		return 0;
	}
	g = &ghostPath[slot];
	if ( !g->points || first >= g->num ) {
		return 0;
	}
	if ( count > g->num - first ) {
		count = g->num - first;
	}
	ODF_CHARGE( (unsigned)count * sizeof( *out ) / 128 );
	Com_Memcpy( out, g->points + first, count * sizeof( *out ) );
	return count;
}

static int Imp_RegisterShaderNoMip( const char *name )
{
	if ( !name || !*name || !mapLoaded ) {
		return 0;
	}
	ODF_CHARGE( 10000 );
	return re.RegisterShaderNoMip( name );
}

static int Imp_RegisterModel( const char *name )
{
	if ( !name || !*name || !mapLoaded ) {
		return 0;
	}
	ODF_CHARGE( 10000 );
	return re.RegisterModel( name );
}

// like the game module draws the models of its HUD: a scene of its own with one entity, into a rectangle of the screen
static void Imp_DrawModel( float x, float y, float w, float h, int model, const float *origin, const float *angles, int time )
{
	refdef_t refdef;
	refEntity_t ent;

	if ( drawPhase != DRAW_2D || model <= 0 || !origin || !angles || !IsFinite3( origin ) || !IsFinite3( angles ) ) {
		return;
	}
	if ( !( w > 0 && w < 4096 && h > 0 && h < 4096 && x > -4096 && x < 4096 && y > -4096 && y < 4096 ) ) {
		return;
	}
	SCR_AdjustFrom640( &x, &y, &w, &h );

	Com_Memset( &refdef, 0, sizeof( refdef ) );
	Com_Memset( &ent, 0, sizeof( ent ) );
	AnglesToAxis( angles, ent.axis );
	VectorCopy( origin, ent.origin );
	ent.hModel = model;
	ent.renderfx = RF_NOSHADOW;

	refdef.rdflags = RDF_NOWORLDMODEL;
	AxisClear( refdef.viewaxis );
	refdef.fov_x = 30;
	refdef.fov_y = 30;
	refdef.x = (int)x;
	refdef.y = (int)y;
	refdef.width = (int)w;
	refdef.height = (int)h;
	refdef.time = time;

	re.ClearScene();
	re.AddRefEntityToScene( &ent, 1 );
	re.RenderScene( &refdef );
}



// ---------------------------------------------------------------------------
// console commands of plugins
// ---------------------------------------------------------------------------

#define MAX_PLUGIN_COMMANDS		256
#define MAX_COMMAND_NAME		32
#define COMMAND_MAX_ARGS		16		// what a handler gets (see CommandHandler in client.api)
#define COMMAND_MAX_ARG_LEN		200

typedef struct {
	char		name[MAX_COMMAND_NAME];
	plugin_t	*plugin;
	int			handler;		// the function of the plugin that runs it
} pluginCommand_t;

static pluginCommand_t	pluginCommands[MAX_PLUGIN_COMMANDS];
static int				numPluginCommands;

static qboolean ValidCommandName( const char *name )
{
	int i, len = (int)strlen( name );

	if ( len < 1 || len >= MAX_COMMAND_NAME ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		char c = name[i];

		if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || c == '_' ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// every plugin command comes here; Cmd_Argv( 0 ) says which
static void Plugins_Command_f( void )
{
	int i;

	for ( i = 0; i < numPluginCommands; i++ ) {
		pluginCommand_t *c = &pluginCommands[i];

		if ( !Q_stricmp( Cmd_Argv( 0 ), c->name ) ) {
			plugin_t *p = c->plugin;

			if ( p->state != PS_RUNNING || !PLUGIN_HAS( p, CommandHandler ) ) {
				return;
			}
			{
				// the words of the command line, like main( argc, argv ); more than fit in the io buffer are cut
				char words[COMMAND_MAX_ARGS][COMMAND_MAX_ARG_LEN];
				const char *argv[COMMAND_MAX_ARGS];
				int k, argc = Cmd_Argc();

				if ( argc > COMMAND_MAX_ARGS ) {
					argc = COMMAND_MAX_ARGS;
				}
				for ( k = 0; k < argc; k++ ) {
					Q_strncpyz( words[k], Cmd_Argv( k ), sizeof( words[k] ) );
					argv[k] = words[k];
				}
				PLUGIN_RUN( p, "command", ODFC_Call_CommandHandler( p->vm, c->handler, argc, argv ) );
			}
			return;
		}
	}
}

static int Imp_CommandRegister( const char *name, int handler )
{
	if ( !current || !name || handler == 0 || !ValidCommandName( name ) || numPluginCommands >= MAX_PLUGIN_COMMANDS ) {
		return 0;
	}
	// the engine's own commands (and those of other plugins) cannot be taken over
	if ( Cmd_CommandExists( name ) ) {
		Com_Printf( S_COLOR_YELLOW "Plugin '%s': command %s already exists\n", current->name, name );
		return 0;
	}
	Q_strncpyz( pluginCommands[numPluginCommands].name, name, MAX_COMMAND_NAME );
	pluginCommands[numPluginCommands].plugin = current;
	pluginCommands[numPluginCommands].handler = handler;
	numPluginCommands++;
	Cmd_AddCommand( name, Plugins_Command_f );
	return 1;
}


// ---------------------------------------------------------------------------
// sounds
// ---------------------------------------------------------------------------

// The clientNum of every entity that has been in a snapshot since the map was loaded (the entity states of the game module
// are not kept once an entity is gone, this is), for the plugins that decide which sounds to play.
static signed char		soundOwner[MAX_GENTITIES];
static int				soundOwnerTime = -1;

static void SoundOwners_Reset( void )
{
	Com_Memset( soundOwner, -1, sizeof( soundOwner ) );
	soundOwnerTime = -1;
}

static void SoundOwners_Update( const clSnapshot_t *s )
{
	int i, count;

	if ( s->serverTime == soundOwnerTime ) {
		return;
	}
	soundOwnerTime = s->serverTime;
	count = s->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	for ( i = 0; i < count; i++ ) {
		const entityState_t *es = &cl.parseEntities[ ( s->parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 ) ];

		if ( es->number >= 0 && es->number < MAX_GENTITIES ) {
			soundOwner[es->number] = ( es->clientNum >= 0 && es->clientNum < MAX_CLIENTS ) ? (signed char)es->clientNum : -1;
		}
	}
}



// ---------------------------------------------------------------------------
// loading
// ---------------------------------------------------------------------------

static qboolean ValidPluginName( const char *name )
{
	int i, len = (int)strlen( name );

	if ( len < 1 || len >= MAX_PLUGIN_NAME ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		char c = name[i];

		if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '_' || c == '-' ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

static void FORMAT_PRINTF( 2, 3 ) Plugin_Fail( plugin_t *p, const char *fmt, ... );

// the commands of a plugin that is not running any more
static void Plugin_RemoveCommands( plugin_t *p )
{
	int i, kept = 0;

	for ( i = 0; i < numPluginCommands; i++ ) {
		if ( pluginCommands[i].plugin == p ) {
			Cmd_RemoveCommand( pluginCommands[i].name );
			continue;
		}
		pluginCommands[kept++] = pluginCommands[i];
	}
	numPluginCommands = kept;
}

// Nothing of a plugin that is gone can run again: its memory is freed with it.
static void Plugin_Unload( plugin_t *p )
{
	SV_StateFreeOwner( (int)( p - plugins ) + 1 );
	PluginVM_Free( p->vm );
	p->vm = NULL;
}

static void Plugin_Fail( plugin_t *p, const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	Q_vsnprintf( p->note, sizeof( p->note ), fmt, ap );
	va_end( ap );
	p->state = PS_FAILED;
	if ( p->notClient ) {
		Com_DPrintf( "%s is not a client plugin: %s\n", p->name, p->note );		// a server plugin
	} else {
		Com_Printf( S_COLOR_YELLOW "Plugin '%s' not loaded: %s\n", p->name, p->note );
	}
	Plugin_RemoveCommands( p );
	Plugin_Unload( p );
}

// A plugin that trapped (bad memory access, a pointer outside its memory, out of instructions) is stopped and not called again.
static void Plugin_Fault( plugin_t *p, const char *what )
{
	Com_sprintf( p->note, sizeof( p->note ), "stopped in %s: %s", what, PluginVM_Error( p->vm ) );
	p->state = PS_FAULTED;
	Com_Printf( S_COLOR_RED "Plugin '%s' stopped in %s: %s\n" S_COLOR_RED "It will not run again until it is switched off and on in the menu.\n",
		p->name, what, PluginVM_Error( p->vm ) );
	Plugin_RemoveCommands( p );
	Plugin_Unload( p );
}

// the value of a "key=value" line of the manifest of a plugin
static qboolean Manifest_Value( const char *manifest, const char *key, char *out, int outSize )
{
	int keyLen = (int)strlen( key );
	const char *s = manifest;

	while ( *s ) {
		const char *end = s;

		while ( *end && *end != '\n' ) {
			end++;
		}
		if ( !Q_strncmp( s, key, keyLen ) && s[keyLen] == '=' ) {
			int len = (int)( end - ( s + keyLen + 1 ) );

			while ( len > 0 && s[keyLen + len] == '\r' ) {
				len--;
			}
			if ( len >= outSize ) {
				len = outSize - 1;
			}
			Com_Memcpy( out, s + keyLen + 1, len );
			out[len] = '\0';
			return qtrue;
		}
		s = *end ? end + 1 : end;
	}
	out[0] = '\0';
	return qfalse;
}

static void Plugin_Config( pluginVMConfig_t *cfg, const plugin_t *p )
{
	Com_Memset( cfg, 0, sizeof( *cfg ) );
	cfg->name = p->name;
	cfg->module = "odfc";
	cfg->dispatch = Plugin_Dispatch;
	cfg->exportNames = odfc_exportNames;
	cfg->numExports = ODFC_NUM_EXPORTS;
	cfg->grantedCaps = ODFC_CAPS_ALL;
	cfg->capNames = odfc_capNames;
	cfg->numCaps = ODFC_NUM_CAPS;
	cfg->maxDataBytes = 16 * 1024 * 1024;
}

// Reads the file of a plugin. Returns NULL (and says why in err) if it cannot be read.
static void *Plugin_ReadFile( const char *path, int *size, char *err, int errSize )
{
	FILE *f = Sys_FOpen( path, "rb" );
	long len;
	void *data;

	if ( !f ) {
		Com_sprintf( err, errSize, "%s not found", path );
		return NULL;
	}
	fseek( f, 0, SEEK_END );
	len = ftell( f );
	fseek( f, 0, SEEK_SET );
	if ( len < 8 || len > MAX_MODULE_FILE ) {
		fclose( f );
		Com_sprintf( err, errSize, "%s is not a plugin (size %ld)", path, len );
		return NULL;
	}
	data = malloc( len );
	if ( !data || fread( data, 1, len, f ) != (size_t)len ) {
		free( data );
		fclose( f );
		Com_sprintf( err, errSize, "cannot read %s", path );
		return NULL;
	}
	fclose( f );
	*size = (int)len;
	return data;
}

// Loads the module of the plugin and starts it, or says why it did not. The state is PS_RUNNING or one of the others after.
static void Plugin_Start( plugin_t *p )
{
	pluginVMConfig_t cfg;
	char err[256], value[64];
	void *data;
	int size = 0, rc = 0;
	qboolean ok;

	p->state = PS_FAILED;
	p->notClient = qfalse;
	p->note[0] = '\0';
	p->version[0] = '\0';

	Q_strncpyz( p->path, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "plugins", va( "%s" PLUGIN_EXT, p->name ) ), sizeof( p->path ) );
	data = Plugin_ReadFile( p->path, &size, err, sizeof( err ) );
	if ( !data ) {
		Plugin_Fail( p, "%s", err );
		return;
	}

	Plugin_Config( &cfg, p );
	p->vm = PluginVM_Load( data, size, &cfg, p, err, sizeof( err ) );
	free( data );
	if ( !p->vm ) {
		// a server plugin is in the same folder: it is not an error, it is just not ours
		p->notClient = !Q_strncmp( err, "is for the API odfs", 19 );
		Plugin_Fail( p, "%s", err );
		return;
	}

	Manifest_Value( PluginVM_Manifest( p->vm ), "version", p->version, sizeof( p->version ) );
	if ( Manifest_Value( PluginVM_Manifest( p->vm ), "api", value, sizeof( value ) ) && atoi( value ) > ODFC_API_VERSION ) {
		Plugin_Fail( p, "built for client plugin API %i, this client has %i", atoi( value ), ODFC_API_VERSION );
		return;
	}

	if ( PLUGIN_HAS( p, OnInit ) ) {
		PLUGIN_RUN_OK( ok, p, "OnInit", ODFC_Call_OnInit( p->vm, &rc ) );
		if ( !ok ) {
			return;		// state and message set by Plugin_Fault
		}
		if ( rc != 0 ) {
			Plugin_Fail( p, "init returned %i", rc );
			return;
		}
	}

	p->state = PS_RUNNING;
	Com_Printf( "Client plugin '%s' %s loaded\n", p->name, p->version );
}

// Stops a running plugin and unloads it.
static void Plugin_Stop( plugin_t *p )
{
	if ( p->state == PS_RUNNING ) {
		if ( mapLoaded && PLUGIN_HAS( p, OnCgameShutdown ) ) {
			PLUGIN_RUN( p, "OnCgameShutdown", ODFC_Call_OnCgameShutdown( p->vm ) );
		}
		if ( p->state == PS_RUNNING && PLUGIN_HAS( p, OnShutdown ) ) {
			PLUGIN_RUN( p, "OnShutdown", ODFC_Call_OnShutdown( p->vm ) );
		}
		if ( p->state == PS_RUNNING ) {
			p->state = PS_STOPPED;
		}
	}
	Plugin_RemoveCommands( p );
	Plugin_Unload( p );
}


// ---------------------------------------------------------------------------
// the plugins that are switched off: plugins/disabled, one name per line
// ---------------------------------------------------------------------------

static const char *DisabledFilePath( void )
{
	return FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "plugins", DISABLED_FILE );
}

static qboolean Disabled_Listed( const char *name )
{
	int i;

	for ( i = 0; i < numDisabledNames; i++ ) {
		if ( !Q_stricmp( disabledNames[i], name ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void Disabled_Read( void )
{
	static char text[MAX_PLUGINS * ( MAX_PLUGIN_NAME + 2 )];
	char *s, *line, *end;
	FILE *f;
	size_t len;

	numDisabledNames = 0;
	f = Sys_FOpen( DisabledFilePath(), "rb" );
	if ( !f ) {
		return;
	}
	len = fread( text, 1, sizeof( text ) - 1, f );
	fclose( f );
	text[len] = '\0';

	s = text;
	if ( (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF ) {
		s += 3;		// a byte order mark, from an editor
	}
	while ( *s ) {
		line = s;
		while ( *s && *s != '\n' ) {
			s++;
		}
		if ( *s ) {
			*s++ = '\0';
		}
		while ( *line == ' ' || *line == '\t' || *line == '\r' ) {
			line++;
		}
		end = line + strlen( line );
		while ( end > line && ( end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ) ) {
			*--end = '\0';
		}
		if ( !*line || *line == '#' || !ValidPluginName( line ) || Disabled_Listed( line ) ) {
			continue;
		}
		if ( numDisabledNames >= MAX_PLUGINS ) {
			break;
		}
		Q_strncpyz( disabledNames[numDisabledNames++], line, MAX_PLUGIN_NAME );
	}
}

// Writes the names of the plugins that are switched off. Names of plugins that are not in the folder any more are dropped.
static qboolean Disabled_Write( void )
{
	FILE *f;
	int i, n = 0;

	for ( i = 0; i < numPlugins; i++ ) {
		if ( plugins[i].disabled && !plugins[i].hidden ) {
			Q_strncpyz( disabledNames[n++], plugins[i].name, MAX_PLUGIN_NAME );
		}
	}
	numDisabledNames = n;

	f = Sys_FOpen( DisabledFilePath(), "wb" );
	if ( !f ) {
		Com_Printf( S_COLOR_YELLOW "Cannot write %s, the choice is lost when the game is closed.\n", DisabledFilePath() );
		return qfalse;
	}
	for ( i = 0; i < n; i++ ) {
		fprintf( f, "%s\n", disabledNames[i] );
	}
	fclose( f );
	return qtrue;
}


// ---------------------------------------------------------------------------
// the plugins folder
// ---------------------------------------------------------------------------

static plugin_t *Plugin_Find( const char *name )
{
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		if ( !Q_stricmp( plugins[i].name, name ) ) {
			return &plugins[i];
		}
	}
	return NULL;
}

static int Plugins_NameCompare( const void *a, const void *b )
{
	return Q_stricmp( *(char *const *)a, *(char *const *)b );
}

/*
===============
Plugins_Scan

Starts every module of the plugins folder that is not known yet, except the ones that are switched off. Modules that are
not client plugins (server plugins) are remembered as hidden, so they are not tried again.
===============
*/
static void Plugins_Scan( void )
{
	char dir[MAX_OSPATH], **files;
	int numFiles, extLen = (int)strlen( PLUGIN_EXT ), i;

	Q_strncpyz( dir, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "plugins", "" ), sizeof( dir ) );
	i = (int)strlen( dir );
	while ( i > 0 && ( dir[i - 1] == '/' || dir[i - 1] == '\\' ) ) {
		dir[--i] = '\0';
	}

	files = Sys_ListFiles( dir, PLUGIN_EXT, NULL, &numFiles, qfalse );
	if ( !files ) {
		return;
	}
	qsort( files, numFiles, sizeof( char * ), Plugins_NameCompare );

	for ( i = 0; i < numFiles; i++ ) {
		char name[MAX_PLUGIN_NAME];
		int len = (int)strlen( files[i] ) - extLen;
		plugin_t *p;

		// the listing can match more than the extension (8.3 names)
		if ( len < 1 || len >= MAX_PLUGIN_NAME || Q_stricmp( files[i] + len, PLUGIN_EXT ) ) {
			continue;
		}
		Q_strncpyz( name, files[i], len + 1 );
		if ( !ValidPluginName( name ) || Plugin_Find( name ) ) {
			continue;
		}
		if ( numPlugins >= MAX_PLUGINS ) {
			Com_Printf( S_COLOR_YELLOW "Plugin '%s' not loaded: at most %i plugins are looked at\n", name, MAX_PLUGINS );
			break;
		}

		p = &plugins[numPlugins++];
		Com_Memset( p, 0, sizeof( *p ) );
		Q_strncpyz( p->name, name, sizeof( p->name ) );
		p->disabled = Disabled_Listed( name );
		if ( p->disabled ) {
			p->state = PS_DISABLED;
			continue;
		}
		Plugin_Start( p );
		p->hidden = p->notClient;
	}
	Sys_FreeFileList( files );
}

static void Plugins_List_f( void )
{
	static const char *const stateNames[] = { "running", "stopped", "failed", "stopped*", "disabled" };
	int i, shown = 0;

	// clplugins stats: what the calls of every plugin cost (wall time, with the engine's work for its imports); clplugins reset
	if ( !Q_stricmp( Cmd_Argv( 1 ), "stats" ) || !Q_stricmp( Cmd_Argv( 1 ), "reset" ) ) {
		qboolean reset = Q_stricmp( Cmd_Argv( 1 ), "reset" ) == 0;

		for ( i = 0; i < numPlugins; i++ ) {
			plugin_t *p = &plugins[i];

			if ( p->hidden || !p->vm ) {
				continue;
			}
			if ( reset ) {
				PluginVM_ResetStats( p->vm );
				continue;
			}
			Com_Printf( "%s\n", p->name );
			PluginVM_PrintStats( p->vm, odfc_exportNames, ODFC_NUM_EXPORTS );
		}
		return;
	}

	Com_Printf( "%-16s %-8s %s\n", "name", "state", "details" );
	for ( i = 0; i < numPlugins; i++ ) {
		const plugin_t *p = &plugins[i];

		if ( p->hidden ) {
			continue;
		}
		Com_Printf( "%-16s %-8s %s%s\n", p->name, stateNames[p->state],
			p->state == PS_RUNNING ? p->version : p->note,
			p->disabled && p->state != PS_DISABLED ? " (disabled, off after a restart)" : "" );
		shown++;
	}
	if ( !shown ) {
		Com_Printf( "No client plugins. Put them in %s/plugins.\n", Cvar_VariableString( "fs_homepath" ) );
	}
}

void CL_Plugins_Init( void )
{
	int i, disabledInFolder = 0;

	Cmd_AddCommand( "clplugins", Plugins_List_f );

	Disabled_Read();
	Plugins_Scan();

	// names of modules that are gone
	for ( i = 0; i < numPlugins; i++ ) {
		if ( plugins[i].disabled ) {
			disabledInFolder++;
		}
	}
	if ( disabledInFolder != numDisabledNames ) {
		Disabled_Write();
	}
}

void CL_Plugins_Shutdown( void )
{
	int i;

	Cmd_RemoveCommand( "clplugins" );
	for ( i = 0; i < numPlugins; i++ ) {
		Plugin_Stop( &plugins[i] );
	}
	for ( i = 0; i < numPluginCommands; i++ ) {
		Cmd_RemoveCommand( pluginCommands[i].name );
	}
	numPluginCommands = 0;
	mapLoaded = qfalse;
	numPlugins = 0;
	numDisabledNames = 0;
	GhostPaths_Free();
}


// ---------------------------------------------------------------------------
// switching plugins on and off while the game runs, for the menu
// ---------------------------------------------------------------------------

// the plugins the menu shows
static plugin_t *Plugin_Visible( int index )
{
	int i;

	if ( index < 0 ) {
		return NULL;
	}
	for ( i = 0; i < numPlugins; i++ ) {
		if ( !plugins[i].hidden && index-- == 0 ) {
			return &plugins[i];
		}
	}
	return NULL;
}

int CL_Plugins_Count( void )
{
	int i, n = 0;

	for ( i = 0; i < numPlugins; i++ ) {
		if ( !plugins[i].hidden ) {
			n++;
		}
	}
	return n;
}

/*
===============
CL_Plugins_Info

One thing about plugin number index of CL_Plugins_Count: field 0 is the name, 1 the version (only of a running plugin),
2 why it is not running, 3 "1" when it is switched off. Returns the state: 0 running, 1 stopped, 2 failed, 3 stopped by
the engine (it trapped or ran out of budget), 4 disabled; -1 when there is no such plugin.
===============
*/
int CL_Plugins_Info( int index, int field, char *out, int outSize )
{
	const plugin_t *p = Plugin_Visible( index );
	const char *text = "";

	if ( !out || outSize < 1 ) {
		return -1;
	}
	out[0] = '\0';
	if ( !p ) {
		return -1;
	}
	switch ( field ) {
	case 0: text = p->name; break;
	case 1: text = p->state == PS_RUNNING ? p->version : ""; break;
	case 2: text = p->state == PS_FAILED || p->state == PS_FAULTED ? p->note : ""; break;
	case 3: text = p->disabled ? "1" : "0"; break;
	}
	Q_strncpyz( out, text, outSize );
	return p->state;
}

/*
===============
CL_Plugins_SetEnabled

Switches a plugin on or off now and remembers it for the next start. Returns 0 only when there is no plugin of that
name; whether it runs is in CL_Plugins_Info (it may have failed to start).
===============
*/
int CL_Plugins_SetEnabled( const char *name, int enable )
{
	plugin_t *p = name ? Plugin_Find( name ) : NULL;

	if ( !p || p->hidden ) {
		return 0;
	}
	if ( ( enable != 0 ) == !p->disabled && p->state != PS_FAILED && p->state != PS_FAULTED ) {
		return 1;
	}

	p->disabled = enable ? qfalse : qtrue;
	if ( p->disabled ) {
		Plugin_Stop( p );
		p->state = PS_DISABLED;
		p->note[0] = '\0';
	} else {
		Plugin_Start( p );
		// it missed the map that is already loaded
		if ( p->state == PS_RUNNING && mapLoaded && PLUGIN_HAS( p, OnMapLoad ) ) {
			PLUGIN_RUN( p, "OnMapLoad", ODFC_Call_OnMapLoad( p->vm, loadedMap ) );
		}
	}
	Disabled_Write();
	return 1;
}

// picks up modules that were put into the folder while the game runs
void CL_Plugins_Rescan( void )
{
	Plugins_Scan();
}


// ---------------------------------------------------------------------------
// running: called from the cgame system calls, see cl_cgame.c
// ---------------------------------------------------------------------------

// the game module hands over its predicted player state once per frame, before it draws
void CL_Plugins_SetPlayerState( const playerState_t *ps, int viewFlags )
{
	if ( !numPlugins || !ps ) {
		return;
	}
	publishedPS = *ps;
	publishedViewFlags = viewFlags;
	publishedTime = cl.serverTime;
	published = qtrue;
}

// The game module hands over a part of the path of a ghost. first 0 starts a new path (the old one is gone), any other part has to
// continue the points that are there; what does not is dropped. The points must be in the order of their time.
void CL_Plugins_SetGhostPath( int slot, int first, int count, const void *points, int complete )
{
	ghostPath_t *g;
	const odfcGhostPoint_t *src = (const odfcGhostPoint_t *)points;
	int i;

	if ( !numPlugins || slot < 0 || slot >= ODFC_GHOST_SLOTS || count < 0 || ( count > 0 && !src ) ) {
		return;
	}
	g = &ghostPath[slot];
	if ( first == 0 ) {
		g->num = 0;
		g->complete = qfalse;
		g->raceMs = 0;
		g->serial = ++ghostSerial;
	} else if ( !g->points || first != g->num ) {
		return;
	}
	if ( count > 0 && g->num + count > g->capacity && g->capacity < ODFC_GHOST_MAX_POINTS ) {
		int capacity = g->capacity ? g->capacity : 8192;
		odfcGhostPoint_t *points;

		while ( capacity < g->num + count && capacity < ODFC_GHOST_MAX_POINTS ) {
			capacity *= 2;
		}
		if ( capacity > ODFC_GHOST_MAX_POINTS ) {
			capacity = ODFC_GHOST_MAX_POINTS;
		}
		points = Z_Malloc( capacity * sizeof( odfcGhostPoint_t ) );
		if ( g->points ) {
			Com_Memcpy( points, g->points, g->num * sizeof( odfcGhostPoint_t ) );
			Z_Free( g->points );
		}
		g->points = points;
		g->capacity = capacity;
	}
	for ( i = 0; i < count && g->num < g->capacity; i++ ) {
		if ( g->num > 0 && src[i].ms < g->points[g->num - 1].ms ) {
			break;
		}
		g->points[g->num++] = src[i];
	}
	if ( complete ) {
		g->complete = qtrue;
	}
}

// the game module says which snapshot the state it publishes is from (before SetPlayerState, every frame)
void CL_Plugins_SetStatsTime( int serverTime )
{
	publishedStatsTime = serverTime;
}

// the exact race time of the run the path of a slot was recorded from
void CL_Plugins_SetGhostTime( int slot, int ms )
{
	if ( !numPlugins || slot < 0 || slot >= ODFC_GHOST_SLOTS ) {
		return;
	}
	ghostPath[slot].raceMs = ms > 0 ? ms : 0;
}

static void GhostPaths_Free( void )
{
	int i;

	for ( i = 0; i < ODFC_GHOST_SLOTS; i++ ) {
		if ( ghostPath[i].points ) {
			Z_Free( ghostPath[i].points );
		}
		Com_Memset( &ghostPath[i], 0, sizeof( ghostPath[i] ) );
	}
}

void CL_Plugins_MapLoad( const char *mapName )
{
	char name[MAX_QPATH];
	int i;

	Q_strncpyz( name, mapName ? mapName : "", sizeof( name ) );
	Q_strncpyz( loadedMap, name, sizeof( loadedMap ) );
	mapLoaded = qtrue;
	published = qfalse;
	publishedStatsTime = 0;
	SoundOwners_Reset();
	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING && PLUGIN_HAS( p, OnMapLoad ) ) {
			PLUGIN_RUN( p, "OnMapLoad", ODFC_Call_OnMapLoad( p->vm, name ) );
		}
	}
}

// the start of a client frame: a snapshot of the game a plugin asked for is applied now
void CL_Plugins_BeforeFrame( void )
{
	const int handle = stateApplyHandle;

	if ( !handle ) {
		return;
	}
	stateApplyHandle = 0;
	if ( cls.state != CA_ACTIVE || clc.demoplaying || !SV_StateApply( handle ) ) {
		return;
	}
	stateWait = qtrue;
	stateWaitMsg = clc.serverMessageSequence;

	// the demo that is recorded goes on from the checkpoint: what was recorded since is given up, and the messages of the old timeline
	// that are still on their way are not recorded
	if ( clc.demoRoute ) {
		if ( DemoRoute_Applied( handle ) ) {
			clc.demowaiting = qtrue;
		} else {
			CL_DemoRouteRestart();
		}
	}
}

// Called by CL_ParseSnapshot with every snapshot it makes current: the first one after an applied game snapshot is on the new
// timeline, so the client clock and the view are set to it. This is before the client sends its next usercmds, which must have
// the new time (the server throws them away until then).
void CL_Plugins_StateSnapshot( void )
{
	int i;

	if ( !stateWait ) {
		return;
	}
	if ( cls.state != CA_ACTIVE || clc.demoplaying ) {
		stateWait = qfalse;
		return;
	}
	if ( cl.snap.messageNum - stateWaitMsg <= 0 ) {
		return;
	}
	stateWait = qfalse;
	stateNotify = qtrue;
	stateOldTime = cl.serverTime;

	cl.serverTime = cl.oldServerTime = cl.oldFrameServerTime = cl.snap.serverTime;
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
	cl.extrapolatedSnapshot = qfalse;

	// The usercmds of the old timeline are in the ring still. The server skips them (they are not newer than the last one it ran), but
	// the cgame looks at the oldest one to see whether the server answers ("Connection Interrupted" when it is newer than the last
	// command the server ran) and predicts with the ones newer than that. Time 0 is "long ago" for both.
	for ( i = 0; i < CMD_BACKUP; i++ ) {
		cl.cmds[i].serverTime = 0;
	}

	// the server turned the player by delta_angles: what the mouse has to say is the rest
	for ( i = 0; i < 3; i++ ) {
		cl.viewangles[i] = SHORT2ANGLE( ANGLE2SHORT( cl.snap.ps.viewangles[i] ) - cl.snap.ps.delta_angles[i] );
	}
}

// Called by CL_SetCGameTime: after the clock was reset (above) the cgame and the plugins are told that time went back.
void CL_Plugins_StateNotify( void )
{
	if ( !stateNotify ) {
		return;
	}
	stateNotify = qfalse;
	if ( cls.state != CA_ACTIVE || clc.demoplaying ) {
		return;
	}
	CL_CGameTimeChange( cl.snap.serverTime, cl.snap.messageNum, clc.lastExecutedServerCommand );
	CL_Plugins_DemoSeek( stateOldTime, cl.snap.serverTime );
}

// the demo that is playing jumped (cl_demoseek.c)
void CL_Plugins_DemoSeek( int fromTime, int toTime )
{
	int i;

	if ( !numPlugins || !mapLoaded ) {
		return;
	}
	published = qfalse;
	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING && PLUGIN_HAS( p, OnDemoSeek ) ) {
			PLUGIN_RUN( p, "OnDemoSeek", ODFC_Call_OnDemoSeek( p->vm, fromTime, toTime ) );
		}
	}
}

void CL_Plugins_CGameShutdown( void )
{
	int i;

	if ( !mapLoaded ) {
		return;
	}
	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING && PLUGIN_HAS( p, OnCgameShutdown ) ) {
			PLUGIN_RUN( p, "OnCgameShutdown", ODFC_Call_OnCgameShutdown( p->vm ) );
		}
	}
	mapLoaded = qfalse;
	GhostPaths_Free();
}

// called with the refdef of the game's 3D scene right before it is rendered
void CL_Plugins_BeforeScene( const refdef_t *fd )
{
	odfcView_t view;
	int i;

	if ( !numPlugins || !mapLoaded || !fd || ( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		return;
	}
	Com_Memset( &view, 0, sizeof( view ) );
	view.time = cl.serverTime;
	view.x = fd->x;
	view.y = fd->y;
	view.width = fd->width;
	view.height = fd->height;
	view.fovX = fd->fov_x;
	view.fovY = fd->fov_y;
	view.screenWidth = cls.glconfig.vidWidth;
	view.screenHeight = cls.glconfig.vidHeight;
	VectorCopy( fd->vieworg, view.origin );
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( fd->viewaxis[i], view.axis[i] );
	}

	// PLUGIN_RUN puts drawPhase back to DRAW_NONE after each call
	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING && PLUGIN_HAS( p, OnDraw3D ) ) {
			drawPhase = DRAW_3D;
			PLUGIN_RUN( p, "OnDraw3D", ODFC_Call_OnDraw3D( p->vm, &view ) );
		}
	}
}

static void Plugins_Draw2D( int phase )
{
	qboolean drew = qfalse;
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING && PLUGIN_HAS( p, OnDraw2D ) ) {
			drawPhase = DRAW_2D;
			drew = qtrue;
			PLUGIN_RUN( p, "OnDraw2D", ODFC_Call_OnDraw2D( p->vm, phase ) );
		}
	}
	if ( drew ) {
		re.SetColor( NULL );		// the game does not expect the colour a plugin left behind
	}
}

// called right after the game's 3D scene was rendered: its HUD is drawn next
void CL_Plugins_AfterScene( const refdef_t *fd )
{
	if ( !numPlugins || !mapLoaded || !fd || ( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		return;
	}
	Plugins_Draw2D( ODFC_DRAW2D_SCENE );
}

// called when the game has drawn the whole frame
void CL_Plugins_AfterFrame( void )
{
	if ( !numPlugins || !mapLoaded || !cgvm ) {
		return;
	}
	Plugins_Draw2D( ODFC_DRAW2D_FRAME );
}

// Whether a plugin wants the sound dropped (see filter_sound in the API). Called by the cgame system calls that start sounds.
qboolean CL_Plugins_FilterSound( int entityNum, qboolean isLoop )
{
	const clSnapshot_t *snap;
	int i, owner;

	if ( !numPlugins || !mapLoaded || cls.state != CA_ACTIVE ) {
		return qfalse;
	}
	snap = CurrentSnapshot();
	if ( !snap ) {
		return qfalse;
	}
	SoundOwners_Update( snap );
	owner = ( entityNum >= 0 && entityNum < MAX_GENTITIES ) ? soundOwner[entityNum] : -1;

	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];
		qboolean ok;
		int drop = 0;

		if ( p->state != PS_RUNNING || !PLUGIN_HAS( p, OnFilterSound ) ) {
			continue;
		}
		PLUGIN_RUN_OK( ok, p, "OnFilterSound", ODFC_Call_OnFilterSound( p->vm, entityNum, isLoop ? 1 : 0, owner, snap->ps.clientNum, &drop ) );
		if ( ok && drop ) {
			return qtrue;
		}
	}
	return qfalse;
}
