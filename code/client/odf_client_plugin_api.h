/*
===========================================================================
oDFe client plugin API

This header is self-contained (it needs nothing but the C library), so a plugin can be
built without the engine sources: copy this file into the plugin project.

A client plugin is a shared library, <homepath>/plugins/<name>.dll (Windows) or
<name>.so (everything else), loaded when the client starts. Which ones are loaded is
decided by the startup-only cvar cl_plugins, a space separated list of names
("trajectory"). Plugins are only ever loaded from that folder, never from downloaded
paks. The client entry point is ODF_ClientPluginEntry, so a server plugin cannot be
loaded here (and the other way round).

HOW IT WORKS

 * The plugin exports one function, ODF_ClientPluginEntry. The engine calls it once with a
   table of engine functions (odfcPluginImport_t) and gets back a table of plugin functions
   (odfcPluginExport_t).
 * The plugin draws by adding polygons and beams to the 3D scene in Draw3D, and 2D pictures
   in Draw2D. It looks at the game through GetSnapshot and Trace.
 * Everything runs on the main thread, inside the client frame. Plugin functions are called
   from it, and the engine functions below may only be called from it. Functions of the
   plugin must return quickly.
 * Shader and model handles are only valid between MapLoad and CGameShutdown (a map change
   or vid_restart invalidates them). Register shaders in MapLoad.

WHAT A PLUGIN MAY SEE: CHEATS

 The API is read-only: a plugin cannot press keys, change the view, or send commands to
 the server. What it can see depends on whether the server allows cheats. It is decided by
 the engine and cannot be switched on by the plugin:

   cheats enabled   = playing a demo, or connected to a server with sv_cheats 1 ("devmap")
   cheats disabled  = everything else (all multiplayer servers)

 Without cheats, GetSnapshot only lists what the player's own game shows about themselves:
 their own missiles. Everything else a snapshot contains (other players, items, movers)
 is left out. With cheats it is all there. Calls that would be an unfair advantage on a
 normal server are marked CHEAT below; without cheats they do nothing and report failure.
 A plugin can ask CheatsEnabled() to switch off settings that cannot work.

COMPATIBILITY

 The import table starts with its size, and only ever grows at the end. A plugin built
 against API version N works with an engine of version >= N, and checks imp->size before
 using functions that were added later. The engine refuses a plugin whose apiVersion is
 higher than its own. The export table starts with its size too, so that it can grow.

CRASH CONTAINMENT (best effort)

 Every call into a plugin is guarded. If it faults in the plugin's own module, the engine
 logs it, stops that plugin for good and carries on. It does not cover faults in engine
 code or in libraries outside the plugin, corrupted memory, other threads or endless loops.
===========================================================================
*/

#ifndef ODF_CLIENT_PLUGIN_API_H
#define ODF_CLIENT_PLUGIN_API_H

#ifdef _WIN32
#define ODFC_EXPORT	__declspec( dllexport )
#else
#define ODFC_EXPORT	__attribute__( ( visibility( "default" ) ) )
#endif

#define ODFC_API_VERSION	4

// ---------------------------------------------------------------------------
// game constants (the values of Quake III Arena and DeFRaG)
// ---------------------------------------------------------------------------

// odfcPlayerState_t.pmType
#define ODFC_PM_NORMAL				0
#define ODFC_PM_NOCLIP				1
#define ODFC_PM_SPECTATOR			2
#define ODFC_PM_DEAD				3

// odfcMoveState_t.pmFlags
#define ODFC_PMF_DUCKED				1
#define ODFC_PMF_JUMP_HELD			2
#define ODFC_PMF_TIME_LAND			32		// pmTime is the time before a rejump
#define ODFC_PMF_TIME_KNOCKBACK		64		// pmTime is an air-accelerate only time
#define ODFC_PMF_TIME_WATERJUMP		256
#define ODFC_PMF_RESPAWNED			512
#define ODFC_PMF_GRAPPLE_PULL		2048
#define ODFC_PMF_FOLLOW				4096	// watching another player
#define ODFC_PMF_PROMODE			32768	// CPM physics

// odfcMoveState_t.userInput, the keys of the watched player (DeFRaG sends them in the player state)
#define ODFC_INPUT_FORWARD			1
#define ODFC_INPUT_BACKWARD			2
#define ODFC_INPUT_LEFT				8
#define ODFC_INPUT_RIGHT			16
#define ODFC_INPUT_JUMP				32
#define ODFC_INPUT_CROUCH			64
#define ODFC_INPUT_ATTACK			256
#define ODFC_INPUT_WALK				512

