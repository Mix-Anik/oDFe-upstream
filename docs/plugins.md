# Plugins and player login

## Plugins

A plugin is a **QVM program** (C compiled with q3lcc, the bytecode of the Quake 3 virtual machine) that the engine runs in a sandbox. There are client plugins (draw into the game, read what the game shows) and server plugins (react to events such as a finished run or a login). The same engine runs both kinds; an x86 or x86-64 system runs them compiled to native code (about as fast as native, see `tests/client_plugin/smoke.sh`), any other system interprets them.

**What a plugin cannot do**, whatever its author wrote:

* touch files or the network, or call anything of the system: it has no system calls. A server plugin never gets at the client's files, a client plugin never gets other players' addresses, keys or tokens;
* read or write any memory but its own: every access is masked into it, and the engine checks every pointer, string and size that a plugin passes to a function (the dispatcher is generated from the API files, so no function has to do it by hand);
* use a group of functions ("capability") it was not given. The client plugins get all of them (the player installs them knowingly); the operator of a server lists them per plugin: `+set sv_plugins "records:auth,chat,storage,http=api.example.com"`. The ones that give power over other people's data (`auth`, `ips`, `http`, `rawcmd`) are never given unless named;
* use a function marked `cheat` in the API files when cheats are not allowed: the engine decides (`CheatsEnabled`), not the plugin;
* run for ever: every call has a budget of instructions (5 million for a call, 100 million for the start), and a plugin that uses it up is stopped. The engine work a call asks for is counted as well: the functions that cost more than a step (file operations, HTTP requests, loading a shader or model, big copies, game snapshots, saving a demo) charge an allowance of the same size, so a loop of them cannot keep the engine busy either;
* take the game down: a bad pointer, a division by zero, a call of a function that does not exist, a jump to a place it may not go to, a stack overflow - the plugin is stopped (it is not called again until the game restarts), the game goes on. The console says which plugin and why.

