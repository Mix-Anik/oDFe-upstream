/*
===========================================================================
sv_state.c -- snapshots of the running game (for the client plugin API)

Everything the game module knows lives in the data segment of its QVM: the level, the entities, the clients, its own memory
pool. Copying that segment and putting it back restores the game exactly. What the game module does NOT keep, the server
keeps for it, and that is saved too:

	sv.time and the part of the next frame already counted
	the number of entities
	how the entities are linked into the world (sv.svEntities and the lists of the world sectors)
	the open / closed state of the area portals (collision map)
	the configstrings the game changed
	the last usercmd of the client

Only for a local game (one player, the loopback client) with cheats, and only for a QVM game module.

Storage: the first snapshot of a game is kept whole (the baseline). Every other snapshot only holds the runs of bytes that
differ from the baseline (about a KB instead of the 1.7 MB of the segment), so any snapshot can be dropped without touching
the others. All snapshots are lost when the game module is shut down or restarted (SV_StateReset).

Applying a snapshot rewinds sv.time. The client is told by the client plugin code (CL_Plugins_StateResync): it waits for the
first snapshot of the new timeline and resets its clock to it. Until it has acknowledged that snapshot the server does not
run its usercmds (they carry times of the old timeline).
===========================================================================
*/

#include "server.h"
#include "sv_diff.h"

#define MAX_STATES			512

// the blocks of memory that are saved as a difference to the baseline
typedef enum {
	REGION_GAME,		// data segment of the game VM
	REGION_ENTITIES,	// sv.svEntities
	REGION_LINKS,		// lists of the world sectors (sv_world.c)
	REGION_PORTALS,		// area portals (cm_test.c)
	NUM_REGIONS
} region_t;

typedef struct {
	byte	*data;		// runs: int offset, int length, length bytes
	int		size;
} diff_t;

typedef struct {
	int			handle;			// 0: the slot is free
	int			owner;
	int			time, timeResidual, numEntities;
	int			clientNum;
	usercmd_t	lastUsercmd;
	diff_t		diff[NUM_REGIONS];
	byte		*cs;			// configstrings that differ from the baseline: short index, string, 0 (index 0xffff ends)
	int			csSize;
} state_t;

static struct {
	qboolean	valid;			// a baseline exists for the game that runs
	byte		*data[NUM_REGIONS];
	int			size[NUM_REGIONS];
	char		*cs[MAX_CONFIGSTRINGS];
} baseline;

static state_t		states[MAX_STATES];
void ( *sv_stateFreed )( int handle );
static int			nextHandle = 1;
static int			totalBytes;
static cvar_t		*sv_stateMemory;


// ---------------------------------------------------------------------------
// the regions
// ---------------------------------------------------------------------------

static int Region_Size( region_t r )
{
	switch ( r ) {
	case REGION_GAME:		return (int)gvm->exactDataLength;
	case REGION_ENTITIES:	return (int)sizeof( sv.svEntities );
	case REGION_LINKS:		return SV_WorldLinksSize();
	case REGION_PORTALS:	return CM_PortalStateSize();
	default:				return 0;
	}
}

// The bytes of a region as they are now. Game and entities are used in place; the others are copied into tmp (Z_Free it).
static const byte *Region_Get( region_t r, byte **tmp )
{
	*tmp = NULL;
	switch ( r ) {
	case REGION_GAME:		return gvm->dataBase;
	case REGION_ENTITIES:	return (const byte *)sv.svEntities;
	case REGION_LINKS:
		*tmp = Z_Malloc( SV_WorldLinksSize() + 1 );
		SV_WorldLinksGet( *tmp );
		return *tmp;
	case REGION_PORTALS:
		*tmp = Z_Malloc( CM_PortalStateSize() + 1 );
		CM_GetPortalState( (int *)*tmp );
		return *tmp;
	default:				return NULL;
	}
}