// odfcMoveState_t.viewFlags
#define ODFC_VIEW_SCOREBOARD		1		// the scoreboard is up
#define ODFC_VIEW_NOT_FIRST_PERSON	2		// third person, chase or replay camera: the view is not the player's

// contents and surfaces, for Trace and PointContents
#define ODFC_CONTENTS_PLAYERCLIP	0x10000
#define ODFC_CONTENTS_WATER			32
#define ODFC_CONTENTS_LAVA			8
#define ODFC_CONTENTS_SLIME			16
#define ODFC_MASK_PLAYERSOLID		( ODFC_CONTENTS_SOLID | ODFC_CONTENTS_PLAYERCLIP | ODFC_CONTENTS_BODY )
#define ODFC_MASK_WATER				( ODFC_CONTENTS_WATER | ODFC_CONTENTS_LAVA | ODFC_CONTENTS_SLIME )
#define ODFC_SURF_SLICK				2

// odfcEntity_t.eType
#define ODFC_ET_PLAYER				1
#define ODFC_ET_MISSILE				3

// odfcEntity_t.weapon, odfcPlayer_t.weapon
#define ODFC_WP_GRENADE_LAUNCHER	4
#define ODFC_WP_ROCKET_LAUNCHER		5

// odfcTrajectory_t.trType
#define ODFC_TR_STATIONARY			0
#define ODFC_TR_INTERPOLATE			1
#define ODFC_TR_LINEAR				2
#define ODFC_TR_LINEAR_STOP			3
#define ODFC_TR_SINE				4
#define ODFC_TR_GRAVITY				5

// odfcEntity_t.eFlags
#define ODFC_EF_BOUNCE_HALF			0x00000020

// contents, for Trace
#define ODFC_CONTENTS_SOLID			1
#define ODFC_CONTENTS_BODY			0x2000000
#define ODFC_CONTENTS_CORPSE		0x4000000
#define ODFC_MASK_SHOT				( ODFC_CONTENTS_SOLID | ODFC_CONTENTS_BODY | ODFC_CONTENTS_CORPSE )

// ---------------------------------------------------------------------------
// data
// ---------------------------------------------------------------------------

// Same layout as polyVert_t of the renderer.
typedef struct {
	float			xyz[3];
	float			st[2];
	unsigned char	rgba[4];
} odfcPolyVert_t;

typedef struct {
	int		trType;			// ODFC_TR_*
	int		trTime;
	int		trDuration;		// if not 0, trTime + trDuration is the stop time
	float	trBase[3];
	float	trDelta[3];
} odfcTrajectory_t;

typedef struct {
	int					number;		// entity number
	int					eType;		// ODFC_ET_*
	int					eFlags;
	int					weapon;		// missiles: the weapon that fired it
	int					clientNum;	// missiles: the player that fired it
	odfcTrajectory_t	pos;
} odfcEntity_t;

// The player the snapshot is for (the one being watched when spectating). This is the state
// the server sent, not the client's prediction, so it is a little behind the player's input.
typedef struct {
	int		clientNum;
	int		weapon;
	float	origin[3];
	float	velocity[3];
	float	viewangles[3];
} odfcPlayer_t;

// The state of the player the game is showing, as the game module predicts it for the current frame: the position and
// aim you see now, not what the server sent a moment ago. Used to work out what a shot fired now would do.
typedef struct {
	int		clientNum;
	int		pmType;			// ODFC_PM_NORMAL when the player is alive and walking; dead, spectating and so on differ
	int		weapon;
	int		viewheight;		// eye height above origin
	float	origin[3];
	float	velocity[3];
	float	viewangles[3];	// pitch yaw roll in degrees
} odfcPlayerState_t;

// What the player's movement depends on besides the state above (version 3). Same rules as odfcPlayerState_t: the prediction of
// the game module for the frame that is being drawn.
typedef struct {
	int		pmFlags;			// ODFC_PMF_*
	int		pmTime;
	int		groundEntityNum;	// 1023 (ENTITYNUM_NONE) in the air
	int		speed;				// g_speed
	int		gravity;
	int		flight;				// the flight powerup is on
	int		userInput;			// ODFC_INPUT_*, the keys of the watched player
	int		viewFlags;			// ODFC_VIEW_*
	int		demoPlayback;		// a demo is playing: there are no commands of your own
	int		cmdValid;			// the fields below are your latest command (not while watching a demo or another player)
	int		forwardmove, rightmove, upmove;		// -127 to 127
	int		buttons;
} odfcMoveState_t;

