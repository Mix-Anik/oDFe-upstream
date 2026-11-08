/*
===========================================================================
Host side of the plugin sandbox, see plugin_vm.h

What keeps a plugin in its box:
  * every load and store of its compiled code is masked into one block of memory (the data of the program, its bss and its stack)
    and the block has a guard zone, so it cannot name anything of the engine. Jumps and calls only reach the code of the program.
  * it has no system calls: the only way out is a call of an engine function, and all of them go through the generated
    dispatcher, which checks the pointers, the capabilities and the cheat rules.
  * a budget of steps ends a program that never ends, a division the processor would trap on is reported (vm_x86.c,
    vm_interpreted.c) and a plugin that overflows its stack or leaves its code is stopped, not the game (VM_Fault).
The int32 the budget is counted in lives right before the memory of the plugin, out of reach of its masked addresses.
===========================================================================
*/

#include "q_shared.h"
#include "qcommon.h"
#include "vm_local.h"
#include "plugin_vm.h"
#include <setjmp.h>

#define PLUGIN_VM_MAX_STRING	16384
#define PLUGIN_VM_ARGS_SIZE		32			// the arguments of a call are the first bytes of the io buffer
#define PLUGIN_VM_PREFIX		64			// bytes before the memory of the plugin: the meter

// the commands of the one entry point (vmMain) of a plugin besides its exports, see the SDK
#define PLUGIN_VM_CMD_IO		1000		// returns the address of the io buffer
#define PLUGIN_VM_CMD_MANIFEST	1001		// returns the address of the text of the manifest (0 if none)
#define PLUGIN_VM_CMD_EXPORTS	1002		// returns a bit mask of the exports the plugin implements

#define VM_ALL_CHECKS			( VM_RTCHECK_PSTACK | VM_RTCHECK_OPSTACK | VM_RTCHECK_JUMP | VM_RTCHECK_DATA )

struct pluginVM_s {
	struct vm_s			vm;
	byte				*memory;
	pluginVMConfig_t	cfg;
	char				name[64];
	void				*user;
	unsigned			ioOffset;
	unsigned			ioUsed;
	unsigned			exportMask;
	char				manifest[PLUGIN_VM_MAX_MANIFEST];
	char				error[256];
	volatile qboolean	failed;
	jmp_buf				*jmp;				// where a stopped plugin goes back to: the innermost call
	int64_t				work;				// what is left of the allowance of engine work of the call that is running (PluginVM_Charge)
	int64_t				lastSteps;			// what the last CallEntry used, for the stats
	int64_t				lastWork;
	pluginVMStats_t		stats[PLUGIN_VM_MAX_EXPORTS];
};

unsigned				pluginVMImportCalls;
int						pluginVMInterpret;

static pluginVM_t		*pluginActive;		// the plugin that is running (it called the dispatcher or faulted)


static ID_INLINE int32_t *Meter( pluginVM_t *p )
{
	return (int32_t *)( p->vm.dataBase - VM_METER_OFFSET );
}


void PluginVM_Trap( pluginVM_t *p, const char *message )
{
	if ( !p->failed ) {
		p->failed = qtrue;
		Q_strncpyz( p->error, message, sizeof( p->error ) );
	}
	if ( p->jmp ) {
		Q_longjmp( *p->jmp, 1 );
	}
}


void PluginVM_Charge( pluginVM_t *p, unsigned units )
{
	p->work -= units;
	if ( p->work < 0 ) {
		p->work = 0;
		PluginVM_Trap( p, "call used up its allowance of engine work" );
	}
}


void PluginVM_Abort( const char *message )
{
	pluginVM_t *p = pluginActive;

	if ( p == NULL ) {
		return;
	}
	if ( !p->failed ) {
		char text[256];

		p->failed = qtrue;
		Com_sprintf( text, sizeof( text ), "the engine stopped with an error during a call: %s", message ? message : "" );
		Q_strncpyz( p->error, text, sizeof( p->error ) );
	}
	// the jump buffers belong to calls that are gone now
	p->jmp = NULL;
	pluginActive = NULL;
}


