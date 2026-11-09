/*
===========================================================================
rl_remote.c -- the relay as a client of the remote server

A reduced copy of the client side of the engine (cl_main.c connect handshake, cl_net_chan.c, cl_parse.c, CL_WritePacket):
no cgame, no renderer, nothing is shown. It keeps the decoded state of the remote server (configstrings, baselines,
snapshots) and sends the usercmds that the local player produces. The protocol on the wire is the unchanged Quake 3
protocol, the remote server sees an ordinary client.

Not here yet: pure servers (cp checksums), pak downloads, forwarding of the local player's commands.
===========================================================================
*/

#include "rl_local.h"

#define RL_RECONNECT_TIMEOUT	3000	// between getchallenge / connect retransmits
#define RL_RETRANSMIT_TIMEOUT	1000	// between packets while waiting for the gamestate
#define RL_TIMEOUT				20000	// no packet from the remote server
#define RL_LOCAL_IDLE			250		// no usercmd from the local player for so long: send our own
#define RL_CMD_BACKUP			128
#define RL_CMD_MASK				( RL_CMD_BACKUP - 1 )
#define RL_MAX_PARSE_ENTITIES	( PACKET_BACKUP * MAX_SNAPSHOT_ENTITIES )

#define RL_RTT_WINDOW			8
#define RL_OFFSET_SETTLE		16		// snapshots that have to agree on the offset before the clock is used
#define RL_OFFSET_JUMP			500		// msec: a change of the offset that is a jump of the remote clock, not jitter
#define RL_OFFSET_WINDOW		64		// snapshots: about 1.5 s

typedef struct {
	qboolean	valid;
	int			messageNum;
	int			deltaNum;
	int			snapFlags;
	int			serverTime;
	int			realtime;			// when we got it
	int			numEntities;
	int			parseEntitiesNum;
	int			areabytes;
	byte		areamask[MAX_MAP_AREA_BYTES];
	playerState_t	ps;
} rlSnap_t;

typedef struct {
	int			cmdNumber;
} rlOutPacket_t;

typedef struct {
	rlRemoteState_t	state;
	qboolean	ready;				// the gamestate was accepted: the server may be used by the relay
	netadr_t	adr;
	char		addrString[64];
	char		password[64];

	int			realtime;
	int			connectTime;
	int			connectPacketCount;
	int			lastPacketTime;		// last packet from the remote server
	int			lastPacketSentTime;
	int			lastCmdTime;

	int			challenge;			// our challenge
	qboolean	compat;				// protocol 68 stream (xor coded)
	int			qport;
	netchan_t	netchan;

	int			reliableSequence;
	int			reliableAcknowledge;
	char		reliableCommands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];
	int			serverCommandSequence;
	char		serverCommands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];
	int			serverMessageSequence;

	int			serverId;
	int			checksumFeed;
	int			clientNum;
	char		*configstrings[MAX_CONFIGSTRINGS];
	entityState_t	baselines[MAX_GENTITIES];

	rlSnap_t	snapshots[PACKET_BACKUP];
	rlSnap_t	snap;				// the newest valid one
	entityState_t	*parseEntities;	// RL_MAX_PARSE_ENTITIES, the circular buffer the snapshots delta from
	int			parseEntitiesNum;

	int			pingSeq;			// round trip time: getinfo probes
	int			pingSent[8];
	int			lastPingTime;
	int			rtt;				// the smallest of the last RL_RTT_WINDOW samples: what the network takes, without its hiccups
	int			rttSamples;
	int			rttWin[RL_RTT_WINDOW];

	int			offWin[RL_OFFSET_WINDOW];	// remote server time - our time, at each snapshot
	int			offCount;
	int			offLow;				// snapshots in a row that were far behind the offset
	int			offset;				// the largest of them: the snapshot that was on its way the shortest time

	qboolean	cmdPending;			// usercmds not sent yet
	int			lastLocalCmdTime;	// when the local player last gave us a usercmd
	int			localCmds;			// how many of those
	int			activeTime;			// when the gamestate was accepted
	int			testCmd;			// tests: how many of relay_testcmds are sent

	int			cmdNumber;			// the number of the newest usercmd
	usercmd_t	cmds[RL_CMD_BACKUP];
	rlOutPacket_t	outPackets[PACKET_BACKUP];
} rlLeg_t;

// Two connections: the active one is the server the local player plays on. A new server is connected as the candidate first
// and only replaces the active one when it is known to work (gate, map), so a wrong address costs the player nothing.
// Everything below works on the leg that cur points to; the public functions pick it.
static rlLeg_t	legs[2];
static rlLeg_t	*cur = &legs[0];
static int		activeLeg = -1;
static int		candLeg = -1;
#define rl		( *cur )

static rlLeg_t *RL_ActiveLeg( void )
{
	return activeLeg >= 0 ? &legs[activeLeg] : NULL;
}

static rlLeg_t *RL_CandLeg( void )
{
	return candLeg >= 0 ? &legs[candLeg] : NULL;
}

static cvar_t	*relay_name;
static cvar_t	*relay_rate;
static cvar_t	*relay_snaps;
static cvar_t	*relay_userinfo;	// extra \key\value pairs for the remote userinfo
static cvar_t	*relay_packetdup;
static cvar_t	*relay_timeout;
static cvar_t	*relay_shownet;
static cvar_t	*relay_testplay;	// tests: the file (dfgolden export) of a run this process plays when nobody else does
static cvar_t	*relay_testturn;	// tests: the player turns by this much (65536 is a full turn) each usercmd and jumps now and then
static cvar_t	*relay_testcmds;	// tests: client commands (separated by ;) this process sends, 3 seconds apart, once it plays
static cvar_t	*relay_testmove;	// tests: forwardmove of the usercmds this process makes when nobody plays
static cvar_t	*relay_lead;		// ms added to the time the local server runs ahead of the remote one


// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

// the state of the server the local player plays on
rlRemoteState_t RL_Remote_State( void )
{
	rlLeg_t *a = RL_ActiveLeg();

	return a ? a->state : RS_IDLE;
}

