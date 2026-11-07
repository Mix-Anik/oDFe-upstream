/*
===========================================================================
Host side of the plugin sandbox: runs a plugin that was built as a QVM (q3lcc + q3asm) with limits.

A plugin cannot do anything the engine functions it may call do not do: it has no filesystem, no network, no system calls
and no way to name engine memory (every load and store of the compiled code is masked into one block of memory of its own).
Every function of the engine it calls goes through one dispatcher (generated from the API description, see
code/plugin_api) which checks the pointers it passes, the capabilities it was given and the cheat rules before the engine
code runs. A plugin that does anything wrong is stopped; the game goes on.

Shared by the client plugins (cl_plugin.c) and the server plugins (sv_plugin.c). The code that runs the QVM is the one of the
game modules (vm.c, vm_x86.c, vm_interpreted.c), with a budget of steps, which is what stops a plugin that never ends.
===========================================================================
*/

#ifndef PLUGIN_VM_H
#define PLUGIN_VM_H

#include "q_shared.h"

// A plugin declares a static buffer of this size (ODF_ENTRY in the SDK) and tells the engine where it is. The engine writes
// the arguments of an export call (strings, structs) into it, so a plugin needs no allocator.
#define PLUGIN_VM_IO_SIZE		4096

#define PLUGIN_VM_MAX_MANIFEST	1024
#define PLUGIN_VM_MAX_EXPORTS	32

// The budgets of a call in steps: a step is a function call or a trip around a loop in the compiled code (in the
// interpreter, which is slower, it is 16 instructions). A plugin that runs out is stopped like one that crashed.
#define PLUGIN_VM_BUDGET_INIT	100000000
#define PLUGIN_VM_BUDGET_FRAME	5000000

typedef struct pluginVM_s pluginVM_t;

// The engine side of the API. args[0] is the number of the function the plugin called, args[1...] its arguments as the
// plugin passed them (pointers are offsets into the memory of the plugin, see PluginVM_Ptr). The result goes back to the plugin.
typedef intptr_t ( *pluginVMDispatch_t )( pluginVM_t *vm, intptr_t *args );

typedef struct {
	const char				*name;				// for messages
	const char				*module;				// "odfc" or "odfs": the API the plugin has to be built for
	pluginVMDispatch_t		dispatch;
	const char *const		*exportNames;		// the exports of the API; their index is the one PluginVM_Call takes
	int						numExports;
	unsigned				grantedCaps;		// bit mask of the capabilities the plugin was given
	const char *const		*capNames;			// names of the capability bits, for messages (index = bit)
	int						numCaps;
	unsigned				maxDataBytes;		// the most memory the plugin may use (its data, bss and stack)
} pluginVMConfig_t;

// Loads, checks and starts a plugin: image is the .qvm file. On failure returns NULL and writes the reason into err. The
// capabilities a plugin declares in its manifest (caps=a,b) and did not get are listed by name in err, as the line an
// operator would add. user is what the dispatcher gets back from PluginVM_User().
pluginVM_t	*PluginVM_Load( void *image, int size, const pluginVMConfig_t *cfg, void *user, char *err, int errSize );
void		PluginVM_Free( pluginVM_t *vm );

qboolean	PluginVM_Has( const pluginVM_t *vm, int exportIndex );
const char	*PluginVM_Manifest( const pluginVM_t *vm );		// the text the plugin gave as its manifest, "" if none
qboolean	PluginVM_ManifestValue( const char *manifest, const char *key, char *out, int outSize );	// a "key=value" line of it
unsigned	PluginVM_Caps( const pluginVM_t *vm );			// the capabilities it was given
const char	*PluginVM_Name( const pluginVM_t *vm );

// Calls an export. args are 32 bit integers (pointers are offsets into the plugin's memory: see PluginVM_IOPut). Returns
// qfalse when the plugin was stopped (bad memory access, out of budget, a call it may not make ...): PluginVM_Error says why,
// and the plugin must not be called again.
qboolean	PluginVM_Call( pluginVM_t *vm, int exportIndex, const int *args, int numArgs, int *result, int budget );

// What the calls of an export have cost so far (wall time, including the work the engine did for the calls the plugin made).
typedef struct {
	unsigned	calls;
	unsigned	importCalls;		// engine functions called by those calls
	int64_t		micros;				// total time
	int			maxMicros;			// the slowest call
} pluginVMStats_t;
void		PluginVM_GetStats( const pluginVM_t *vm, int exportIndex, pluginVMStats_t *out );
void		PluginVM_ResetStats( pluginVM_t *vm );
void		PluginVM_PrintStats( const pluginVM_t *vm, const char *const *exportNames, int numExports );		// to the console

// counted by the generated dispatcher, to say how many engine functions a call made
extern unsigned	pluginVMImportCalls;

// set to run plugins in the interpreter even where there is a compiler for the processor (for finding bugs)
extern int		pluginVMInterpret;

const char	*PluginVM_Error( const pluginVM_t *vm );		// "" while the plugin is fine
void		PluginVM_Fail( pluginVM_t *vm, const char *message );	// for the engine to say why a call could not be made

// The memory the engine hands data to the plugin in. PluginVM_IOBegin starts a call (everything put before is forgotten); the
// Put functions return the offset in the plugin's memory, or 0 when it does not fit.
void		PluginVM_IOBegin( pluginVM_t *vm );
unsigned	PluginVM_IOPut( pluginVM_t *vm, const void *data, unsigned size, unsigned align );
unsigned	PluginVM_IOPutString( pluginVM_t *vm, const char *s );

// For the dispatcher: the memory of the plugin as seen from the engine. A pointer the plugin passes is checked here, with
// the size the function says it needs. All of them stop the plugin (they do not return) when it passed something wrong.
void		*PluginVM_User( pluginVM_t *vm );
void		PluginVM_Ptr( pluginVM_t *vm, intptr_t offset, unsigned size, unsigned align, qboolean nullable, void **out );
void		PluginVM_String( pluginVM_t *vm, intptr_t offset, qboolean nullable, const char **out );
void		PluginVM_Trap( pluginVM_t *vm, const char *message );		// stops the plugin
void		PluginVM_Denied( pluginVM_t *vm, const char *function, const char *capability );	// a call without the capability

#endif // PLUGIN_VM_H
