# oDFe plugin SDK

A plugin is a **QVM program**: C code compiled to the bytecode of the Quake 3 virtual machine. The engine runs it compiled to
native code (x86, x86-64) or interpreted (other systems), in a sandbox:

* it can only read and write its own memory, every access is masked into it;
* it can only do what the functions of the API let it (`odfc.h` for client plugins, `odfs.h` for server plugins); the engine
  checks every pointer, string and size a call passes, and the groups of functions a plugin may use ("capabilities") are
  decided by the player or the server operator, not by the plugin;
* functions marked `cheat` in the API files do nothing unless the engine says cheats are allowed;
* it has a budget of instructions per call: a loop that never ends stops the plugin, not the game;
* any mistake of the plugin (a bad pointer, a division by zero, a call of a function it was not given) stops the plugin and
  the game goes on.

## Layout

```
include/    odf_common.h, odfc.h, odfs.h, the type headers, the entry points (odfc_entry.h, odfs_entry.h) and the lists of
            engine functions for the assembler (odfc_syscalls.asm, odfs_syscalls.asm): all but odf_common.h and the type
            headers are generated from client.api / server.api / common.api by gen_api.py
sdk/        this folder
  odf-build.c    the program that builds a plugin; it has the compiler in itself (see below), Makefile builds it
  odf_lib.c      the C library: string.h, ctype.h, stdlib.h (strtol, strtof, qsort, rand), math.h, stdio.h (snprintf)
  odf_json.[ch]  a JSON writer and reader;  odf_http.[ch]  HTTP requests with a callback (server plugins)
  libc/          the headers of that library
  examples/      hello.c (client), hello_server.c (server), hello_http.c (server, uses the libraries): the smallest plugins
  check.sh       a smoke test of a built odf-build (the CI runs it on every system)
toolchain/  the compiler (lcc), its preprocessor and the assembler (q3asm), as in ioquake3: see toolchain/README.md
```

## A plugin

This is `examples/hello.c`:

```c
/* hello.c */
#define ODF_PLUGIN_VERSION     "1.0"
#define ODF_PLUGIN_DESCRIPTION "says hello"
#define EXPORTS( X ) X(OnInit)
#include "odfc_entry.h"          /* defines vmMain; it has to come before the first function of the program */

#include <stdio.h>

/* A handler gets the words of the command line like main: "hello World" is argc 2, argv[0] "hello", argv[1] "World". */
static void CmdHello( int argc, const char **argv )
{
	char text[96];

	snprintf( text, sizeof( text ), "hello, %s! %.1f seconds since the start", argc > 1 ? argv[1] : "stranger", Milliseconds() / 1000.0f );
	Print( text );
}

int OnInit( void )
{
	CommandRegister( "hello", CmdHello );
	return 0;                    /* not 0: the plugin is not loaded */
}
```

The functions of the engine (`Print`, `Milliseconds`, `CvarString` ...) are called by their plain names. The functions a
plugin implements for the engine to call (the exports) start with `On`: `OnInit`, `OnShutdown`, `OnDraw3D` ... The
`EXPORTS( X )` line lists the ones the program has. For everything the engine calls later, because the plugin asked it to
(a console command, an HTTP answer, an event), the plugin gives the engine a function: see *Handlers* below. If a function of your own has the name of an engine function or of an
export, the linker says `Multiple definitions`: rename yours.

Build it and put it in the `plugins` folder of the home path:

```
make -C code/plugin_api/sdk            (once, with any C compiler; writes build/odf-build/odf-build)
build/odf-build/odf-build --client -o plugins/hello.qvm hello.c
```

`--server` builds a server plugin (`odfs.h`). `make OUT=folder` builds `odf-build` into another folder. Every engine release on GitHub also has a ready-made
`odf-build-<system>.zip` (Windows, Linux x86_64 and arm64, macOS arm64) with `odf-build`, the license of lcc and the examples,
so building it yourself is only needed for another system. `odf-build` is one program (one file for each system) that has everything in
itself: the preprocessor, the compiler and the assembler, and the headers and the C library of the SDK. Nothing else is
needed, neither Python nor the tools of ioquake3. The files it carries are the ones from the time it was built; `--sdk`
or the environment variable `ODF_SDK` name a folder `code/plugin_api` to take them from instead. Other options: `-I`,
`-D`, `--no-lib` (without odf_lib.c), `--map` (the map file of the assembler), `-v` (show the commands).

The first file of a program has to include the entry header; the program is the files in the order given, then the library
(`odf_lib.c`), then the engine functions. The entry header makes `vmMain`, the one function the engine calls, and lists
which of the exports (`OnInit`, `OnDraw3D` ...) the program has. Leave out the ones you do not need.

### A program of several files

`odf-build` compiles and links the `.c` files given on the command line and no others; it does not look for files:

```
odf-build --client -Isrc -o plugins/myplugin.qvm src/main.c src/util.c src/hud.c
```