qboolean RL_Remote_IsRemote( const netadr_t *from )
{
	int i;

	for ( i = 0; i < 2; i++ ) {
		if ( legs[i].state != RS_IDLE && NET_CompareAdr( from, &legs[i].adr ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void RL_ClearConfigstrings( void )
{
	int i;

	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		if ( rl.configstrings[i] ) {
			Z_Free( rl.configstrings[i] );
			rl.configstrings[i] = NULL;
		}
	}
}

static void RL_SetConfigstring( int index, const char *s )
{
	if ( rl.configstrings[index] ) {
		Z_Free( rl.configstrings[index] );
	}
	rl.configstrings[index] = s[0] ? CopyString( s ) : NULL;
}

static const char *RL_Configstring( int index )
{
	return rl.configstrings[index] ? rl.configstrings[index] : "";
}

// forget everything about the remote server, the netchan included
static void RL_ClearState( void )
{
	RL_ClearConfigstrings();
	if ( rl.parseEntities ) {
		Z_Free( rl.parseEntities );
	}
	Com_Memset( &rl, 0, sizeof( rl ) );
}


// ---------------------------------------------------------------------------
// netchan, the xor coding of the legacy protocol (cl_net_chan.c)
// ---------------------------------------------------------------------------

static void RL_Netchan_Encode( msg_t *msg )
{
	int serverId, messageAcknowledge, reliableAcknowledge;
	int i, index, srdc, sbit;
	byte key, *string;
	qboolean soob;

	if ( msg->cursize <= CL_ENCODE_START ) {
		return;
	}

	srdc = msg->readcount;
	sbit = msg->bit;
	soob = msg->oob;

	msg->bit = 0;
	msg->readcount = 0;
	msg->oob = qfalse;

	serverId = MSG_ReadLong( msg );
	messageAcknowledge = MSG_ReadLong( msg );
	reliableAcknowledge = MSG_ReadLong( msg );

	msg->oob = soob;
	msg->bit = sbit;
	msg->readcount = srdc;

	string = (byte *)rl.serverCommands[ reliableAcknowledge & ( MAX_RELIABLE_COMMANDS - 1 ) ];
	index = 0;
	key = rl.challenge ^ serverId ^ messageAcknowledge;
	for ( i = CL_ENCODE_START; i < msg->cursize; i++ ) {
		if ( !string[index] ) {
			index = 0;
		}
		if ( string[index] > 127 || string[index] == '%' ) {
			key ^= '.' << ( i & 1 );
		} else {
			key ^= string[index] << ( i & 1 );
		}
		index++;
		*( msg->data + i ) = ( *( msg->data + i ) ) ^ key;
	}
}

static void RL_Netchan_Decode( msg_t *msg )
{
	long reliableAcknowledge, i, index;
	byte key, *string;
	int srdc, sbit;
	qboolean soob;

	srdc = msg->readcount;
	sbit = msg->bit;
	soob = msg->oob;

	msg->oob = qfalse;

	reliableAcknowledge = MSG_ReadLong( msg );

	msg->oob = soob;
	msg->bit = sbit;
	msg->readcount = srdc;

	string = (byte *)rl.reliableCommands[ reliableAcknowledge & ( MAX_RELIABLE_COMMANDS - 1 ) ];
	index = 0;
	key = rl.challenge ^ LittleLong( *(unsigned *)msg->data );
	for ( i = msg->readcount + CL_DECODE_START; i < msg->cursize; i++ ) {
		if ( !string[index] ) {
			index = 0;
		}
		if ( string[index] > 127 || string[index] == '%' ) {
			key ^= '.' << ( i & 1 );
		} else {
			key ^= string[index] << ( i & 1 );
		}
		index++;
		*( msg->data + i ) = *( msg->data + i ) ^ key;
	}
}

static void RL_Netchan_Transmit( msg_t *msg )
{
	if ( rl.compat ) {
		RL_Netchan_Encode( msg );
	}

	Netchan_Transmit( &rl.netchan, msg->cursize, msg->data );

	while ( rl.netchan.unsentFragments ) {
		Netchan_TransmitNextFragment( &rl.netchan );
	}
}


// ---------------------------------------------------------------------------
// what we send
// ---------------------------------------------------------------------------

static void RL_LegEnd( const char *reason, qboolean tellServer );
static void RL_LegDisconnect( const char *reason );

// a client command for the server of the leg
static void RL_AddCommand( const char *cmd )
{
	int index;

	if ( rl.state < RS_CONNECTED ) {
		return;
	}

	// if we would be losing an old command that hasn't been acknowledged, we must drop the connection
	if ( rl.reliableSequence - rl.reliableAcknowledge >= MAX_RELIABLE_COMMANDS ) {
		RL_LegEnd( "too many unacknowledged commands", qfalse );
		return;
	}

	rl.reliableSequence++;
	index = rl.reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( rl.reliableCommands[index], cmd, sizeof( rl.reliableCommands[index] ) );
}

// a client command of the local player for the server he plays on
void RL_Remote_AddCommand( const char *cmd )
{
	rlLeg_t *saved = cur, *a = RL_ActiveLeg();

	if ( a ) {
		cur = a;
		RL_AddCommand( cmd );
		cur = saved;
	}
}

static void RL_QueueCmd( const usercmd_t *cmd )
{
	rl.cmdNumber++;
	rl.cmds[rl.cmdNumber & RL_CMD_MASK] = *cmd;
	rl.cmdPending = qtrue;
}

// the local player's usercmd, serverTime in the time of the remote server; sent with the next packet
void RL_Remote_SubmitCmd( const usercmd_t *cmd )
{
	rlLeg_t *saved = cur, *a = RL_ActiveLeg();

	if ( !a || a->state != RS_ACTIVE ) {
		return;
	}
	cur = a;
	rl.lastLocalCmdTime = rl.realtime;
	rl.localCmds++;
	RL_QueueCmd( cmd );
	cur = saved;
}

// ---- what the local server needs to know about the server the local player plays on ----

int RL_Remote_ClientNum( void )
{
	const rlLeg_t *a = RL_ActiveLeg();

	return a && a->state == RS_ACTIVE ? a->clientNum : -1;
}

int RL_Remote_ServerId( void )
{
	const rlLeg_t *a = RL_ActiveLeg();

	return a ? a->serverId : 0;
}

static const char *RL_LegConfigstring( const rlLeg_t *l, int index )
{
	return l->configstrings[index] ? l->configstrings[index] : "";
}

const char *RL_Remote_Configstring( int index )
{
	const rlLeg_t *a = RL_ActiveLeg();

	return a ? RL_LegConfigstring( a, index ) : "";
}

// the gamestate of the candidate, to look at it before it is used
const char *RL_Remote_CandidateConfigstring( int index )
{
	const rlLeg_t *c = RL_CandLeg();

	return c ? RL_LegConfigstring( c, index ) : "";
}

const playerState_t *RL_Remote_PlayerState( void )
{
	const rlLeg_t *a = RL_ActiveLeg();

	return a && a->state == RS_ACTIVE && a->snap.valid ? &a->snap.ps : NULL;
}

int RL_Remote_Rtt( void )
{
	const rlLeg_t *a = RL_ActiveLeg();

	return a ? a->rtt : 0;
}

static int RL_LegTime( const rlLeg_t *l )
{
	if ( l->state != RS_ACTIVE || !l->snap.valid ) {
		return -1;
	}
	return Sys_Milliseconds() + l->offset + l->rtt / 2;
}

// The time the remote server has now, as far as we can tell: its last snapshot, the time since we got it and the way
// it took to us. -1 while we have no snapshot.
int RL_Remote_Time( void )
{
	const rlLeg_t *a = RL_ActiveLeg();

	return a ? RL_LegTime( a ) : -1;
}

// The time our local server has to show so that a usercmd made at that time gets to the remote server when its clock reads
// the same: the remote time now plus the way from us to it.
int RL_Remote_TargetTime( void )
{
	const rlLeg_t *a = RL_ActiveLeg();
	int t = RL_Remote_Time();

	// a few snapshots first: the first ones after a map change can still carry the time of the old map
	return t < 0 || !a->rttSamples || a->offCount < RL_OFFSET_SETTLE ? -1 : t + a->rtt / 2 + relay_lead->integer;
}

// The server that is being tried: its map once it passed the gate and has a snapshot and a round trip time, else NULL.
const char *RL_Remote_CandidateMap( void )
{
	const rlLeg_t *c = RL_CandLeg();

	if ( !c || c->state != RS_ACTIVE || !c->ready || !c->snap.valid || !c->rttSamples ) {
		return NULL;
	}
	return Info_ValueForKey( RL_LegConfigstring( c, CS_SERVERINFO ), "mapname" );
}

static void RL_WritePacket( void )
{
	msg_t		buf;
	byte		data[MAX_MSGLEN_BUF];
	int			i, j, n;
	usercmd_t	*cmd, *oldcmd;
	usercmd_t	nullcmd;
	int			packetNum, oldPacketNum;
	int			count, key;

	Com_Memset( &nullcmd, 0, sizeof( nullcmd ) );
	oldcmd = &nullcmd;

	MSG_Init( &buf, data, MAX_MSGLEN );
	MSG_Bitstream( &buf );

	MSG_WriteLong( &buf, rl.serverId );
	MSG_WriteLong( &buf, rl.serverMessageSequence );
	MSG_WriteLong( &buf, rl.serverCommandSequence );

	// unacknowledged client commands
	n = rl.reliableSequence - rl.reliableAcknowledge;
	for ( i = 0; i < n; i++ ) {
		const int index = rl.reliableAcknowledge + 1 + i;
		MSG_WriteByte( &buf, clc_clientCommand );
		MSG_WriteLong( &buf, index );
		MSG_WriteString( &buf, rl.reliableCommands[index & ( MAX_RELIABLE_COMMANDS - 1 )] );
	}

	// all the usercmds since a few packets ago, so a couple of dropped packets lose nothing
	oldPacketNum = ( rl.netchan.outgoingSequence - 1 - relay_packetdup->integer ) & PACKET_MASK;
	count = rl.cmdNumber - rl.outPackets[oldPacketNum].cmdNumber;
	if ( count > MAX_PACKET_USERCMDS ) {
		count = MAX_PACKET_USERCMDS;
	}
	if ( count > RL_CMD_BACKUP - 1 ) {
		count = RL_CMD_BACKUP - 1;
	}
	if ( count >= 1 ) {
		if ( !rl.snap.valid || rl.serverMessageSequence != rl.snap.messageNum ) {
			MSG_WriteByte( &buf, clc_moveNoDelta );
		} else {
			MSG_WriteByte( &buf, clc_move );
		}

		MSG_WriteByte( &buf, count );

		key = rl.checksumFeed;
		key ^= rl.serverMessageSequence;
		key ^= MSG_HashKey( rl.serverCommands[rl.serverCommandSequence & ( MAX_RELIABLE_COMMANDS - 1 )], 32 );

		for ( i = 0; i < count; i++ ) {
			j = ( rl.cmdNumber - count + i + 1 ) & RL_CMD_MASK;
			cmd = &rl.cmds[j];
			MSG_WriteDeltaUsercmdKey( &buf, key, oldcmd, cmd );
			oldcmd = cmd;
		}
	}

	packetNum = rl.netchan.outgoingSequence & PACKET_MASK;
	rl.outPackets[packetNum].cmdNumber = rl.cmdNumber;
	rl.lastPacketSentTime = rl.realtime;

	MSG_WriteByte( &buf, clc_EOF );

	if ( buf.overflowed ) {
		RL_LegDisconnect( "Message overflowed" );
		return;
	}

	RL_Netchan_Transmit( &buf );
}

// the userinfo sent with the connect: what the player is called and how the remote server shall talk to us
static void RL_BuildUserinfo( char *info, int size )
{
	info[0] = '\0';

	// the local player is who the remote server gets to see; nobody there yet: the settings of the relay
	if ( !RL_Mirror_LocalUserinfo( info, size ) ) {
		Info_SetValueForKey_s( info, size, "name", relay_name->string );
		Info_SetValueForKey_s( info, size, "rate", relay_rate->string );
		Info_SetValueForKey_s( info, size, "snaps", relay_snaps->string );
	}
	if ( rl.password[0] ) {
		Info_SetValueForKey_s( info, size, "password", rl.password );
	}

	// extra keys, e.g. cl_guid
	if ( relay_userinfo->string[0] ) {
		const char *s = relay_userinfo->string;
		char key[BIG_INFO_KEY], value[BIG_INFO_VALUE];

		while ( *s ) {
			s = Info_NextPair( s, key, value );
			if ( !key[0] ) {
				break;
			}
			Info_SetValueForKey_s( info, size, key, value );
		}
	}

	Info_SetValueForKey_s( info, size, "protocol", rl.compat ? XSTRING( OLD_PROTOCOL_VERSION ) : XSTRING( NEW_PROTOCOL_VERSION ) );
	Info_SetValueForKey_s( info, size, "qport", va( "%i", rl.qport ) );
	Info_SetValueForKey_s( info, size, "challenge", va( "%i", rl.challenge ) );
}

static void RL_CheckForResend( void )
{
	char info[MAX_INFO_STRING];
	char data[MAX_INFO_STRING];
	int len;

	if ( rl.state != RS_CHALLENGING && rl.state != RS_CONNECTING ) {
		return;
	}
	if ( rl.realtime - rl.connectTime < RL_RECONNECT_TIMEOUT ) {
		return;
	}

	rl.connectTime = rl.realtime;
	rl.connectPacketCount++;

	if ( rl.connectPacketCount > 5 ) {
		RL_LegDisconnect( "Remote server did not answer" );
		return;
	}

	if ( rl.state == RS_CHALLENGING ) {
		NET_OutOfBandPrint( NS_CLIENT, &rl.adr, "getchallenge %d %s", rl.challenge, GAMENAME_FOR_MASTER );
	} else {
		RL_BuildUserinfo( info, sizeof( info ) );
		len = Com_sprintf( data, sizeof( data ), "connect \"%s\"", info );
		NET_OutOfBandCompress( NS_CLIENT, &rl.adr, (byte *)data, len );
	}
}


// ---------------------------------------------------------------------------
// what we receive
// ---------------------------------------------------------------------------

static void RL_ParseServerInfo( void )
{
	// nothing needed from CS_SERVERINFO yet, kept for the download settings
}

// Does the remote server allow the relay? Returns the reason if not, NULL if it does.
static const char *RL_CheckRemote( void )
{
	const char *sys = RL_Configstring( CS_SYSTEMINFO );
	const char *interference = Info_ValueForKey( sys, "df_mp_InterferenceOff" );

	if ( !interference[0] ) {
		return "the server does not announce df_mp_InterferenceOff";
	}
	if ( atoi( interference ) != RL_REQUIRED_INTERFERENCE ) {
		return va( "players can interact on this server (df_mp_InterferenceOff is %s, the relay needs %i), connect to it directly",
			interference, RL_REQUIRED_INTERFERENCE );
	}
	if ( atoi( Info_ValueForKey( sys, "sv_pure" ) ) ) {
		return "the server is pure (not supported by the relay yet)";
	}
	return NULL;
}

static void RL_ParseGamestate( msg_t *msg )
{
	int i, cmd, newnum, len;
	const char *s, *reason;
	entityState_t nullstate;

	Com_Memset( &nullstate, 0, sizeof( nullstate ) );

	RL_ClearConfigstrings();
	Com_Memset( rl.baselines, 0, sizeof( rl.baselines ) );
	Com_Memset( rl.snapshots, 0, sizeof( rl.snapshots ) );
	Com_Memset( &rl.snap, 0, sizeof( rl.snap ) );
	rl.offCount = 0;			// the time of the server starts anew
	rl.parseEntitiesNum = 0;
	rl.cmdNumber = 0;
	Com_Memset( rl.outPackets, 0, sizeof( rl.outPackets ) );

	// a gamestate always marks a server command sequence
	rl.serverCommandSequence = MSG_ReadLong( msg );

	while ( 1 ) {
		cmd = MSG_ReadByte( msg );

		if ( cmd == svc_EOF ) {
			break;
		}

		if ( cmd == svc_configstring ) {
			i = MSG_ReadShort( msg );
			if ( i < 0 || i >= MAX_CONFIGSTRINGS ) {
				Com_Error( ERR_DROP, "%s: configstring > MAX_CONFIGSTRINGS", __func__ );
			}
			s = MSG_ReadBigString( msg );
			len = strlen( s );
			if ( len >= BIG_INFO_STRING ) {
				Com_Error( ERR_DROP, "%s: configstring too long", __func__ );
			}
			RL_SetConfigstring( i, s );
		} else if ( cmd == svc_baseline ) {
			newnum = MSG_ReadEntitynum( msg );
			if ( newnum < 0 || newnum >= MAX_GENTITIES ) {
				Com_Error( ERR_DROP, "%s: baseline number out of range: %i", __func__, newnum );
			}
			MSG_ReadDeltaEntity( msg, &nullstate, &rl.baselines[newnum], newnum );
		} else {
			Com_Error( ERR_DROP, "%s: bad command byte", __func__ );
		}
	}

	rl.clientNum = MSG_ReadLong( msg );
	rl.checksumFeed = MSG_ReadLong( msg );

	rl.serverId = atoi( Info_ValueForKey( RL_Configstring( CS_SYSTEMINFO ), "sv_serverid" ) );
	RL_ParseServerInfo();

	reason = RL_CheckRemote();
	if ( reason ) {
		RL_LegDisconnect( reason );
		return;
	}

	Com_Printf( "Relay: %s, map %s, client %i\n", rl.addrString,
		Info_ValueForKey( RL_Configstring( CS_SERVERINFO ), "mapname" ), rl.clientNum );

	rl.state = RS_ACTIVE;
	rl.ready = qtrue;
	if ( !rl.activeTime ) {
		rl.activeTime = rl.realtime;
	}
	rl.lastPingTime = rl.realtime - 1000;	// probe right away

	// The server the local player plays on changed its map: the local server follows. A candidate is looked at by
	// RL_Mirror_Frame when it is ready.
	if ( &rl == RL_ActiveLeg() ) {
		reason = RL_Mirror_Start();
		if ( reason ) {
			RL_LegDisconnect( reason );
		}
	}
}

static void RL_DeltaEntity( msg_t *msg, rlSnap_t *frame, int newnum, const entityState_t *old, qboolean unchanged )
{
	entityState_t *state = &rl.parseEntities[rl.parseEntitiesNum & ( RL_MAX_PARSE_ENTITIES - 1 )];

	if ( unchanged ) {
		*state = *old;
	} else {
		MSG_ReadDeltaEntity( msg, old, state, newnum );
	}

	if ( state->number == ( MAX_GENTITIES - 1 ) ) {
		return;		// entity was delta removed
	}
	rl.parseEntitiesNum++;
	frame->numEntities++;
}

static void RL_ParsePacketEntities( msg_t *msg, const rlSnap_t *oldframe, rlSnap_t *newframe )
{
	const entityState_t *oldstate = NULL;
	int newnum, oldindex = 0, oldnum;

	newframe->parseEntitiesNum = rl.parseEntitiesNum;
	newframe->numEntities = 0;

#define RL_NEXT_OLD() \
	do { \
		oldindex++; \
		if ( oldindex >= oldframe->numEntities ) { \
			oldnum = MAX_GENTITIES + 1; \
		} else { \
			oldstate = &rl.parseEntities[( oldframe->parseEntitiesNum + oldindex ) & ( RL_MAX_PARSE_ENTITIES - 1 )]; \
			oldnum = oldstate->number; \
		} \
	} while ( 0 )

	if ( !oldframe || oldframe->numEntities <= 0 ) {
		oldnum = MAX_GENTITIES + 1;
	} else {
		oldstate = &rl.parseEntities[oldframe->parseEntitiesNum & ( RL_MAX_PARSE_ENTITIES - 1 )];
		oldnum = oldstate->number;
	}

	while ( 1 ) {
		newnum = MSG_ReadEntitynum( msg );

		if ( newnum < 0 ) {
			Com_Error( ERR_DROP, "%s: end of message", __func__ );
		}
		if ( newnum == ( MAX_GENTITIES - 1 ) ) {
			break;
		}

		while ( oldnum < newnum ) {
			RL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );
			RL_NEXT_OLD();
		}
		if ( oldnum == newnum ) {
			RL_DeltaEntity( msg, newframe, newnum, oldstate, qfalse );
			RL_NEXT_OLD();
			continue;
		}
		if ( oldnum > newnum ) {
			RL_DeltaEntity( msg, newframe, newnum, &rl.baselines[newnum], qfalse );
		}
	}

	while ( oldnum != MAX_GENTITIES + 1 ) {
		RL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );
		RL_NEXT_OLD();
	}

#undef RL_NEXT_OLD
}

static void RL_ParseSnapshot( msg_t *msg )
{
	const rlSnap_t *old;
	rlSnap_t newSnap;
	int deltaNum, oldMessageNum, i, n;

	Com_Memset( &newSnap, 0, sizeof( newSnap ) );

	newSnap.serverTime = MSG_ReadLong( msg );
	newSnap.realtime = Sys_Milliseconds();
	newSnap.messageNum = rl.serverMessageSequence;

	deltaNum = MSG_ReadByte( msg );
	if ( !deltaNum ) {
		newSnap.deltaNum = -1;
	} else {
		newSnap.deltaNum = newSnap.messageNum - deltaNum;
	}
	newSnap.snapFlags = MSG_ReadByte( msg );

	// a delta from something we no longer have is read and dropped, the server then sends an uncompressed one
	if ( newSnap.deltaNum <= 0 ) {
		newSnap.valid = qtrue;
		old = NULL;
	} else {
		old = &rl.snapshots[newSnap.deltaNum & PACKET_MASK];
		if ( !old->valid || old->messageNum != newSnap.deltaNum ||
			rl.parseEntitiesNum - old->parseEntitiesNum > RL_MAX_PARSE_ENTITIES - MAX_SNAPSHOT_ENTITIES ) {
			Com_DPrintf( "Relay: delta from an unavailable snapshot\n" );
		} else {
			newSnap.valid = qtrue;
		}
	}

	newSnap.areabytes = MSG_ReadByte( msg );
	if ( newSnap.areabytes > (int)sizeof( newSnap.areamask ) ) {
		Com_Error( ERR_DROP, "%s: invalid size %d for areamask", __func__, newSnap.areabytes );
		return;
	}
	MSG_ReadData( msg, &newSnap.areamask, newSnap.areabytes );

	MSG_ReadDeltaPlayerstate( msg, old ? &old->ps : NULL, &newSnap.ps );
	RL_ParsePacketEntities( msg, old, &newSnap );

	if ( !newSnap.valid ) {
		return;
	}

	// invalidate the snapshots we did not get, so nothing deltas from them
	oldMessageNum = rl.snap.messageNum + 1;
	if ( newSnap.messageNum - oldMessageNum >= PACKET_BACKUP ) {
		oldMessageNum = newSnap.messageNum - ( PACKET_BACKUP - 1 );
	}
	for ( i = 0, n = newSnap.messageNum - oldMessageNum; i < n; i++ ) {
		rl.snapshots[( oldMessageNum + i ) & PACKET_MASK].valid = qfalse;
	}

	rl.snap = newSnap;

	// the remote clock against ours: a snapshot that came late looks as if the remote clock were behind, so the one that came
	// quickest of the last ones is the best guess. Using the newest one would move the local clock by every bit of jitter.
	{
		const int sample = newSnap.serverTime - newSnap.realtime;

		// The time of the remote server jumped (a new map starts it anew): what was kept is of no use. One late snapshot looks
		// the same as a jump back, so it takes a few of them in a row.
		if ( rl.offCount > 0 && sample < rl.offset - RL_OFFSET_JUMP ) {
			if ( ++rl.offLow >= 3 ) {
				rl.offCount = 0;
			}
		} else {
			rl.offLow = 0;
		}
		if ( rl.offCount > 0 && sample > rl.offset + RL_OFFSET_JUMP ) {
			rl.offCount = 0;
		}
		rl.offWin[rl.offCount % RL_OFFSET_WINDOW] = sample;
		rl.offCount++;
	}
	{
		int i, n = MIN( rl.offCount, RL_OFFSET_WINDOW );

		rl.offset = rl.offWin[0];
		for ( i = 1; i < n; i++ ) {
			rl.offset = MAX( rl.offset, rl.offWin[i] );
		}
	}
	rl.snapshots[rl.snap.messageNum & PACKET_MASK] = rl.snap;

	if ( relay_shownet->integer ) {
		Com_Printf( "Relay: snapshot %i time %i delta %i entities %i\n", rl.snap.messageNum, rl.snap.serverTime,
			rl.snap.deltaNum, rl.snap.numEntities );
	}

	// the local game has to agree with what the server of the local player says about the player
	if ( &rl == RL_ActiveLeg() ) {
		RL_Reconcile_Snapshot( &rl.snap.ps );
	}
}

static void RL_ParseCommandString( msg_t *msg )
{
	const char *s;
	int seq, index;

	seq = MSG_ReadLong( msg );
	s = MSG_ReadString( msg );

	// already seen
	if ( rl.serverCommandSequence - seq >= 0 ) {
		return;
	}
	rl.serverCommandSequence = seq;

	index = seq & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( rl.serverCommands[index], s, sizeof( rl.serverCommands[index] ) );

	Cmd_TokenizeString( s );
	if ( !Q_stricmp( Cmd_Argv( 0 ), "disconnect" ) ) {
		// the server is done with us, nothing to say back
		RL_LegEnd( va( "the server disconnected us%s%s", Cmd_Argc() > 1 ? ": " : "", Cmd_Argc() > 1 ? Cmd_Argv( 1 ) : "" ), qfalse );
		return;
	}
	if ( relay_shownet->integer ) {
		Com_Printf( "Relay: server command %i: %s\n", seq, s );
	}

	// what the server of the local player says goes to the local player
	if ( &rl == RL_ActiveLeg() ) {
		RL_Mirror_RemoteCommand( s );
	}
}

static void RL_ParseServerMessage( msg_t *msg )
{
	int cmd;

	MSG_Bitstream( msg );

	rl.reliableAcknowledge = MSG_ReadLong( msg );

	if ( rl.reliableSequence - rl.reliableAcknowledge > MAX_RELIABLE_COMMANDS ) {
		rl.reliableAcknowledge = rl.reliableSequence;
	} else if ( rl.reliableSequence - rl.reliableAcknowledge < 0 ) {
		Com_Error( ERR_DROP, "%s: incorrect reliable sequence acknowledge number", __func__ );
	}

	while ( rl.state != RS_IDLE ) {
		if ( msg->readcount > msg->cursize ) {
			Com_Error( ERR_DROP, "%s: read past end of server message", __func__ );
			break;
		}

		cmd = MSG_ReadByte( msg );

		if ( cmd == svc_EOF ) {
			break;
		}

		switch ( cmd ) {
		default:
			Com_Error( ERR_DROP, "%s: illegible server message", __func__ );
			break;
		case svc_nop:
			break;
		case svc_serverCommand:
			RL_ParseCommandString( msg );
			break;
		case svc_gamestate:
			RL_ParseGamestate( msg );
			break;
		case svc_snapshot:
			RL_ParseSnapshot( msg );
			break;
		case svc_download:
			// we never ask for a download
			RL_AddCommand( "stopdl" );
			return;
		case svc_voipSpeex:
		case svc_voipOpus:
			return;
		}
	}
}

static void RL_InfoResponse( msg_t *msg );

static void RL_ConnectionlessPacket( const netadr_t *from, msg_t *msg )
{
	const char *s, *c;
	int challenge = 0;

	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );	// skip the -1

	s = MSG_ReadStringLine( msg );
	Cmd_TokenizeString( s );
	c = Cmd_Argv( 0 );

	if ( !Q_stricmp( c, "challengeResponse" ) ) {
		if ( rl.state != RS_CHALLENGING ) {
			return;
		}

		c = Cmd_Argv( 2 );
		if ( *c != '\0' ) {
			challenge = atoi( c );
		}

		rl.compat = qtrue;
		s = Cmd_Argv( 3 );	// protocol of the server
		if ( *s != '\0' ) {
			int sv_proto = atoi( s );
			if ( sv_proto > OLD_PROTOCOL_VERSION && sv_proto == NEW_PROTOCOL_VERSION ) {
				rl.compat = qfalse;
			}
		}

		if ( !rl.compat && ( *c == '\0' || challenge != rl.challenge ) ) {
			Com_Printf( "Relay: bad challenge for challengeResponse. Ignored.\n" );
			return;
		}

		rl.challenge = atoi( Cmd_Argv( 1 ) );
		rl.state = RS_CONNECTING;
		Com_DPrintf( "Relay: challenge answered, protocol %s\n", rl.compat ? "68" : "71" );
		rl.connectPacketCount = 0;
		rl.connectTime = rl.realtime - RL_RECONNECT_TIMEOUT;
		rl.lastPacketTime = rl.realtime;
		return;
	}

	if ( !Q_stricmp( c, "connectResponse" ) ) {
		if ( rl.state != RS_CONNECTING ) {
			return;
		}

		if ( !rl.compat ) {
			c = Cmd_Argv( 1 );
			if ( *c == '\0' || atoi( c ) != rl.challenge ) {
				Com_Printf( "Relay: bad connectResponse. Ignored.\n" );
				return;
			}
			c = Cmd_Argv( 2 );	// the protocol the server really uses
			if ( *c != '\0' && atoi( c ) > 0 ) {
				rl.compat = atoi( c ) <= OLD_PROTOCOL_VERSION;
			}
		}

		Netchan_Setup( NS_CLIENT, &rl.netchan, from, rl.qport, rl.challenge, rl.compat );

		rl.state = RS_CONNECTED;
		Com_DPrintf( "Relay: connected, waiting for the gamestate\n" );
		rl.lastPacketTime = rl.realtime;
		rl.lastPacketSentTime = rl.realtime - RL_RETRANSMIT_TIMEOUT;	// send the first packet immediately
		return;
	}

	if ( !Q_stricmp( c, "infoResponse" ) ) {
		RL_InfoResponse( msg );
		return;
	}

	if ( !Q_stricmp( c, "print" ) ) {
		s = MSG_ReadString( msg );
		Com_Printf( "Relay: remote server says: %s", s );
		return;
	}
}