// The weapons and ammunition of the watched player (version 4).
#define ODFC_MAX_WEAPONS	16
typedef struct {
	int		weapons;			// bit n is set when the player has weapon n (ODFC_WP_*; 2 is the machinegun, 9 the BFG)
	int		ammo[ODFC_MAX_WEAPONS];		// by weapon number
} odfcWeaponState_t;

#define ODFC_MAX_SNAPSHOT_ENTITIES	256

typedef struct {
	int				serverTime;		// server time of this snapshot in ms
	int				cheats;			// what CheatsEnabled() says
	odfcPlayer_t	player;
	int				numEntities;
	odfcEntity_t	entities[ODFC_MAX_SNAPSHOT_ENTITIES];
} odfcSnapshot_t;

typedef struct {
	int		allSolid;		// nonzero if the trace started and ended inside a solid
	int		startSolid;
	float	fraction;		// 1.0 = nothing was hit
	float	endpos[3];
	float	normal[3];		// of the surface that was hit
	int		surfaceFlags;
	int		contents;
} odfcTrace_t;

// The view of the 3D scene that is being drawn (Draw3D).
typedef struct {
	int		time;			// the time entities are drawn at: use it to evaluate trajectories
	int		x, y, width, height;
	float	fovX, fovY;
	float	origin[3];
	float	axis[3][3];		// forward, left, up
	int		screenWidth, screenHeight;	// of the whole screen in pixels (version 3; an older plugin does not see them)
} odfcView_t;

// cvar flags for CvarRegister
enum {
	ODFC_CVAR_ARCHIVE = 1,		// saved to the config file
	ODFC_CVAR_ROM = 2			// shows a value (the default given) that cannot be changed (version 4)
};

// Draw2D phases
enum {
	ODFC_DRAW2D_SCENE = 0,		// right after the 3D scene: the pictures end up below the game's own HUD
	ODFC_DRAW2D_FRAME = 1		// after the game drew its HUD: on top of everything of the game
};

// ---------------------------------------------------------------------------
// what the engine offers the plugin
// ---------------------------------------------------------------------------

typedef struct {
	int		size;			// sizeof( odfcPluginImport_t ) of the engine
	int		apiVersion;		// ODFC_API_VERSION of the engine

	// -- general --

	// Writes a line to the console, prefixed with the plugin name.
	void	( *Print )( const char *text );

	// Milliseconds since the engine started (wall clock, not game time).
	int		( *Milliseconds )( void );

	// Path of the engine's home directory (where config and the plugins folder live),
	// without a trailing slash.
	void	( *HomePath )( char *out, int outSize );

	// -- cvars --

	// Creates the cvar if it does not exist. An existing value (config file, command
	// line) is kept.
	void	( *CvarRegister )( const char *name, const char *defaultValue, int flags );
	void	( *CvarString )( const char *name, char *out, int outSize );
	int		( *CvarInteger )( const char *name );
	float	( *CvarValue )( const char *name );

	// -- game --

	// Whether cheats are allowed: playing a demo, or connected to a server with sv_cheats 1.
	int		( *CheatsEnabled )( void );

	// Fills *out with the snapshot the game is currently showing (the newest one that is not
	// ahead of the view time). Returns 0 when there is none (not in a game). See "WHAT A
	// PLUGIN MAY SEE" above for what is left out without cheats.
	int		( *GetSnapshot )( odfcSnapshot_t *out );

	// Traces a line (or a box, if mins and maxs are not NULL) through the static map. Doors,
	// platforms and players are not part of it. contentMask is a set of ODFC_CONTENTS_*
	// flags, ODFC_MASK_SHOT is what missiles collide with. Positions are float[3].
	void	( *Trace )( odfcTrace_t *out, const float *start, const float *mins, const float *maxs, const float *end, int contentMask );

	// -- drawing --

	// Returns a shader handle (0 is the default shader, which is also what a missing shader
	// gives). Valid until CGameShutdown. Only works from MapLoad on.
	int		( *RegisterShader )( const char *name );

	// Adds a polygon (3 or more vertices, convex) to the 3D scene. Only in Draw3D.
	void	( *AddPoly )( int shader, int numVerts, const odfcPolyVert_t *verts );

	// Adds a line that looks like a rail gun beam core between two points, coloured with
	// rgba (4 bytes), to the 3D scene. Only in Draw3D. Use a shader like "railCore".
	void	( *AddBeam )( int shader, const float *from, const float *to, const unsigned char *rgba );

	// 2D drawing in the 640x480 virtual screen, only in Draw2D. SetColor takes float[4] or
	// NULL for white. The engine resets the colour after Draw2D returns.
	void	( *SetColor )( const float *rgba );
	void	( *DrawStretchPic )( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader );

	// -- added in later versions go below this line; check size first --

	// version 2:

	// Fills *out with the predicted state of the player being watched, for the frame that is being drawn. Returns 0 when
	// the game module does not publish it (only the oDFs game module does), or outside of a game. A plugin that needs it
	// has to cope with that, for example by drawing nothing.
	int		( *GetPlayerState )( odfcPlayerState_t *out );

	// version 3:

	// Fills *out with what the movement of the watched player depends on (see odfcMoveState_t). Returns 0 under the same
	// conditions as GetPlayerState.
	int		( *GetMoveState )( odfcMoveState_t *out );

	// The contents of the static map at a point (ODFC_CONTENTS_*): 0 in the air, ODFC_MASK_WATER bits in water.
	int		( *PointContents )( const float *point );

	// version 4:

	// Fills *out with the weapons and ammunition of the watched player. Returns 0 under the same conditions as GetPlayerState.
	int		( *GetWeaponState )( odfcWeaponState_t *out );

	// Returns a model handle (0 when there is no such model). Same rules as RegisterShader.
	int		( *RegisterModel )( const char *name );

	// Draws a model into the rectangle of the 640x480 screen, lit and turned like the models of the game's own HUD (field of
	// view 30, the model at origin, in front of the viewer, turned by angles). Only in Draw2D. time is the animation time.
	void	( *DrawModel )( float x, float y, float w, float h, int model, const float *origin, const float *angles, int time );

	// Adds a console command (lower case letters, digits and _, up to 31 long) that calls the plugin's Command. Returns 0
	// when the name is invalid or already used by the engine or another plugin. Commands are removed when the client shuts down.
	int		( *CommandRegister )( const char *name );

	// The arguments of the command being run (in Command): the count, and argument n (0 is the name) copied to out.
	int		( *CommandArgc )( void );
	void	( *CommandArgv )( int n, char *out, int outSize );

	// Like RegisterShader for pictures of the HUD: not reduced by r_picmip, no mipmaps.
	int		( *RegisterShaderNoMip )( const char *name );
} odfcPluginImport_t;

