/*
===========================================================================
Plugin crash guard, see sv_plugin_guard.h
===========================================================================
*/

#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <stdio.h>
#include <string.h>

#include "sv_plugin_guard.h"

// ----------------------------------------------------------------------------
// state shared by both implementations. Plugin calls are made on the main thread only.
// ----------------------------------------------------------------------------

typedef struct {
	volatile int	active;		// a guarded call is running
	volatile int	faulted;	// set by the handler right before it leaves the call
	pluginRange_t	range;
	pluginFault_t	fault;
	int				installed;
} guardState_t;

static guardState_t g;

#ifdef _WIN32

#include <windows.h>

static PVOID	g_handler;
static CONTEXT	g_saved;

static const char *ExceptionName( const EXCEPTION_RECORD *er, char *buf, size_t size )
{
	switch ( er->ExceptionCode ) {
	case EXCEPTION_ACCESS_VIOLATION:
		snprintf( buf, size, "access violation (%s)", er->ExceptionInformation[0] == 0 ? "read" :
			er->ExceptionInformation[0] == 1 ? "write" : "execute" );
		break;
	case EXCEPTION_IN_PAGE_ERROR:			snprintf( buf, size, "in-page error" ); break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:		snprintf( buf, size, "illegal instruction" ); break;
	case EXCEPTION_PRIV_INSTRUCTION:		snprintf( buf, size, "privileged instruction" ); break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:		snprintf( buf, size, "integer divide by zero" ); break;
	case EXCEPTION_INT_OVERFLOW:			snprintf( buf, size, "integer overflow" ); break;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:	snprintf( buf, size, "array bounds exceeded" ); break;
	case EXCEPTION_DATATYPE_MISALIGNMENT:	snprintf( buf, size, "misaligned access" ); break;
	default:								return NULL;
	}
	return buf;
}

static LONG CALLBACK GuardHandler( EXCEPTION_POINTERS *ep )
{
	char what[64];
	uintptr_t pc;

	if ( !g.active ) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	if ( !ExceptionName( ep->ExceptionRecord, what, sizeof( what ) ) ) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	pc = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
	if ( pc < g.range.lo || pc >= g.range.hi ) {
		return EXCEPTION_CONTINUE_SEARCH;		// not the plugin's code: leave it to the engine
	}

	memcpy( g.fault.what, what, sizeof( g.fault.what ) );
	g.fault.address = pc;
	g.fault.offset = pc - g.range.lo;
	g.fault.dataAddress = ep->ExceptionRecord->NumberParameters > 1 ? (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1] : 0;
	g.faulted = 1;
	g.active = 0;

	// resume as if RtlCaptureContext() in PluginGuard_Call had just returned
	*ep->ContextRecord = g_saved;
	return EXCEPTION_CONTINUE_EXECUTION;
}

int PluginGuard_Init( void )
{
	if ( !g.installed ) {
		g_handler = AddVectoredExceptionHandler( 1, GuardHandler );
		g.installed = g_handler != NULL;
	}
	return g.installed;
}

void PluginGuard_Shutdown( void )
{
	if ( g.installed ) {
		RemoveVectoredExceptionHandler( g_handler );
		g_handler = NULL;
		g.installed = 0;
	}
}

int PluginGuard_ModuleRange( void *handle, const void *symbol, pluginRange_t *out )
{
	const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)handle;
	const IMAGE_NT_HEADERS *nt;

	(void)symbol;
	if ( !handle || dos->e_magic != IMAGE_DOS_SIGNATURE ) {
		return 0;
	}
	nt = (const IMAGE_NT_HEADERS *)( (const char *)handle + dos->e_lfanew );
	if ( nt->Signature != IMAGE_NT_SIGNATURE ) {
		return 0;
	}
	out->lo = (uintptr_t)handle;
	out->hi = out->lo + nt->OptionalHeader.SizeOfImage;
	return 1;
}

