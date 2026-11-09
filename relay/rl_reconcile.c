/*
===========================================================================
rl_reconcile.c -- the remote server decides where the player is

The local server runs the player at once, the remote server runs the same usercmds a little later. When the remote server tells
(in a snapshot) where the player was at its time T and that is not where the local game had him at T, the local game is put
back to T (sv_state.c), the player is put where the remote server says, and the frames since are played again with the same
usercmds. The player sees a small correction (the cgame smooths it), and the local game, with its items and triggers, has gone
through the same steps as the remote one.

The two servers run on the same clock and frame grid (rl_mirror.c), so "the frame at time T" is the same frame on both.
What is taken over from the remote server is what moves the player: origin, velocity and the pmove state. The rest of the state
(health, ammo, timers, ...) is next.
===========================================================================
*/

#include "rl_local.h"
#include "../code/server/server.h"

#define RL_RING				128				// frames kept: about a second
#define RL_STATE_OWNER		0x52454c59		// "RELY": the owner of the game snapshots the relay makes

typedef struct {
	qboolean	valid;
	int			time;			// sv.time after the frame
	int			handle;			// game snapshot of that moment, 0: none
	playerState_t	ps;			// the local player then
	usercmd_t	cmd;			// the usercmd the frame ran
} rlFrame_t;

static rlFrame_t	ring[RL_RING];
static int			killTime[RL_RING];		// sv.time of the frames the local game ran "kill" before, by frame

static playerState_t	pending;	// the newest player state of the remote server, not looked at yet
static qboolean		havePending;

static struct {
	int		samples;			// remote states compared with the local one
	int		exact;				// they were the same
	int		corrected;			// the local game was put back and run again
	int		snapped;			// of those: all the way to the remote state (the difference was big)
	int		size[4];			// of those: by distance, under 0.5, 2, 8 units and more
	int		noData;				// the local frame was not kept
	int		failed;				// a correction was not possible
	int		replayed;			// frames played again
	int		captures;			// frames kept
	int64_t	captureUsec;		// what keeping them took
	double	errSum;				// the distances (units) of the states compared that are not far off, for the mean
	int		errN;
	int		otherCompared;		// states where the player was in the right place: the rest of the state was looked at
	int		statsDiffer[MAX_STATS];	// of those: how often each of ps.stats was not the same
	int		persistantDiffer, powerupsDiffer, ammoDiffer, weaponDiffer, weaponTimeDiffer, flagsDiffer;
	float	maxError;			// largest distance (units) of a correction
	float	lastError;
} stats;

static cvar_t	*relay_reconcile;
static cvar_t	*relay_epsilon;
static cvar_t	*relay_smooth;
static cvar_t	*relay_snap;
static cvar_t	*relay_snapVelocity;

void RL_Reconcile_Init( void )
{
	relay_reconcile = Cvar_Get( "relay_reconcile", "1", CVAR_ARCHIVE );
	Cvar_SetDescription( relay_reconcile, "The remote server corrects the position of the player in the local game." );
	relay_epsilon = Cvar_Get( "relay_epsilon", "0.05", CVAR_ARCHIVE );
	Cvar_SetDescription( relay_epsilon, "Units the position or velocity of the player may differ from the remote server before it is corrected." );
	relay_smooth = Cvar_Get( "relay_smooth", "0.5", CVAR_ARCHIVE );
	Cvar_SetDescription( relay_smooth, "A small difference to the remote server is closed by this fraction at each correction (1: at once)." );
	relay_snap = Cvar_Get( "relay_snap", "8", CVAR_ARCHIVE );
	Cvar_SetDescription( relay_snap, "Units of position difference from which the correction is not smoothed." );
	relay_snapVelocity = Cvar_Get( "relay_snapVelocity", "100", CVAR_ARCHIVE );
	Cvar_SetDescription( relay_snapVelocity, "Units per second of velocity difference from which the correction is not smoothed." );
}

static int RL_FrameMsec( void )
{
	return MAX( 1, 1000 / MAX( 1, sv_fps->integer ) );
}

