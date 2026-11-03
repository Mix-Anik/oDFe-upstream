# Plugins and player login

## Plugins

A plugin is a **QVM program** (C compiled with q3lcc, the bytecode of the Quake 3 virtual machine) that the engine runs in a sandbox. There are client plugins (draw into the game, read what the game shows) and server plugins (react to events such as a finished run or a login). The same engine runs both kinds; an x86 or x86-64 system runs them compiled to native code (about as fast as native, see `tests/client_plugin/smoke.sh`), any other system interprets them.

**What a plugin cannot do**, whatever its author wrote:

* touch files or the network, or call anything of the system: it has no system calls. A server plugin never gets at the client's files, a client plugin never gets other players' addresses, keys or tokens;
* read or write any memory but its own: every access is masked into it, and the engine checks every pointer, string and size that a plugin passes to a function (the dispatcher is generated from the API files, so no function has to do it by hand);
* use a group of functions ("capability") it was not given. The client plugins get all of them (the player installs them knowingly); the operator of a server lists them per plugin: `+set sv_plugins "records:auth,chat,storage,http=api.example.com"`. The ones that give power over other people's data (`auth`, `ips`, `http`, `rawcmd`) are never given unless named;
* use a function marked `cheat` in the API files when cheats are not allowed: the engine decides (`CheatsEnabled`), not the plugin;
* run for ever: every call has a budget of instructions (5 million for a call, 100 million for the start), and a plugin that uses it up is stopped;
* take the game down: a bad pointer, a division by zero, a call of a function that does not exist, a jump to a place it may not go to, a stack overflow - the plugin is stopped (it is not called again until the game restarts), the game goes on. The console says which plugin and why.