// a packet of the server of the leg that cur points to
static void RL_LegPacket( const netadr_t *from, msg_t *msg )
{
	if ( msg->cursize < 5 ) {
		return;
	}

	if ( *(int *)msg->data == -1 ) {
		RL_ConnectionlessPacket( from, msg );
		return;
	}

	if ( rl.state < RS_CONNECTED ) {
		return;
	}

	if ( relay_shownet->integer ) {
		Com_Printf( "Relay: packet, %i bytes\n", msg->cursize );
	}

	if ( !Netchan_Process( &rl.netchan, msg ) ) {
		return;		// out of order, duplicated, etc
	}
	if ( rl.compat ) {
		RL_Netchan_Decode( msg );
	}

	rl.serverMessageSequence = LittleLong( *(int32_t *)msg->data );
	rl.lastPacketTime = rl.realtime;

	RL_ParseServerMessage( msg );
}

// every packet from an address of a remote server comes here
qboolean RL_Remote_Packet( const netadr_t *from, msg_t *msg )
{
	int i;

	for ( i = 0; i < 2; i++ ) {
		if ( legs[i].state != RS_IDLE && NET_CompareAdr( from, &legs[i].adr ) ) {
			cur = &legs[i];
			RL_LegPacket( from, msg );
			return qtrue;
		}
	}
	return qfalse;
}


