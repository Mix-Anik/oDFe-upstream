# Relay server

A local server between the player and a remote DeFRaG server. The player plays on the local server (no ping); the relay
sends his input to the remote server as an ordinary client and brings its answers back. Works with unmodified clients:
start the relay, then `connect 127.0.0.1:<net_port>`.

Status: connect, map (with download), clock, input, commands, server switching, **position correction** (the local game is
rolled back and replayed when the remote server disagrees about where the player is) and the other players are in. Not
yet: health/ammo/race timer from the remote server (needs a hook in the relay's game module), missiles and items of other
players, Linux build. Tested headless against stand-in servers with `tests/udp_delay.py` (delay, jitter, loss), not on a
real server.

## Build

`make BUILD_RELAY=1 BUILD_CLIENT=0 BUILD_SERVER=0` -> `build/<...>/oDFe.relay.<arch>[.exe]`

The relay is the dedicated server built with `-DRELAY` plus the files in this folder. The engine calls into it through
`#ifdef RELAY` hooks (qcommon.h, common.c, sv_main.c, sv_client.c, sv_game.c, sv_init.c). Needs the oDFs `qagame`.

## Use

    oDFe.relay.x64.exe +set fs_game defrag +set net_port 27960 [+relay connect <address> [password]]

Without `+relay connect` the relay runs a lobby map (`relay_idle` (seconds without a player after which the remote server is left, default 60; the relay also says "disconnect" to the remote servers when it is closed or quit),
`relay_lobby`, default q3dm1). The player connects to it and types in his
own console (the engine sends unknown commands to the server):

    relay connect <address[:port]> [password]    play on that server
    relay disconnect                             back to the lobby
    relay status                                 where he plays
    relay settings [all]                         settings of the remote server against the local ones (differences are shown)

Switching is safe: the new server is connected beside the old one and only replaces it when it works. If it does not (wrong
address, players can interact there, map missing) the player is told why and stays where he is. A switch looks like a local
map change.

## Rules

- Only servers with `df_mp_InterferenceOff 3` are used; on others connect directly. Pure servers (`sv_pure 1`) are not supported.
- The map has to exist locally or is downloaded from `dl_source` / `dl_source2` (same cvars and defaults as the client).
- The relay only talks to 127.0.0.1 (other addresses are dropped) and to the remote servers.

## Cvars

`relay_name` `relay_rate` `relay_snaps` `relay_userinfo` (used until a player is there; then his userinfo is used),
`relay_lobby`, `relay_lead` (ms the local clock runs ahead more), `relay_timeout`, `relay_status` (seconds between status
lines), `relay_shownet`, `dl_source`, `dl_source2`, `dl_usebaseq3`. For tests: `relay_testmove`, `relay_testcmds`.

## Files

| File | |
|---|---|
| rl_remote.c | the client side: two connections (active, candidate), handshake, gamestate, snapshots, usercmds, RTT |
| rl_mirror.c | the local server: map, physics cvars, slot, clock, lobby, commands and configstrings both ways |
| rl_download.c | map download |
| rl_reconcile.c | frame history, rollback and replay when the remote server disagrees (uses `sv_state.c`) |
| rl_overlay.c | the other players of the remote server in the local snapshots |
| rl_main.c | `relay` command, entry points |
| tests/udp_delay.py | delay / jitter / loss proxy for testing |

`kill` is run by the local game at once and sent to the remote server; replays after a correction do it again at the same frame. The local clock follows the quickest snapshot of the last ~1.5 s (not the newest one) and moves at most 3% fast or slow.

Cvars of the correction: `relay_reconcile` (1), `relay_epsilon` (units of difference allowed, 0.05), `relay_smooth` (0.5: a small
difference is closed by this fraction at each correction instead of at once), `relay_snap` / `relay_snapVelocity` (8 units /
100 ups: bigger differences are taken over at once), `relay_lead` (ms the local clock runs ahead more, default 4: a usercmd should
reach the remote server just before the frame that runs it). `relay status` shows how often and how much the remote server
corrected the player.

Testing a recorded run: `dfgolden export` (oDFs, testing/dfgolden) writes the usercmds of a golden file as text;
`+set relay_runfile <file>` makes the relay play them for the local player, one per frame.
