# Server plugins and player login

## Plugins

The plugin for the oDFe servers (online records) is built in its own project, `df-records-plugin`, which carries a copy of the interface header and builds the Linux and Windows libraries.

A plugin is a native shared library that the server loads at startup and that can react to events (a player finished a run, a client answered a login challenge, ...). The interface is the single self-contained header [`code/server/odf_plugin_api.h`](../code/server/odf_plugin_api.h); copy it into your plugin project.

* Put the library in `<home path>/plugins/`: `<name>.dll` on Windows, `<name>.so` elsewhere. Plugins are loaded from there only, never from downloaded paks.
* Load it with the startup-only cvar `sv_plugins`, a space separated list of names: `+set sv_plugins "records"`. It must be set on the command line or in a config that is executed before the server starts (`autoexec.cfg`), not changed later from the console.
* `plugins` lists what was loaded and why something is not running.
* A plugin exports one function, `ODF_PluginEntry`, which receives the engine functions and returns the plugin's `Init`, `Shutdown`, `Frame` and `OnEvent`. Everything runs on the server's main thread, so `OnEvent` and `Frame` must not block. Start network work, return, and pick up the result in a later `Frame`.
* Dependencies of a plugin (for example libcurl) are best linked statically into it. On Windows the system searches the usual DLL paths, not the plugins folder.

### Crash protection

Every call into a plugin is guarded. If it crashes (access violation, `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`) in the plugin's own module, the server logs the plugin, the kind of fault and the offset, stops that plugin for good (until the server restarts), ends the logins it was verifying, and keeps running. It is best effort: it does not cover faults in engine code or in libraries outside the plugin's module, memory corrupted before the crash, crashes in threads the plugin started, endless loops, `exit()`, or `abort()` on Windows. The guard exists for Windows and Linux (the Linux part is written but was not run when this was added). On other platforms, for example macOS, plugins run unguarded and the log says so.

### Events

Events are delivered to `OnEvent( name, data )`, `data` being a Quake info string (`\key\value\...`). Version 1 has `player_finish` (sent by the game module, see below), `auth_response`, `chat_command` (a chat message starting with `!`, still shown as chat), `client_disconnect` and `map_begin`; the keys are listed in the header.

The game module raises events with the `trap_Event( name, data )` syscall. The number is found by name, so a mod works with engines that do not have it:

```c
char value[16];
trap_GetValue( value, sizeof( value ), "trap_Event_ODF" );   // returns the syscall number (710), or false if unknown
```

Event names are lower case words (`a-z`, `0-9`, `_`, at most 31 characters), data is at most 1023 characters; anything else is dropped. Which plugin gets it is not decided by the game: every running plugin gets every event.

## Player login

Players prove who they are with an Ed25519 key instead of a password.

* The client creates its key on first use and keeps the seed in `odf_key.dat` in the home path (hex, one line). Keep that file private. `auth_key` prints the public key, which is what you register on the records website. If the file is damaged the client never overwrites it.
* `cl_login` (default 1) is a userinfo cvar. `login` / `logout` set it to 1 / 0. A server only challenges clients that have it set to 1; a client without the cvar (an old client) is never challenged.
* `cl_loginStatus` (read only) shows the result for menus: empty, `ok <name>`, `off`, `unknown_key`, `failed <reason>`, `unavailable`.

### Protocol

```
server -> client   auth_challenge <64 hex>             random nonce, sent when the client enters the world
client -> server   auth_response <pubkey hex> <signature hex> <server address>
server -> client   auth_state ok <account> | off | unknown_key | failed <reason> | unavailable
```

The client signs `"authv1" || nonce (32 bytes) || server address` where the address is the resolved `ip:port` it connected to (`NET_AdrToStringwPort`). The server does not check the signature itself: it raises the `auth_response` event, the plugin that has `ODF_PLUGIN_FLAG_AUTH` verifies it with the records API and calls `AuthResult`. Because the address is part of the signature, a rogue server cannot reuse a signature it got from a player on another server. The account is kept on the client until it disconnects or sets `cl_login 0`; the plugin reads it with `GetClientInfo`.

The server messages are handled by the engine and never reach the cgame.

## Tests

Native tests, no game needed (MSYS2 MinGW64 or any gcc):

```
sh tests/auth_proto/run.sh      # hex, signed message, Ed25519; prints an interop vector checked by the Go API
sh tests/plugin_guard/run.sh    # crash guard against a plugin that crashes in several ways
sh tests/server_auth/run.sh     # the real sv_auth.c / sv_plugin.c against a mock plugin
```
