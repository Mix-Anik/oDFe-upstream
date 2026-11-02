/*
===========================================================================
Host side of the plugin sandbox, see odf_wasm.h
===========================================================================
*/

#include "q_shared.h"
#include "qcommon.h"
#include "odf_wasm.h"
#include "wasm_export.h"

#define MAX_MODULE_SIZE		( 8 * 1024 * 1024 )
#define MAX_MODULES			4

struct odfWasm_s {
	void					*binary;
	wasm_module_t			module;
	wasm_module_inst_t		inst;
	wasm_exec_env_t			env;
	wasm_function_inst_t	exports[ODF_WASM_MAX_EXPORTS];
	int						numExports;
	unsigned				ioOffset;		// where odf_io is in the memory of the plugin
	unsigned				ioUsed;
	char					manifest[ODF_WASM_MAX_MANIFEST];
	char					error[256];
	void					*user;
	odfWasmStats_t			stats[ODF_WASM_MAX_EXPORTS];
};

unsigned				odfWasmImportCalls;

static int				runtimeRefs;

// the import tables handed to the runtime; they have to stay alive as long as it does
static struct {
	char			module[16];
	NativeSymbol	*symbols;
	int				count;
} registered[MAX_MODULES];


// WAMR's own messages (a failed link, ...) go to the console
int ODF_Wasm_VPrintf( const char *format, va_list ap )
{
	char text[1024];

	Q_vsnprintf( text, sizeof( text ), format, ap );
	Com_Printf( "%s", text );
	return (int)strlen( text );
}


qboolean OdfWasm_Init( void )
{
	RuntimeInitArgs args;

	if ( runtimeRefs > 0 ) {
		runtimeRefs++;
		return qtrue;
	}
	Com_Memset( &args, 0, sizeof( args ) );
	args.mem_alloc_type = Alloc_With_System_Allocator;
	if ( !wasm_runtime_full_init( &args ) ) {
		return qfalse;
	}
	wasm_runtime_set_log_level( WASM_LOG_LEVEL_ERROR );
	runtimeRefs = 1;
	return qtrue;
}

void OdfWasm_Shutdown( void )
{
	int i;

	if ( runtimeRefs <= 0 || --runtimeRefs > 0 ) {
		return;
	}
	wasm_runtime_destroy();
	for ( i = 0; i < MAX_MODULES; i++ ) {
		free( registered[i].symbols );
	}
	Com_Memset( registered, 0, sizeof( registered ) );
}

// makes the imports of a module name known to the runtime (once per name; the runtime must be restarted to change them)
static qboolean RegisterImports( const odfWasmConfig_t *cfg )
{
	int i;

	for ( i = 0; i < MAX_MODULES && registered[i].module[0]; i++ ) {
		if ( !strcmp( registered[i].module, cfg->module ) ) {
			return qtrue;
		}
	}
	if ( i >= MAX_MODULES || strlen( cfg->module ) >= sizeof( registered[i].module ) ) {
		return qfalse;
	}
	registered[i].symbols = (NativeSymbol *)calloc( cfg->numImports + 1, sizeof( NativeSymbol ) );
	if ( !registered[i].symbols ) {
		return qfalse;
	}
	{
		int n;

		for ( n = 0; n < cfg->numImports; n++ ) {
			registered[i].symbols[n].symbol = cfg->imports[n].name;
			registered[i].symbols[n].func_ptr = cfg->imports[n].func;
			registered[i].symbols[n].signature = cfg->imports[n].sig;
			registered[i].symbols[n].attachment = NULL;
		}
	}
	registered[i].count = cfg->numImports;
	Q_strncpyz( registered[i].module, cfg->module, sizeof( registered[i].module ) );
	if ( !wasm_runtime_register_natives( registered[i].module, registered[i].symbols, cfg->numImports ) ) {
		free( registered[i].symbols );
		Com_Memset( &registered[i], 0, sizeof( registered[i] ) );
		return qfalse;
	}
	return qtrue;
}

static const odfWasmImport_t *FindImport( const odfWasmConfig_t *cfg, const char *name )
{
	int i;

	for ( i = 0; i < cfg->numImports; i++ ) {
		if ( !strcmp( cfg->imports[i].name, name ) ) {
			return &cfg->imports[i];
		}
	}
	return NULL;
}