// ---------------------------------------------------------------------------
// connect / disconnect / frame
// ---------------------------------------------------------------------------

qboolean RL_Remote_Connect( const char *address, const char *password )
{
	netadr_t adr;

	if ( !NET_StringToAdr( address, &adr, NA_UNSPEC ) ) {
		Com_Printf( "Relay: bad server address %s\n", address );
		return qfalse;
	}
	if ( adr.port == 0 ) {
		adr.port = BigShort( PORT_SERVER );
	}

	if ( activeLeg >= 0 && NET_CompareAdr( &adr, &legs[activeLeg].adr ) ) {
		RL_Notify( va( "Relay: already connected to %s", legs[activeLeg].addrString ) );
		return qfalse;
	}

	// a server that is being tried is dropped for the new one
	if ( candLeg >= 0 ) {
		cur = &legs[candLeg];
		RL_LegDisconnect( "replaced by a newer request" );
	}

	// the new server goes into the leg the local player is not on, the old one stays until the new one is known to work
	candLeg = activeLeg == 0 ? 1 : 0;
	cur = &legs[candLeg];
	RL_ClearState();

	rl.parseEntities = Z_Malloc( sizeof( entityState_t ) * RL_MAX_PARSE_ENTITIES );
	rl.adr = adr;
	Q_strncpyz( rl.addrString, NET_AdrToStringwPort( &adr ), sizeof( rl.addrString ) );
	Q_strncpyz( rl.password, password ? password : "", sizeof( rl.password ) );
	rl.realtime = Sys_Milliseconds();
	rl.challenge = ( ( rand() << 16 ) ^ rand() ) ^ rl.realtime;
	rl.qport = Cvar_VariableIntegerValue( "net_qport" );	// the netchan writes this one into every packet
	rl.compat = qtrue;
	rl.connectTime = rl.realtime - RL_RECONNECT_TIMEOUT;	// send the first request right away
	rl.lastPacketTime = rl.realtime;
	rl.state = RS_CHALLENGING;

	RL_Notify( va( "Relay: connecting to %s", rl.addrString ) );
	return qtrue;
}