// Must not be inlined and keeps almost no state in registers: execution can return into it a
// second time, from the handler, with the register values of the first pass.
#if defined( __GNUC__ )
__attribute__( ( noinline ) )
#elif defined( _MSC_VER )
__declspec( noinline )
#endif
int PluginGuard_Call( const pluginRange_t *range, pluginGuardFn_t fn, void *arg, pluginFault_t *fault )
{
	if ( !g.installed || g.active ) {
		fn( arg );
		return 1;
	}

	g.range = *range;
	g.faulted = 0;

	RtlCaptureContext( &g_saved );
	if ( g.faulted ) {
		// second pass, after a fault in fn
		g.faulted = 0;
		if ( fault ) {
			*fault = g.fault;
		}
		return 0;
	}

	g.active = 1;
	fn( arg );
	g.active = 0;
	return 1;
}

int PluginGuard_Available( void )
{
	return g.installed;
}

#else // !_WIN32

// Implemented for Linux only (dl_iterate_phdr and the register layout of ucontext_t are glibc/Linux things).
// macOS, the BSDs and other systems get the fallback at the end: plugin calls are simply not guarded there.
#if defined( __linux__ )

#include <dlfcn.h>
#include <link.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>

#if defined( __x86_64__ )
#define GUARD_PC( uc )	( (uintptr_t)( (ucontext_t *)( uc ) )->uc_mcontext.gregs[REG_RIP] )
#elif defined( __i386__ )
#define GUARD_PC( uc )	( (uintptr_t)( (ucontext_t *)( uc ) )->uc_mcontext.gregs[REG_EIP] )
#elif defined( __aarch64__ )
#define GUARD_PC( uc )	( (uintptr_t)( (ucontext_t *)( uc ) )->uc_mcontext.pc )
#elif defined( __arm__ )
#define GUARD_PC( uc )	( (uintptr_t)( (ucontext_t *)( uc ) )->uc_mcontext.arm_pc )
#endif

#endif // __linux__

#ifdef GUARD_PC

static const int guardSignals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL };
#define NUM_GUARD_SIGNALS ( (int)( sizeof( guardSignals ) / sizeof( guardSignals[0] ) ) )

static struct sigaction	g_old[NUM_GUARD_SIGNALS];
static sigjmp_buf		g_jmp;
static stack_t			g_altstack;

static const char *SignalName( int sig, const siginfo_t *si, char *buf, size_t size )
{
	switch ( sig ) {
	case SIGSEGV:	snprintf( buf, size, "SIGSEGV (%s)", si->si_code == SEGV_MAPERR ? "unmapped address" : "protection violation" ); break;
	case SIGBUS:	snprintf( buf, size, "SIGBUS" ); break;
	case SIGFPE:	snprintf( buf, size, "SIGFPE (%s)", si->si_code == FPE_INTDIV ? "integer divide by zero" : "arithmetic error" ); break;
	case SIGILL:	snprintf( buf, size, "SIGILL (illegal instruction)" ); break;
	default:		snprintf( buf, size, "signal %d", sig ); break;
	}
	return buf;
}

static void GuardHandler( int sig, siginfo_t *si, void *uc )
{
	uintptr_t pc = GUARD_PC( uc );
	int i;

	if ( g.active && pc >= g.range.lo && pc < g.range.hi ) {
		SignalName( sig, si, g.fault.what, sizeof( g.fault.what ) );
		g.fault.address = pc;
		g.fault.offset = pc - g.range.lo;
		g.fault.dataAddress = (uintptr_t)si->si_addr;
		g.faulted = 1;
		g.active = 0;
		siglongjmp( g_jmp, 1 );
	}

	// not ours: put the previous handler back and return, the instruction faults again and reaches it
	for ( i = 0; i < NUM_GUARD_SIGNALS; i++ ) {
		if ( guardSignals[i] == sig ) {
			sigaction( sig, &g_old[i], NULL );
			return;
		}
	}
	signal( sig, SIG_DFL );
}