static void Region_Put( region_t r, const byte *data )
{
	switch ( r ) {
	case REGION_GAME:		Com_Memcpy( gvm->dataBase, data, gvm->exactDataLength ); break;
	case REGION_ENTITIES:	Com_Memcpy( sv.svEntities, data, sizeof( sv.svEntities ) ); break;
	case REGION_LINKS:		SV_WorldLinksSet( data ); break;
	case REGION_PORTALS:	CM_SetPortalState( (const int *)data ); break;
	default:				break;
	}
}


// ---------------------------------------------------------------------------
// the store
// ---------------------------------------------------------------------------

static int State_Bytes( const state_t *st )
{
	int i, n = (int)sizeof( *st ) + st->csSize;

	for ( i = 0; i < NUM_REGIONS; i++ ) {
		n += st->diff[i].size;
	}
	return n;
}

static void State_Release( state_t *st )
{
	int i;

	if ( sv_stateFreed && st->handle ) {
		sv_stateFreed( st->handle );
	}
	totalBytes -= State_Bytes( st );
	for ( i = 0; i < NUM_REGIONS; i++ ) {
		if ( st->diff[i].data ) {
			Z_Free( st->diff[i].data );
		}
	}
	if ( st->cs ) {
		Z_Free( st->cs );
	}
	Com_Memset( st, 0, sizeof( *st ) );
}

static state_t *State_Find( int handle )
{
	int i;

	if ( handle <= 0 ) {
		return NULL;
	}
	for ( i = 0; i < MAX_STATES; i++ ) {
		if ( states[i].handle == handle ) {
			return &states[i];
		}
	}
	return NULL;
}

static void Baseline_Free( void )
{
	int i;

	for ( i = 0; i < NUM_REGIONS; i++ ) {
		if ( baseline.data[i] ) {
			Z_Free( baseline.data[i] );
		}
	}
	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		if ( baseline.cs[i] ) {
			Z_Free( baseline.cs[i] );
		}
	}
	Com_Memset( &baseline, 0, sizeof( baseline ) );
}

// The game module was shut down or restarted: nothing saved fits it any more.
void SV_StateReset( void )
{
	int i;

	for ( i = 0; i < MAX_STATES; i++ ) {
		if ( states[i].handle ) {
			State_Release( &states[i] );
		}
	}
	Baseline_Free();
	totalBytes = 0;
}

void SV_StateFree( int handle )
{
	state_t *st = State_Find( handle );

	if ( st ) {
		State_Release( st );
	}
}

void SV_StateFreeOwner( int owner )
{
	int i;

	for ( i = 0; i < MAX_STATES; i++ ) {
		if ( states[i].handle && states[i].owner == owner ) {
			State_Release( &states[i] );
		}
	}
}

int SV_StateOwner( int handle )
{
	const state_t *st = State_Find( handle );

	return st ? st->owner : 0;
}

int SV_StateTotalBytes( void )
{
	return totalBytes;
}


// ---------------------------------------------------------------------------
// what is needed to save or apply
// ---------------------------------------------------------------------------

// Returns the client the state belongs to, or NULL (a reason is printed if print is set).
static client_t *State_Client( qboolean print )
{
	client_t *found = NULL;
	int i;

#define REFUSE( what_ ) \
	do { \
		if ( print ) { \
			Com_Printf( "Game snapshot: %s.\n", what_ ); \
		} \
		return NULL; \
	} while ( 0 )

	if ( !com_sv_running || !com_sv_running->integer || sv.state != SS_GAME || !svs.clients || !gvm ) {
		REFUSE( "no local game is running" );
	}
	if ( gvm->entryPoint || !gvm->dataBase ) {
		REFUSE( "the game module is not a QVM (vm_game 0 is not supported)" );
	}
	if ( !Cvar_VariableIntegerValue( "sv_cheats" ) ) {
		REFUSE( "cheats are not enabled" );
	}
	if ( gvm->callLevel ) {
		REFUSE( "the game module is running" );
	}
	for ( i = 0; i < sv.maxclients; i++ ) {
		client_t *cl = &svs.clients[i];

		if ( cl->state == CS_FREE ) {
			continue;
		}
		if ( found || cl->state != CS_ACTIVE || cl->netchan.remoteAddress.type != NA_LOOPBACK ) {
			REFUSE( "only for a local game without other players" );
		}
		found = cl;
	}
	if ( !found ) {
		REFUSE( "no player" );
	}
#undef REFUSE
	return found;
}