static rlFrame_t *RL_Find( int time )
{
	rlFrame_t *f = &ring[( time / RL_FrameMsec() ) & ( RL_RING - 1 )];

	return f->valid && f->time == time ? f : NULL;
}

// keeps the state of the local player after the frame that sv.time says
static void RL_Capture( int slot )
{
	rlFrame_t *f = &ring[( sv.time / RL_FrameMsec() ) & ( RL_RING - 1 )];
	const client_t *cl = &svs.clients[slot];

	if ( f->valid && f->handle ) {
		SV_StateFree( f->handle );
	}
	Com_Memset( f, 0, sizeof( *f ) );

	if ( cl->state != CS_ACTIVE || !SV_StateAvailable() ) {
		return;
	}
	f->handle = SV_StateCreate( RL_STATE_OWNER );
	f->time = sv.time;
	f->ps = *SV_GameClientNum( slot );
	f->cmd = cl->lastUsercmd;
	f->valid = qtrue;
}

// The player asked for "kill" (the local game does it at once, the remote server a moment later): a replay from before that has to
// do it as well, or the player would live on in it.
void RL_Reconcile_NoteKill( void )
{
	const int t = sv.time + RL_FrameMsec();

	killTime[( t / RL_FrameMsec() ) & ( RL_RING - 1 )] = t;
}

// the local server has run a frame
void RL_FrameDone( void )
{
	const int slot = RL_Remote_ClientNum();

	if ( slot >= 0 && slot < sv.maxclients ) {
		const int64_t start = Sys_Microseconds();

		RL_Capture( slot );
		stats.captures++;
		stats.captureUsec += Sys_Microseconds() - start;
	}
}

// the game was restarted: nothing kept fits it any more
void RL_Reconcile_Reset( void )
{
	SV_StateFreeOwner( RL_STATE_OWNER );
	Com_Memset( ring, 0, sizeof( ring ) );
	Com_Memset( killTime, 0, sizeof( killTime ) );
	havePending = qfalse;
}

// a state of the player from the remote server
void RL_Reconcile_Snapshot( const playerState_t *ps )
{
	pending = *ps;
	havePending = qtrue;
}

static float RL_Distance( const vec3_t a, const vec3_t b )
{
	vec3_t d;

	VectorSubtract( a, b, d );
	return VectorLength( d );
}

// The race time and the timer flags both sides publish (ps.stats[7], [8] and [12]; plain ms on a dedicated server), for the
// status: the most either has shown, which after the finish is the finish time.
static struct {
	int		localMax, remoteMax;
	int		localFlags, remoteFlags;
} race;

static void RL_NoteRace( const playerState_t *local, const playerState_t *remote )
{
	const int l = ( local->stats[7] << 16 ) | ( local->stats[8] & 0xffff );
	const int r = ( remote->stats[7] << 16 ) | ( remote->stats[8] & 0xffff );

	race.localFlags = local->stats[12];
	race.remoteFlags = remote->stats[12];
	if ( ( race.remoteFlags & 2 ) || ( race.localFlags & 2 ) ) {
		race.localMax = MAX( race.localMax, l );
		race.remoteMax = MAX( race.remoteMax, r );
	}
}

// When the player is where the remote server has him: is the rest of the state (what the game works out from its own, private
// state) the same as well? This is only counted, to see whether the remote server needs to decide that too.
static void RL_CompareOther( const playerState_t *local, const playerState_t *remote )
{
	int i;

	static qboolean shown;

	stats.otherCompared++;
	for ( i = 0; i < MAX_STATS; i++ ) {
		if ( local->stats[i] != remote->stats[i] ) {
			stats.statsDiffer[i]++;
			if ( !shown ) {
				Com_Printf( "reconcile: first difference: stats[%i] local %i remote %i (time %i)\n", i, local->stats[i], remote->stats[i], local->commandTime );
			}
		}
	}
	for ( i = 0; i < MAX_PERSISTANT; i++ ) {
		if ( local->persistant[i] != remote->persistant[i] ) {
			stats.persistantDiffer++;
			if ( !shown ) {
				Com_Printf( "reconcile: first difference: persistant[%i] local %i remote %i\n", i, local->persistant[i], remote->persistant[i] );
			}
		}
	}
	shown = shown || stats.otherCompared > 20;
	for ( i = 0; i < MAX_POWERUPS; i++ ) {
		if ( local->powerups[i] != remote->powerups[i] ) {
			stats.powerupsDiffer++;
		}
	}
	for ( i = 0; i < MAX_WEAPONS; i++ ) {
		if ( local->ammo[i] != remote->ammo[i] ) {
			stats.ammoDiffer++;
		}
	}
	if ( local->weapon != remote->weapon ) {
		stats.weaponDiffer++;
	}
	if ( local->weaponTime != remote->weaponTime ) {
		stats.weaponTimeDiffer++;
	}
	if ( local->pm_flags != remote->pm_flags ) {
		stats.flagsDiffer++;
	}
}