// ---------------------------------------------------------------------------
// what the plugin offers the engine
// ---------------------------------------------------------------------------

typedef struct {
	int			size;			// sizeof( odfcPluginExport_t ) the plugin was built with
	int			apiVersion;		// ODFC_API_VERSION the plugin was built against
	const char	*name;			// short name, as in cl_plugins ("trajectory")
	const char	*version;		// free text, shown in the log

	// Called once after loading. Return 0 on success; anything else unloads the plugin
	// (it is not called again, Shutdown is not called either).
	int		( *Init )( void );

	// Called once when the client shuts down.
	void	( *Shutdown )( void );

	// Called when a map is loaded by the game (and again after a vid_restart): register
	// shaders and reset all state that belongs to the map.
	void	( *MapLoad )( const char *mapName );

	// Called when the game module is shut down (disconnect, map change, vid_restart).
	// Shader handles are no longer valid.
	void	( *CGameShutdown )( void );

	// Called once per frame before the 3D scene is rendered. Add polygons and beams here.
	void	( *Draw3D )( const odfcView_t *view );

	// Called twice per frame, with an ODFC_DRAW2D_* phase.
	void	( *Draw2D )( int phase );

	// -- added in later versions go below this line; the engine checks size before using them --

	// version 4:

	// A command registered with CommandRegister was entered in the console; the arguments are available through CommandArgc / CommandArgv.
	void	( *Command )( const char *name );

	// Called for every sound the game module starts on an entity (not at a position of its own) or loops on one. Return non-zero
	// to drop the sound. entityNum is the entity (1023 is the world), isLoop whether it is a looping sound, entityClientNum the
	// clientNum field of that entity in the snapshots seen so far (-1 if it has not been in one) and ownClientNum the watched
	// player. Dropping sounds can only take information away from the player, so it is not limited by cheats.
	int		( *FilterSound )( int entityNum, int isLoop, int entityClientNum, int ownClientNum );
} odfcPluginExport_t;

// The one function a plugin must export. Return NULL to refuse to load.
typedef const odfcPluginExport_t *( *odfcPluginEntry_t )( const odfcPluginImport_t *imp );
// ODFC_EXPORT const odfcPluginExport_t *ODF_ClientPluginEntry( const odfcPluginImport_t *imp );

#endif // ODF_CLIENT_PLUGIN_API_H