The VM code is `code/qcommon/plugin_vm.c` with small additions in `vm_x86.c` and `vm_interpreted.c` that only apply to plugins (the game's own QVMs run as before). Operators and players are responsible for the plugins they install.

**Writing a plugin** is described in [`code/plugin_api/sdk/README.md`](../code/plugin_api/sdk/README.md): the SDK has the API headers (generated from [`client.api`](../code/plugin_api/client.api) / [`server.api`](../code/plugin_api/server.api), which are also the documentation of every function), a small C library and the build program `odf-build` (one program with the compiler in it; `make -C code/plugin_api/sdk` builds it). The plugin for the oDFe servers (online records) is built in its own project, `df-records-plugin`; the client plugin that replaces the cgame proxymod is `df-proxymod-plugin`.

### Server plugins

* Put the program in `<home path>/plugins/` as `<name>.qvm`. Plugins are loaded from there only, never from downloaded paks.
* Load it with the startup-only cvar `sv_plugins`, a space separated list of `name` or `name:grants`: `+set sv_plugins "records:auth,chat,storage,http=api.example.com"`. It must be set on the command line or in a config that is executed before the server starts (`autoexec.cfg`), not changed later from the console. `http=` lists the hosts (separated by `|`) the plugin may make requests to. `storage=<size>` sets how much disk the plugin's store (`plugin-data/<name>/` in the home path) may use, a number with `k`, `m` or `g` from 1k to 1g (`storage=64m`); a single `storage` gives 8 MB in all and 4 MB a file, with a size one file may use all of it.
* A plugin names the capabilities it needs in its manifest (`caps=`); it is not loaded when the operator did not give them, and the console says which line to use if the operator trusts it.
* `plugins` lists what was loaded and why something is not running; `plugins stats` shows how much time each plugin used.
* Everything runs on the server's main thread, so handlers and `OnFrame` must not block. HTTP requests are made by the engine (libcurl); the handler the plugin gave `HttpRequest` is called with the answer in a later frame.

### Events

A plugin gets events by subscribing to them: `EventSubscribe( "name", handler )` makes the engine call `handler( data )`, `data` being a Quake info string (`\key\value\...`). Version 1 has `player_finish` (sent by the game module, see below), `auth_response`, `client_disconnect` and `map_begin`; the keys are listed in `server.api`. A chat message that starts with `!` (it is still shown as chat) goes to the plugins that registered the command with `ChatCommandRegister( "top", handler )`; the engine calls `handler( clientNum, serial, args )`.

The game module raises events with the `trap_Event( name, data )` syscall. The number is found by name, so a mod works with engines that do not have it:

```c
char value[16];
trap_GetValue( value, sizeof( value ), "trap_Event_ODF" );   // returns the syscall number (710), or false if unknown
```

Event names are lower case words (`a-z`, `0-9`, `_`, at most 31 characters), data is at most 1023 characters; anything else is dropped. Which plugin gets it is not decided by the game: every running plugin that subscribed to the name gets it (events that need a capability, such as `auth_response`, only if the plugin has it).

## Client plugins

A client plugin draws into the game's 3D scene and HUD and can read what the game shows, without replacing the cgame (no `vm_cgame 0`, nothing that depends on the DeFRaG version).

* Put the program in `<home path>/plugins/` (the same folder as the server plugins) as `<name>.qvm`. Every program there that is a client plugin is loaded at startup (a server plugin is skipped without a message), except the plugins that are switched off: their names are in the text file `plugins/disabled`, one per line (blank lines and `#` lines are ignored, names of programs that are gone are dropped). At most 64 programs are looked at; a name is the file name without the extension, letters, digits, `_` and `-`. `clplugins` lists what was found, what runs and why something does not; `clplugins stats` shows the time each plugin used.
* The menu (oDFs, main menu "Plugins") switches plugins on and off while the game runs, through four UI extension syscalls found by name with `trap_GetValue`: `trap_ODF_PluginCount`, `trap_ODF_PluginInfo( index, field, buf, size )` (field 0 name, 1 version, 2 why it is not running, 3 "1" when switched off; returns the state: 0 running, 1 stopped, 2 failed, 3 stopped by the engine, 4 disabled), `trap_ODF_PluginSetEnabled( name, enable )` (saves the choice in `plugins/disabled`) and `trap_ODF_PluginRescan` (starts programs that were put into the folder since). Switching off calls `cgame_shutdown` and `shutdown`, removes the plugin's commands and frees the program; switching on starts it again and calls `map_load` with the map that is loaded.
* A plugin gets `OnMapLoad` (register shaders there), `OnCgameShutdown`, `OnDraw3D` (add polygons and beams before the scene is rendered), `OnDraw2D` (right after the scene, below the game's HUD, and after the frame, on top of it), `OnFilterSound` and `OnDemoSeek`.
* The engine functions are cvars (a plugin reads the cvars it created and the game's `cg_*`, `r_*`, `df_*`, `m_*` settings, nothing else; it can create 256 cvars, more are refused with a message; a cvar belongs to the plugin that created it, unless that plugin and the others that want it all register it with `ODFC_CVAR_SHARED`, e.g. settings several plugins of one family read), `GetSnapshot`, `Trace` (static world), shader and model registration and the drawing calls. The API is read-only apart from the game snapshots below: a plugin cannot press keys, change the view or send commands.
* `GetPlayerState` gives the predicted state of the watched player for the frame being drawn: the position and aim you see now, which is what a plugin needs to work out what a shot fired now would do. The engine does not predict, the game module does, so it is only there when the game module publishes it: the oDFs cgame hands `cg.predictedPlayerState` over once per frame with the extension syscall `trap_ODF_SetPlayerState( ps, viewFlags )` (number 710, found by name with `trap_GetValue`, announced by the read-only cvar `//trap_GetValue`). With any other cgame `GetPlayerState` returns 0 and a plugin has to do without.
* `GetMoveState`, `GetWeaponState`, `GetPlayerStats` (the raw stats and persistant arrays, where DeFRaG keeps the race timer), `PointContents` and `odfcView_t.screenWidth/screenHeight` are what a plugin needs to redo the player's movement for a frame (strafe helpers such as snap and CGaZ) and to draw the ammunition: movement flags, ground, `g_speed`, gravity, flight, the key mask of the watched player, your own latest command (not while a demo plays) and view flags (`ODFC_VIEW_SCOREBOARD`, `ODFC_VIEW_NOT_FIRST_PERSON`: the view is not the player's, so a helper should not draw). They come from the same published state as `GetPlayerState`.
* `GetGhostInfo( slot, out )` and `GetGhostPath( slot, first, count, out )` give the path of a replay ghost of the game (DeFRaG ghost slots 0 to 4): a point per sample of the recording (race time in ms from the start of the run, position, a flag for a teleport). The game module has to hand it over: the oDFs cgame reads the ghost file of a ghost that starts to play a second time, a few hundred frames per client frame, and gives each part to the engine with the extension syscall `trap_ODF_GhostPath( slot, first, count, points, complete )` (number 712, found by name with `trap_GetValue`; `trap_ODF_GhostTime( slot, ms )`, number 713, adds the exact race time of the run the path was recorded from, which `GetGhostInfo` gives as `raceMs`; `first` 0 starts a new path, a later part has to continue where the last ended). The engine keeps the path (up to `ODFC_GHOST_MAX_POINTS`, 131072, points per slot; the store grows as the path comes in) after the ghost has stopped, until the slot gets another ghost, and frees it with the game module. Without a game module that does this both calls return 0. They need no cheats: the path is the one of a ghost the player sees anyway.
* The oDFs cgame publishes the race time in `stats[7]` / `stats[8]` of the player state decoded, as a plain value, also on servers that scramble it (the cgame has to decode it for its own timer); `GetPlayerStats` therefore gives a usable race time in any game of oDFs, not only with cheats. It belongs to the snapshot the predicted state starts from, whose server time is `snapshotTime` (extension syscall `trap_ODF_SetStatsTime`, number 714): a plugin that compares the race time with the predicted position adds `commandTime` minus `snapshotTime`.
* `RegisterModel` and `DrawModel` draw a model in a rectangle of the 2D screen, as the game's own HUD does; `RegisterShaderNoMip` is for pictures that `r_picmip` does not blur; the `ODFC_CVAR_ROM` flag makes a cvar that shows a value and cannot be changed; `CommandRegister( "name", handler )` adds a console command with a handler function of its own, called like `main( argc, argv )` with the words of the command line (a name already used by the engine or another plugin is refused). `filter_sound` is asked about every sound the game module starts on an entity or loops on one, with the `clientNum` of that entity from the snapshots seen so far, and can drop it. A plugin can only take sounds away from the player, so this is not limited by cheats.
* `OnDemoSeek( fromTime, toTime )` says that the position of the game was changed: the demo that is playing was moved (seek, rewind, scrub with `+mouseseek`), or a game snapshot was applied (see below). Time can go backwards, so a plugin that keeps history (graphs, the run it follows) should drop what it kept after `toTime`.
* **Game snapshots** (`SnapshotCreate`, `SnapshotApply`, `SnapshotFree`, capability `state`, cheats only): `SnapshotCreate( 0 )` saves the whole state of the running game and returns a handle; `SnapshotApply( handle, 0 )` puts the game back as it was, with everything in it: the player's position, speed, weapons and race timer, but also every entity, trigger, item, mover and door, the random numbers of the game module, and the server clock. Only for a local game (`devmap`, no other player) with a QVM game module, and for a cgame that takes time changes (the oDFs one: `CG_ODF_TIME_CHANGE`). A snapshot is stored as the difference to the first one of the game, about a KB; `sv_stateMemory` (MB, default 32) limits the total. Handles are valid until the game module is shut down or restarted (map change, `map_restart`) or the plugin stops. `SnapshotApply` returns 1 when it will happen, and it is made at the start of the next client frame (never inside a call of the plugin); the first snapshot of the new timeline resets the client clock, and the plugins are told with `OnDemoSeek( oldTime, newTime )`. The server runs no usercmds of the client between the apply and that moment. `tests/game_state` tests the difference code; `sv_state.c` explains the rest. Applying a snapshot sends the line `[SNAPSHOT LOAD]` as a server command (`print`) to the player, so it is in a demo that is being recorded.
* **Demos and game snapshots:** a demo that is recorded in a local game with cheats does not hold the server time jump that `SnapshotApply` makes. While recording, the messages go to the file `demos/temp/segments.store` in segments, one for each snapshot made (`SnapshotCreate`); loading one drops the segment that was written since, and what follows is a new segment after the checkpoint. When the recording stops (`stoprecord`, or `savedemo <name>`, which goes on recording) the demo file is the route: the chain of segments from the start to the end, with message and server command numbers made continuous where a segment does not follow the one before. Time in that demo never goes back, so playback, seeking and the timeline need nothing special. The gamestate at the start of the demo has the configstring 1023 (`\cheats\1\route\1`), which the game never writes: the demo says it was recorded with cheats even if `sv_cheats` is turned off later in it. The store file is deleted when the game ends; `cl_demoRoute` (read-only) is 1 while a recording works this way. `tests/demo_route` tests it, `cl_demoroute.h` explains it.
* **Demo calls** (capability `demo`, `DemoRecord`, `DemoStop( discard )`, `DemoSave( name )` need cheats, `DemoState` does not): a plugin records a demo of its own (`demos/temp/plugin.dm_*`, silent), and saves what was recorded under a name, which goes on recording. It can only stop and save the recording it started, not one the player started with `record`, and `DemoSave` only works for a recording made of segments (above). The name is checked: folders are allowed in `demos/`, letters, digits and `_ - . ( ) [ ] + , #`, no `..`, no leading dot or slash; the engine adds `[tas]` when the name has no word `tas` in it, so a demo of a game with cheats is always named so. This is what lets a plugin do an autorecord with cheats (the segment plugin does).

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
sh tests/demo_route/run.sh      # demos of a game with checkpoints: segments, the route, renumbered messages
sh tests/client_plugin/run.sh   # the real cl_plugin.c against a mock plugin: draw phases, cheat rules, snapshots, a stopped plugin
sh tests/client_plugin/smoke.sh plugin.qvm [frames] [cvar=value ...]   # a real plugin in the real host, compiled and interpreted, with timing
```