// The connection of the leg that cur points to is over. The player hears about it if it was the server he plays on or one
// he asked for.
static void RL_LegEnd( const char *reason, qboolean tellServer )
{
	const qboolean wasActive = &rl == RL_ActiveLeg();
	const qboolean wasCand = &rl == RL_CandLeg();
	char addr[sizeof( rl.addrString )];
	int i;

	if ( rl.state == RS_IDLE ) {
		return;
	}

	if ( tellServer && rl.state >= RS_CONNECTED ) {
		// a few times, so it gets through
		RL_AddCommand( "disconnect" );
		for ( i = 0; i < 3; i++ ) {
			RL_WritePacket();
		}
	}

	Q_strncpyz( addr, rl.addrString, sizeof( addr ) );
	RL_ClearState();

	if ( !reason ) {
		reason = "unknown reason";
	}
	if ( wasActive ) {
		activeLeg = -1;
		RL_Mirror_RemoteLost( addr, reason );
	} else if ( wasCand ) {
		candLeg = -1;
		RL_Notify( va( "Relay: can not use %s: %s", addr, reason ) );
	}
}

// The relay ends (the command quit, the window is closed): the remote servers hear "disconnect" as from any client that leaves, and
// do not wait for a timeout.
void RL_Remote_Goodbye( void )
{
	int i, j;

	for ( i = 0; i < 2; i++ ) {
		cur = &legs[i];
		if ( rl.state >= RS_CONNECTED ) {
			RL_AddCommand( "disconnect" );
			for ( j = 0; j < 3; j++ ) {
				RL_WritePacket();
			}
		}
		rl.state = RS_IDLE;
	}
}