void PluginVM_Denied( pluginVM_t *p, const char *function, const char *capability )
{
	char message[256];

	Com_sprintf( message, sizeof( message ), "called %s without the capability %s", function, capability );
	PluginVM_Trap( p, message );
}


void PluginVM_Fail( pluginVM_t *p, const char *message )
{
	PluginVM_Trap( p, message );
}


// a program of the game (not a plugin) that does something it must not drops to the menu as before
void VM_Fault( const char *reason )
{
	if ( pluginActive ) {
		PluginVM_Trap( pluginActive, reason );
	}
	Com_Error( ERR_DROP, "%s", reason );
}


void *PluginVM_User( pluginVM_t *p )
{
	return p->user;
}


unsigned PluginVM_Caps( const pluginVM_t *p )
{
	return p->cfg.grantedCaps;
}


const char *PluginVM_Name( const pluginVM_t *p )
{
	return p->cfg.name;
}


const char *PluginVM_Error( const pluginVM_t *p )
{
	return p->error;
}


const char *PluginVM_Manifest( const pluginVM_t *p )
{
	return p->manifest;
}


qboolean PluginVM_Has( const pluginVM_t *p, int exportIndex )
{
	return exportIndex >= 0 && exportIndex < 32 && ( p->exportMask & ( 1u << exportIndex ) ) != 0;
}


// ---------------------------------------------------------------------------
// the memory of the plugin as the engine sees it
// ---------------------------------------------------------------------------

void PluginVM_Ptr( pluginVM_t *p, intptr_t offset, unsigned size, unsigned align, qboolean nullable, void **out )
{
	const uint64_t o = (uint32_t)offset;

	*out = NULL;
	if ( o == 0 ) {
		if ( nullable ) {
			return;
		}
		PluginVM_Trap( p, "null pointer passed to the engine" );
		return;
	}
	if ( align > 1 && ( o & ( align - 1 ) ) ) {
		PluginVM_Trap( p, "misaligned pointer passed to the engine" );
		return;
	}
	if ( o + size > (uint64_t)p->vm.dataMask + 1 ) {
		PluginVM_Trap( p, "pointer passed to the engine is outside the memory of the plugin" );
		return;
	}
	*out = p->vm.dataBase + o;
}


void PluginVM_String( pluginVM_t *p, intptr_t offset, qboolean nullable, const char **out )
{
	const uint64_t o = (uint32_t)offset;
	uint64_t room;

	*out = NULL;
	if ( o == 0 ) {
		if ( nullable ) {
			return;
		}
		PluginVM_Trap( p, "null string passed to the engine" );
		return;
	}
	room = (uint64_t)p->vm.dataMask + 1;
	if ( o >= room ) {
		PluginVM_Trap( p, "string passed to the engine is outside the memory of the plugin" );
		return;
	}
	room -= o;
	if ( room > PLUGIN_VM_MAX_STRING ) {
		room = PLUGIN_VM_MAX_STRING;
	}
	if ( memchr( p->vm.dataBase + o, 0, (size_t)room ) == NULL ) {
		PluginVM_Trap( p, "string passed to the engine is not terminated (or too long)" );
		return;
	}
	*out = (const char *)p->vm.dataBase + o;
}


void PluginVM_IOBegin( pluginVM_t *p )
{
	p->ioUsed = PLUGIN_VM_ARGS_SIZE;
}


unsigned PluginVM_IOPut( pluginVM_t *p, const void *data, unsigned size, unsigned align )
{
	unsigned at;

	if ( align < 1 ) {
		align = 1;
	}
	at = ( p->ioUsed + align - 1 ) & ~( align - 1 );
	if ( at + size > PLUGIN_VM_IO_SIZE ) {
		return 0;
	}
	Com_Memcpy( p->vm.dataBase + p->ioOffset + at, data, size );
	p->ioUsed = at + size;
	return p->ioOffset + at;
}