// whether the remote player is somewhere else, or going somewhere else, than the local one
static qboolean RL_Differs( const playerState_t *local, const playerState_t *remote )
{
	const float eps = relay_epsilon->value;

	return RL_Distance( local->origin, remote->origin ) > eps || RL_Distance( local->velocity, remote->velocity ) > eps;
}

// what moves the player is what the remote server says; position and velocity go the fraction k of the way to it (1: all of
// it), the rest is the way of the next corrections. The state of the movement (on the ground, ducked, ...) is not something to
// go halfway to.
static void RL_TakeOver( playerState_t *ps, const playerState_t *remote, float k )
{
	int i;

	for ( i = 0; i < 3; i++ ) {
		ps->origin[i] += ( remote->origin[i] - ps->origin[i] ) * k;
		ps->velocity[i] += ( remote->velocity[i] - ps->velocity[i] ) * k;
	}
	VectorCopy( remote->grapplePoint, ps->grapplePoint );
	ps->pm_type = remote->pm_type;
	ps->pm_flags = remote->pm_flags;
	ps->pm_time = remote->pm_time;
	ps->groundEntityNum = remote->groundEntityNum;
	ps->viewheight = remote->viewheight;
	ps->bobCycle = remote->bobCycle;
	ps->jumppad_ent = remote->jumppad_ent;
	ps->jumppad_frame = remote->jumppad_frame;
}