static void RL_LegDisconnect( const char *reason )
{
	RL_LegEnd( reason, qtrue );
}

// everything: the server the local player is on and the one that is being tried
void RL_Remote_Disconnect( const char *reason )
{
	int i;

	for ( i = 0; i < 2; i++ ) {
		cur = &legs[i];
		RL_LegDisconnect( reason );
	}
}

// the server that is being tried becomes the server the local player plays on, the old one is dropped
void RL_Remote_Commit( void )
{
	int old = activeLeg;

	if ( candLeg < 0 ) {
		return;
	}
	activeLeg = candLeg;
	candLeg = -1;
	if ( old >= 0 ) {
		cur = &legs[old];
		RL_LegDisconnect( NULL );	// no longer the active one: nobody is told
	}
}

// The entity states of the players (eType) other than the local player from the newest snapshot of the remote server, in
// increasing numbers. Returns how many.
int RL_Remote_PlayerEntities( entityState_t *out, int max, int eType, int exclude )
{
	const rlLeg_t *a = RL_ActiveLeg();
	int i, n = 0;

	if ( !a || a->state != RS_ACTIVE || !a->snap.valid ) {
		return 0;
	}
	for ( i = 0; i < a->snap.numEntities && n < max; i++ ) {
		const entityState_t *es = &a->parseEntities[( a->snap.parseEntitiesNum + i ) & ( RL_MAX_PARSE_ENTITIES - 1 )];

		if ( es->eType == eType && es->number != exclude && es->number >= 0 && es->number < MAX_CLIENTS ) {
			out[n++] = *es;
		}
	}
	return n;
}