unsigned PluginVM_IOPutString( pluginVM_t *p, const char *s )
{
	return PluginVM_IOPut( p, s, (unsigned)strlen( s ) + 1, 1 );
}


// ---------------------------------------------------------------------------
// calling the plugin
// ---------------------------------------------------------------------------

// the one function that is the system call of every plugin
static intptr_t PluginVM_Syscall( intptr_t *args )
{
	pluginVM_t *p = pluginActive;

	if ( p == NULL ) {
		Com_Error( ERR_FATAL, "plugin call without a plugin" );
		return 0;
	}
	if ( p->failed ) {
		PluginVM_Trap( p, p->error );
	}
	return p->cfg.dispatch( p, args );
}


static int32_t RunVM( pluginVM_t *p, int32_t *args, int nargs )
{
#if ( idx64 || id386 ) && !defined( NO_VM_COMPILED )		// the only processors the plugin compiler exists for (see CompileOrPrepare)
	if ( p->vm.compiled ) {
		return VM_CallCompiled( &p->vm, nargs, args );
	}
#endif
	return VM_CallInterpreted2( &p->vm, nargs, args );
}


// runs vmMain( command, io ) with a budget; returns qfalse if the plugin was stopped
static qboolean CallEntry( pluginVM_t *p, int command, int budget, int *result )
{
	jmp_buf				jb;
	jmp_buf				*prevJmp = p->jmp;
	pluginVM_t			*prevActive = pluginActive;
	const int32_t		savedMeter = *Meter( p );
	const int32_t		stackOnEntry = p->vm.programStack;
	const int64_t		prevWork = p->work;
	int64_t				work;
	volatile qboolean	ok;
	int32_t				args[MAX_VMMAIN_CALL_ARGS];
	int32_t				r = 0;

	if ( p->failed ) {
		return qfalse;
	}
	args[0] = command;
	args[1] = (int32_t)p->ioOffset;
	args[2] = 0;
	args[3] = 0;

	// a call inside a call (the plugin's code is running already) cannot have more than what is left of the outer one
	work = budget > PLUGIN_VM_BUDGET_FRAME ? PLUGIN_VM_WORK_INIT : PLUGIN_VM_WORK_FRAME;
	p->work = prevWork > 0 && prevWork < work ? prevWork : work;

	p->jmp = &jb;
	pluginActive = p;
	if ( Q_setjmp( jb ) == 0 ) {
		*Meter( p ) = budget;
		r = RunVM( p, args, 2 );
		ok = qtrue;
	} else {
		ok = qfalse;
		p->vm.programStack = stackOnEntry;
	}
	p->lastWork = work - p->work;
	p->lastSteps = ok ? budget - *Meter( p ) : budget;
	*Meter( p ) = savedMeter;
	p->work = prevWork;
	pluginActive = prevActive;
	p->jmp = prevJmp;

	if ( ok && result ) {
		*result = r;
	}
	return ok;
}


qboolean PluginVM_Call( pluginVM_t *p, int exportIndex, const int *args, int numArgs, int *result, int budget )
{
	pluginVMStats_t *st;
	int64_t started;
	unsigned importsBefore;
	qboolean ok;
	int i, r = 0;

	if ( p->failed ) {
		return qfalse;
	}
	if ( exportIndex < 0 || exportIndex >= PLUGIN_VM_MAX_EXPORTS || numArgs < 0 || numArgs > (int)( PLUGIN_VM_ARGS_SIZE / sizeof( int ) ) ) {
		PluginVM_Fail( p, "bad call of an export" );
		return qfalse;
	}
	for ( i = 0; i < numArgs; i++ ) {
		( (int32_t *)( p->vm.dataBase + p->ioOffset ) )[i] = args[i];
	}

	started = Sys_Microseconds();
	importsBefore = pluginVMImportCalls;
	ok = CallEntry( p, exportIndex, budget, &r );

	st = &p->stats[exportIndex];
	{
		const int took = (int)( Sys_Microseconds() - started );

		st->calls++;
		st->importCalls += pluginVMImportCalls - importsBefore;
		st->micros += took;
		if ( took > st->maxMicros ) {
			st->maxMicros = took;
		}
		if ( p->lastSteps > st->maxSteps ) {
			st->maxSteps = (int)p->lastSteps;
		}
		if ( p->lastWork > st->maxWork ) {
			st->maxWork = (int)p->lastWork;
		}
	}
	if ( ok && result ) {
		*result = r;
	}
	return ok;
}