// "auth, ips" for the bits of mask
static void CapList( const odfWasmConfig_t *cfg, unsigned mask, char *out, int outSize )
{
	int i;

	out[0] = '\0';
	for ( i = 0; i < cfg->numCaps && i < 32; i++ ) {
		if ( mask & ( 1u << i ) ) {
			if ( out[0] ) {
				Q_strcat( out, outSize, "," );		// the same list as in the sv_plugins line the operator copies
			}
			Q_strcat( out, outSize, cfg->capNames[i] );
		}
	}
}

/*
===============
CheckImports

Every import of the module has to be a function of the engine's module that exists. Nothing else is ever linked: a plugin
that wants the system (WASI), memory or a table from outside cannot be loaded. Sets *needed to the capabilities the imports
need.
===============
*/
static qboolean CheckImports( wasm_module_t module, const odfWasmConfig_t *cfg, unsigned *needed, char *err, int errSize )
{
	int i, count = wasm_runtime_get_import_count( module );

	*needed = 0;
	for ( i = 0; i < count; i++ ) {
		wasm_import_t imp;
		const odfWasmImport_t *def;

		wasm_runtime_get_import_type( module, i, &imp );
		if ( imp.kind != WASM_IMPORT_EXPORT_KIND_FUNC || strcmp( imp.module_name, cfg->module ) ) {
			Com_sprintf( err, errSize, "imports %s.%s, which a plugin cannot have (it can only import functions of the \"%s\" module)",
				imp.module_name, imp.name, cfg->module );
			return qfalse;
		}
		def = FindImport( cfg, imp.name );
		if ( !def ) {
			Com_sprintf( err, errSize, "imports %s.%s, which this version does not have (built for a newer API?)", imp.module_name, imp.name );
			return qfalse;
		}
		*needed |= def->cap;
	}
	return qtrue;
}

qboolean OdfWasm_Inspect( const void *binary, int size, const odfWasmConfig_t *cfg, unsigned *needed, char *err, int errSize )
{
	LoadArgs la;
	wasm_module_t module;
	void *copy;
	qboolean ok;

	if ( !binary || size < 8 || size > MAX_MODULE_SIZE || runtimeRefs <= 0 ) {
		Com_sprintf( err, errSize, "not a WebAssembly module" );
		return qfalse;
	}
	copy = malloc( size );
	if ( !copy ) {
		Com_sprintf( err, errSize, "out of memory" );
		return qfalse;
	}
	memcpy( copy, binary, size );
	Com_Memset( &la, 0, sizeof( la ) );
	la.name = "plugin";
	la.no_resolve = true;
	module = wasm_runtime_load_ex( (uint8_t *)copy, size, &la, err, errSize );
	if ( !module ) {
		free( copy );
		return qfalse;
	}
	ok = CheckImports( module, cfg, needed, err, errSize );
	wasm_runtime_unload( module );
	free( copy );
	return ok;
}

