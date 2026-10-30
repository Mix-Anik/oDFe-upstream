/*
===========================================================================
Native client plugins: loading, calling them with a crash guard and giving them a
read-only view of the game. The interface is in odf_client_plugin_api.h.

The crash guard is the one of the server plugins (sv_plugin_guard.c).
===========================================================================
*/

#include "client.h"
#include "../qcommon/cm_public.h"
#include "../server/sv_plugin_guard.h"
#include "odf_client_plugin_api.h"

#ifdef _WIN32
#define PLUGIN_EXT	".dll"
#elif defined( __APPLE__ )
#define PLUGIN_EXT	".dylib"
#else
#define PLUGIN_EXT	".so"
#endif

#define MAX_PLUGINS			8
#define MAX_PLUGIN_NAME		32
#define MAX_POLY_VERTS		64

// the API structs are copied to renderer structs byte by byte
typedef char odfcPolyVertSize_t[ sizeof( odfcPolyVert_t ) == sizeof( polyVert_t ) ? 1 : -1 ];
typedef char odfcPolyVertSt_t[ offsetof( odfcPolyVert_t, st ) == offsetof( polyVert_t, st ) ? 1 : -1 ];
typedef char odfcPolyVertRgba_t[ offsetof( odfcPolyVert_t, rgba ) == offsetof( polyVert_t, modulate ) ? 1 : -1 ];
typedef char odfcEntitiesMatch_t[ ODFC_MAX_SNAPSHOT_ENTITIES == MAX_ENTITIES_IN_SNAPSHOT ? 1 : -1 ];

typedef enum {
	PS_RUNNING,
	PS_STOPPED,		// shut down normally
	PS_FAILED,		// did not load or refused to start
	PS_FAULTED		// crashed, never called again
} pluginState_t;

typedef struct {
	char						name[MAX_PLUGIN_NAME];
	char						path[MAX_OSPATH];
	void						*lib;
	const odfcPluginExport_t	*exp;
	pluginRange_t				range;
	qboolean					guarded;
	pluginState_t				state;
	char						note[192];		// why it is not running
} plugin_t;

// what the plugin is allowed to draw right now
typedef enum {
	DRAW_NONE,
	DRAW_3D,
	DRAW_2D
} drawPhase_t;

static plugin_t				plugins[MAX_PLUGINS];
static int					numPlugins;
static plugin_t				*current;		// the plugin whose code is running, for log prefixes
static cvar_t				*cl_plugins;
static odfcPluginImport_t	imports;
static qboolean				guardInstalled;
static qboolean				mapLoaded;		// between MapLoad and CGameShutdown
static drawPhase_t			drawPhase;

// the predicted player state the game module published (trap_ODF_SetPlayerState), and the view time it was published at
static playerState_t		publishedPS;
static int					publishedTime;
static qboolean				published;
static int					publishedViewFlags;


// ---------------------------------------------------------------------------
// guarded calls into a plugin
// ---------------------------------------------------------------------------

typedef enum {
	PC_ENTRY,
	PC_INIT,
	PC_SHUTDOWN,
	PC_MAPLOAD,
	PC_CGAMESHUTDOWN,
	PC_DRAW3D,
	PC_DRAW2D,
	PC_COMMAND,
	PC_FILTERSOUND
} callKind_t;

typedef struct {
	callKind_t					kind;
	plugin_t					*plugin;
	odfcPluginEntry_t			entry;		// PC_ENTRY
	const odfcPluginExport_t	*result;	// PC_ENTRY
	int							rc;			// PC_INIT
	const char					*mapName;	// PC_MAPLOAD
	const odfcView_t			*view;		// PC_DRAW3D
	int							phase;		// PC_DRAW2D
	const char					*cmdName;	// PC_COMMAND
	int							sound[4];	// PC_FILTERSOUND: entityNum, isLoop, entityClientNum, ownClientNum
	int							soundResult;	// PC_FILTERSOUND
} call_t;