void PluginVM_GetStats( const pluginVM_t *p, int exportIndex, pluginVMStats_t *out )
{
	Com_Memset( out, 0, sizeof( *out ) );
	if ( p && exportIndex >= 0 && exportIndex < PLUGIN_VM_MAX_EXPORTS ) {
		*out = p->stats[exportIndex];
	}
}


void PluginVM_ResetStats( pluginVM_t *p )
{
	Com_Memset( p->stats, 0, sizeof( p->stats ) );
}


void PluginVM_PrintStats( const pluginVM_t *p, const char *const *exportNames, int numExports )
{
	int i, any = 0;

	for ( i = 0; i < numExports && i < PLUGIN_VM_MAX_EXPORTS; i++ ) {
		const pluginVMStats_t *st = &p->stats[i];

		if ( !st->calls ) {
			continue;
		}
		if ( !any ) {
			Com_Printf( "  %-16s %9s %9s %9s %12s %10s %10s\n", "export", "calls", "avg us", "max us", "imports/call", "max steps", "max work" );
			any = 1;
		}
		Com_Printf( "  %-16s %9u %9.1f %9i %12.1f %10i %10i\n", exportNames[i], st->calls, (double)st->micros / st->calls, st->maxMicros,
			(double)st->importCalls / st->calls, st->maxSteps, st->maxWork );
	}
	if ( !any ) {
		Com_Printf( "  no calls yet\n" );
	}
}


// ---------------------------------------------------------------------------
// loading
// ---------------------------------------------------------------------------

static unsigned Pow2Ceil( unsigned v )
{
	unsigned p = 1;

	while ( p < v && p != 0 ) {
		p <<= 1;
	}
	return p;
}


static const char *CheckHeader( const vmHeader_t *h, uint32_t fileSize )
{
	static char errMsg[128];

	if ( fileSize < sizeof( vmHeader_t ) ) {
		Com_sprintf( errMsg, sizeof( errMsg ), "truncated image header (%u bytes long)", fileSize );
		return errMsg;
	}
	if ( LittleLong( h->vmMagic ) != VM_MAGIC_VER2 ) {
		Com_sprintf( errMsg, sizeof( errMsg ), "bad file magic %08x (build the plugin with q3asm, without -vq3)", LittleLong( h->vmMagic ) );
		return errMsg;
	}
	if ( h->instructionCount == 0 || h->instructionCount > h->codeLength ) {
		return "bad instruction count";
	}
	if ( h->codeOffset >= fileSize || h->codeLength > fileSize - h->codeOffset ) {
		return "bad code segment";
	}
	if ( h->dataOffset >= fileSize || h->dataOffset != h->codeOffset + h->codeLength || h->dataLength > fileSize - h->dataOffset ) {
		return "bad data segment";
	}
	if ( (uint64_t)h->litLength + h->jtrgLength != fileSize - ( h->dataOffset + h->dataLength ) ) {
		return "bad lit/jtrg segment length";
	}
	if ( (uint64_t)h->bssLength + h->dataLength + h->litLength >= ( 1U << 30 ) ) {
		return "bad bss segment length";
	}
	return NULL;
}