odfWasm_t *OdfWasm_Load( const void *binary, int size, const odfWasmConfig_t *cfg, void *user, char *err, int errSize )
{
	odfWasm_t *w;
	LoadArgs la;
	InstantiationArgs ia;
	unsigned needed, missing;
	int i;

	err[0] = '\0';
	if ( runtimeRefs <= 0 || !binary || size < 8 || size > MAX_MODULE_SIZE || cfg->numExports > ODF_WASM_MAX_EXPORTS ) {
		Com_sprintf( err, errSize, "not a WebAssembly module" );
		return NULL;
	}
	if ( !RegisterImports( cfg ) ) {
		Com_sprintf( err, errSize, "cannot set up the imports" );
		return NULL;
	}

	w = (odfWasm_t *)calloc( 1, sizeof( *w ) );
	if ( !w ) {
		Com_sprintf( err, errSize, "out of memory" );
		return NULL;
	}
	w->user = user;
	w->binary = malloc( size );
	if ( !w->binary ) {
		Com_sprintf( err, errSize, "out of memory" );
		free( w );
		return NULL;
	}
	memcpy( w->binary, binary, size );

	Com_Memset( &la, 0, sizeof( la ) );
	la.name = "plugin";
	la.no_resolve = true;		// the imports are checked before anything is linked
	w->module = wasm_runtime_load_ex( (uint8_t *)w->binary, size, &la, err, errSize );
	if ( !w->module ) {
		goto fail;
	}
	if ( !CheckImports( w->module, cfg, &needed, err, errSize ) ) {
		goto fail;
	}
	missing = needed & ~cfg->grantedCaps;
	if ( missing ) {
		char list[128];

		CapList( cfg, missing, list, sizeof( list ) );
		Com_sprintf( err, errSize, "needs capabilities that were not granted: %s", list );
		goto fail;
	}
	if ( !wasm_runtime_resolve_symbols( w->module ) ) {
		Com_sprintf( err, errSize, "cannot link the imports" );
		goto fail;
	}

	ia.default_stack_size = cfg->stackSize ? cfg->stackSize : 256 * 1024;
	ia.host_managed_heap_size = 0;
	ia.max_memory_pages = cfg->maxMemoryPages ? cfg->maxMemoryPages : 256;
	w->inst = wasm_runtime_instantiate_ex( w->module, &ia, err, errSize );
	if ( !w->inst ) {
		goto fail;
	}
	w->env = wasm_runtime_create_exec_env( w->inst, ia.default_stack_size );
	if ( !w->env ) {
		Com_sprintf( err, errSize, "out of memory" );
		goto fail;
	}
	wasm_runtime_set_user_data( w->env, w );

	// the buffer the engine passes arguments in: the plugin tells where it is by returning its address from odf_io
	{
		wasm_function_inst_t ioFn = wasm_runtime_lookup_function( w->inst, "odf_io" );
		wasm_val_t ioResult[1];

		ioResult[0].kind = WASM_I32;
		ioResult[0].of.i32 = 0;
		if ( !ioFn || wasm_func_get_param_count( ioFn, w->inst ) != 0 || wasm_func_get_result_count( ioFn, w->inst ) != 1 ) {
			Com_sprintf( err, errSize, "has no odf_io export (use ODF_DEFINE_IO())" );
			goto fail;
		}
		wasm_runtime_set_instruction_count_limit( w->env, ODF_WASM_BUDGET_INIT );
		if ( !wasm_runtime_call_wasm_a( w->env, ioFn, 1, ioResult, 0, NULL ) ) {
			Com_sprintf( err, errSize, "odf_io failed: %s", wasm_runtime_get_exception( w->inst ) );
			goto fail;
		}
		w->ioOffset = (unsigned)ioResult[0].of.i32;
	}
	if ( w->ioOffset == 0 || ( w->ioOffset & 7 ) || !wasm_runtime_validate_app_addr( w->inst, w->ioOffset, ODF_WASM_IO_SIZE ) ) {
		wasm_runtime_clear_exception( w->inst );
		Com_sprintf( err, errSize, "odf_io is not a buffer of %i bytes", ODF_WASM_IO_SIZE );
		goto fail;
	}

	// the exports the engine knows; all of them take and return 32 bit integers only
	w->numExports = cfg->numExports;
	for ( i = 0; i < cfg->numExports; i++ ) {
		wasm_function_inst_t fn = wasm_runtime_lookup_function( w->inst, cfg->exportNames[i] );

		if ( fn ) {
			uint32_t n, k;
			wasm_valkind_t kinds[8];

			n = wasm_func_get_param_count( fn, w->inst );
			if ( n > 8 || wasm_func_get_result_count( fn, w->inst ) > 1 ) {
				Com_sprintf( err, errSize, "export %s has too many parameters or results", cfg->exportNames[i] );
				goto fail;
			}
			wasm_func_get_param_types( fn, w->inst, kinds );
			for ( k = 0; k < n; k++ ) {
				if ( kinds[k] != WASM_I32 ) {
					Com_sprintf( err, errSize, "export %s must take 32 bit integers only", cfg->exportNames[i] );
					goto fail;
				}
			}
			if ( wasm_func_get_result_count( fn, w->inst ) == 1 ) {
				wasm_func_get_result_types( fn, w->inst, kinds );
				if ( kinds[0] != WASM_I32 ) {
					Com_sprintf( err, errSize, "export %s must return a 32 bit integer", cfg->exportNames[i] );
					goto fail;
				}
			}
		}
		w->exports[i] = fn;
	}

	// a plugin built as a reactor wants its static data set up once
	{
		wasm_function_inst_t init = wasm_runtime_lookup_function( w->inst, "_initialize" );

		if ( init ) {
			wasm_runtime_clear_exception( w->inst );
			wasm_runtime_set_instruction_count_limit( w->env, ODF_WASM_BUDGET_INIT );
			if ( !wasm_runtime_call_wasm_a( w->env, init, 0, NULL, 0, NULL ) ) {
				Com_sprintf( err, errSize, "_initialize failed: %s", wasm_runtime_get_exception( w->inst ) );
				goto fail;
			}
		}
	}
	// what the plugin says about itself ("version=1.0" lines): the export odf_manifest returns a string
	{
		wasm_function_inst_t fn = wasm_runtime_lookup_function( w->inst, "odf_manifest" );
		wasm_val_t res[1];

		res[0].kind = WASM_I32;
		res[0].of.i32 = 0;
		if ( fn && wasm_func_get_param_count( fn, w->inst ) == 0 && wasm_func_get_result_count( fn, w->inst ) == 1 ) {
			wasm_runtime_clear_exception( w->inst );
			wasm_runtime_set_instruction_count_limit( w->env, ODF_WASM_BUDGET_INIT );
			if ( wasm_runtime_call_wasm_a( w->env, fn, 1, res, 0, NULL ) && res[0].of.i32 != 0 &&
				wasm_runtime_validate_app_str_addr( w->inst, (uint32_t)res[0].of.i32 ) ) {
				Q_strncpyz( w->manifest, (const char *)wasm_runtime_addr_app_to_native( w->inst, (uint32_t)res[0].of.i32 ), sizeof( w->manifest ) );
			}
			wasm_runtime_clear_exception( w->inst );
		}
	}
	return w;

fail:
	if ( !err[0] ) {
		Com_sprintf( err, errSize, "cannot load" );
	}
	OdfWasm_Free( w );
	return NULL;
}