The VM code is `code/qcommon/plugin_vm.c` with small additions in `vm_x86.c` and `vm_interpreted.c` that only apply to plugins (the game's own QVMs run as before). Operators and players are responsible for the plugins they install.

**Writing a plugin** is described in [`code/plugin_api/sdk/README.md`](../code/plugin_api/sdk/README.md): the SDK has the API headers (generated from [`client.api`](../code/plugin_api/client.api) / [`server.api`](../code/plugin_api/server.api), which are also the documentation of every function), a small C library and the build program `odf-build` (a single C file, `odf-build.c`). The plugin for the oDFe servers (online records) is built in its own project, `df-records-plugin`; the client plugin that replaces the cgame proxymod is `df-proxymod-plugin`.

### Server plugins

* Put the program in `<home path>/plugins/` as `<name>.qvm`. Plugins are loaded from there only, never from downloaded paks.
* Load it with the startup-only cvar `sv_plugins`, a space separated list of `name` or `name:grants`: `+set sv_plugins "records:auth,chat,storage,http=api.example.com"`. It must be set on the command line or in a config that is executed before the server starts (`autoexec.cfg`), not changed later from the console. `http=` lists the hosts (separated by `|`) the plugin may make requests to. `storage=<size>` sets how much disk the plugin's store (`plugin-data/<name>/` in the home path) may use, a number with `k`, `m` or `g` from 1k to 1g (`storage=64m`); a single `storage` gives 8 MB in all and 4 MB a file, with a size one file may use all of it.
* A plugin names the capabilities it needs in its manifest (`caps=`); it is not loaded when the operator did not give them, and the console says which line to use if the operator trusts it.
* `plugins` lists what was loaded and why something is not running; `plugins stats` shows how much time each plugin used.
* Everything runs on the server's main thread, so event handlers and `OnFrame` must not block. HTTP requests are made by the engine (libcurl) and answered with an event (`OnHttp`) in a later frame.

### Events

Events are delivered to the plugin's `OnEvent( name, data )`, `data` being a Quake info string (`\key\value\...`). Version 1 has `player_finish` (sent by the game module, see below), `auth_response`, `chat_command` (a chat message starting with `!`, still shown as chat), `client_disconnect` and `map_begin`; the keys are listed in `server.api`.

The game module raises events with the `trap_Event( name, data )` syscall. The number is found by name, so a mod works with engines that do not have it:

```c
char value[16];
trap_GetValue( value, sizeof( value ), "trap_Event_ODF" );   // returns the syscall number (710), or false if unknown
```

Event names are lower case words (`a-z`, `0-9`, `_`, at most 31 characters), data is at most 1023 characters; anything else is dropped. Which plugin gets it is not decided by the game: every running plugin gets every event.

## Client plugins

A client plugin draws into the game's 3D scene and HUD and can read what the game shows, without replacing the cgame (no `vm_cgame 0`, nothing that depends on the DeFRaG version).

* Put the program in `<home path>/plugins/` (the same folder as the server plugins) as `<name>.qvm`. Every program there that is a client plugin is loaded at startup (a server plugin is skipped without a message), except the plugins that are switched off: their names are in the text file `plugins/disabled`, one per line (blank lines and `#` lines are ignored, names of programs that are gone are dropped). At most 64 programs are looked at; a name is the file name without the extension, letters, digits, `_` and `-`. `clplugins` lists what was found, what runs and why something does not; `clplugins stats` shows the time each plugin used.
* The menu (oDFs, main menu "Plugins") switches plugins on and off while the game runs, through four UI extension syscalls found by name with `trap_GetValue`: `trap_ODF_PluginCount`, `trap_ODF_PluginInfo( index, field, buf, size )` (field 0 name, 1 version, 2 why it is not running, 3 "1" when switched off; returns the state: 0 running, 1 stopped, 2 failed, 3 stopped by the engine, 4 disabled), `trap_ODF_PluginSetEnabled( name, enable )` (saves the choice in `plugins/disabled`) and `trap_ODF_PluginRescan` (starts programs that were put into the folder since). Switching off calls `cgame_shutdown` and `shutdown`, removes the plugin's commands and frees the program; switching on starts it again and calls `map_load` with the map that is loaded.
* A plugin gets `OnMapLoad` (register shaders there), `OnCgameShutdown`, `OnDraw3D` (add polygons and beams before the scene is rendered), `OnDraw2D` (right after the scene, below the game's HUD, and after the frame, on top of it), `OnCommand` (its console commands), `OnFilterSound` and `OnDemoSeek`.
* The engine functions are cvars (a plugin reads the cvars it created and the game's `cg_*`, `r_*`, `df_*`, `m_*` settings, nothing else), `GetSnapshot`, `Trace` (static world), shader and model registration and the drawing calls. The API is read-only: a plugin cannot press keys, change the view or send commands.
* `GetPlayerState` gives the predicted state of the watched player for the frame being drawn: the position and aim you see now, which is what a plugin needs to work out what a shot fired now would do. The engine does not predict, the game module does, so it is only there when the game module publishes it: the oDFs cgame hands `cg.predictedPlayerState` over once per frame with the extension syscall `trap_ODF_SetPlayerState( ps, viewFlags )` (number 710, found by name with `trap_GetValue`, announced by the read-only cvar `//trap_GetValue`). With any other cgame `GetPlayerState` returns 0 and a plugin has to do without.
* `GetMoveState`, `GetWeaponState`, `PointContents` and `odfcView_t.screenWidth/screenHeight` are what a plugin needs to redo the player's movement for a frame (strafe helpers such as snap and CGaZ) and to draw the ammunition: movement flags, ground, `g_speed`, gravity, flight, the key mask of the watched player, your own latest command (not while a demo plays) and view flags (`ODFC_VIEW_SCOREBOARD`, `ODFC_VIEW_NOT_FIRST_PERSON`: the view is not the player's, so a helper should not draw). They come from the same published state as `GetPlayerState`.
* `RegisterModel` and `DrawModel` draw a model in a rectangle of the 2D screen, as the game's own HUD does; `RegisterShaderNoMip` is for pictures that `r_picmip` does not blur; the `ODFC_CVAR_ROM` flag makes a cvar that shows a value and cannot be changed; `CommandRegister`, `CommandArgc`, `CommandArgv` and the `command` export are console commands (a name already used by the engine or another plugin is refused); `odf_cmd.h` of the SDK gives each command its own handler function (`AddCommand( "name", Handler )`) instead of one `command( name )` function with a `strcmp` chain. `filter_sound` is asked about every sound the game module starts on an entity or loops on one, with the `clientNum` of that entity from the snapshots seen so far, and can drop it. A plugin can only take sounds away from the player, so this is not limited by cheats.
* `OnDemoSeek( fromTime, toTime )` says that the demo that is playing was moved (seek, rewind, scrub with `+mouseseek`). Time can go backwards, so a plugin that keeps history (graphs, the run it follows) should drop what it kept after `toTime`. Not called outside demos.

### Cheats

What a plugin can see depends on whether cheats are allowed: playing a demo, or being connected to a server with `sv_cheats 1` (`devmap`). The engine decides, a plugin cannot change it (`CheatsEnabled()` only tells it).

* Without cheats `GetSnapshot` lists the missiles of the watched player and nothing else (no other players, no items). This is enough for helpers like rocket and grenade paths.
* With cheats it lists every entity of the snapshot.
* A function that gives an unfair advantage on a normal server is marked `cheat` in `client.api`: the generated dispatcher makes it do nothing and return 0 unless cheats are allowed, so the plugin author cannot forget the check or get around it.

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

## Updater (oDFs mod and the engine)

`cl_update.c` updates the oDFs mod pk3 `zzz-defrag_vm.pk3` and the engine itself; the menu part lives in oDFs `ui_menu.c`. The two run as independent jobs with the same code and the same commands and cvars, `odfs_*` for the mod and `odfe_*` for the engine. It only talks to `https://api.github.com/repos/<repo>/releases/latest` and `https://github.com/<repo>/releases/download/<tag>/<file>[.sha512]`; the repository is the startup-only cvar `odfs_update_repo` (default `Mix-Anik/oDFs`) or `odfe_update_repo` (default `Mix-Anik/oDFe-upstream`), the tag must be `vMAJOR.MINOR.PATCH` and the file name is fixed (the pk3 name, or for the engine the zip of the platform).

* `odfs_checkupdate <installed version number> [auto]` and `odfe_checkupdate [auto]` ask for the latest release (`auto` is skipped when `odfs_update_check` / `odfe_update_check` is 0). The engine knows its own version (`odfe_version`, from `code/qcommon/odfe_version.h`) and looks once by itself at startup. `odfs_update` / `odfe_update` download the file, check the SHA-512 from the release's `.sha512` file and install it.
* Results for menus, all read only (`odfs_update_*` and `odfe_update_*`): `_state` (empty, `checking`, `uptodate`, `available`, `downloading`, `ready`, `error`, `checkfailed`, `unsupported`; a failed check, for example no release yet or no network, is meant to stay invisible), `_latest` (`2.1.0`), `_latest_num` (`20100`), `_progress` (0-100), `_error`.
* Mod: the pk3 is left as `zzz-defrag_vm.pk3.new` next to the loaded mod pk3 and swapped in at the next start, before any pk3 is opened (a loaded pk3 is locked on Windows): older `zz*defrag_vm*.pk3` files of that folder are removed and the `.new` file becomes `zzz-defrag_vm.pk3`. If an old file cannot be removed the `.new` file stays and is tried again at the following start.
* Engine: the release zip of the platform (`oDFe-windows-x86_64.zip` for MinGW builds on Windows, `oDFe-linux-x86_64.zip` on Linux, as `build.yml` names them) holds every build of it: OpenGL, Vulkan and the dedicated server. All `oDFe*` files of it are checked (executable, CRC) and then replace the files of that name next to the running executable, or are added there, so a player who has only the OpenGL exe also gets the Vulkan and server ones. Either all are installed or none. On Windows a running exe cannot be overwritten but can be renamed: the old files become `<name>.old` and are deleted at a later start, on Linux the files are renamed over the old ones. The new version runs after a restart. Other builds (MSVC, macOS, 32 bit, ARM) report `unsupported`. There is no checksum for the zip: the connection is HTTPS only and the zip content is checked by its CRC.
* Releasing the engine: raise `ODFE_VERSION_*`, push, then publish a GitHub release tagged `v<version>` (Releases, Draft a new release). The `build` workflow builds that tag and `update-release` attaches the zips. The tag must equal `odfe_version.h`, otherwise players are offered the same update again and again. The rolling `latest` build is a pre-release on purpose, GitHub's "latest release" (which the updater asks for) skips those.

## Tests

Native tests, no game needed (MSYS2 MinGW64 or any gcc; the ones with QVM programs need q3lcc and q3asm, `Q3TOOLS` is the folder they are in):

```
sh tests/auth_proto/run.sh      # hex, signed message, Ed25519; prints an interop vector checked by the Go API
sh tests/plugin_vm/run.sh       # the plugin VM against hostile programs: bad pointers, loops, divisions, forged jumps and calls, a fuzzer
sh tests/plugin_api/run.sh      # the generated dispatcher: capabilities, the cheat rule, pointer checks, floats, 64 bit numbers
sh tests/plugin_sdk/run.sh      # the C library of the SDK against the one of the host
sh tests/plugin_layout/run.sh   # the structs of the API have the same layout for gcc and for q3lcc
sh tests/server_auth/run.sh     # the real sv_auth.c / sv_plugin.c against a mock plugin
sh tests/client_plugin/run.sh   # the real cl_plugin.c against a mock plugin: draw phases, cheat rules, snapshots, a stopped plugin
sh tests/client_plugin/smoke.sh plugin.qvm [frames] [cvar=value ...]   # a real plugin in the real host, compiled and interpreted, with timing
```