// the value of a "key=value" line of a manifest
qboolean PluginVM_ManifestValue( const char *manifest, const char *key, char *out, int outSize )
{
	const int keyLen = (int)strlen( key );
	const char *s = manifest;

	while ( *s ) {
		const char *end = s;

		while ( *end && *end != '\n' ) {
			end++;
		}
		if ( !Q_strncmp( s, key, keyLen ) && s[keyLen] == '=' ) {
			int len = (int)( end - ( s + keyLen + 1 ) );

			while ( len > 0 && s[keyLen + len] == '\r' ) {
				len--;
			}
			if ( len >= outSize ) {
				len = outSize - 1;
			}
			Com_Memcpy( out, s + keyLen + 1, len );
			out[len] = '\0';
			return qtrue;
		}
		s = *end ? end + 1 : end;
	}
	out[0] = '\0';
	return qfalse;
}


// the names of the capabilities in mask, comma separated, the way an operator writes them
static void CapList( const pluginVMConfig_t *cfg, unsigned mask, char *out, int outSize )
{
	int i;

	out[0] = '\0';
	for ( i = 0; i < cfg->numCaps && i < 32; i++ ) {
		if ( mask & ( 1u << i ) ) {
			if ( out[0] ) {
				Q_strcat( out, outSize, "," );
			}
			Q_strcat( out, outSize, cfg->capNames[i] );
		}
	}
}


// the capabilities the manifest asks for ("caps=a,b"); qfalse if it names one that does not exist
static qboolean ManifestCaps( const pluginVM_t *p, unsigned *mask, char *bad, int badSize )
{
	const char *text = p->manifest;

	*mask = 0;
	while ( *text ) {
		const char *eol = strchr( text, '\n' );
		const int len = eol ? (int)( eol - text ) : (int)strlen( text );

		if ( len > 5 && !Q_strncmp( text, "caps=", 5 ) ) {
			const char *c = text + 5, *end = text + len;

			while ( c < end ) {
				char name[32];
				int n = 0, i;

				while ( c < end && *c != ',' && *c != ' ' ) {
					if ( n < (int)sizeof( name ) - 1 ) {
						name[n++] = *c;
					}
					c++;
				}
				name[n] = '\0';
				while ( c < end && ( *c == ',' || *c == ' ' ) ) {
					c++;
				}
				if ( !n ) {
					continue;
				}
				for ( i = 0; i < p->cfg.numCaps && i < 32; i++ ) {
					if ( !Q_stricmp( name, p->cfg.capNames[i] ) ) {
						*mask |= 1u << i;
						break;
					}
				}
				if ( i == p->cfg.numCaps || i >= 32 ) {
					Q_strncpyz( bad, name, badSize );
					return qfalse;
				}
			}
		}
		text = eol ? eol + 1 : text + len;
	}
	return qtrue;
}


void PluginVM_Free( pluginVM_t *p )
{
	if ( !p ) {
		return;
	}
	if ( p->vm.destroy ) {
		p->vm.destroy( &p->vm );
	}
	free( p->memory );
	free( p );
}


static qboolean CompileOrPrepare( pluginVM_t *p, vmHeader_t *header )
{
#if ( idx64 || id386 ) && !defined( NO_VM_COMPILED )
	if ( !pluginVMInterpret ) {
		// The compiler of the game modules reads vm_rtChecks, a cvar the player can set: switch every check on for the plugin.
		// (The jump and block copy checks are what keeps a program inside its code and memory.)
		const int savedChecks = vm_rtChecks->integer;
		pluginVM_t *prevActive = pluginActive;
		jmp_buf jb;
		volatile qboolean compiled = qfalse, faulted = qfalse;

		// a program the compiler cannot make sense of ends in VM_Fault, which comes back here
		p->jmp = &jb;
		pluginActive = p;
		vm_rtChecks->integer = VM_ALL_CHECKS;
		if ( Q_setjmp( jb ) == 0 ) {
			compiled = VM_Compile( &p->vm, header );
		} else {
			faulted = qtrue;
			VM_CompileAbort( &p->vm );
		}
		vm_rtChecks->integer = savedChecks;
		pluginActive = prevActive;
		p->jmp = NULL;
		if ( faulted ) {
			return qfalse;		// p->error says why
		}
		if ( compiled ) {
			p->vm.compiled = qtrue;
			return qtrue;
		}
	}
#endif
	// no compiler for this processor, or the program was refused: the interpreter checks the same things (and says why)
	p->vm.compiled = qfalse;
	return VM_PrepareInterpreter2( &p->vm, header );
}


