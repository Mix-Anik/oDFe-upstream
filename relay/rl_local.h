/*
===========================================================================
rl_local.h -- relay server: a local server (0 ping for the player) that forwards the player to a remote server

The relay is the dedicated server plus the code in this folder. It listens for the local client on the normal server
socket and, through the same socket, plays a client against the remote server (rl_remote.c). Everything in this folder
only exists in the RELAY build (-DRELAY); the engine sources call into it through a few #ifdef RELAY hooks.
===========================================================================
*/

#ifndef RL_LOCAL_H
#define RL_LOCAL_H

#include "../code/qcommon/q_shared.h"
#include "../code/qcommon/qcommon.h"

// the remote server is only usable by the relay while the players of that server can not touch each other
#define RL_REQUIRED_INTERFERENCE	3

typedef enum {
	RS_IDLE,			// no remote server
	RS_CHALLENGING,		// getchallenge sent
	RS_CONNECTING,		// connect sent
	RS_CONNECTED,		// netchan is up, waiting for the gamestate
	RS_ACTIVE			// gamestate received and accepted, snapshots are flowing
} rlRemoteState_t;

// ---- rl_remote.c: the client side of the relay (talks to the remote server) ----

void	RL_Remote_Init( void );
qboolean RL_Remote_Connect( const char *address, const char *password );
void	RL_Remote_Disconnect( const char *reason );
void	RL_Remote_Frame( int now );
qboolean RL_Remote_Packet( const netadr_t *from, msg_t *msg );
qboolean RL_Remote_IsRemote( const netadr_t *from );
void	RL_Remote_Status( void );
void	RL_Remote_AddCommand( const char *cmd );
void	RL_Remote_SubmitCmd( const usercmd_t *cmd );
const char *RL_Remote_CandidateMap( void );				// the server being tried when it is ready, else NULL
void	RL_Remote_Commit( void );						// the server being tried becomes the server of the local player
void	RL_Remote_AbortCandidate( const char *reason );
qboolean RL_Remote_Trying( void );						// a server is being tried
const char *RL_Remote_Summary( void );					// where the local player plays, one line
void	RL_Notify( const char *msg );					// to the console of the relay and the player

rlRemoteState_t	RL_Remote_State( void );
int		RL_Remote_ClientNum( void );					// the slot the remote server gave us, -1 if not active
int		RL_Remote_ServerId( void );
const char *RL_Remote_Configstring( int index );
const playerState_t *RL_Remote_PlayerState( void );		// newest snapshot of the remote server, NULL if none
int		RL_Remote_PlayerEntities( entityState_t *out, int max, int eType, int exclude );	// the other players, from the newest snapshot
int		RL_Remote_Rtt( void );
int		RL_Remote_Time( void );							// time of the remote server now, -1 if unknown
int		RL_Remote_TargetTime( void );					// time the local server has to show, -1 if unknown

// ---- rl_download.c: the map of the remote server, from dl_source / dl_source2 like the client's download command ----

void	RL_Download_Init( void );
qboolean RL_Download_Busy( void );
const char *RL_Download_Start( const char *map );		// NULL: under way, else why not
int		RL_Download_Frame( void );						// 1: done, -1: failed, 0: going on
const char *RL_Download_Error( void );
void	RL_Download_Abort( void );

// ---- rl_overlay.c: the other players of the remote server in the snapshots of the local player ----

void	RL_Overlay_Status( void );

// ---- rl_reconcile.c: the remote server decides where the player is ----

void	RL_Reconcile_Init( void );
void	RL_Reconcile_Reset( void );
void	RL_Reconcile_Snapshot( const playerState_t *ps );	// a state of the player from the remote server
void	RL_Reconcile_Frame( void );
void	RL_Reconcile_NoteKill( void );					// the player asked the local game to kill him: the replays have to do it too
void	RL_Reconcile_Status( void );

// ---- rl_mirror.c: the local server follows the remote one ----

void	RL_Mirror_Init( void );
void	RL_Mirror_RemoteLost( const char *address, const char *reason );
void	RL_Mirror_RemoteCommand( const char *s );
qboolean RL_Mirror_LocalUserinfo( char *info, int size );	// the local player's userinfo for a remote server, qfalse: none
void	RL_Relay_Command( qboolean fromClient );		// the "relay" command, Cmd_Argv has it
const char *RL_Mirror_Start( void );					// NULL: ok, else why not
void	RL_Mirror_Frame( void );
void	RL_Mirror_Clock( void );
void	RL_Mirror_Status( void );
qboolean	RL_Mirror_HasPlayer( void );			// somebody is connected to the local server
void	RL_Remote_Goodbye( void );				// tell the remote servers that the relay goes
void	RL_Mirror_Settings( qboolean all, qboolean toPlayer );	// remote settings against the local ones

#endif