static void CallThunk( void *arg )
{
	call_t *c = (call_t *)arg;
	const odfcPluginExport_t *e = c->plugin->exp;

	switch ( c->kind ) {
	case PC_ENTRY:			c->result = c->entry( &imports ); break;
	case PC_INIT:			c->rc = e->Init(); break;
	case PC_SHUTDOWN:		e->Shutdown(); break;
	case PC_MAPLOAD:		e->MapLoad( c->mapName ); break;
	case PC_CGAMESHUTDOWN:	e->CGameShutdown(); break;
	case PC_DRAW3D:			e->Draw3D( c->view ); break;
	case PC_DRAW2D:			e->Draw2D( c->phase ); break;
	case PC_COMMAND:		e->Command( c->cmdName ); break;
	case PC_FILTERSOUND:	c->soundResult = e->FilterSound( c->sound[0], c->sound[1], c->sound[2], c->sound[3] ); break;
	}
}

static const char *CallDescription( const call_t *c )
{
	switch ( c->kind ) {
	case PC_ENTRY:			return "ODF_ClientPluginEntry";
	case PC_INIT:			return "Init";
	case PC_SHUTDOWN:		return "Shutdown";
	case PC_MAPLOAD:		return "MapLoad";
	case PC_CGAMESHUTDOWN:	return "CGameShutdown";
	case PC_DRAW3D:			return "Draw3D";
	case PC_DRAW2D:			return "Draw2D";
	case PC_COMMAND:		return "Command";
	case PC_FILTERSOUND:	return "FilterSound";
	}
	return "?";
}

/*
===============
Plugin_Fault

A plugin crashed: it is never called again.
===============
*/
static void Plugin_Fault( plugin_t *p, const call_t *c, const pluginFault_t *f )
{
	Com_sprintf( p->note, sizeof( p->note ), "crashed in %s: %s at offset 0x%lx", CallDescription( c ), f->what, (unsigned long)f->offset );
	p->state = PS_FAULTED;

	Com_Printf( S_COLOR_RED "Plugin '%s' crashed in %s: %s at %s+0x%lx, accessing 0x%lx.\n"
		S_COLOR_RED "The plugin has been stopped and will not run again until the game is restarted.\n",
		p->name, CallDescription( c ), f->what, p->path, (unsigned long)f->offset, (unsigned long)f->dataAddress );
}

/*
===============
Plugin_Run

Returns qfalse if the plugin crashed.
===============
*/
static qboolean Plugin_Run( plugin_t *p, call_t *c )
{
	plugin_t *prev = current;
	pluginFault_t fault;
	int ok;

	c->plugin = p;
	current = p;
	if ( p->guarded ) {
		ok = PluginGuard_Call( &p->range, CallThunk, c, &fault );
	} else {
		CallThunk( c );
		ok = 1;
	}
	current = prev;
	drawPhase = DRAW_NONE;

	if ( !ok ) {
		Plugin_Fault( p, c, &fault );
		return qfalse;
	}
	return qtrue;
}

// Whether the export table of the plugin is big enough to have the function (plugins built for an older API have a shorter one)
#define PLUGIN_HAS( p_, fn_ ) \
	( (p_)->exp->size >= (int)( offsetof( odfcPluginExport_t, fn_ ) + sizeof( (p_)->exp->fn_ ) ) && (p_)->exp->fn_ )

// Runs one call of the given kind on every running plugin that has the function.
#define PLUGINS_RUN( kind_, fn_, setup_ ) \
	do { \
		int i_; \
		for ( i_ = 0; i_ < numPlugins; i_++ ) { \
			plugin_t *p_ = &plugins[i_]; \
			call_t call_; \
			if ( p_->state != PS_RUNNING || !PLUGIN_HAS( p_, fn_ ) ) { \
				continue; \
			} \
			Com_Memset( &call_, 0, sizeof( call_ ) ); \
			call_.kind = kind_; \
			setup_ \
			Plugin_Run( p_, &call_ ); \
		} \
	} while ( 0 )


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

static void Imp_HomePath( char *out, int outSize )
{
	int len;

	if ( !out || outSize < 1 ) {
		return;
	}
	Cvar_VariableStringBuffer( "fs_homepath", out, outSize );
	len = (int)strlen( out );
	while ( len > 0 && ( out[len - 1] == '/' || out[len - 1] == '\\' ) ) {
		out[--len] = '\0';
	}
}

static void Imp_CvarRegister( const char *name, const char *defaultValue, int flags )
{
	if ( !name || !*name || !defaultValue ) {
		return;
	}
	if ( flags & ODFC_CVAR_ROM ) {
		Cvar_Get( name, defaultValue, CVAR_ROM );		// a ROM cvar always has the value given
		return;
	}
	Cvar_Get( name, defaultValue, ( flags & ODFC_CVAR_ARCHIVE ) ? CVAR_ARCHIVE_ND : 0 );
}