pluginVM_t *PluginVM_Load( void *image, int size, const pluginVMConfig_t *cfg, void *user, char *err, int errSize )
{
	vmHeader_t		*header = (vmHeader_t *)image;
	pluginVM_t		*p = NULL;
	const char		*errMsg = NULL;
	unsigned		exactDataLength, dataLength, dataAlloc, extra;
	int32_t			*targets = NULL;
	int				result = 0, i;

	err[0] = '\0';
	if ( size < 0 || ( errMsg = CheckHeader( header, (uint32_t)size ) ) != NULL ) {
		Com_sprintf( err, errSize, "not a plugin: %s", size < 0 ? "no data" : errMsg );
		return NULL;
	}
	if ( vm_rtChecks == NULL ) {
		Com_sprintf( err, errSize, "the VM is not initialized" );
		return NULL;
	}

	// the size of the memory: the same layout as the game modules have (data, lit, bss, then the program stack at the end)
	exactDataLength = header->dataLength + header->litLength + header->bssLength;
	dataLength = exactDataLength;
	if ( dataLength < PROGRAM_STACK_SIZE ) {
		dataLength = PROGRAM_STACK_SIZE;
	}
	extra = PROGRAM_STACK_EXTRA;
	if ( Pow2Ceil( dataLength ) - dataLength >= extra ) {
		extra = Pow2Ceil( dataLength ) - dataLength;
	} else {
		dataLength += extra;
	}
	dataLength = Pow2Ceil( dataLength );
	if ( dataLength == 0 || dataLength >= ( 1U << 30 ) || dataLength > cfg->maxDataBytes ) {
		Com_sprintf( err, errSize, "needs %u KB of memory, the limit is %u KB", dataLength >> 10, cfg->maxDataBytes >> 10 );
		return NULL;
	}
	dataAlloc = dataLength + VM_DATA_GUARD_SIZE;

	p = (pluginVM_t *)calloc( 1, sizeof( *p ) );
	if ( !p ) {
		Com_sprintf( err, errSize, "out of memory" );
		return NULL;
	}
	p->cfg = *cfg;
	Q_strncpyz( p->name, cfg->name, sizeof( p->name ) );
	p->cfg.name = p->name;
	p->user = user;
	p->memory = (byte *)calloc( 1, PLUGIN_VM_PREFIX + dataAlloc );
	if ( !p->memory ) {
		Com_sprintf( err, errSize, "out of memory" );
		goto fail;
	}

	p->vm.name = p->name;
	p->vm.index = VM_BAD;
	p->vm.systemCall = PluginVM_Syscall;
	p->vm.dataBase = p->memory + PLUGIN_VM_PREFIX;
	p->vm.dataMask = dataLength - 1;
	p->vm.dataAlloc = dataAlloc;
	p->vm.dataLength = dataLength;
	p->vm.exactDataLength = exactDataLength;
	p->vm.programStackExtra = extra;
	p->vm.programStack = (int32_t)( p->vm.dataMask + 1 );
	p->vm.stackBottom = p->vm.programStack - PROGRAM_STACK_SIZE - (int32_t)extra;
	p->vm.instructionCount = (int32_t)header->instructionCount;
	p->vm.codeLength = header->codeLength;
	p->vm.forceDataMask = qtrue;		// stores and loads are masked into the memory, not just checked
	p->vm.metered = qtrue;

	Com_Memcpy( p->vm.dataBase, (byte *)header + header->dataOffset, header->dataLength + header->litLength );

	header->jtrgLength &= ~0x03;
	p->vm.numJumpTableTargets = (int32_t)( header->jtrgLength >> 2 );
	if ( p->vm.numJumpTableTargets > 0 ) {
		targets = (int32_t *)malloc( header->jtrgLength );
		if ( !targets ) {
			Com_sprintf( err, errSize, "out of memory" );
			goto fail;
		}
		Com_Memcpy( targets, (byte *)header + header->dataOffset + header->dataLength + header->litLength, header->jtrgLength );
		p->vm.jumpTableTargets = targets;
	}

	if ( !CompileOrPrepare( p, header ) ) {
		Com_sprintf( err, errSize, "the program is not valid%s%s", p->error[0] ? ": " : "", p->error );
		goto fail;
	}
	// the tables were only needed to check and translate the program
	p->vm.jumpTableTargets = NULL;
	free( targets );
	targets = NULL;

	// ask the plugin where its io buffer is, what it calls itself and which exports it implements
	if ( !CallEntry( p, PLUGIN_VM_CMD_IO, PLUGIN_VM_BUDGET_INIT, &result ) ) {
		Com_sprintf( err, errSize, "the plugin failed to start: %s", p->error );
		goto fail;
	}
	p->ioOffset = (unsigned)result;
	if ( p->ioOffset == 0 || ( p->ioOffset & 3 ) || (uint64_t)p->ioOffset + PLUGIN_VM_IO_SIZE > (uint64_t)p->vm.dataMask + 1 ) {
		Com_sprintf( err, errSize, "has no io buffer of %i bytes (use ODF_ENTRY)", PLUGIN_VM_IO_SIZE );
		goto fail;
	}
	if ( !CallEntry( p, PLUGIN_VM_CMD_EXPORTS, PLUGIN_VM_BUDGET_INIT, &result ) ) {
		Com_sprintf( err, errSize, "the plugin failed to start: %s", p->error );
		goto fail;
	}
	p->exportMask = (unsigned)result;
	for ( i = cfg->numExports; i < 32; i++ ) {
		p->exportMask &= ~( 1u << i );
	}
	if ( !CallEntry( p, PLUGIN_VM_CMD_MANIFEST, PLUGIN_VM_BUDGET_INIT, &result ) ) {
		Com_sprintf( err, errSize, "the plugin failed to start: %s", p->error );
		goto fail;
	}
	if ( result != 0 ) {
		const char *text;

		PluginVM_String( p, result, qtrue, &text );		// stops the plugin (we are not in a call) if it is not a string
		if ( p->failed ) {
			Com_sprintf( err, errSize, "bad manifest: %s", p->error );
			goto fail;
		}
		Q_strncpyz( p->manifest, text, sizeof( p->manifest ) );
	}

	// a program built for the other API (a server plugin in the folder of the client plugins) is not ours
	{
		char module[16];

		PluginVM_ManifestValue( p->manifest, "module", module, sizeof( module ) );
		if ( !module[0] ) {
			Com_sprintf( err, errSize, "has no module line in its manifest (use the entry point of the SDK)" );
			goto fail;
		}
		if ( Q_stricmp( module, cfg->module ) ) {
			Com_sprintf( err, errSize, "is for the API %s", module );
			goto fail;
		}
	}

	// the capabilities it asks for in its manifest have to be given up front, so that the operator is told what is missing
	{
		unsigned wanted;
		char bad[32];

		if ( !ManifestCaps( p, &wanted, bad, sizeof( bad ) ) ) {
			Com_sprintf( err, errSize, "the manifest asks for the capability %s, which does not exist", bad );
			goto fail;
		}
		if ( wanted & ~cfg->grantedCaps ) {
			char list[128];

			CapList( cfg, wanted & ~cfg->grantedCaps, list, sizeof( list ) );
			Com_sprintf( err, errSize, "needs capabilities that were not granted: %s", list );
			goto fail;
		}
	}
	return p;

fail:
	free( targets );
	p->vm.jumpTableTargets = NULL;
	PluginVM_Free( p );
	return NULL;
}
