/*
===========================================================================
Plugin crash guard

Runs a call into a plugin so that a crash inside the plugin's own module can be
survived. It has no dependencies on the rest of the engine.

Windows: a vectored exception handler resumes execution at the point where the
guarded call began. Unix (Linux): signal handlers siglongjmp out.

What is recovered: access violations / SIGSEGV, SIGBUS, SIGFPE (integer division),
SIGILL and similar, when the faulting instruction lies inside the plugin's module.
What is not: faults anywhere else, memory corrupted before the fault, crashes in
other threads, endless loops, exit(), abort() on Windows.
===========================================================================
*/

#ifndef SV_PLUGIN_GUARD_H
#define SV_PLUGIN_GUARD_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uintptr_t	lo;		// first address of the module
	uintptr_t	hi;		// one past the last address
} pluginRange_t;

typedef struct {
	char		what[64];		// "access violation (read)", "SIGSEGV", ...
	uintptr_t	address;		// address of the faulting instruction
	uintptr_t	offset;			// address - module start
	uintptr_t	dataAddress;	// address the instruction tried to access, 0 if unknown
} pluginFault_t;

typedef void ( *pluginGuardFn_t )( void *arg );

// Installs the fault handlers. Returns 0 if this platform cannot guard plugin calls
// (then PluginGuard_Call just calls the function).
int		PluginGuard_Init( void );
void	PluginGuard_Shutdown( void );
int		PluginGuard_Available( void );

// Finds the address range of a loaded library. handle is what Sys_LoadLibrary returned,
// symbol is the address of any function or variable inside it.
// Returns 0 if the range cannot be determined.
int		PluginGuard_ModuleRange( void *handle, const void *symbol, pluginRange_t *out );

// Calls fn( arg ). Returns 1 when it returned normally, 0 when a fault inside range was
// caught (the call was abandoned, *fault describes it). Faults outside range are not
// handled here and end the process as before. Calls do not nest: a call made while
// another is running is simply executed unguarded.
int		PluginGuard_Call( const pluginRange_t *range, pluginGuardFn_t fn, void *arg, pluginFault_t *fault );

#endif
