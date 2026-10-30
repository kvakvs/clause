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
  split into DIST3a/3b/3c; `epmd_names` and `epmd_interop` goldens identical to OTP — `HASH`