// a line for the player: where he plays
const char *RL_Remote_Summary( void )
{
	const rlLeg_t *a = RL_ActiveLeg();
	const rlLeg_t *c = RL_CandLeg();
	const char *s;

	if ( a ) {
		s = va( "Relay: playing on %s, map %s, ping %i", a->addrString,
			Info_ValueForKey( RL_LegConfigstring( a, CS_SERVERINFO ), "mapname" ), a->rtt );
	} else {
		s = "Relay: not connected to a server";
	}
	if ( c ) {
		s = va( "%s (trying %s)", s, c->addrString );
	}
	return s;
}

qboolean RL_Remote_Trying( void )
{
	return candLeg >= 0;
}

// the server that is being tried is not wanted any more
void RL_Remote_AbortCandidate( const char *reason )
{
	if ( candLeg >= 0 ) {
		cur = &legs[candLeg];
		RL_LegDisconnect( reason );
	}
}

// the round trip time to the remote server: a getinfo each second, the answer echoes our challenge
static void RL_ProbeRtt( void )
{
	if ( rl.realtime - rl.lastPingTime < 1000 ) {
		return;
	}
	rl.lastPingTime = rl.realtime;
	rl.pingSeq++;
	rl.pingSent[rl.pingSeq & 7] = Sys_Milliseconds();
	NET_OutOfBandPrint( NS_CLIENT, &rl.adr, "getinfo relay%d", rl.pingSeq );
}

static void RL_InfoResponse( msg_t *msg )
{
	const char *info = MSG_ReadString( msg );
	const char *challenge = Info_ValueForKey( info, "challenge" );
	int seq, sample;

	if ( Q_strncmp( challenge, "relay", 5 ) ) {
		return;
	}
	seq = atoi( challenge + 5 );
	if ( rl.pingSeq - seq < 0 || rl.pingSeq - seq > 7 ) {
		return;
	}
	sample = Sys_Milliseconds() - rl.pingSent[seq & 7];
	rl.rttWin[rl.rttSamples % RL_RTT_WINDOW] = sample;
	rl.rttSamples++;
	{
		int i, n = MIN( rl.rttSamples, RL_RTT_WINDOW );

		rl.rtt = rl.rttWin[0];
		for ( i = 1; i < n; i++ ) {
			rl.rtt = MIN( rl.rtt, rl.rttWin[i] );
		}
	}

	if ( relay_shownet->integer ) {
		Com_Printf( "Relay: rtt %i (sample %i)\n", rl.rtt, sample );
	}
}

// ---- tests: plays a recorded run (the text file of "dfgolden export"; 8 numbers a step: angles, moves, buttons, weapon) ----

static struct {
	qboolean	loaded;
	int			count;
	int			*steps;			// count * 8
	int			next;			// the step to play
	int			nextTime;		// when, on the clock of the remote server
} play;

static void RL_TestPlayLoad( void )
{
	FILE *f = fopen( relay_testplay->string, "r" );
	char line[256];
	int n = 0;

	play.loaded = qtrue;
	if ( !f ) {
		Com_Printf( S_COLOR_YELLOW "Relay: can not read %s\n", relay_testplay->string );
		return;
	}
	while ( fgets( line, sizeof( line ), f ) ) {
		if ( !strncmp( line, "steps ", 6 ) ) {
			play.count = atoi( line + 6 );
			play.steps = Z_Malloc( sizeof( int ) * 8 * MAX( 1, play.count ) );
			break;
		}
	}
	while ( play.steps && n < play.count && fgets( line, sizeof( line ), f ) ) {
		if ( sscanf( line, "%d %d %d %d %d %d %d %d", &play.steps[n * 8], &play.steps[n * 8 + 1], &play.steps[n * 8 + 2],
			&play.steps[n * 8 + 3], &play.steps[n * 8 + 4], &play.steps[n * 8 + 5], &play.steps[n * 8 + 6], &play.steps[n * 8 + 7] ) == 8 ) {
			n++;
		}
	}
	fclose( f );
	play.count = n;
	Com_Printf( "Relay: playing %i steps of %s\n", n, relay_testplay->string );
}

static void RL_TestPlay( void )
{
	const int now = RL_LegTime( &rl );

	if ( !play.loaded ) {
		RL_TestPlayLoad();
	}
	if ( now < 0 ) {
		return;
	}
	if ( !play.nextTime ) {
		play.nextTime = ( ( now + 3000 + 7 ) / 8 ) * 8;		// stand a while first, so the run starts from the spawn
	}
	while ( now >= play.nextTime ) {
		usercmd_t cmd;
		const int *s = play.next < play.count ? &play.steps[play.next * 8] : NULL;

		Com_Memset( &cmd, 0, sizeof( cmd ) );
		cmd.serverTime = play.nextTime;
		if ( s ) {
			cmd.angles[0] = s[0] & 0xffff;
			cmd.angles[1] = s[1] & 0xffff;
			cmd.angles[2] = s[2] & 0xffff;
			cmd.forwardmove = s[3];
			cmd.rightmove = s[4];
			cmd.upmove = s[5];
			cmd.buttons = s[6];
			cmd.weapon = s[7];
			play.next++;
		}
		RL_QueueCmd( &cmd );
		play.nextTime += 8;
	}
	if ( rl.cmdPending ) {
		rl.cmdPending = qfalse;
		RL_WritePacket();
	}
}