// whether the regions are what the baseline was made of
static qboolean Baseline_Fits( void )
{
	int i;

	if ( !baseline.valid ) {
		return qfalse;
	}
	for ( i = 0; i < NUM_REGIONS; i++ ) {
		if ( baseline.size[i] != Region_Size( i ) ) {
			return qfalse;
		}
	}
	return qtrue;
}


// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

static void Baseline_Take( void )
{
	int i;

	Baseline_Free();
	for ( i = 0; i < NUM_REGIONS; i++ ) {
		byte *tmp;
		const byte *now = Region_Get( i, &tmp );

		baseline.size[i] = Region_Size( i );
		baseline.data[i] = Z_Malloc( baseline.size[i] + 1 );
		Com_Memcpy( baseline.data[i], now, baseline.size[i] );
		totalBytes += baseline.size[i];
		if ( tmp ) {
			Z_Free( tmp );
		}
	}
	for ( i = 2; i < MAX_CONFIGSTRINGS; i++ ) {		// 0 and 1 follow the cvars
		if ( sv.configstrings[i] && sv.configstrings[i][0] ) {
			baseline.cs[i] = CopyString( sv.configstrings[i] );
		}
	}
	baseline.valid = qtrue;
}

static const char *Baseline_String( int i )
{
	return baseline.cs[i] ? baseline.cs[i] : "";
}

// Returns a handle (> 0) or 0.
int SV_StateCreate( int owner )
{
	client_t *cl = State_Client( qtrue );
	state_t *st = NULL;
	int i, limit;

	if ( !cl ) {
		return 0;
	}
	if ( !sv_stateMemory ) {
		sv_stateMemory = Cvar_Get( "sv_stateMemory", "32", CVAR_ARCHIVE );
		Cvar_SetDescription( sv_stateMemory, "Memory (in MB) the snapshots of the running game may use (client plugin API)." );
	}
	limit = sv_stateMemory->integer * 1024 * 1024;

	for ( i = 0; i < MAX_STATES; i++ ) {
		if ( !states[i].handle ) {
			st = &states[i];
			break;
		}
	}
	if ( !st ) {
		Com_Printf( "Game snapshot: too many snapshots.\n" );
		return 0;
	}

	if ( !baseline.valid ) {
		Baseline_Take();
	}
	if ( totalBytes > limit ) {
		Com_Printf( "Game snapshot: memory limit reached (sv_stateMemory %i MB).\n", sv_stateMemory->integer );
		return 0;
	}
	if ( !Baseline_Fits() ) {
		Com_Printf( "Game snapshot: the game changed.\n" );
		return 0;
	}

	Com_Memset( st, 0, sizeof( *st ) );
	st->owner = owner;
	st->time = sv.time;
	st->timeResidual = sv.timeResidual;
	st->numEntities = sv.num_entities;
	st->clientNum = (int)( cl - svs.clients );
	st->lastUsercmd = cl->lastUsercmd;

	for ( i = 0; i < NUM_REGIONS; i++ ) {
		byte *tmp;
		const byte *now = Region_Get( i, &tmp );
		diff_t *d = &st->diff[i];

		d->size = Diff_Make( baseline.data[i], now, baseline.size[i], NULL );
		if ( d->size ) {
			d->data = Z_Malloc( d->size );
			Diff_Make( baseline.data[i], now, baseline.size[i], d->data );
		}
		if ( tmp ) {
			Z_Free( tmp );
		}
	}

	// the configstrings that are not what they were
	for ( i = 2; i < MAX_CONFIGSTRINGS; i++ ) {
		if ( strcmp( sv.configstrings[i], Baseline_String( i ) ) ) {
			st->csSize += 2 + (int)strlen( sv.configstrings[i] ) + 1;
		}
	}
	if ( st->csSize ) {
		byte *p;

		st->csSize += 2;
		st->cs = p = Z_Malloc( st->csSize );
		for ( i = 2; i < MAX_CONFIGSTRINGS; i++ ) {
			if ( strcmp( sv.configstrings[i], Baseline_String( i ) ) ) {
				const int len = (int)strlen( sv.configstrings[i] ) + 1;
				const unsigned short idx = (unsigned short)i;

				Com_Memcpy( p, &idx, 2 );
				Com_Memcpy( p + 2, sv.configstrings[i], len );
				p += 2 + len;
			}
		}
		{
			const unsigned short end = 0xffff;

			Com_Memcpy( p, &end, 2 );
		}
	}

	st->handle = nextHandle++;
	if ( nextHandle <= 0 ) {
		nextHandle = 1;
	}
	totalBytes += State_Bytes( st );

	Com_DPrintf( "Game snapshot %i: %i bytes game, %i entities, %i links, %i portals, %i configstrings; %i bytes in all\n",
		st->handle, st->diff[REGION_GAME].size, st->diff[REGION_ENTITIES].size, st->diff[REGION_LINKS].size,
		st->diff[REGION_PORTALS].size, st->csSize, totalBytes );
	return st->handle;
}


