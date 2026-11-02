/*
===========================================================================
oDFe plugin SDK: what client and server plugins share.

A plugin is a WebAssembly module (target wasm32, no WASI, no libc functions that talk to
the system). It runs in a sandbox: it has no files, no network, no system calls and no way
to name engine memory. Everything it can do is a function it imports from the engine
(odfc.h for client plugins, odfs.h for server plugins); the player or the server operator
decides which groups of functions ("capabilities") a plugin may use.

Building (clang with a wasm32 target, for example from wasi-sdk):

    clang --target=wasm32 -O2 -nostdlib -mcpu=mvp -Wl,--no-entry -Wl,--export-dynamic ...

A plugin has to
  * call ODF_DEFINE_IO() once: the engine passes strings and structs in this buffer,
  * export the functions it wants to implement (declared in odfc.h / odfs.h; leave out the
    ones it does not need),
  * optionally describe itself with ODF_MANIFEST( "version=1.0\ndescription=..." ).
===========================================================================
*/

#ifndef ODF_COMMON_H
#define ODF_COMMON_H

// the size of the buffer ODF_DEFINE_IO() creates
#define ODF_IO_SIZE		4096

#if defined( __wasm__ )

// declares a function of the engine; the plugin cannot call anything else
#define ODF_IMPORT( module, name )	__attribute__( ( import_module( module ), import_name( name ) ) )

// marks the definition of a function as an export the engine calls
#define ODF_EXPORT_FN( name )		__attribute__( ( export_name( name ), used ) )

// The memory the engine writes the arguments of calls into. The engine finds it by calling the export odf_io.
#define ODF_DEFINE_IO() \
	static char odf_io_buffer[ODF_IO_SIZE] __attribute__( ( aligned( 8 ) ) ); \
	__attribute__( ( export_name( "odf_io" ), used ) ) char *odf_io( void ) { return odf_io_buffer; }

// A few lines of "key=value" shown to the player ("version", "api", "description"). Put it once in one source file. The
// engine reads it by calling the export odf_manifest.
#define ODF_MANIFEST( text ) \
	__attribute__( ( export_name( "odf_manifest" ), used ) ) const char *odf_manifest( void ) { return text; }

#else

// When the logic of a plugin is compiled natively (for unit tests), the engine functions are plain externs.
#define ODF_IMPORT( module, name )
#define ODF_EXPORT_FN( name )
#define ODF_DEFINE_IO()
#define ODF_MANIFEST( text )

#endif

#endif // ODF_COMMON_H
