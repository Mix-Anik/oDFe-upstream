/*
===========================================================================
oDFe plugin SDK: what client and server plugins share.

A plugin is a QVM program (built with q3lcc and q3asm, see the SDK README). It runs in a sandbox: it has no files, no
network, no system calls and no way to name engine memory. Everything it can do is a function of the engine (odfc.h for
client plugins, odfs.h for server plugins); the player or the server operator decides which groups of functions
("capabilities") a plugin may use, and the engine checks every call.

There is no C library of the system. The SDK has the usual string, math and snprintf functions (sdk/odf_lib.c, headers in
sdk/libc).

A plugin has to
  * start its first source file with the entry point: define ODF_PLUGIN_VERSION (and ODF_PLUGIN_CAPS, ODF_PLUGIN_DESCRIPTION) and the list of exports it implements, then
    include odfc_entry.h or odfs_entry.h (the VM wants vmMain to be the first function of the program),
  * be built with sdk/odf-build.py, which compiles the files, adds the library and the syscalls file of the API (the list
    of engine functions, last) and assembles the program. See sdk/README.md.
===========================================================================
*/

#ifndef ODF_COMMON_H
#define ODF_COMMON_H

// there is no C library, so the few things every program needs are here
#ifndef NULL
#define NULL			( (void *)0 )
#endif

// the size of the buffer the engine passes arguments in
#define ODF_IO_SIZE			4096

// the commands the engine gives to vmMain besides the exports (which are numbered from 0)
#define ODF_CMD_IO			1000		// returns the address of the io buffer
#define ODF_CMD_MANIFEST	1001		// returns the address of the manifest, a string of "key=value" lines
#define ODF_CMD_EXPORTS		1002		// returns a bit mask of the exports the plugin implements

#endif // ODF_COMMON_H