// ---------------------------------------------------------------------------
// apply
// ---------------------------------------------------------------------------

// Whether SV_StateApply would work now (the state is from the game that runs, and the game is as it was).
qboolean SV_StateCanApply( int handle )
{
	const state_t *st = State_Find( handle );

	return st && State_Client( qfalse ) && Baseline_Fits() && st->numEntities <= MAX_GENTITIES;
}

// Puts the game back as it was. The client has to be told (CL_Plugins_StateResync).
qboolean SV_StateApply( int handle )
{
	state_t *st = State_Find( handle );
	client_t *cl;
	int i;

	if ( !st || !SV_StateCanApply( handle ) ) {
		return qfalse;
	}
	cl = &svs.clients[st->clientNum];
	if ( cl != State_Client( qtrue ) ) {
		return qfalse;
	}

	// The game module's memory, how its entities are linked into the world and the doors. Nothing is linked again: that would
	// work out the boxes of the entities anew, and some (bodies) are not where they were linked.
	for ( i = 0; i < NUM_REGIONS; i++ ) {
		byte *now = Z_Malloc( baseline.size[i] + 1 );

		if ( Diff_Apply( st->diff[i].data, st->diff[i].size, baseline.data[i], now, baseline.size[i] ) ) {
			Z_Free( now );
			Com_Error( ERR_DROP, "%s: corrupt state", __func__ );
		}
		Region_Put( i, now );
		Z_Free( now );
	}

	sv.time = st->time;
	sv.timeResidual = st->timeResidual;
	sv.num_entities = st->numEntities;

	// configstrings: back to the baseline, then what the state changed
	{
		const byte *p = st->cs;
		const char *changed[MAX_CONFIGSTRINGS];

		Com_Memset( changed, 0, sizeof( changed ) );
		while ( p ) {
			unsigned short idx;

			Com_Memcpy( &idx, p, 2 );
			if ( idx == 0xffff ) {
				break;
			}
			if ( idx >= MAX_CONFIGSTRINGS ) {
				Com_Error( ERR_DROP, "%s: corrupt state", __func__ );
			}
			changed[idx] = (const char *)p + 2;
			p += 2 + strlen( (const char *)p + 2 ) + 1;
		}
		for ( i = 2; i < MAX_CONFIGSTRINGS; i++ ) {
			SV_SetConfigstring( i, changed[i] ? changed[i] : Baseline_String( i ) );
		}
	}

	// the client: the next snapshot is a whole one, and its usercmds do not run before it has the new time
	cl->lastUsercmd = st->lastUsercmd;
	cl->deltaStart = cl->netchan.outgoingSequence;
	cl->stateWait = qtrue;

	// a demo that is recorded has the cut in it (cl_demoroute.h): this says so to whoever watches it
	SV_SendServerCommand( cl, "print \"^3[SNAPSHOT LOAD]^7 game put back to server time %i\n\"", st->time );
	return qtrue;
}

qboolean SV_StateAvailable( void )
{
	return State_Client( qfalse ) != NULL;
}