void OdfWasm_Free( odfWasm_t *w )
{
	if ( !w ) {
		return;
	}
	if ( w->env ) {
		wasm_runtime_destroy_exec_env( w->env );
	}
	if ( w->inst ) {
		wasm_runtime_deinstantiate( w->inst );
	}
	if ( w->module ) {
		wasm_runtime_unload( w->module );
	}
	free( w->binary );
	free( w );
}

qboolean OdfWasm_Has( const odfWasm_t *w, int exportIndex )
{
	return w && exportIndex >= 0 && exportIndex < w->numExports && w->exports[exportIndex] ? qtrue : qfalse;
}

const char *OdfWasm_Manifest( const odfWasm_t *w )
{
	return w ? w->manifest : "";
}

void OdfWasm_GetStats( const odfWasm_t *w, int exportIndex, odfWasmStats_t *out )
{
	memset( out, 0, sizeof( *out ) );
	if ( w && exportIndex >= 0 && exportIndex < ODF_WASM_MAX_EXPORTS ) {
		*out = w->stats[exportIndex];
	}
}

void OdfWasm_ResetStats( odfWasm_t *w )
{
	memset( w->stats, 0, sizeof( w->stats ) );
}

void OdfWasm_PrintStats( const odfWasm_t *w, const char *const *exportNames, int numExports )
{
	int i, any = 0;

	for ( i = 0; i < numExports && i < ODF_WASM_MAX_EXPORTS; i++ ) {
		const odfWasmStats_t *st = &w->stats[i];

		if ( !st->calls ) {
			continue;
		}
		if ( !any ) {
			Com_Printf( "  %-16s %9s %9s %9s %12s\n", "export", "calls", "avg us", "max us", "imports/call" );
			any = 1;
		}
		Com_Printf( "  %-16s %9u %9.1f %9i %12.1f\n", exportNames[i], st->calls, (double)st->micros / st->calls, st->maxMicros,
			(double)st->importCalls / st->calls );
	}
	if ( !any ) {
		Com_Printf( "  no calls yet\n" );
	}
}

void OdfWasm_Fail( odfWasm_t *w, const char *message )
{
	Q_strncpyz( w->error, message, sizeof( w->error ) );
}

const char *OdfWasm_Error( const odfWasm_t *w )
{
	return w ? w->error : "";
}

