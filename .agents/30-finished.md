# Plan 30 completed work (from 2026-10-30)

Compact record of the network distribution plan, one bullet per step;
[the index](00-finished.md) lists the other plans. Open steps are in [30-dist.md](30-dist.md).

- **DIST0** distribution contract in `docs/distribution.md` (user choices: the executable relaunches
  itself as epmd, OTP's `dist_util` carrier interface, atom cache and fragments later, `--env APP
  KEY VALUE` with Clause node options); DIST4 split into DIST4a/DIST4b — `235c0f1b`
- **DIST1** pids, ports and references of other nodes: `ExternalIdentityCell`, guards, `node/1`,
  OTP's order, printing and hashes, ETF in current and older encodings, copies, non-alive answers of
  process builtins; `external_identities` golden identical to OTP — `fde12357`
- **DIST2a** this node's name: `erlang:setnode/2` under a registered `net_kernel` (OTP's checks,
  creation cut and raised to 4), `get_creation/0`, OTP's `is_alive/0`, the name's end with
  net_kernel, every hard-coded `nonode@nohost` reads the name, `monitor_node/2,3` checks in OTP's
  order, exact-creation decoding, name monitors in `process_info/2`; DIST2 split into
  DIST2a/2b/2c; `this_node` golden identical to OTP — `b21f21fb`
- **DIST2b** connection entries: executor `ConnectionTable`, dist handles, `new_connection/1`,
  `abort_pending_connection/2`, `create_dist_channel/3`, `setnode/3`, `get_dflags/0`, `nodes/0,1,2`
  in OTP's order, `process_flag(monitor_nodes, ...)` and `net_kernel:monitor_nodes/1,2`, controller
  and net_kernel ends; DIST2b split again into DIST2b/2c/2d; `connections` golden identical to OTP
  — `c8ccabdd`
- **DIST2c** node monitors and auto-connect: `monitor_node/2,3` counted per entry, `{auto_connect,
  Node, DHandle}` to net_kernel (abort without one), passive connects through
  `clause_dist:passive_monitor/3` and `net_kernel:passive_cnct/1`, `dmonitor_node/3`,
  `list_to_pid/port/ref` of a connected node; `connections` golden's monitors run identical to OTP
  — `701f3409`
- **DIST2d** controller data path: `dist_ctrl_get_data/1`, `dist_ctrl_get_data_notification/1`,
  `dist_ctrl_put_data/2` (iodata through `erts_internal:dist_ctrl_put_data/2`),
  `dist_ctrl_input_handler/2`, `dist_ctrl_get_opt/2`, `dist_ctrl_set_opt/3`, `dist_get_stat/1` with
  OTP's notsup/badarg order; queue filled and data decoded from DIST5; `connections` golden's
  data_path run identical to OTP — `a8c363b3`
- **DIST3a** epmd server and client: shared C++ epmd (`runtime/src/epmd/`, OTP's replies and command
  checks incl. `NOEXISTSTOPPED`, DUMP's NUL bytes, refused registrations kept open), foreground
  `clause-epmd`, `erl_epmd` client and registration server (`stop/0` with `shutdown`), `net_adm:names`
  over `epmd_module`; multi-node golden runs (`runs[].nodes`, per-run epmd, `kind`, `host_otp`); DIST3
  split into DIST3a/3b/3c; `epmd_names` and `epmd_interop` goldens identical to OTP — `ddeea2a3`
- **DIST3b** bundled epmd: the epmd server's objects in `clause_runtime`, run on an executor-owned thread
  (`epmd/bundled`) when `erl_epmd`'s registration finds no epmd (`clause_dist:start_epmd/0`), giving way to an
  epmd that won the port and retrying the connection for a second; `--epmd bundled|none` (`external` not yet);
  runner nodes run `--epmd none`, `"epmd": "bundled"` runs start none; `epmd_names` bundled and race runs
  identical to OTP — `b2a8bf3a`
- **DIST3c** `clause-epmd` beside programs: `clau --epmd-executable` / `epmd_executable = true` link it from the
  `clause_epmd_program` archive as a link companion; `-names`, `-kill` (OTP's messages and statuses), `-daemon`
  (POSIX double fork, Windows `-detached` respawn); DIST3c split into DIST3c/3d; `epmd_executable` golden identical
  to OTP (daemon run authored) — `617a08d6`
- **DIST3d** bundled epmd as the `clause_runtime_epmd` archive behind the startup extension entry
  `CLAUSE_epmd_extension_v1` (`RuntimeExtension::bundled_epmd`); `--no-bundled-epmd` / `bundled_epmd = false` leave
  it out, `--epmd bundled` then acts as `none` after one warning; `--epmd external` finds `clause-epmd`/`epmd` in
  `clause_dist` and starts it detached (`clause_dist:start_daemon/1`); `epmd_executable` golden identical to OTP
  (external run authored) — `9c51b58e`
- **DIST4a1** node start options (`--sname`, `--name`, `--setcookie`, `--nocookie`, `--hidden`, `--proto-dist`,
  `--dist-listen`, `--epmd`) and `--env APP KEY VALUE` as init flags under OTP's names; `application_controller`
  merges `--env` as OTP's `get_cmd_env` (bad kernel value halts with OTP's slogan); golden `runtime_env_options`
  with OTP observing `ERL_FLAGS` (run field `otp_flags`) — `7a2d9a7a`
- **DIST4a2** `vm.args` investigation (no code): erlexec's args file syntax and the flags of real releases mapped,
  user choices recorded as the [args files](../docs/executables.md#args-files) contract (OTP and Clause spellings,
  unsupported and tuning flags ignored with one warning, `-config` and `-env` implemented) — `6ce77a00`
- **DIST4a3a** `--args-file` (`startup/args_file.cpp`): erlexec's splitting, nested files, `-extra`, OTP and Clause
  spellings, `-Flag` init flags, `-env`, `+t`/`+S`/`+sbt`/`+hms` mapped, ignored flags in one warning line;
  `system_info(atom_limit)` reports `--max-atoms`; golden `runtime_args_files` with OTP reading the same file — `320343c9`
- **DIST4a3b** manifest `runtime_options` built into executables (`StartupDescriptor::options`, applied before
  `CLAUSE_FLAGS` and the command line), in `--print-env` and the compile cache key; test `linking_runtime_options`;
  ABI revision reset to 1 beforehand (user decision, `47e69d48`) — `64729881`
- **DIST4a4** `--config FILE` and args files' `-config`: `kernel/clause_config.erl` reads the files as OTP's
  `check_conf` (sys.config includes, OTP's merge and boot failures), `application_controller` merges them over the
  `.app` env and under `--env`; Windows splits multi-file `-config` as erlexec; golden `runtime_config_files` — `1fdb6925`
- **DIST4b1a** `net_kernel:start/1,2` and `stop/0`: `net_kernel` as OTP's gen_server under `erl_distribution`'s
  unlinked `net_sup`, `inet_tcp_dist` listen and epmd registration, OTP's names, options, errors and log lines;
  golden `dist_start` — 803d0144
- **DIST4b1b** `--sname`/`--name` start distribution before the entry (`clause_dist_start:boot/3`, a liveness root,
  `erl_distribution:start_link/0`); distribution modules on demand (`clau --distribution`, manifest `distribution`,
  `runtime_options` naming the node, `net_kernel` callers); golden `dist_node` — 9f1a899c
- **DIST4b1c** cookies: `auth` server under `net_sup` (`--setcookie`, `--nocookie`, `.erlang.cookie` read or
  created), `erlang:get_cookie/0,1` and `set_cookie/1,2` continue in `auth` while alive; multi-node goldens take a
  per-node fresh `home`; golden `dist_cookie` — 7e057e3e
- **DIST4b1d** `kernel_sup` (`kernel:init/1`, `clause_kernel` keeper that halts with OTP's slogan) at the first use
  of `rex`, `global_name_server`, `global_group` or distribution; `net_sup` static child, `net_sup_dynamic` via
  `start_child`; `global_group` server; golden `kernel_sup` — 2a091875
- **DIST4b1e** `exclude_kernel_services` / `--exclude-kernel-service`: generated `clause_kernel_services` lists the
  included services for `kernel:init/1` and links their modules; excluded `global`/`rpc` start unlinked at first use;
  docs and template say it saves processes more than size (gen_server links global and rpc) — 518b781f
- **DIST4b2a** handshake and first connections: `dist_util` (protocol 6 with status `ok`, cookies' MD5 digests,
  mandatory flags, ticks), `dist.hrl`/`dist_util.hrl`, `inet_tcp_dist` accept/setup with a controller process,
  `net_kernel` connections, ticker and `disconnect/1` (`erlang:disconnect_node/1` over it); golden `dist_connect` — a9dac44c
- **DIST4b2b** handshake statuses and dynamic names: simultaneous connects (`ok_simultaneous`/`nok` by name), `alive`
  replacing an old connection (`wait_pending`), `nok` for this node's name, `allow/1`/`allowed/0` with `not_allowed`,
  `named:` dynamic names restarting `net_sup` when lost; runtime `wake()` no longer queues an ended process (crash
  under simultaneous connects); golden `dist_connect` runs synchronized through epmd marker names — e403d9e6
- **DIST4b2c** IPv6 carrier and carrier options: `inet6_tcp_dist` over `inet_tcp_dist`'s `gen_*` driver functions
  and `fam_address/1`, `fam_select/2`; `merge_options/2,3` as observed, listen/connect kernel options under the
  carrier's own (a family there is `badarg`), listen-socket `setopts/2`/`getopts/2`; golden `dist_connect` IPv6 run — 54959c19
- **DIST4b3a** Messages across nodes: sends to another node's pid (`SEND_SENDER`, `SEND` while pending),
  `{Name, Node}` and alias as pass-through packets on the connection (auto-connect, queued while pending, OTP's
  pending-byte counts); `dist_ctrl_put_data/2` delivers the message operations and kills the controller on
  unreadable data with OTP's reports; `net_kernel` logs unexpected messages and takes `undefined@Host`; golden
  `dist_ping`; test programs and epmds listen on loopback only, nodes named @localhost — 7305f15a
- **DIST4b3b1** Process monitors and ping: `MONITOR_P`/`DEMONITOR_P`/`MONITOR_P_EXIT` (payload form with
  `EXIT_PAYLOAD`) by pid and by name in `scheduler/remote_monitors.cpp`, `'DOWN'` `noconnection` on connection loss
  and at once when not alive, monitor signals checked as `dist.c`; `net_adm:ping/1` through `net_kernel`'s
  `{is_auth, Node}`; golden `dist_ping` (fake-connection wire checks, pings, monitors, gen calls) — 51436b89
- **DIST4b3b2** The program's end: OTP 29's `halt` runs no process during its flush and kills the distribution
  ports, losing packets queued just before it (observed), so Clause's immediate end already matches; the wrong
  `differences.md` row went, `distribution.md#messages` records the behavior — e682996b
- **DIST4b4** OTP interoperability: `dist_ping` and `dist_connect` (`host_otp`) repeat their first run with an OTP
  node `a` on `clause-epmd` and an OTP node `b` on OTP's `epmd`; stdout matched OTP's first try, no wire difference
  found; OTP nodes' stderr patterns in OTP's log format, `dist_connect`'s a waits for b's `dcb_ready` marker (an OTP
  node boots slower and missed its nodeup) — b671ac03
- **DIST5a** Links and exit signals: `LINK`, `UNLINK_ID`/`UNLINK_ID_ACK` with OTP's unlinking state, `EXIT`/`EXIT2`
  and payload forms both ways (`scheduler/remote_links.cpp`), noconnection on connection loss and when not alive,
  noproc for no process or an earlier incarnation, exits as port events; golden `dist_signals` part one — b3e33a15
- **DIST5b1** Group leaders: another node's pid as a process's group leader (a buffer in `Signals`, inherited by
  spawns), `group_leader/2` of another node's pid as `GROUP_LEADER`, the other node's `GROUP_LEADER` taken — eded4f16
- **DIST5b2** Spawning on other nodes: `spawn_request/2..5` as `SPAWN_REQUEST` with pending requests until
  `SPAWN_REPLY` (`scheduler/remote_spawns.cpp`, `builtins/erlang/dist_spawns.cpp`), served requests' children,
  abandoned and orphaned requests as OTP 29, blocking spawns in `clause_dist_spawn:spawn/5`; continuations of
  unqualified auto-imported calls; golden `dist_signals` part two — eded4f16
- **DIST5c** Signal order (pending and up connections, between nodes), aliases and send options checked across nodes;
  `dist_signals` with OTP 29 nodes as a and as b (`host_otp`), no runtime change needed; DIST5 closed, follow-ups
  in the plan — d0e00ea5
- **DIST5d** Funs across nodes: local funs as OTP's `NEW_FUN_EXT` (Clause's uniq = MD5 of the module name),
  decoded to the program's fun or a foreign one the code server keeps (prints, compares, encodes and fails as OTP's
  stubs), in messages, exit reasons and spawns; goldens `external_funs`, `dist_funs` (`host_otp`) — 6f335252