// tests: the commands of relay_testcmds, one every 3 seconds
static void RL_TestCmds( void )
{
	char list[MAX_STRING_CHARS];
	char *cmd;
	int i;

	if ( !relay_testcmds->string[0] || rl.realtime - rl.activeTime < 3000 * ( rl.testCmd + 1 ) ) {
		return;
	}
	Q_strncpyz( list, relay_testcmds->string, sizeof( list ) );
	cmd = list;
	for ( i = 0; i < rl.testCmd && cmd; i++ ) {
		cmd = strchr( cmd, ';' );
		if ( cmd ) {
			cmd++;
		}
	}
	rl.testCmd++;
	if ( cmd && *cmd ) {
		char *end = strchr( cmd, ';' );

		if ( end ) {
			*end = 0;
		}
		Com_Printf( "Relay: test command: %s\n", cmd );
		RL_AddCommand( cmd );
	}
}

static void RL_LegFrame( int now )
{
	if ( rl.state == RS_IDLE ) {
		return;
	}

	rl.realtime = now;

	RL_CheckForResend();
	if ( rl.state < RS_CONNECTED ) {
		return;
	}

	if ( rl.realtime - rl.lastPacketTime > relay_timeout->integer * 1000 ) {
		RL_LegDisconnect( "Remote server connection timed out" );
		return;
	}

	if ( rl.state == RS_CONNECTED ) {
		// waiting for the gamestate: only keep the connection alive
		if ( rl.realtime - rl.lastPacketSentTime >= RL_RETRANSMIT_TIMEOUT ) {
			RL_WritePacket();
		}
		return;
	}

	RL_ProbeRtt();
	RL_TestCmds();

	// the local player drives: what he made goes out now
	if ( rl.realtime - rl.lastLocalCmdTime < RL_LOCAL_IDLE ) {
		if ( rl.cmdPending ) {
			rl.cmdPending = qfalse;
			RL_WritePacket();
		}
		return;
	}

	// tests: a recorded run, one usercmd for each frame of the clock of the remote server
	if ( relay_testplay->string[0] ) {
		RL_TestPlay();
		return;
	}

	// nobody plays: a null usercmd on the clock of the remote server keeps us alive
	if ( rl.realtime - rl.lastCmdTime >= 8 ) {
		usercmd_t cmd;

		rl.lastCmdTime = rl.realtime;
		// The time of the remote server is not known yet (the first snapshots after a map change can still have the time of the
		// old map): a usercmd with a wrong time would make the remote server throw away all the later ones, so only the packet goes.
		if ( rl.offCount < RL_OFFSET_SETTLE ) {
			RL_WritePacket();
			return;
		}
		Com_Memset( &cmd, 0, sizeof( cmd ) );
		cmd.serverTime = RL_LegTime( &rl );
		cmd.forwardmove = relay_testmove->integer;	// for tests: this process is the player
		if ( relay_testmove->integer ) {
			static int yaw;

			yaw += relay_testturn->integer;
			cmd.angles[YAW] = yaw & 0xffff;
			cmd.upmove = relay_testturn->integer && ( rl.cmdNumber / 60 ) % 2 ? 127 : 0;	// a jump now and then
		}
		RL_QueueCmd( &cmd );
		RL_WritePacket();
	}
}

void RL_Remote_Frame( int now )
{
	int i;

	for ( i = 0; i < 2; i++ ) {
		if ( legs[i].state != RS_IDLE ) {
			cur = &legs[i];
			RL_LegFrame( now );
		}
	}
}

void RL_Remote_Status( void )
{
	static const char *names[] = { "idle", "challenging", "connecting", "connected", "active" };
	int i;

	for ( i = 0; i < 2; i++ ) {
		const rlLeg_t *l = &legs[i];

		if ( l->state == RS_IDLE ) {
			continue;
		}
		Com_Printf( "%s: %s (%s)\n", i == activeLeg ? "remote" : "trying", l->addrString, names[l->state] );
		if ( l->state == RS_ACTIVE ) {
			Com_Printf( "map %s, client %i, snapshot %i at server time %i, %i entities, rtt %i\n",
				Info_ValueForKey( RL_LegConfigstring( l, CS_SERVERINFO ), "mapname" ), l->clientNum,
				l->snap.messageNum, l->snap.serverTime, l->snap.numEntities, l->rtt );
		}
		if ( l->state == RS_ACTIVE && i == activeLeg ) {
			Com_Printf( "usercmds of the local player forwarded: %i\n", l->localCmds );
			Com_Printf( "remote player: origin %.1f %.1f %.1f velocity %.1f %.1f %.1f commandTime %i\n",
				l->snap.ps.origin[0], l->snap.ps.origin[1], l->snap.ps.origin[2],
				l->snap.ps.velocity[0], l->snap.ps.velocity[1], l->snap.ps.velocity[2], l->snap.ps.commandTime );
		}
	}
	if ( activeLeg < 0 && candLeg < 0 ) {
		Com_Printf( "remote: none\n" );
	}
}

void RL_Remote_Init( void )
{
	relay_name = Cvar_Get( "relay_name", "relay", CVAR_ARCHIVE );
	relay_rate = Cvar_Get( "relay_rate", "25000", CVAR_ARCHIVE );
	relay_snaps = Cvar_Get( "relay_snaps", "40", CVAR_ARCHIVE );
	relay_userinfo = Cvar_Get( "relay_userinfo", "", CVAR_ARCHIVE );
	relay_packetdup = Cvar_Get( "relay_packetdup", "1", CVAR_ARCHIVE );
	relay_timeout = Cvar_Get( "relay_timeout", "20", CVAR_ARCHIVE );
	relay_shownet = Cvar_Get( "relay_shownet", "0", CVAR_TEMP );
	relay_lead = Cvar_Get( "relay_lead", "4", CVAR_ARCHIVE );
	relay_testmove = Cvar_Get( "relay_testmove", "0", CVAR_TEMP );
	relay_testcmds = Cvar_Get( "relay_testcmds", "", CVAR_TEMP );
	relay_testturn = Cvar_Get( "relay_testturn", "0", CVAR_TEMP );
	relay_testplay = Cvar_Get( "relay_testplay", "", CVAR_TEMP );

	Com_Memset( legs, 0, sizeof( legs ) );
	activeLeg = candLeg = -1;
	cur = &legs[0];
}
