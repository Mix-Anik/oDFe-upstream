/*
===========================================================================
rl_overlay.c -- the other players of the remote server, in the snapshots of the local player

The local game only knows the local player. The other players are on the remote server: their entity states come from its
snapshots and are put into the snapshots of the local player as they are (same entity numbers, same times: the clocks agree,
see rl_mirror.c). They are only shown: nothing is simulated for them here, and with df_mp_InterferenceOff 3 they can not touch
the local player anyway. They are as old as the remote server's snapshot, like any other player seen over a network.
===========================================================================
*/

#include "rl_local.h"
#include "../code/server/server.h"

#define RL_ET_PLAYER		1		// ET_PLAYER (game/bg_public.h)
#define RL_STORE_FRAMES		64		// snapshots that may still be used as the base of a delta: PACKET_BACKUP is 32

// The states have to stay where they are as long as a snapshot that points to them can be the base of a delta.
static entityState_t	store[RL_STORE_FRAMES][MAX_CLIENTS];
static int				storeNext;
static int				lastCount, snapshots;	// for the status

// Adds the other players of the remote server to the snapshot of the local player. The list is in increasing entity numbers.
void RL_AddSnapshotEntities( client_t *client, clientSnapshot_t *frame )
{
	const int own = RL_Remote_ClientNum();
	entityState_t *mine = store[storeNext++ & ( RL_STORE_FRAMES - 1 )];
	entityState_t *merged[MAX_SNAPSHOT_ENTITIES];
	int count, i, a = 0, b = 0, n = 0;

	if ( own < 0 || client - svs.clients != own ) {
		return;
	}

	count = RL_Remote_PlayerEntities( mine, MAX_CLIENTS, RL_ET_PLAYER, own );
	lastCount = count;
	snapshots++;
	if ( count <= 0 ) {
		return;
	}

	// two lists in increasing order of the number: the entities of the local game and these
	while ( ( a < frame->num_entities || b < count ) && n < MAX_SNAPSHOT_ENTITIES ) {
		if ( b >= count || ( a < frame->num_entities && frame->ents[a]->number < mine[b].number ) ) {
			merged[n++] = frame->ents[a++];
		} else if ( a < frame->num_entities && frame->ents[a]->number == mine[b].number ) {
			merged[n++] = frame->ents[a++];		// the local game has one with this number: it wins
			b++;
		} else {
			merged[n++] = &mine[b++];
		}
	}
	for ( i = 0; i < n; i++ ) {
		frame->ents[i] = merged[i];
	}
	frame->num_entities = n;
}

void RL_Overlay_Status( void )
{
	Com_Printf( "overlay: %i other players in the last snapshot (%i snapshots)\n", lastCount, snapshots );
}
