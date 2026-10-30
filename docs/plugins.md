# Plugins and player login

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

## Client plugins

A client plugin is a native library that draws into the game's 3D scene and HUD and can read what the game shows, without replacing the cgame (no `vm_cgame 0`, nothing that depends on the DeFRaG version). The interface is the single self-contained header [`code/client/odf_client_plugin_api.h`](../code/client/odf_client_plugin_api.h).

* Put the library in `<home path>/plugins/` (the same folder as the server plugins) and load it with the startup-only cvar `cl_plugins`, a space separated list of names: `+set cl_plugins "proxymod"`. `clplugins` lists what was loaded and why something is not running.
* The entry point is `ODF_ClientPluginEntry`, so a server plugin cannot be loaded by the client or the other way round. The crash guard of the server plugins protects every call.
* The plugin gets `MapLoad` (register shaders there), `CGameShutdown`, `Draw3D` (add polygons and beams before the scene is rendered) and `Draw2D` (right after the scene, below the game's HUD, and after the frame, on top of it).
* The engine functions are cvars, `GetSnapshot`, `Trace` (static world), shader registration and the drawing calls. The API is read-only: a plugin cannot press keys, change the view or send commands.
* `GetPlayerState` (API version 2) gives the predicted state of the watched player for the frame being drawn: the position and aim you see now, which is what a plugin needs to work out what a shot fired now would do. The engine does not predict, the game module does, so it is only there when the game module publishes it: the oDFs cgame hands `cg.predictedPlayerState` over once per frame with the extension syscall `trap_ODF_SetPlayerState` (number 710, found by name with `trap_GetValue`, announced by the read-only cvar `//trap_GetValue`). With any other cgame `GetPlayerState` returns 0 and a plugin has to do without.
* `GetMoveState`, `PointContents` and `odfcView_t.screenWidth/screenHeight` (API version 3) are what a plugin needs to redo the player's movement for a frame (strafe helpers such as snap and CGaZ): `GetMoveState` has the movement flags, ground, `g_speed`, gravity, flight, the key mask of the watched player, your own latest command (not while a demo plays) and view flags (`ODFC_VIEW_SCOREBOARD`, `ODFC_VIEW_NOT_FIRST_PERSON`: the view is not the player's, so a helper should not draw). `PointContents` is the contents of the static world at a point (water). `GetMoveState` comes from the same published state as `GetPlayerState`: the syscall `trap_ODF_SetPlayerState( ps, viewFlags )` has the view flags as its second argument.

* API version 4 adds what the HUD of the cgame proxymod needs: `GetWeaponState` (weapons and ammunition, from the same published state), `RegisterModel` and `DrawModel` (a model in a rectangle of the 2D screen, as the game's own HUD draws models), `RegisterShaderNoMip` (pictures that `r_picmip` does not blur), the `ODFC_CVAR_ROM` flag (a cvar that shows a value and cannot be changed), console commands (`CommandRegister`, `CommandArgc`, `CommandArgv`, and the plugin's `Command` export; a name already used by the engine or another plugin is refused) and the plugin's `FilterSound` export: it is asked about every sound the game module starts on an entity or loops on one, with the `clientNum` of that entity from the snapshots seen so far, and can drop it. A plugin can only take sounds away from the player, so this is not limited by cheats. A plugin built for an older version keeps working (its export table is shorter and the new exports are not called).

### Cheats

What a plugin can see depends on whether cheats are allowed: playing a demo, or being connected to a server with `sv_cheats 1` (`devmap`). The engine decides, a plugin cannot change it (`CheatsEnabled()` only tells it).

* Without cheats `GetSnapshot` lists the missiles of the watched player and nothing else (no other players, no items). This is enough for helpers like rocket and grenade paths.
* With cheats it lists every entity of the snapshot.
* New calls that give an unfair advantage on a normal server are to be added behind the same rule: without cheats they do nothing.

`GetSnapshot` returns the snapshot the game itself is showing at that moment (the newest one that is not ahead of the view time), with the player state as the server sent it, not the client's prediction.

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

## Mod updater

`cl_update.c` lets the oDFs main menu update `zzz-defrag_vm.pk3` (the menu part lives in oDFs `ui_menu.c`). It only talks to `https://api.github.com/repos/<odfs_update_repo>/releases/latest` and `https://github.com/<odfs_update_repo>/releases/download/<tag>/zzz-defrag_vm.pk3[.sha512]`; the repository is the startup-only cvar `odfs_update_repo` (default `Mix-Anik/oDFs`), the tag must be `vMAJOR.MINOR.PATCH` and the file name is fixed.

* `odfs_checkupdate <installed version number> [auto]` asks for the latest release (`auto` is skipped when `odfs_update_check` is 0). `odfs_update` downloads it, checks the SHA-512 from the release's `.sha512` file and the pk3 signature, and leaves it as `zzz-defrag_vm.pk3.new` next to the loaded mod pk3.
* Results for menus, all read only: `odfs_update_state` (empty, `checking`, `uptodate`, `available`, `downloading`, `ready`, `error`, `checkfailed`; a failed check, for example no release yet or no network, is meant to stay invisible), `odfs_update_latest` (`2.1.0`), `odfs_update_latest_num` (`20100`), `odfs_update_progress` (0-100), `odfs_update_error`.
* The swap happens at the next start, before any pk3 is opened (a loaded pk3 is locked on Windows): older `zz*defrag_vm*.pk3` files of that folder are removed and the `.new` file becomes `zzz-defrag_vm.pk3`. If an old file cannot be removed the `.new` file stays and is tried again at the following start.

## Tests

Native tests, no game needed (MSYS2 MinGW64 or any gcc):

```
sh tests/auth_proto/run.sh      # hex, signed message, Ed25519; prints an interop vector checked by the Go API
sh tests/plugin_guard/run.sh    # crash guard against a plugin that crashes in several ways
sh tests/server_auth/run.sh     # the real sv_auth.c / sv_plugin.c against a mock plugin
sh tests/client_plugin/run.sh   # the real cl_plugin.c against a mock plugin: draw phases, cheat rules, snapshots, crash
```