int PluginGuard_Init( void )
{
	struct sigaction sa;
	int i;

	if ( g.installed ) {
		return 1;
	}

	// run the handler on its own stack, so that a plugin overflowing the stack can be survived as well
	g_altstack.ss_size = 64 * 1024;
	g_altstack.ss_sp = malloc( g_altstack.ss_size );
	g_altstack.ss_flags = 0;
	if ( !g_altstack.ss_sp || sigaltstack( &g_altstack, NULL ) != 0 ) {
		return 0;
	}

	memset( &sa, 0, sizeof( sa ) );
	sa.sa_sigaction = GuardHandler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
	sigemptyset( &sa.sa_mask );
	for ( i = 0; i < NUM_GUARD_SIGNALS; i++ ) {
		sigaction( guardSignals[i], &sa, &g_old[i] );
	}
	g.installed = 1;
	return 1;
}

void PluginGuard_Shutdown( void )
{
	int i;

	if ( !g.installed ) {
		return;
	}
	for ( i = 0; i < NUM_GUARD_SIGNALS; i++ ) {
		sigaction( guardSignals[i], &g_old[i], NULL );
	}
	g.installed = 0;
}

typedef struct {
	const void	*symbol;
	uintptr_t	lo, hi;
	int			found;
} rangeSearch_t;

static int PhdrCallback( struct dl_phdr_info *info, size_t size, void *data )
{
	rangeSearch_t *rs = (rangeSearch_t *)data;
	uintptr_t lo = (uintptr_t)-1, hi = 0, sym = (uintptr_t)rs->symbol;
	int i, contains = 0;

	(void)size;
	for ( i = 0; i < info->dlpi_phnum; i++ ) {
		const ElfW( Phdr ) *ph = &info->dlpi_phdr[i];
		uintptr_t start, end;

		if ( ph->p_type != PT_LOAD ) {
			continue;
		}
		start = info->dlpi_addr + ph->p_vaddr;
		end = start + ph->p_memsz;
		if ( start < lo ) lo = start;
		if ( end > hi ) hi = end;
		if ( sym >= start && sym < end ) contains = 1;
	}
	if ( contains ) {
		rs->lo = lo;
		rs->hi = hi;
		rs->found = 1;
		return 1;
	}
	return 0;
}

int PluginGuard_ModuleRange( void *handle, const void *symbol, pluginRange_t *out )
{
	rangeSearch_t rs;

	(void)handle;
	if ( !symbol ) {
		return 0;
	}
	rs.symbol = symbol;
	rs.found = 0;
	dl_iterate_phdr( PhdrCallback, &rs );
	if ( !rs.found ) {
		return 0;
	}
	out->lo = rs.lo;
	out->hi = rs.hi;
	return 1;
}

__attribute__( ( noinline ) )
int PluginGuard_Call( const pluginRange_t *range, pluginGuardFn_t fn, void *arg, pluginFault_t *fault )
{
	if ( !g.installed || g.active ) {
		fn( arg );
		return 1;
	}

	g.range = *range;
	g.faulted = 0;

	if ( sigsetjmp( g_jmp, 1 ) != 0 ) {
		g.faulted = 0;
		if ( fault ) {
			*fault = g.fault;
		}
		return 0;
	}

	g.active = 1;
	fn( arg );
	g.active = 0;
	return 1;
}

int PluginGuard_Available( void )
{
	return g.installed;
}

#else // not Linux (or a CPU without program counter access): plugin calls are not guarded

int PluginGuard_Init( void ) { return 0; }
void PluginGuard_Shutdown( void ) {}
int PluginGuard_Available( void ) { return 0; }
int PluginGuard_ModuleRange( void *handle, const void *symbol, pluginRange_t *out ) { (void)handle; (void)symbol; (void)out; return 0; }
int PluginGuard_Call( const pluginRange_t *range, pluginGuardFn_t fn, void *arg, pluginFault_t *fault )
{
	(void)range; (void)fault;
	fn( arg );
	return 1;
}

#endif // GUARD_PC

#endif // _WIN32
