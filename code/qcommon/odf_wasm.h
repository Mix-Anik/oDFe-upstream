/*
===========================================================================
Host side of the plugin sandbox: loads a WebAssembly module, links it against the imports the
engine offers (and the plugin is allowed to have) and calls its exports with limits.

A plugin cannot do anything the imports do not do: it has no filesystem, no network, no
system calls and no way to name engine memory (its memory is one byte array of its own).
This layer is shared by the client plugins (cl_plugin.c) and the server plugins (sv_plugin.c).
The WAMR runtime lives in code/wasm/wamr; its types stay inside odf_wasm.c.
===========================================================================
*/

#ifndef ODF_WASM_H
#define ODF_WASM_H

#include "q_shared.h"

// A plugin declares a static buffer of this size with ODF_DEFINE_IO() and exports a function odf_io that returns its address. The engine writes the
// arguments of an export call (strings, structs) into it, so a plugin needs no allocator.
#define ODF_WASM_IO_SIZE		4096

#define ODF_WASM_MAX_MANIFEST	1024
#define ODF_WASM_MAX_EXPORTS	32

typedef struct odfWasm_s odfWasm_t;

// One function the plugin may import. func is a native function taking the exec env (void *) first and the arguments as
// the signature says ( WAMR signature syntax: "(ii)i" ), cap is the capability bit it needs (0 = always available).
typedef struct {
	const char	*name;
	void		*func;
	const char	*sig;
	unsigned	cap;
} odfWasmImport_t;

typedef struct {
	const char				*module;			// the module name the plugin imports from ("odfc" or "odfs")
	const odfWasmImport_t	*imports;
	int						numImports;
	const char *const		*exportNames;		// the exports to look up; their index is the one OdfWasm_Call takes
	int						numExports;
	unsigned				grantedCaps;		// bit mask of the capabilities the plugin was given
	const char *const		*capNames;			// names of the capability bits, for messages (index = bit)
	int						numCaps;
	unsigned				maxMemoryPages;		// linear memory limit in 64 KiB pages
	unsigned				stackSize;			// size of the interpreter stack in bytes
} odfWasmConfig_t;

// the budgets (WAMR instruction counts) of a call. A plugin that runs out is stopped like one that crashed.
#define ODF_WASM_BUDGET_INIT		200000000
#define ODF_WASM_BUDGET_FRAME		20000000

// Brings the runtime up. Calls are counted: every OdfWasm_Init needs an OdfWasm_Shutdown.
qboolean	OdfWasm_Init( void );
void		OdfWasm_Shutdown( void );

// Loads, checks and instantiates a module. On failure returns NULL and writes the reason into err. The needed capabilities
// the module did not get are listed by name in err, as the line an operator would add. user is what the import functions
// get back from OdfWasm_User().
odfWasm_t	*OdfWasm_Load( const void *binary, int size, const odfWasmConfig_t *cfg, void *user, char *err, int errSize );
void		OdfWasm_Free( odfWasm_t *w );

// The bit mask of the capabilities the imports of a module need, without instantiating it. Returns qfalse (and err) when the
// module is broken or imports something that does not exist at all.
qboolean	OdfWasm_Inspect( const void *binary, int size, const odfWasmConfig_t *cfg, unsigned *needed, char *err, int errSize );

qboolean	OdfWasm_Has( const odfWasm_t *w, int exportIndex );
const char	*OdfWasm_Manifest( const odfWasm_t *w );		// the text of the "odf_manifest" section, "" if none

// Calls an export. All arguments and the result are 32 bit integers (pointers are offsets into the plugin's memory: see
// OdfWasm_IOPut). Returns qfalse when the plugin trapped (bad memory access, instruction limit, bad pointer passed to an
// import ...): OdfWasm_Error says why, and the plugin must not be called again.
qboolean	OdfWasm_Call( odfWasm_t *w, int exportIndex, const int *args, int numArgs, int *result, int budget );

// What the calls of an export have cost so far (wall time, including the work the engine did for the imports the plugin called).
typedef struct {
	unsigned	calls;
	unsigned	importCalls;		// imports called by those calls
	int64_t		micros;				// total time
	int			maxMicros;			// the slowest call
} odfWasmStats_t;
void		OdfWasm_GetStats( const odfWasm_t *w, int exportIndex, odfWasmStats_t *out );
void		OdfWasm_ResetStats( odfWasm_t *w );
void		OdfWasm_PrintStats( const odfWasm_t *w, const char *const *exportNames, int numExports );		// to the console

// counted by the generated import functions, to say how many imports a call made
extern unsigned	odfWasmImportCalls;
const char	*OdfWasm_Error( const odfWasm_t *w );
void		OdfWasm_Fail( odfWasm_t *w, const char *message );		// for the engine to say why a call could not be made

// The memory the engine hands data to the plugin in. OdfWasm_IOBegin starts a call (everything put before is forgotten); Put
// functions return the offset in the plugin's memory, or 0 when it does not fit.
void		OdfWasm_IOBegin( odfWasm_t *w );
unsigned	OdfWasm_IOPut( odfWasm_t *w, const void *data, unsigned size, unsigned align );
unsigned	OdfWasm_IOPutString( odfWasm_t *w, const char *s );

// The memory of the plugin as seen by an import function. A pointer the plugin passes is checked here, with the size the
// import says it needs. Returns qfalse (and stops the plugin with a trap) for anything outside its memory.
void		*OdfWasm_User( void *env );
qboolean	OdfWasm_Ptr( void *env, unsigned offset, unsigned size, unsigned align, qboolean nullable, void **out );
qboolean	OdfWasm_String( void *env, unsigned offset, qboolean nullable, const char **out );
void		OdfWasm_Trap( void *env, const char *message );

#endif // ODF_WASM_H