// private cvars (keys, passwords) are not readable, like for the game module
static void Imp_CvarString( const char *name, char *out, int outSize )
{
	if ( !out || outSize < 1 ) {
		return;
	}
	if ( !name ) {
		out[0] = '\0';
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

static int Imp_RegisterShaderNoMip( const char *name )
{
	if ( !name || !*name || !mapLoaded ) {
		return 0;
	}
	return re.RegisterShaderNoMip( name );
}

static int Imp_RegisterModel( const char *name )
{
	if ( !name || !*name || !mapLoaded ) {
		return 0;
	}
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

#define MAX_PLUGIN_COMMANDS		32
#define MAX_COMMAND_NAME		32

typedef struct {
	char		name[MAX_COMMAND_NAME];
	plugin_t	*plugin;
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
			call_t call;

			if ( c->plugin->state != PS_RUNNING || !PLUGIN_HAS( c->plugin, Command ) ) {
				return;
			}
			Com_Memset( &call, 0, sizeof( call ) );
			call.kind = PC_COMMAND;
			call.cmdName = c->name;
			Plugin_Run( c->plugin, &call );
			return;
		}
	}
}

static int Imp_CommandRegister( const char *name )
{
	if ( !current || !name || !ValidCommandName( name ) || numPluginCommands >= MAX_PLUGIN_COMMANDS ) {
		return 0;
	}
	// the engine's own commands (and those of other plugins) cannot be taken over
	if ( Cmd_CommandExists( name ) ) {
		Com_Printf( S_COLOR_YELLOW "Plugin '%s': command %s already exists\n", current->name, name );
		return 0;
	}
	Q_strncpyz( pluginCommands[numPluginCommands].name, name, MAX_COMMAND_NAME );
	pluginCommands[numPluginCommands].plugin = current;
	numPluginCommands++;
	Cmd_AddCommand( name, Plugins_Command_f );
	return 1;
}

static int Imp_CommandArgc( void )
{
	return Cmd_Argc();
}

static void Imp_CommandArgv( int n, char *out, int outSize )
{
	if ( !out || outSize < 1 ) {
		return;
	}
	if ( n < 0 || n >= Cmd_Argc() ) {
		out[0] = '\0';
		return;
	}
	Cmd_ArgvBuffer( n, out, outSize );
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
static void Plugin_Fail( plugin_t *p, const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	Q_vsnprintf( p->note, sizeof( p->note ), fmt, ap );
	va_end( ap );
	p->state = PS_FAILED;
	Com_Printf( S_COLOR_YELLOW "Plugin '%s' not loaded: %s\n", p->name, p->note );
}

static void Plugin_Load( const char *name )
{
	plugin_t *p;
	odfcPluginEntry_t entry;
	call_t call;
	FILE *f;
	int i;

	for ( i = 0; i < numPlugins; i++ ) {
		if ( !Q_stricmp( plugins[i].name, name ) ) {
			return;		// listed twice
		}
	}
	if ( numPlugins >= MAX_PLUGINS ) {
		Com_Printf( S_COLOR_YELLOW "Plugin '%s' not loaded: at most %i plugins\n", name, MAX_PLUGINS );
		return;
	}

	p = &plugins[numPlugins++];
	Com_Memset( p, 0, sizeof( *p ) );
	Q_strncpyz( p->name, name, sizeof( p->name ) );

	if ( !ValidPluginName( name ) ) {
		Plugin_Fail( p, "invalid name (letters, digits, _ and - only)" );
		return;
	}

	Q_strncpyz( p->path, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "plugins", va( "%s" PLUGIN_EXT, name ) ), sizeof( p->path ) );
	f = Sys_FOpen( p->path, "rb" );
	if ( !f ) {
		Plugin_Fail( p, "%s not found", p->path );
		return;
	}
	fclose( f );

	p->lib = Sys_LoadLibrary( p->path );
	if ( !p->lib ) {
		Plugin_Fail( p, "cannot load %s (missing dependency or wrong architecture?)", p->path );
		return;
	}
	entry = (odfcPluginEntry_t)Sys_LoadFunction( p->lib, "ODF_ClientPluginEntry" );
	if ( !entry ) {
		Plugin_Fail( p, "%s has no ODF_ClientPluginEntry (not a client plugin?)", p->path );
		Sys_UnloadLibrary( p->lib );
		p->lib = NULL;
		return;
	}

	p->guarded = guardInstalled && PluginGuard_ModuleRange( p->lib, (const void *)entry, &p->range );
	if ( !p->guarded ) {
		Com_Printf( S_COLOR_YELLOW "Plugin '%s': crash protection is not available on this platform, a crash in the plugin will crash the game.\n", name );
	}

	Com_Memset( &call, 0, sizeof( call ) );
	call.kind = PC_ENTRY;
	call.entry = entry;
	if ( !Plugin_Run( p, &call ) ) {
		return;		// state and message set by Plugin_Fault
	}
	p->exp = call.result;
	if ( !p->exp ) {
		Plugin_Fail( p, "the plugin refused to load" );
		return;
	}
	if ( p->exp->apiVersion > ODFC_API_VERSION ) {
		Plugin_Fail( p, "built for client plugin API %i, this client has %i", p->exp->apiVersion, ODFC_API_VERSION );
		p->exp = NULL;
		return;
	}
	// the functions of version 1 have to be there; newer ones are optional and checked where they are used
	if ( p->exp->size < (int)( offsetof( odfcPluginExport_t, Draw2D ) + sizeof( p->exp->Draw2D ) ) ) {
		Plugin_Fail( p, "export table is too small (%i bytes)", p->exp->size );
		p->exp = NULL;
		return;
	}

	if ( PLUGIN_HAS( p, Init ) ) {
		Com_Memset( &call, 0, sizeof( call ) );
		call.kind = PC_INIT;
		if ( !Plugin_Run( p, &call ) ) {
			return;
		}
		if ( call.rc != 0 ) {
			Plugin_Fail( p, "Init returned %i", call.rc );
			return;
		}
	}

	p->state = PS_RUNNING;
	Com_Printf( "Client plugin '%s' %s loaded%s\n", p->name, p->exp->version ? p->exp->version : "", p->guarded ? "" : " (unguarded)" );
}

static void Plugins_List_f( void )
{
	static const char *const stateNames[] = { "running", "stopped", "failed", "crashed" };
	int i;

	if ( !numPlugins ) {
		Com_Printf( "No client plugins. Set cl_plugins to a list of names to load them from %s/plugins at startup.\n", Cvar_VariableString( "fs_homepath" ) );
		return;
	}
	Com_Printf( "%-16s %-8s %s\n", "name", "state", "details" );
	for ( i = 0; i < numPlugins; i++ ) {
		const plugin_t *p = &plugins[i];

		Com_Printf( "%-16s %-8s %s%s\n", p->name, stateNames[p->state],
			p->state == PS_RUNNING && p->exp->version ? p->exp->version : p->note,
			p->state == PS_RUNNING && !p->guarded ? " (no crash protection)" : "" );
	}
}

void CL_Plugins_Init( void )
{
	char list[MAX_CVAR_VALUE_STRING];
	char *s, *token;

	Cmd_AddCommand( "clplugins", Plugins_List_f );

	cl_plugins = Cvar_Get( "cl_plugins", "", CVAR_INIT | CVAR_PROTECTED );
	Cvar_SetDescription( cl_plugins, "Client plugins to load at startup, separated by spaces. Each is a library in the plugins folder of the home path." );

	Com_Memset( &imports, 0, sizeof( imports ) );
	imports.size = sizeof( imports );
	imports.apiVersion = ODFC_API_VERSION;
	imports.Print = Imp_Print;
	imports.Milliseconds = Imp_Milliseconds;
	imports.HomePath = Imp_HomePath;
	imports.CvarRegister = Imp_CvarRegister;
	imports.CvarString = Imp_CvarString;
	imports.CvarInteger = Imp_CvarInteger;
	imports.CvarValue = Imp_CvarValue;
	imports.CheatsEnabled = Imp_CheatsEnabled;
	imports.GetSnapshot = Imp_GetSnapshot;
	imports.Trace = Imp_Trace;
	imports.GetPlayerState = Imp_GetPlayerState;
	imports.GetMoveState = Imp_GetMoveState;
	imports.PointContents = Imp_PointContents;
	imports.RegisterShader = Imp_RegisterShader;
	imports.AddPoly = Imp_AddPoly;
	imports.AddBeam = Imp_AddBeam;
	imports.SetColor = Imp_SetColor;
	imports.DrawStretchPic = Imp_DrawStretchPic;
	imports.GetWeaponState = Imp_GetWeaponState;
	imports.RegisterModel = Imp_RegisterModel;
	imports.RegisterShaderNoMip = Imp_RegisterShaderNoMip;
	imports.DrawModel = Imp_DrawModel;
	imports.CommandRegister = Imp_CommandRegister;
	imports.CommandArgc = Imp_CommandArgc;
	imports.CommandArgv = Imp_CommandArgv;

	Q_strncpyz( list, cl_plugins->string, sizeof( list ) );
	if ( !list[0] ) {
		return;
	}

	guardInstalled = PluginGuard_Init() != 0;

	for ( s = list; ( token = strtok( s, " ,;" ) ) != NULL; s = NULL ) {
		Plugin_Load( token );
	}
}

void CL_Plugins_Shutdown( void )
{
	int i;

	Cmd_RemoveCommand( "clplugins" );
	for ( i = 0; i < numPluginCommands; i++ ) {
		Cmd_RemoveCommand( pluginCommands[i].name );
	}
	numPluginCommands = 0;
	mapLoaded = qfalse;

	for ( i = 0; i < numPlugins; i++ ) {
		plugin_t *p = &plugins[i];

		if ( p->state == PS_RUNNING ) {
			if ( PLUGIN_HAS( p, Shutdown ) ) {
				call_t call;

				Com_Memset( &call, 0, sizeof( call ) );
				call.kind = PC_SHUTDOWN;
				if ( !Plugin_Run( p, &call ) ) {
					continue;
				}
			}
			p->state = PS_STOPPED;
		}
		// a crashed plugin stays mapped: nothing of it runs again, and unloading could run more of its code
		if ( p->lib && p->state != PS_FAULTED ) {
			Sys_UnloadLibrary( p->lib );
			p->lib = NULL;
		}
	}
	numPlugins = 0;

	if ( guardInstalled ) {
		PluginGuard_Shutdown();
		guardInstalled = qfalse;
	}
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

void CL_Plugins_MapLoad( const char *mapName )
{
	char name[MAX_QPATH];

	if ( !numPlugins ) {
		return;
	}
	Q_strncpyz( name, mapName ? mapName : "", sizeof( name ) );
	mapLoaded = qtrue;
	published = qfalse;
	SoundOwners_Reset();
	PLUGINS_RUN( PC_MAPLOAD, MapLoad, call_.mapName = name; );
}

void CL_Plugins_CGameShutdown( void )
{
	if ( !numPlugins || !mapLoaded ) {
		mapLoaded = qfalse;
		return;
	}
	PLUGINS_RUN( PC_CGAMESHUTDOWN, CGameShutdown, );
	mapLoaded = qfalse;
}

// called with the refdef of the game's 3D scene right before it is rendered
void CL_Plugins_BeforeScene( const refdef_t *fd )
{
	odfcView_t view;
	int i;

	if ( !numPlugins || !mapLoaded || !fd || ( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		return;
	}
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

	// Plugin_Run puts drawPhase back to DRAW_NONE after each call
	PLUGINS_RUN( PC_DRAW3D, Draw3D, call_.view = &view; drawPhase = DRAW_3D; );
}

static void Plugins_Draw2D( int phase )
{
	qboolean drew = qfalse;

	PLUGINS_RUN( PC_DRAW2D, Draw2D, call_.phase = phase; drawPhase = DRAW_2D; drew = qtrue; );
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

// Whether a plugin wants the sound dropped (see FilterSound in the API). Called by the cgame system calls that start sounds.
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
		call_t call;

		if ( p->state != PS_RUNNING || !PLUGIN_HAS( p, FilterSound ) ) {
			continue;
		}
		Com_Memset( &call, 0, sizeof( call ) );
		call.kind = PC_FILTERSOUND;
		call.sound[0] = entityNum;
		call.sound[1] = isLoop ? 1 : 0;
		call.sound[2] = owner;
		call.sound[3] = snap->ps.clientNum;
		if ( Plugin_Run( p, &call ) && call.soundResult ) {
			return qtrue;
		}
	}
	return qfalse;
}
