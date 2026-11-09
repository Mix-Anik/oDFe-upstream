/*
===========================================================================
rl_main.c -- relay server: entry points called from the engine, console commands

relay connect <address> [password]		connect the relay to a remote server
relay disconnect						drop the remote server
relay status							what the relay is doing
relay settings [all]					the settings of the remote server against the local ones
===========================================================================
*/

#include "rl_local.h"

// The "relay" command: typed at the console of the relay or, by the player, in his own console (the engine sends commands it
// does not know to the server).
void RL_Relay_Command( qboolean fromClient )
{
	const char *sub = Cmd_Argv( 1 );

	if ( !Q_stricmp( sub, "connect" ) && Cmd_Argc() >= 3 ) {
		RL_Remote_Connect( Cmd_Argv( 2 ), Cmd_Argv( 3 ) );
	} else if ( !Q_stricmp( sub, "disconnect" ) ) {
		RL_Remote_Disconnect( "Disconnect requested" );
	} else if ( !Q_stricmp( sub, "status" ) ) {
		if ( fromClient ) {
			RL_Notify( RL_Remote_Summary() );
		} else {
			RL_Remote_Status();
			RL_Mirror_Status();
			RL_Reconcile_Status();
		}
	} else if ( !Q_stricmp( sub, "settings" ) ) {
		RL_Mirror_Settings( !Q_stricmp( Cmd_Argv( 2 ), "all" ), fromClient );
	} else {
		RL_Notify( "usage: relay connect <address> [password] | relay disconnect | relay status | relay settings [all]" );
	}
}

static void RL_Relay_f( void )
{
	RL_Relay_Command( qfalse );
}

static cvar_t	*relay_status;	// seconds between status lines in the log, 0: none
static cvar_t	*relay_idle;	// seconds without a player at the local server after which the remote server is left, 0: never

// (can be called any number of times: the servers that were told are not told again)
void RL_Shutdown( void )
{
	RL_Remote_Goodbye();
}

static void RL_AtExit( void )
{
	RL_Shutdown();
}

// after the server is initialised
void RL_Init( void )
{
	RL_Remote_Init();
	RL_Mirror_Init();
	RL_Reconcile_Init();
	RL_Download_Init();
	relay_status = Cvar_Get( "relay_status", "0", CVAR_TEMP );
	relay_idle = Cvar_Get( "relay_idle", "60", CVAR_ARCHIVE );
	Cvar_SetDescription( relay_idle, "Seconds without a player at the relay after which the remote server is left (0: never)." );
	atexit( RL_AtExit );		// the window is closed: Sys_Quit exits without any shutdown
	Cmd_AddCommand( "relay", RL_Relay_f );

	// net_port is the port the socket really got: another program may have had the one that was asked for
	Com_Printf( S_COLOR_GREEN "Relay: the game connects to 127.0.0.1:%i  (in its console: connect 127.0.0.1:%i)\n", Cvar_VariableIntegerValue( "net_port" ), Cvar_VariableIntegerValue( "net_port" ) );
}

// every frame, after the server frame
void RL_Frame( int msec )
{
	static int lastStatus, lastPlayer;
	int now = Sys_Milliseconds();

	(void)msec;
	RL_Remote_Frame( now );
	RL_Mirror_Frame();
	RL_Reconcile_Frame();
	RL_Mirror_Clock();

	// nobody plays any more: the remote server should not keep a player that is not there
	if ( RL_Remote_ClientNum() < 0 || RL_Mirror_HasPlayer() ) {
		lastPlayer = now;
	} else if ( relay_idle->integer > 0 && now - lastPlayer > relay_idle->integer * 1000 ) {
		lastPlayer = now;
		Com_Printf( "Relay: nobody plays here any more, leaving the remote server\n" );
		RL_Remote_Disconnect( "The player left" );
	}

	if ( relay_status->integer > 0 && now - lastStatus >= relay_status->integer * 1000 ) {
		lastStatus = now;
		RL_Remote_Status();
		RL_Mirror_Status();
		RL_Reconcile_Status();
	}
}

// Every packet the server gets passes here first. True: the relay took it.
// Packets of the remote server belong to the relay; the local server only talks to the local machine.
qboolean RL_PacketEvent( const netadr_t *from, msg_t *msg )
{
	if ( RL_Remote_IsRemote( from ) ) {
		return RL_Remote_Packet( from, msg );
	}
	if ( from->type == NA_LOOPBACK || ( from->type == NA_IP && from->ipv._4[0] == 127 ) ) {
		return qfalse;
	}
	return qtrue;
}