qboolean OdfWasm_Call( odfWasm_t *w, int exportIndex, const int *args, int numArgs, int *result, int budget )
{
	wasm_function_inst_t fn;
	wasm_val_t in[8], out[1];
	uint32_t numResults;
	int64_t started;
	unsigned importsBefore;
	qboolean ok;
	int i;

	if ( !OdfWasm_Has( w, exportIndex ) || numArgs < 0 || numArgs > 8 ) {
		return qfalse;
	}
	fn = w->exports[exportIndex];
	if ( wasm_func_get_param_count( fn, w->inst ) != (uint32_t)numArgs ) {
		Com_sprintf( w->error, sizeof( w->error ), "export has %u parameters, the engine passes %i", wasm_func_get_param_count( fn, w->inst ), numArgs );
		return qfalse;
	}
	numResults = wasm_func_get_result_count( fn, w->inst );
	for ( i = 0; i < numArgs; i++ ) {
		in[i].kind = WASM_I32;
		in[i].of.i32 = args[i];
	}
	out[0].kind = WASM_I32;
	out[0].of.i32 = 0;

	wasm_runtime_clear_exception( w->inst );
	wasm_runtime_set_instruction_count_limit( w->env, budget );
	started = Sys_Microseconds();
	importsBefore = odfWasmImportCalls;
	ok = wasm_runtime_call_wasm_a( w->env, fn, numResults, out, numArgs, in );
	{
		odfWasmStats_t *st = &w->stats[exportIndex];
		int took = (int)( Sys_Microseconds() - started );

		st->calls++;
		st->importCalls += odfWasmImportCalls - importsBefore;
		st->micros += took;
		if ( took > st->maxMicros ) {
			st->maxMicros = took;
		}
	}
	if ( !ok ) {
		const char *why = wasm_runtime_get_exception( w->inst );

		Q_strncpyz( w->error, why && *why ? why : "unknown error", sizeof( w->error ) );
		return qfalse;
	}
	if ( result ) {
		*result = numResults ? out[0].of.i32 : 0;
	}
	return qtrue;
}

void OdfWasm_IOBegin( odfWasm_t *w )
{
	w->ioUsed = 0;
}

unsigned OdfWasm_IOPut( odfWasm_t *w, const void *data, unsigned size, unsigned align )
{
	unsigned at;

	if ( align < 1 ) {
		align = 1;
	}
	at = ( w->ioUsed + align - 1 ) & ~( align - 1 );
	if ( size > ODF_WASM_IO_SIZE || at > ODF_WASM_IO_SIZE - size ) {
		return 0;
	}
	memcpy( (char *)wasm_runtime_addr_app_to_native( w->inst, w->ioOffset + at ), data, size );
	w->ioUsed = at + size;
	return w->ioOffset + at;
}

unsigned OdfWasm_IOPutString( odfWasm_t *w, const char *s )
{
	return OdfWasm_IOPut( w, s ? s : "", (unsigned)( s ? strlen( s ) : 0 ) + 1, 1 );
}

void *OdfWasm_User( void *env )
{
	return wasm_runtime_get_user_data( (wasm_exec_env_t)env );
}

void OdfWasm_Trap( void *env, const char *message )
{
	wasm_runtime_set_exception( wasm_runtime_get_module_inst( (wasm_exec_env_t)env ), message );
}

qboolean OdfWasm_Ptr( void *env, unsigned offset, unsigned size, unsigned align, qboolean nullable, void **out )
{
	wasm_module_inst_t inst = wasm_runtime_get_module_inst( (wasm_exec_env_t)env );

	*out = NULL;
	if ( offset == 0 ) {
		if ( nullable ) {
			return qtrue;
		}
		wasm_runtime_set_exception( inst, "null pointer passed to the engine" );
		return qfalse;
	}
	if ( align > 1 && ( offset & ( align - 1 ) ) ) {
		wasm_runtime_set_exception( inst, "misaligned pointer passed to the engine" );
		return qfalse;
	}
	if ( !wasm_runtime_validate_app_addr( inst, offset, size ? size : 1 ) ) {
		return qfalse;		// the runtime has set the exception
	}
	*out = wasm_runtime_addr_app_to_native( inst, offset );
	return qtrue;
}

qboolean OdfWasm_String( void *env, unsigned offset, qboolean nullable, const char **out )
{
	wasm_module_inst_t inst = wasm_runtime_get_module_inst( (wasm_exec_env_t)env );

	*out = NULL;
	if ( offset == 0 ) {
		if ( nullable ) {
			return qtrue;
		}
		wasm_runtime_set_exception( inst, "null string passed to the engine" );
		return qfalse;
	}
	if ( !wasm_runtime_validate_app_str_addr( inst, offset ) ) {
		return qfalse;
	}
	*out = (const char *)wasm_runtime_addr_app_to_native( inst, offset );
	return qtrue;
}