// every frame, before the local server runs
void RL_Reconcile_Frame( void )
{
	const int slot = RL_Remote_ClientNum();
	const int fm = RL_FrameMsec();
	const rlFrame_t *e;
	client_t *cl;
	usercmd_t savedCmd;
	int savedResidual, savedTime, t;
	float err, errVel, k;
	qboolean snap;

	if ( !havePending ) {
		return;
	}
	havePending = qfalse;

	if ( !relay_reconcile->integer || slot < 0 || slot >= sv.maxclients || sv.state != SS_GAME ) {
		return;
	}
	cl = &svs.clients[slot];
	if ( cl->state != CS_ACTIVE ) {
		return;
	}

	e = RL_Find( pending.commandTime );
	if ( !e ) {
		stats.noData++;
		return;
	}
	stats.samples++;
	RL_NoteRace( &e->ps, &pending );
	err = RL_Distance( e->ps.origin, pending.origin );
	errVel = RL_Distance( e->ps.velocity, pending.velocity );
	if ( err <= relay_snap->value ) {		// the jump to the spawn of the remote server is not what this is about
		stats.errSum += err;
		stats.errN++;
	}
	if ( !RL_Differs( &e->ps, &pending ) ) {
		stats.exact++;
		RL_CompareOther( &e->ps, &pending );
		return;
	}

	// a small difference is closed in steps, a big one (spawn, teleport, a different path) at once
	snap = err > relay_snap->value || errVel > relay_snapVelocity->value;
	k = snap ? 1.0f : Com_Clamp( 0.05f, 1.0f, relay_smooth->value );

	// every frame since has to be there, and the state of the frame has to be one the game can go back to
	for ( t = e->time + fm; t <= sv.time; t += fm ) {
		if ( !RL_Find( t ) ) {
			stats.failed++;
			return;
		}
	}
	if ( !e->handle || !SV_StateCanApply( e->handle ) ) {
		stats.failed++;
		return;
	}

	savedTime = sv.time;
	savedResidual = sv.timeResidual;
	savedCmd = cl->lastUsercmd;

	if ( !SV_StateRewind( e->handle ) ) {
		stats.failed++;
		return;
	}
	RL_TakeOver( SV_GameClientNum( slot ), &pending, k );
	RL_Capture( slot );		// the corrected frame replaces the one that was kept

	// the frames since, with the usercmds they had; the game takes the usercmd from the server as it does when one arrives
	for ( t = pending.commandTime + fm; t <= savedTime; t += fm ) {
		const rlFrame_t *f = RL_Find( t );
		usercmd_t cmd;

		// the entry is replaced by RL_Capture below: take what is needed first
		cmd = f ? f->cmd : savedCmd;
		if ( killTime[( t / fm ) & ( RL_RING - 1 )] == t ) {
			Cmd_TokenizeString( "kill" );
			VM_Call( gvm, 1, GAME_CLIENT_COMMAND, slot );
		}
		cl->lastUsercmd = cmd;
		VM_Call( gvm, 1, GAME_CLIENT_THINK, slot );
		sv.time += fm;
		VM_Call( gvm, 1, GAME_RUN_FRAME, sv.time );
		RL_Capture( slot );
		stats.replayed++;
	}

	sv.timeResidual = savedResidual;
	cl->lastUsercmd = savedCmd;

	stats.corrected++;
	if ( snap ) {
		stats.snapped++;
	}
	stats.size[err < 0.5f ? 0 : err < 2.0f ? 1 : err < 8.0f ? 2 : 3]++;
	stats.lastError = err;
	if ( err > stats.maxError ) {
		stats.maxError = err;
	}
	if ( sv.time != savedTime ) {
		Com_Printf( S_COLOR_YELLOW "Relay: the replay ended at %i, not at %i\n", sv.time, savedTime );
	}
}

void RL_Reconcile_Status( void )
{
	Com_Printf( "reconcile: %i states compared, %i same, %i corrected (%i frames replayed, last %.2f, max %.2f units), %i not kept, %i failed\n",
		stats.samples, stats.exact, stats.corrected, stats.replayed, stats.lastError, stats.maxError, stats.noData, stats.failed );
	if ( race.localMax || race.remoteMax ) {
		Com_Printf( "reconcile: race time local %i ms, remote %i ms (timer flags now %x / %x)\n", race.localMax, race.remoteMax,
			race.localFlags, race.remoteFlags );
	}
	if ( stats.otherCompared ) {
		char list[256] = "";
		int i;

		for ( i = 0; i < MAX_STATS; i++ ) {
			if ( stats.statsDiffer[i] ) {
				Q_strcat( list, sizeof( list ), va( " [%i]:%i", i, stats.statsDiffer[i] ) );
			}
		}
		Com_Printf( "reconcile: of %i states with the player in the right place, these differed: stats%s, persistant %i, powerups %i, "
			"ammo %i, weapon %i, weaponTime %i, pm_flags %i\n", stats.otherCompared, list[0] ? list : " none",
			stats.persistantDiffer, stats.powerupsDiffer, stats.ammoDiffer, stats.weaponDiffer, stats.weaponTimeDiffer, stats.flagsDiffer );
	}
	if ( stats.errN ) {
		Com_Printf( "reconcile: mean distance to the remote state before correcting: %.3f units\n", stats.errSum / stats.errN );
	}
	Com_Printf( "reconcile: corrections by distance: %i under 0.5, %i under 2, %i under 8, %i more; %i of all went the whole way\n",
		stats.size[0], stats.size[1], stats.size[2], stats.size[3], stats.snapped );
	if ( stats.captures ) {
		Com_Printf( "reconcile: keeping a frame takes %.0f usec (%i kept, %i KB of snapshots)\n",
			(double)stats.captureUsec / stats.captures, stats.captures, SV_StateTotalBytes() / 1024 );
	}
	RL_Overlay_Status();
}
