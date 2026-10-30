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
  — `PENDING`
