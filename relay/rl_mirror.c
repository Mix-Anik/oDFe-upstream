/*
===========================================================================
rl_mirror.c -- the local server follows the remote server

The local player plays on the local server, so that he sees no ping. This file makes that server look like the remote one:
the same map, the same physics settings, the local player in the slot the remote server gave the relay, a clock that runs
ahead of the remote one by the time a usercmd needs to get there, and the usercmds of the local player going on to the
remote server.
===========================================================================
*/

#include "rl_local.h"
#include "../code/server/server.h"

#define RL_SPAWN_FRAMES_MSEC	300		// SV_SpawnServer runs the game 3 times 100 msec before the first client frame

#define RL_CMD_TIME_RANGE		1000	// msec a usercmd may be off the time of the local server
#define RL_CLOCK_RESTART		3000	// msec off the remote server: the local map starts again
#define RL_CLOCK_JUMP			100		// msec off: jump instead of slew
#define RL_CLOCK_DEADBAND		3		// msec off: leave it alone
#define RL_CLOCK_SLEW			0.25f	// msec per frame the clock is moved while it is off: the player sees the time run 3% fast or slow at most

// configstrings: the remote server decides these (the match state, who is who); the rest the local game makes itself
#define RL_CS_FIRST_REMOTE		3		// CS_MESSAGE
#define RL_CS_LAST_REMOTE		26		// up to CS_BOTINFO
#define RL_CS_PLAYERS			544		// CS_PLAYERS: CS_MODELS 32 + MAX_MODELS 256 + MAX_SOUNDS 256

static char	pendingMap[MAX_QPATH];	// the map the local server has to start as soon as the remote clock is known
static qboolean	importing;			// the relay itself is setting configstrings
static int		spawnPendingSince;
static qboolean	spawnPending;		// the map command of the relay is in the command buffer: the clock waits for the new map
static qboolean	importPending;		// the configstrings of the remote server have to be copied once the map runs

static enum {
	LOCAL_NONE,			// nothing started yet
	LOCAL_LOBBY,		// the lobby, or a map somebody started by hand
	LOCAL_REMOTE		// the map of a remote server
} localMode;

static cvar_t	*relay_lobby;
static cvar_t	*relay_runfile;		// tests: the file of "dfgolden export", a run the local server plays for the player

void RL_Mirror_Init( void )
{
	relay_runfile = Cvar_Get( "relay_runfile", "", CVAR_TEMP );		// tests: see RL_FrameStart
	relay_lobby = Cvar_Get( "relay_lobby", "q3dm1", CVAR_ARCHIVE );
	Cvar_SetDescription( relay_lobby, "Map the local server runs while no remote server is connected." );
}

