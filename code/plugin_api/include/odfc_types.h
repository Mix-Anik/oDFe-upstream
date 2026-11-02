/*
===========================================================================
oDFe client plugin API: the data a client plugin and the engine exchange.

Plain data only (32 bit integers and floats, fixed size arrays), laid out the same for the
engine and for a plugin built as a QVM. The functions are in odfc.h.
===========================================================================
*/

#ifndef ODFC_TYPES_H
#define ODFC_TYPES_H

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

#endif // ODFC_TYPES_H