The file with the entry header goes first, the others in any order. `#include "x.h"` finds headers next to the `.c` file
that includes them; `-I folder` adds other folders. A `*.c` in the command line works where the shell expands it (Linux,
macOS, MSYS2), but then the entry file must still come first: a Makefile such as
`SOURCES := src/main.c $(filter-out src/main.c,$(wildcard src/*.c))` does that. Every file is compiled on its own, so
`static` functions and variables of the same name in different files do not collide; a name that is not `static` and is
defined in two files does (`Multiple definitions`). The plugin repositories (`df-proxymod-plugin`, `df-records-plugin`) are
examples with a Makefile.

On Windows, build `odf-build` itself (`make -C code/plugin_api/sdk`) in an MSYS2 MINGW64 shell with `make` and `gcc`.

The manifest is described by three macros before the entry header: `ODF_PLUGIN_VERSION` (required), `ODF_PLUGIN_CAPS` (the
capabilities it needs, comma separated; it is not loaded when it was not given them) and `ODF_PLUGIN_DESCRIPTION`. The
entry header adds the API version the plugin was written for (a test of the engine can set `ODF_PLUGIN_API` to a string to
pretend to be from another version).

### Libraries

The SDK has libraries that a plugin uses by including their header: `odf_json.h` (a JSON writer and reader, `Jw_*` and `Json_*`)
and `odf_http.h` (`Http_Get`, `Http_PostJSON`: a request, a callback with the status and the answer; server plugins). There is
nothing to list on the command line: `odf-build` looks at the files the preprocessor read, and for every header
`odf_<name>.h` of the SDK that has an `odf_<name>.c` beside it, that file is added to the program (also when the header is
included by another library). A plugin that includes none of them gets none. See `examples/hello_http.c`. The libraries
run inside the sandbox like the rest of the plugin: the engine never parses what a server answers.

### Handlers

Some engine functions take a function of the plugin, a *handler*, that the engine calls when something happens. The type of
the handler is in the header (`odfcCommandHandler_t`, `odfsHttpHandler_t` ...) with the arguments the engine passes:

```c
static void CmdHello( int argc, const char **argv ) { Print( argc > 1 ? argv[1] : "hello" ); }
CommandRegister( "hello", CmdHello );                 /* client: the console command "hello", like main( argc, argv ) */

static void OnRank( int clientNum, int serial, const char *args ) { ... }
ChatCommandRegister( "rank", OnRank );                /* server: "!rank vq3" in the chat */
EventSubscribe( "player_finish", OnFinish );          /* server: an event, data is an info string */
HttpRequest( ODF_HTTP_GET, url, NULL, NULL, 0, 5000, OnAnswer, myIndex );   /* server: myIndex comes back as userData */
```

Each of them returns 0 when the engine refuses (bad name, no handler, a capability the plugin does not have, too many).
A console command handler gets the words of the command line like `main`: `argv[0]` is the name as typed, `argc` counts all of
them (at most 16 words, each cut after 199 characters).
A handler is an ordinary function; the engine can only call the ones the plugin gave it, and the VM only lets a call go to
the start of a function. They are removed when the plugin stops. (`tests/client_plugin/cmd_test.sh` tests the commands.)

## What to know about the language

The compiler is lcc of ioquake3, and the machine is the QVM:

* **C89.** No `inline`, no declarations after statements, no `for ( int i ...`, no compound literals, no initializers of local
  arrays and structs that are not constants. `//` comments work.
* **Integers are 32 bit.** `long long` is accepted but is only 32 bits wide (silently!). There are no 64 bit integers; the API
  passes them as two halves (`accountIDLo`, `accountIDHi`).
* **A `double` is a float** (4 bytes), so `sin` and `sinf` are the same.
* **No heap.** Use static arrays; there is no `malloc`. The memory of a plugin is fixed (16 MB client, 32 MB server at most).
* **No C library of the system.** `odf_lib.c` has what is likely to be needed (see the headers in `libc/`); `snprintf` has
  `%d %i %u %x %X %o %c %s %p %f %%` with flags, width and precision, but no `%e` and `%g`. Missing: `pow`, `exp`, `log`.
* **Static functions and variables with the same name in two files** are fine: the build script keeps them apart.
* **Function pointers** work, also in tables (`static void ( *const handlers[] )( void ) = { ... }`).

## Engine functions

`odfc.h` / `odfs.h` document every function: what it does, its capability, whether it is `cheats only`. They are generated
from `client.api` / `server.api`; to add a function, add a line there and run `python gen_api.py` (only whoever changes
the API needs Python: the generated files are in the repository). The generated dispatcher
in the engine checks the pointers (`outbuf(size)`, `inbuf(size)`, `str`, `in(type, n)` ...) so no engine function has to.

## Testing

* `tests/plugin_vm` : the VM itself against hostile programs (bad pointers, endless loops, divisions, jumps and calls to
  places a program may not go to, a fuzzer that changes programs at random).
* `tests/plugin_api` : the generated dispatcher: capabilities, the cheat rule, pointer checks, floats, 64 bit numbers.
* `tests/plugin_sdk` : this library, compared with the C library of the host.
* `tests/client_plugin`, `tests/server_auth` : the hosts of the engine, with plugins built for the test.
* `tests/client_plugin/smoke.sh plugin.qvm` runs a real client plugin in the host with a made-up engine, compiled and
  interpreted, and says how long a frame takes.
