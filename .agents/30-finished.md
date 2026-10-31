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