// The cvars of the remote server that change what the game does with the player (physics, rules). The game module
// that runs here reads them from its own cvars, the remote values are put there before the map loads.
static qboolean RL_Mirror_Wanted( const char *key )
{
	static const char *prefixes[] = { "df_", "g_", "pmove_", NULL };
	static const char *names[] = { "dmflags", "fraglimit", "timelimit", "capturelimit", "sv_fps", NULL };
	static const char *never[] = { "g_needpass", "g_password", "g_log", "g_logSync", "g_banIPs", "g_filterBan", NULL };
	int i;

	for ( i = 0; never[i]; i++ ) {
		if ( !Q_stricmp( key, never[i] ) ) {
			return qfalse;
		}
	}
	for ( i = 0; names[i]; i++ ) {
		if ( !Q_stricmp( key, names[i] ) ) {
			return qtrue;
		}
	}
	for ( i = 0; prefixes[i]; i++ ) {
		if ( !Q_stricmpn( key, prefixes[i], (int)strlen( prefixes[i] ) ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void RL_Mirror_Info( const char *info )
{
	const char *s = info;
	char key[BIG_INFO_KEY], value[BIG_INFO_VALUE];

	while ( *s ) {
		s = Info_NextPair( s, key, value );
		if ( !key[0] ) {
			break;
		}
		if ( !RL_Mirror_Wanted( key ) ) {
			continue;
		}
		if ( Cvar_Flags( key ) & ( CVAR_ROM | CVAR_INIT ) ) {
			continue;
		}
		Cvar_Set( key, value );
	}
}

// Called with the gamestate of the remote server. Starts the local server on the same map; NULL if that works.
const char *RL_Mirror_Start( void )
{
	const char *serverinfo = RL_Remote_Configstring( CS_SERVERINFO );
	const char *map = Info_ValueForKey( serverinfo, "mapname" );
	int maxclients = atoi( Info_ValueForKey( serverinfo, "sv_maxclients" ) );

	if ( !map[0] ) {
		return "the server does not announce its map";
	}

	RL_Download_Abort();
	RL_Mirror_Info( serverinfo );
	RL_Mirror_Info( RL_Remote_Configstring( CS_SYSTEMINFO ) );

	// the local server must have the slot of the remote one
	if ( maxclients <= RL_Remote_ClientNum() ) {
		maxclients = RL_Remote_ClientNum() + 1;
	}
	// (a latched value is only looked at when a server was running: the first map of a relay would start with the old one)
	if ( com_sv_running->integer ) {
		Cvar_SetLatched( "sv_maxclients", va( "%i", MAX( maxclients, 2 ) ) );
	} else {
		Cvar_Set( "sv_maxclients", va( "%i", MAX( maxclients, 2 ) ) );
	}

	// nothing here is pure, and nobody may cheat on a map the remote server started
	Cvar_Set( "sv_pure", "0" );
	Cvar_Set( "sv_cheats", "0" );

	// the map starts when the clock of the remote server is known (RL_Mirror_Frame)
	Q_strncpyz( pendingMap, map, sizeof( pendingMap ) );
	return NULL;
}

// Is the map installed? If not it is downloaded, like the player does with the download command.
// 1: it is here, 0: it is coming, -1: it can not be had (why says why).
static int RL_Mirror_EnsureMap( const char *map, const char **why )
{
	static char downloading[MAX_QPATH];
	char path[MAX_QPATH];

	Com_sprintf( path, sizeof( path ), "maps/%s.bsp", map );
	if ( FS_FOpenFileRead( path, NULL, qfalse ) != -1 ) {
		downloading[0] = '\0';
		return 1;
	}

	if ( !RL_Download_Busy() || Q_stricmp( downloading, map ) ) {
		*why = RL_Download_Start( map );
		if ( *why ) {
			downloading[0] = '\0';
			return -1;
		}
		Q_strncpyz( downloading, map, sizeof( downloading ) );
		return 0;
	}

	switch ( RL_Download_Frame() ) {
	case 1:		downloading[0] = '\0'; return 1;
	case -1:	downloading[0] = '\0'; *why = RL_Download_Error(); return -1;
	default:	return 0;
	}
}

// the local server shows a small map of its own while the player is not on a remote server
static void RL_Mirror_Lobby( void )
{
	char path[MAX_QPATH];

	Com_sprintf( path, sizeof( path ), "maps/%s.bsp", relay_lobby->string );
	if ( FS_FOpenFileRead( path, NULL, qfalse ) == -1 ) {
		Com_Printf( S_COLOR_YELLOW "Relay: the lobby map %s (relay_lobby) is not installed\n", relay_lobby->string );
		localMode = LOCAL_LOBBY;	// do not try again
		return;
	}

	Com_Printf( "Relay: starting the lobby on %s\n", relay_lobby->string );
	Cvar_Set( "sv_pure", "0" );		// nobody has to have anything the relay has
	Cvar_Set( "sv_cheats", "0" );
	Cbuf_InsertText( va( "map %s\n", relay_lobby->string ) );
	localMode = LOCAL_LOBBY;
}

// the configstrings the remote server owns are copied to the local server once its game runs
static void RL_Mirror_Import( void )
{
	int i;

	importing = qtrue;
	for ( i = RL_CS_FIRST_REMOTE; i <= RL_CS_LAST_REMOTE; i++ ) {
		SV_SetConfigstring( i, RL_Remote_Configstring( i ) );
	}
	for ( i = RL_CS_PLAYERS; i < RL_CS_PLAYERS + MAX_CLIENTS; i++ ) {
		SV_SetConfigstring( i, RL_Remote_Configstring( i ) );
	}
	importing = qfalse;
}

// The frames of the local server have to be the frames of the remote one: same times, so that what one says about the frame at
// time T is about the same frame of the other. The grid of the local server is moved a few msec if it is not on the remote one.
static void RL_Mirror_AlignGrid( void )
{
	const playerState_t *r = RL_Remote_PlayerState();
	const int fm = MAX( 1, 1000 / MAX( 1, sv_fps->integer ) );
	int d;

	if ( !r ) {
		return;
	}
	d = ( ( r->commandTime - sv.time ) % fm + fm ) % fm;
	if ( d ) {
		Com_Printf( "Relay: the frames of the local server move %i msec to be the frames of the remote one\n", d );
		sv.time += d;
		svs.time += d;
	}
	RL_Reconcile_Reset();
}

// every frame
void RL_Mirror_Frame( void )
{
	char cand[MAX_QPATH];
	const char *why = NULL;
	const char *candMap;
	int r;

	if ( importPending && sv.state == SS_GAME ) {
		importPending = qfalse;
		if ( RL_Remote_ClientNum() >= 0 ) {
			RL_Mirror_Import();
			RL_Mirror_AlignGrid();
		}
	}

	// the server of the local player is gone, or he never had one: the lobby
	if ( RL_Remote_State() == RS_IDLE && !RL_Remote_Trying() && !pendingMap[0] ) {
		if ( localMode == LOCAL_NONE && sv.state == SS_GAME ) {
			localMode = LOCAL_LOBBY;	// somebody started a map: that is the lobby
		} else if ( localMode != LOCAL_LOBBY ) {
			RL_Mirror_Lobby();
		}
	}

	// the local server follows a map change of the server of the local player
	if ( pendingMap[0] ) {
		if ( RL_Remote_ClientNum() < 0 ) {
			pendingMap[0] = '\0';	// the remote server is gone
			RL_Download_Abort();
			return;
		}
		r = RL_Mirror_EnsureMap( pendingMap, &why );
		if ( r < 0 ) {
			char map[MAX_QPATH];

			Q_strncpyz( map, pendingMap, sizeof( map ) );
			pendingMap[0] = '\0';
			RL_Remote_Disconnect( va( "map %s is not installed and could not be downloaded: %s", map, why ) );
			return;
		}
		if ( r == 0 || RL_Remote_TargetTime() < 0 ) {
			return;
		}

		Com_Printf( "Relay: starting the local server on %s\n", pendingMap );
		Cbuf_InsertText( va( "map %s\n", pendingMap ) );
		spawnPending = qtrue;
		spawnPendingSince = Sys_Milliseconds();
		pendingMap[0] = '\0';
		localMode = LOCAL_REMOTE;
		importPending = qtrue;
		return;
	}

	// a server the player asked for: when it works and its map is here, the player moves to it
	candMap = RL_Remote_CandidateMap();
	if ( !candMap ) {
		return;
	}
	Q_strncpyz( cand, candMap, sizeof( cand ) );
	if ( !cand[0] ) {
		RL_Remote_AbortCandidate( "the server does not announce its map" );
		return;
	}
	r = RL_Mirror_EnsureMap( cand, &why );
	if ( r < 0 ) {
		RL_Remote_AbortCandidate( va( "map %s is not installed and could not be downloaded: %s", cand, why ) );
	} else if ( r > 0 ) {
		RL_Remote_Commit();
		why = RL_Mirror_Start();
		if ( why ) {
			RL_Remote_Disconnect( why );
		}
	}
}

// what the player is told: in the console of the relay and, when he is there, in his own
void RL_Notify( const char *msg )
{
	char clean[MAX_STRING_CHARS - 16];
	int i;

	Com_Printf( "%s\n", msg );

	if ( sv.state != SS_GAME || !svs.clients ) {
		return;
	}
	Q_strncpyz( clean, msg, sizeof( clean ) );
	for ( i = 0; clean[i]; i++ ) {
		if ( clean[i] == '"' ) {
			clean[i] = '\'';
		} else if ( clean[i] == '\n' || clean[i] == '\r' ) {
			clean[i] = ' ';
		}
	}
	for ( i = 0; i < sv.maxclients; i++ ) {
		if ( svs.clients[i].state >= CS_ACTIVE && svs.clients[i].netchan.remoteAddress.type != NA_BOT ) {
			SV_SendServerCommand( &svs.clients[i], "print \"%s\n\"", clean );
		}
	}
}

// the server the local player plays on is gone (kicked, down, closed): back to the lobby, the frame does that
void RL_Mirror_RemoteLost( const char *address, const char *reason )
{
	pendingMap[0] = '\0';
	importPending = qfalse;
	RL_Download_Abort();
	RL_Notify( va( "Relay: left %s: %s", address, reason ) );
	localMode = LOCAL_REMOTE;	// the lobby starts when RL_Mirror_Frame sees there is no remote server
}

// a server command of the remote server for the local player
void RL_Mirror_RemoteCommand( const char *s )
{
	int slot = RL_Remote_ClientNum();

	Cmd_TokenizeString( s );

	// configstrings: the server keeps them for the players who join later, the players who are there are told by it
	if ( !Q_stricmp( Cmd_Argv( 0 ), "cs" ) ) {
		int index = atoi( Cmd_Argv( 1 ) );

		if ( sv.state == SS_GAME && index >= 2 && index < MAX_CONFIGSTRINGS ) {
			importing = qtrue;
			SV_SetConfigstring( index, Cmd_Argv( 2 ) );
			importing = qfalse;
		}
		return;
	}

	if ( slot >= 0 && sv.state == SS_GAME && svs.clients[slot].state >= CS_ACTIVE ) {
		SV_SendServerCommand( &svs.clients[slot], "%s", s );
	}
}

// The userinfo of the local player without what belongs to one connection only: it goes to the remote server.
static void RL_FilterUserinfo( const char *in, char *out, int size )
{
	const char *s = in;
	char key[BIG_INFO_KEY], value[BIG_INFO_VALUE];

	out[0] = '\0';
	while ( *s ) {
		s = Info_NextPair( s, key, value );
		if ( !key[0] ) {
			break;
		}
		if ( !Q_stricmp( key, "ip" ) || !Q_stricmp( key, "qport" ) || !Q_stricmp( key, "challenge" ) ||
			!Q_stricmp( key, "protocol" ) || !Q_stricmp( key, "password" ) || !Q_stricmp( key, "client" ) ) {
			continue;
		}
		Info_SetValueForKey_s( out, size, key, value );
	}
}

qboolean RL_Mirror_HasPlayer( void )
{
	int i;

	if ( sv.state == SS_DEAD || !svs.clients ) {
		return qfalse;
	}
	for ( i = 0; i < sv.maxclients; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED && svs.clients[i].netchan.remoteAddress.type != NA_BOT ) {
			return qtrue;
		}
	}
	return qfalse;
}

// The userinfo of the local player, for a connection to a remote server. False: nobody is there.
qboolean RL_Mirror_LocalUserinfo( char *info, int size )
{
	int i;

	if ( sv.state == SS_DEAD || !svs.clients ) {
		return qfalse;
	}
	for ( i = 0; i < sv.maxclients; i++ ) {
		const client_t *cl = &svs.clients[i];

		if ( cl->state >= CS_CONNECTED && cl->netchan.remoteAddress.type != NA_BOT ) {
			RL_FilterUserinfo( cl->userinfo, info, size );
			return qtrue;
		}
	}
	return qfalse;
}


// Keeps the time of the local server on the target time (rl_remote.c). Small differences are slewed 1 msec per frame, big
// ones (the map had to load, the connection to the remote server changed) are jumped.
void RL_Mirror_Clock( void )
{
	static float slew;
	static int lastRestart;
	int target = RL_Remote_TargetTime();
	int err;

	if ( spawnPending && Sys_Milliseconds() - spawnPendingSince > 10000 ) {
		spawnPending = qfalse;		// the map command did not get anywhere
	}
	if ( sv.state != SS_GAME || target < 0 || spawnPending ) {
		return;
	}

	// the time of the local server, with the part of the next frame already counted
	err = sv.time + sv.timeResidual - target;

	if ( ( err > RL_CLOCK_RESTART || err < -RL_CLOCK_RESTART ) && !spawnPending && Sys_Milliseconds() - lastRestart > 5000 && RL_Remote_ClientNum() >= 0 ) {
		// far off the remote server (its time started anew without a map change we know of): waiting for it would stop the local
		// server, running the missing frames would take as long as the time is, the map starts again with the right time instead
		lastRestart = Sys_Milliseconds();
		Com_Printf( "Relay: the time of the remote server is %i msec off the local one, starting the map again\n", err );
		Q_strncpyz( pendingMap, Info_ValueForKey( RL_Remote_Configstring( CS_SERVERINFO ), "mapname" ), sizeof( pendingMap ) );
		return;
	}
	if ( err > RL_CLOCK_JUMP || err < -RL_CLOCK_JUMP ) {
		sv.timeResidual -= err;		// ahead: SV_Frame waits, behind: it runs the missing frames at once
		slew = 0;
	} else {
		if ( err > RL_CLOCK_DEADBAND ) {
			slew -= RL_CLOCK_SLEW;
		} else if ( err < -RL_CLOCK_DEADBAND ) {
			slew += RL_CLOCK_SLEW;
		} else {
			slew = 0;
		}
		if ( slew <= -1.0f ) {
			sv.timeResidual--;
			slew += 1.0f;
		} else if ( slew >= 1.0f ) {
			sv.timeResidual++;
			slew -= 1.0f;
		}
	}
}

// A line of "relay settings": always in the log of the relay, to the player only when it matters (the commands to a client are
// limited, so not more than a screenful).
static int settingsSent;

static void RL_SettingsLine( qboolean toPlayer, const char *text )
{
	if ( toPlayer && settingsSent < 24 ) {
		settingsSent++;
		RL_Notify( text );
	} else {
		Com_Printf( "%s\n", text );
	}
}

// What the remote server tells about its settings (serverinfo and systeminfo) against what the local game runs with: the ones
// the relay takes over must be the same, a difference means the local game plays another game than the remote one. "all" lists
// the settings the relay does not take over as well (in the log of the relay only).
void RL_Mirror_Settings( qboolean all, qboolean toPlayer )
{
	static const char *hidden[] = { "g_speed", "g_gravity", "g_knockback", "g_synchronousClients", "sv_fps", NULL };
	const char *infos[2];
	int i, n = 0, diff = 0, same = 0;

	settingsSent = 0;
	if ( RL_Remote_ClientNum() < 0 || sv.state != SS_GAME ) {
		RL_Notify( "settings: no remote server" );
		return;
	}
	infos[0] = RL_Remote_Configstring( CS_SERVERINFO );
	infos[1] = RL_Remote_Configstring( CS_SYSTEMINFO );

	for ( i = 0; i < 2; i++ ) {
		const char *s = infos[i];
		char key[BIG_INFO_KEY], value[BIG_INFO_VALUE];

		while ( *s ) {
			s = Info_NextPair( s, key, value );
			if ( !key[0] ) {
				break;
			}
			n++;
			if ( RL_Mirror_Wanted( key ) ) {
				const char *local = Cvar_VariableString( key );

				if ( Q_stricmp( local, value ) ) {
					RL_SettingsLine( toPlayer, va( "settings: DIFFERENT %s remote \"%s\" local \"%s\"%s", key, value, local,
						Cvar_Flags( key ) & ( CVAR_ROM | CVAR_INIT ) ? " (cannot be set)" : "" ) );
					diff++;
				} else {
					same++;
					if ( all ) {
						Com_Printf( "settings: same %s \"%s\"\n", key, value );
					}
				}
			} else if ( all ) {
				Com_Printf( "settings: not taken over %s remote \"%s\" local \"%s\"\n", key, value, Cvar_VariableString( key ) );
			}
		}
	}
	RL_SettingsLine( toPlayer, va( "settings: the remote server tells %i, of those taken over %i same, %i different", n, same, diff ) );

	// what the remote server does not tell: only the local values can be shown, to compare with the server by hand
	for ( i = 0; hidden[i]; i++ ) {
		if ( !*Info_ValueForKey( infos[0], hidden[i] ) && !*Info_ValueForKey( infos[1], hidden[i] ) ) {
			RL_SettingsLine( toPlayer, va( "settings: not told by the remote server: %s is \"%s\" here", hidden[i],
				Cvar_VariableString( hidden[i] ) ) );
		}
	}
}

void RL_Mirror_Status( void )
{
	int target = RL_Remote_TargetTime();

	if ( sv.state != SS_GAME ) {
		Com_Printf( "local server: not running\n" );
		return;
	}
	Com_Printf( "local server: map %s, time %i, rtt %i, clock error %i ms (local time - target)\n",
		sv_mapname->string, sv.time, RL_Remote_Rtt(), target < 0 ? 0 : sv.time + sv.timeResidual - target );
	if ( RL_Remote_ClientNum() >= 0 && svs.clients[RL_Remote_ClientNum()].state >= CS_CONNECTED ) {
		const playerState_t *ps = SV_GameClientNum( RL_Remote_ClientNum() );

		Com_Printf( "local player: origin %.1f %.1f %.1f velocity %.1f %.1f %.1f commandTime %i\n",
			ps->origin[0], ps->origin[1], ps->origin[2], ps->velocity[0], ps->velocity[1], ps->velocity[2], ps->commandTime );
	}
}


// ---------------------------------------------------------------------------
// hooks of the server (#ifdef RELAY in code/server)
// ---------------------------------------------------------------------------

// the slot the local player has to get, -1: any
int RL_LocalSlot( void )
{
	return RL_Remote_ClientNum();
}

// Called while a map is spawned, before the game starts: the local player has to be in the slot the remote server gave the
// relay. He may have another one (the lobby, a former server), then the two slots swap places.
void RL_SpawnSlots( void )
{
	static client_t tmp;
	const int slot = RL_Remote_ClientNum();
	int i, local = -1;

	RL_Reconcile_Reset();		// the game starts anew: what was kept of the old one is gone

	if ( slot < 0 || slot >= sv_maxclients->integer || !svs.clients ) {
		return;
	}
	for ( i = 0; i < sv_maxclients->integer; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED && svs.clients[i].netchan.remoteAddress.type != NA_BOT ) {
			local = i;
			break;
		}
	}
	if ( local < 0 || local == slot ) {
		return;
	}

	Com_Printf( "Relay: the player moves from slot %i to slot %i\n", local, slot );
	Com_Memcpy( &tmp, &svs.clients[local], sizeof( tmp ) );
	Com_Memcpy( &svs.clients[local], &svs.clients[slot], sizeof( tmp ) );
	Com_Memcpy( &svs.clients[slot], &tmp, sizeof( tmp ) );
}

// A client command of a player of the local server (the unknown ones, the game would get them). True: the relay took it.
// Cmd_Argv has the tokens of s.
qboolean RL_ClientCommand( int clientNum, const char *s )
{
	char clean[MAX_STRING_CHARS];
	int i;

	if ( !Q_stricmp( Cmd_Argv( 0 ), "relay" ) ) {
		RL_Relay_Command( qtrue );
		return qtrue;
	}

	// the player is on a remote server: it decides, the local game never sees what he says or asks
	if ( clientNum != RL_Remote_ClientNum() ) {
		return qfalse;
	}
	Q_strncpyz( clean, s, sizeof( clean ) );
	for ( i = 0; clean[i]; i++ ) {
		if ( clean[i] == '\n' || clean[i] == '\r' ) {
			clean[i] = ' ';
		}
	}
	RL_Remote_AddCommand( clean );

	// the one command the local game runs as well: the player starts anew there at once, the remote server does it a moment
	// later (the position then comes from it)
	if ( !Q_stricmp( Cmd_Argv( 0 ), "kill" ) ) {
		RL_Reconcile_NoteKill();
		return qfalse;
	}
	return qtrue;
}

// the player changed his userinfo (name, rate, ...): the remote server hears about it
void RL_UserinfoChanged( int clientNum, const char *userinfo )
{
	char info[MAX_INFO_STRING];

	if ( clientNum != RL_Remote_ClientNum() ) {
		return;
	}
	RL_FilterUserinfo( userinfo, info, sizeof( info ) );
	RL_Remote_AddCommand( va( "userinfo \"%s\"", info ) );
}

// What the local game says to the players is dropped while the player is on a remote server: its server says what is true.
qboolean RL_DropGameCommand( int clientNum, const char *text )
{
	return RL_Remote_ClientNum() >= 0;
}

// The remote server decides these configstrings; the local game must not change them. Only the relay sets them.
qboolean RL_ConfigstringBlocked( int index )
{
	if ( importing || RL_Remote_ClientNum() < 0 ) {
		return qfalse;
	}
	return ( index >= RL_CS_FIRST_REMOTE && index <= RL_CS_LAST_REMOTE ) ||
		( index >= RL_CS_PLAYERS && index < RL_CS_PLAYERS + MAX_CLIENTS );
}

// The time the local server starts with (SV_SpawnServer): the time it has to show once its first frame has run. 0: no idea.
int RL_SpawnTime( void )
{
	int t = RL_Remote_TargetTime();

	spawnPending = qfalse;

	// a map that has just started on the remote server has a small time: the local one has to start small as well, or it would
	// be ahead by the difference and wait for it
	return t < 0 ? 0 : MAX( t - RL_SPAWN_FRAMES_MSEC, 8 );
}

// ---- tests: a recorded run played by the local server itself, exactly one usercmd for each frame ----
// (a player on the network gives one usercmd per frame only about; a precise run does not survive that)

static struct {
	qboolean	loaded;
	int			count, *steps;
	int			delta[3];				// the delta_angles the recorded view angles are relative to
	int			next;
	int			startTime;				// sv.time when the run begins: a few seconds after the player is there
} run;

static void RL_RunLoad( void )
{
	FILE *f = fopen( relay_runfile->string, "r" );
	char line[256];
	int n = 0;

	run.loaded = qtrue;
	if ( !f ) {
		Com_Printf( S_COLOR_YELLOW "Relay: can not read %s\n", relay_runfile->string );
		return;
	}
	while ( fgets( line, sizeof( line ), f ) ) {
		if ( !strncmp( line, "delta ", 6 ) ) {
			sscanf( line + 6, "%d %d %d", &run.delta[0], &run.delta[1], &run.delta[2] );
		} else if ( !strncmp( line, "steps ", 6 ) ) {
			run.count = atoi( line + 6 );
			run.steps = Z_Malloc( sizeof( int ) * 8 * MAX( 1, run.count ) );
			break;
		}
	}
	while ( run.steps && n < run.count && fgets( line, sizeof( line ), f ) ) {
		if ( sscanf( line, "%d %d %d %d %d %d %d %d", &run.steps[n * 8], &run.steps[n * 8 + 1], &run.steps[n * 8 + 2],
			&run.steps[n * 8 + 3], &run.steps[n * 8 + 4], &run.steps[n * 8 + 5], &run.steps[n * 8 + 6], &run.steps[n * 8 + 7] ) == 8 ) {
			n++;
		}
	}
	fclose( f );
	run.count = n;
	Com_Printf( "Relay: the local player will run %i steps of %s\n", n, relay_runfile->string );
}

static void RL_ClientThinkRemote( int clientNum, const usercmd_t *cmd );

// every frame, before the game runs it
void RL_FrameStart( void )
{
	const int slot = RL_Remote_ClientNum();
	client_t *cl;
	usercmd_t cmd;
	const int *s;

	if ( !relay_runfile || !relay_runfile->string[0] || slot < 0 || slot >= sv.maxclients ) {
		return;
	}
	cl = &svs.clients[slot];
	if ( cl->state != CS_ACTIVE ) {
		return;
	}
	if ( !run.loaded ) {
		RL_RunLoad();
	}
	if ( !run.startTime ) {
		run.startTime = sv.time + 3000;		// stand at the spawn a while first
	}
	if ( sv.time < run.startTime || run.next >= run.count ) {
		return;
	}

	s = &run.steps[run.next++ * 8];
	Com_Memset( &cmd, 0, sizeof( cmd ) );
	cmd.serverTime = sv.time;
	{
		// the recorded angles were relative to the delta_angles of the recording
		const playerState_t *ps = SV_GameClientNum( slot );
		int i;

		for ( i = 0; i < 3; i++ ) {
			cmd.angles[i] = ( s[i] + run.delta[i] - ps->delta_angles[i] ) & 0xffff;
		}
	}
	cmd.forwardmove = s[3];
	cmd.rightmove = s[4];
	cmd.upmove = s[5];
	cmd.buttons = s[6];
	cmd.weapon = s[7];

	cl->lastUsercmd = cmd;
	VM_Call( gvm, 1, GAME_CLIENT_THINK, slot );
	RL_ClientThinkRemote( slot, &cmd );
}

// A usercmd of the local player is about to be run by the game: the remote server gets it too. False: the game must not run it.
// The game of the standard DeFRaG moves the player in steps until his time is the time of the usercmd, so a usercmd from another
// time (the clock of the local server jumps when a map starts, or a usercmd of the map before comes late) would keep it busy for ever.
qboolean RL_ClientThink( int clientNum, const usercmd_t *cmd )
{
	if ( cmd->serverTime - sv.time > RL_CMD_TIME_RANGE || sv.time - cmd->serverTime > RL_CMD_TIME_RANGE ) {
		static int lastShown;

		if ( Sys_Milliseconds() - lastShown > 2000 ) {
			lastShown = Sys_Milliseconds();
			Com_Printf( "Relay: usercmd of the player dropped, its time %i is not the time of the server %i\n", cmd->serverTime, sv.time );
		}
		return qfalse;
	}
	// while a run is played, what the player's own process sends is not the run
	if ( relay_runfile && relay_runfile->string[0] && run.next < run.count && sv.time >= run.startTime ) {
		return qtrue;
	}
	RL_ClientThinkRemote( clientNum, cmd );
	return qtrue;
}

static void RL_ClientThinkRemote( int clientNum, const usercmd_t *cmd )
{
	const playerState_t *remote = RL_Remote_PlayerState();
	const playerState_t *local;
	usercmd_t out;
	int i;

	if ( clientNum != RL_Remote_ClientNum() || !remote ) {
		return;
	}

	// A usercmd in another time than the remote server has (the map changed there and the local game has not caught up yet) is
	// not sent: the remote server remembers the time of the newest usercmd it got and throws away every one that is older, so
	// one from the old time would silence the player until the new time has got there.
	{
		const int target = RL_Remote_TargetTime();

		if ( target < 0 || cmd->serverTime - target > RL_CMD_TIME_RANGE || target - cmd->serverTime > RL_CMD_TIME_RANGE ) {
			return;
		}
	}

	local = SV_GameClientNum( clientNum );
	out = *cmd;

	// The view angles of a usercmd are relative to the delta_angles of the player state of whoever runs it, and those
	// differ (spawn, teleport): what we see here has to be what the remote server sees.
	for ( i = 0; i < 3; i++ ) {
		out.angles[i] = ( cmd->angles[i] + local->delta_angles[i] - remote->delta_angles[i] ) & 0xffff;
	}

	RL_Remote_SubmitCmd( &out );
}