- **DIST5e** Other nodes' references in `spawn_request_abandon/1` and `demonitor/1,2`, `UNLINK_ID` ids below 2^64,
  the useless pid's `** Can not start ... **` warning (`clause_dist_spawn:crasher/6`); `dist_signals` extended — 6f335252
- **DIST6a** `net_kernel` complete: tick time changes (aux ticker, `{ongoing_change_to, T}`), `node_info/1,2`,
  `nodes_info/0`, `i/0,1`, `getopts/2`/`setopts/2` (also `new`; `mf_setopts`/`mf_getopts` in `inet_tcp_dist`),
  `dist_auto_connect once`, passive connects, `net_setuptime`, `net_tickintensity`, `verbose/1`, `kernel_apply/3`,
  the old spawn protocol; `dist_util` ticks every idle interval as OTP; golden `dist_net_kernel` with OTP nodes both
  ways — b415095e
- **DIST6b** `auth` as OTP's server (requests, `print/3`, `sync_cookie`, cookie-file checks, configuration-directory
  fallback, `--setcookie` before `--nocookie`); `global` checks `connect_all` and `prevent_overlapping_partitions`;
  golden `dist_cookie` extended — 7dbfe4af
- **DIST7a** `rpc` and `erpc` across nodes: erpc requests and casts as spawn requests on the called node, rex's
  block calls, server calls and broadcasts over `[node() | nodes()]`, OTP's `{From, {call, ...}}`; golden `dist_rpc`
  with OTP nodes both ways (`host_otp`) — 89295469
