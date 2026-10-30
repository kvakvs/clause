# Plan 30: Network distribution

Distribution in the runtime and the libraries that depend on it, so a Clause executable is a
distributed Erlang node. Added 2026-10-30 as plan 11's phase DIST and moved here the same day (user
direction). Backlog: F38.

- **Today:** a Clause program is a non-distributed node.
  - The runtime knows only `nonode@nohost` and local identities: `binary_to_term/1` rejects other
    nodes' pids, ports and references (`badarg`), and the `erlang` distribution group
    (`setnode/2,3`, `dist_ctrl_*`, `dmonitor_node/3`, `dist_get_stat/1`) is
    `{unavailable, distribution}`.
  - The libraries answer as OTP does without distribution: `rpc`/`erpc` (`{badrpc, nodedown}`,
    `noconnection`), `net_adm` (`pang`, `names` `{error, address}`), `net_kernel` (`start/1,2` and
    internals declined), `auth` (`nocookie`), `erl_epmd` (no epmd, server declined), `global` and
    `global_group` (one node, locker internals declined), `pg` (no peers), `peer`/`slave`
    (`not_alive`), `pool` (collector declined), `application` (`takeover/2` exits `noproc`, no
    `dist_ac`).
- **Goal:** a Clause executable is a distributed Erlang node that connects to OTP 29 nodes and to
  other Clause nodes over OTP's protocol, and those libraries behave as OTP's distributed ones.
- **Architecture (proposed; DIST0 decides):**
  - Wire compatibility with OTP 29: distribution protocol 6 (handshake 23), OTP 26's mandatory
    flags (`DFLAG_DIST_MANDATORY`), the external term format for control messages and payloads
    (native records with `DFLAG_NATIVE_RECORDS`).
  - OTP's split of work: the runtime keeps this node's identity, one entry per connected node and
    the signal data path (encoding outgoing signals, decoding and delivering incoming ones) behind
    OTP's process-controller interface (`erlang:setnode/2,3`, `dist_ctrl_*`,
    `erts_internal:new_connection/1`). Erlang library code runs epmd, the handshake, `net_kernel`,
    `auth` and the carriers (`inet_tcp_dist` over `gen_tcp`).
  - What other nodes observe stays OTP's: registered names (`net_kernel`, `rex`,
    `global_name_server`, `pg` scopes), message shapes and inter-node protocols, so mixed OTP and
    Clause clusters work.
  - A program without a node name stays exactly as today; distribution starts only with a node
    name option or `net_kernel:start/1,2`.
- **Testing:**
  - Multi-node fixtures: a runner that starts an epmd and several executables (or one executable
    several times with different arguments), then checks each node's stdout and exit status;
    goldens generated once from OTP nodes.
  - Interop cases (a Clause node with an OTP `erl` node) carry label `host_otp`; Clause-only cases
    stay OTP-free.
- **Afterwards:** revisit plan 21's MIM1 (`mnesia`) design, which assumes one node.

## Working rules for plan 30 steps

- Commit each step when its gate is green: subject `[plan30] DISTn. Title`, e.g.
  `[plan30] DIST4. Handshake, TCP carrier and first connections`.
- The plan 11 gate applies (`check-quality` first, then fast CTest; full CTest when the phase
  closes). Steps that change only library code gate on their own goldens and the library checks
  first (AGENTS.md testing strategy).
- A step that grows beyond one reviewable change is split into lettered sub-steps (`DIST5a`,
  `DIST5b`) before coding; prefer bullet lists, nested where needed.
- When a step is done, remove it from this file and add a 1-2 line bullet with its commit hash to
  `30-finished.md` (created with the first finished step, linked from `00-finished.md`).

## Steps

<a id="step-dist0"></a>

## DIST0. Decide the distribution contract

Backlog: F38. Depends on: none. **Decision.**

- Success criteria
  - [ ] `docs/distribution.md` records:
    - scope: TCP over IPv4 and IPv6 first; TLS distribution later (DIST12); left out unless
      selected: code loading over distribution, `erl_boot_server`, remote shells (`-remsh`);
    - the runtime/library split and the optional flags Clause offers (atom cache, fragments);
    - whether user carriers (`-proto_dist` modules over `dist_util` and its record header) are
      supported.
  - [ ] epmd: a Clause-owned epmd started on demand like OTP's `-start_epmd` (a standalone
    daemon program, not inside a node that may exit), and the epmd-less modes
    (`-erl_epmd_port`, a custom `-epmd_module`).
  - [ ] Node start options: runtime options (node name, cookie, hidden, connect-all, carrier,
    epmd module) in the command line, `CLAUSE_FLAGS` and the manifest, and what
    `init:get_argument/1` reports for them (OTP's `sname`, `name`, `setcookie`, `hidden`).
  - [ ] The multi-node fixture runner and golden format.
- Tests
  - [ ] None beyond the gate.

<a id="step-dist1"></a>

## DIST1. External identities in the runtime

Backlog: F38. Depends on: [DIST0](#step-dist0).

- Success criteria
  - [ ] New term kinds under the representations rule: external pid, port and reference (node atom,
    creation, numbers):
    - construction by `binary_to_term/1,2` (`NEW_PID_EXT`, `V4_PORT_EXT`, `NEWER_REFERENCE_EXT`
      of other nodes) and `list_to_pid/1`, `list_to_port/1`, `list_to_ref/1` with a node part;
    - comparison and order as OTP's (local against external included), equality and hashing
      (`phash2/1,2`, map keys, `ets` keys);
    - printing `<N.X.Y>`, `#Port<N.X>`, `#Ref<N.A.B.C>` with OTP's node index;
    - copying in messages, `ets`, `persistent_term` and garbage collection;
    - `node/1`, type tests in guards, and OTP's `badarg` from local-only BIFs
      (`is_process_alive/1`, `process_info/2`).
  - [ ] This node's identity (name, creation) lives in the runtime, starts as `nonode@nohost`/0,
    changes with `setnode`, and local identities survive the change as in OTP (`node(self())`,
    `term_to_binary/1` of local pids follow the new name).
- Tests
  - [ ] Golden `external_identities`: OTP-produced binaries of other nodes' identities decoded,
    ordered, printed, hashed, copied and stored.

<a id="step-dist2"></a>

## DIST2. Connection entries and controller BIFs

Backlog: F38. Depends on: [DIST1](#step-dist1).

- Success criteria
  - [ ] A runtime table with one entry per remote node: state (pending, up, exiting), controller
    (process or port), flags, creation, connection id.
  - [ ] OTP's process-controller interface: `erlang:setnode/2,3`,
    `erts_internal:new_connection/1`, `abort_pending_connection/2`, `get_creation/0`,
    `dynamic_node_name/0,1`, `get_dflags/0`, `dist_ctrl_get_data/1`,
    `dist_ctrl_get_data_notification/1`, `dist_ctrl_put_data/2`, `dist_ctrl_input_handler/2`,
    `dist_ctrl_get_opt/2`, `dist_ctrl_set_opt/3`, `dist_get_stat/1`.
  - [ ] `nodes/0,1,2`, `is_alive/0`, `disconnect_node/1`, `monitor_node/2,3`, `dmonitor_node/3`
    over the table.
  - [ ] The `{unavailable, distribution}` entries of `erlang.terms` and the declined distribution
    group of `erts_internal` become implemented.
- Tests
  - [ ] Unit tests only for entry state transitions source cannot reach; behavior through the
    DIST4/DIST5 goldens.

<a id="step-dist3"></a>

## DIST3. epmd client and Clause's epmd

Backlog: F38. Depends on: [DIST0](#step-dist0).

- Success criteria
  - [ ] `erl_epmd` client over the epmd protocol: `register_node/2,3`, `port_please/2,3`,
    `names/0,1`, `address_please/3`, `listen_port_please/2`; the declined server functions
    (`start/0`, `start_link/0`, `stop/0`, `open/0,1,2`, callbacks) implemented; `ERL_EPMD_PORT`.
  - [ ] Clause's epmd daemon (as DIST0 decides): registrations, `names`, the alive connection
    that unregisters a node when it closes; started when a node starts and none listens.
  - [ ] `net_adm:names/0,1` ask epmd (removes its `differences.md` row).
- Tests
  - [ ] Golden of epmd registrations and `names` with two registered names; interop against OTP's
    `epmd` (`host_otp`).

<a id="step-dist4"></a>

## DIST4. Handshake, TCP carrier and first connections

Backlog: F38. Depends on: [DIST2](#step-dist2), [DIST3](#step-dist3).

- Success criteria
  - [ ] The handshake of protocol 6 in Erlang: `send_name`, challenge and reply digests from the
    cookie, `ok`/`ok_simultaneous`/`nok`/`not_allowed`/`alive`/`named` statuses, simultaneous
    connects, `DFLAG_NAME_ME` dynamic names.
  - [ ] `inet_tcp_dist` and `inet6_tcp_dist` (the `-proto_dist` callback interface: `listen`,
    `accept`, `accept_connection`, `setup`, `close`, `select`, `address`, `is_node_name`), a
    controller process over `gen_tcp` using the `dist_ctrl_*` BIFs, ticks and `net_ticktime`.
  - [ ] First data path: `SEND_SENDER`, `REG_SEND`, `MONITOR_P`/`DEMONITOR_P`/`MONITOR_P_EXIT`
    (enough for `gen:call/3` to `{net_kernel, Node}`), pass-through framing.
  - [ ] Start: the DIST0 node options, `net_kernel:start/1,2`, `stop/0`, `connect_node/1`;
    `auth` cookies (`erlang:set_cookie/1,2`, `get_cookie/0,1`, `~/.erlang.cookie` read or created
    as OTP does); `net_adm:ping/1` answers `pong`.
  - [ ] `net_kernel:start/1,2` leaves `differences.md`.
- Tests
  - [ ] Golden `dist_ping`: two Clause nodes register, ping and exchange messages to pids and to
    `{Name, Node}`; wrong cookie gives `pang` and OTP's log line.
  - [ ] The same with one OTP node (`host_otp`), both directions.

<a id="step-dist5"></a>

## DIST5. Every signal across nodes

Backlog: F38. Depends on: [DIST4](#step-dist4).

- Success criteria
  - [ ] Control messages: `LINK`, `UNLINK_ID`/`UNLINK_ID_ACK`, `EXIT`/`EXIT2` and their payload
    forms, `GROUP_LEADER`, `ALIAS_SEND`, `SPAWN_REQUEST`/`SPAWN_REPLY`, monitors by name
    (`{Name, Node}`).
  - [ ] Remote BIFs: `link/1`, `unlink/1`, `monitor/2,3`, `demonitor/1,2`, `exit/2`,
    `group_leader/2`, `spawn/2,4`, `spawn_link/2,4`, `spawn_monitor/2,4`, `spawn_opt/3,5`,
    `spawn_request/2..5`, aliases reachable from other nodes.
  - [ ] Ordering: signals between a pair of processes arrive in order, as locally.
  - [ ] Connection loss: links exit `noconnection`, monitors deliver `'DOWN'` with
    `noconnection`, node monitors `{nodedown, Node}`.
  - [ ] Auto-connect: signals to an unconnected node wait while `net_kernel` connects
    (`dist_auto_connect`); `erlang:send/3` `noconnect` and `nosuspend`.
  - [ ] Large messages: fragments (`DFLAG_FRAGMENTS`) if DIST0 selects them.
- Tests
  - [ ] Golden `dist_signals`: remote spawn, links, monitors, exits, group leaders, aliases and a
    node going down, between Clause nodes and with an OTP node (`host_otp`).

<a id="step-dist6"></a>

## DIST6. `net_kernel` and `auth` complete

Backlog: F38. Depends on: [DIST5](#step-dist5).

- Success criteria
  - [ ] `net_kernel`: `monitor_nodes/1,2` (with `nodeup`/`nodedown` info options), `allow/1`,
    hidden nodes and `connect_all`, `set_net_ticktime/1,2`, `get_net_ticktime/0`,
    `getopts/2`, `setopts/2`, `get_state/0`, `node_info/1,2`, `nodes_info/0`, `i/0,1`,
    `hidden_connect_node/1`; its declined internals implemented.
  - [ ] `auth`'s declined server functions implemented.
  - [ ] `kernel` environment: `net_ticktime`, `dist_auto_connect`, `connect_all`,
    `inet_dist_listen_min`/`max`, `inet_dist_use_interface`, `inet_dist_listen_options`,
    `inet_dist_connect_options`.
- Tests
  - [ ] Golden `dist_net_kernel`: node monitoring, hidden nodes, `allow/1`, tick time changes and a
    stopped node's `nodedown`.

<a id="step-dist7"></a>

## DIST7. `rpc`, `erpc`, `gen` and I/O across nodes

Backlog: F38. Depends on: [DIST6](#step-dist6).

- Success criteria
  - [ ] `rex` runs while distribution runs; `rpc` remote `call`, `cast`, `multicall`,
    `async_call`, `sbcast`, `abcast`, `eval_everywhere`, `server_call`, `parallel_eval`, `pmap`
    with OTP's `badrpc` results; `erpc` over `spawn_request` with OTP's `noconnection` and
    `timeout` errors.
  - [ ] `gen:call/3,4` to `{Name, Node}`, `gen_server:multi_call/2,3,4` and `abcast/2,3`,
    remote `gen_statem` and `gen_event` calls.
  - [ ] I/O of remote processes reaches their group leaders: output of `rpc:call/4` appears on the
    calling node.
  - [ ] `net_adm`: `world/0,1`, `world_list/1,2`, `host_file/0`, `localhost/0`,
    `dns_hostname/1` with real nodes.
- Tests
  - [ ] Golden `dist_rpc` between Clause nodes and with an OTP node (`host_otp`).

<a id="step-dist8"></a>

## DIST8. `global` and `global_group` across nodes

Backlog: F38. Depends on: [DIST6](#step-dist6).

- Success criteria
  - [ ] OTP's `global` protocol between nodes: replicated registrations, name conflicts resolved by
    `random_exit_name/3`, `random_notify_name/3`, `notify_all_name/3` or user functions, locks
    (`set_lock/1,2,3`, `trans/2,3,4`) over nodes, `sync/0`, overlapping partitions
    (`prevent_overlapping_partitions`); the declined locker internals implemented.
  - [ ] `global_group` with the `global_groups` environment: `own_nodes/0`, `registered_names/1`,
    `send/2,3`, `whereis_name/1,2`, `monitor_nodes/1`, `sync/0`, `info/0`; the declined group
    server implemented.
  - [ ] The `pg`/`global` row of `differences.md` is narrowed to what remains.
- Tests
  - [ ] Golden `dist_global`: registration, conflict resolution, locks and a partitioned node,
    with an OTP node (`host_otp`).

<a id="step-dist9"></a>

## DIST9. `pg` across nodes

Backlog: F38. Depends on: [DIST6](#step-dist6).

- Success criteria
  - [ ] Scope processes discover peers on `nodeup`, exchange memberships, relay joins and leaves,
    drop a node's members on `nodedown`, as OTP's `pg` does on the wire.
  - [ ] `monitor_scope/0,1` and `monitor/1,2,3` notify about remote members.
  - [ ] The `differences.md` row on `pg` and `global` is removed.
- Tests
  - [ ] Golden `dist_pg` between Clause nodes and with an OTP node (`host_otp`).

<a id="step-dist10"></a>

## DIST10. Starting nodes: `peer`, `slave`, `pool`

Backlog: F38. Depends on: [DIST7](#step-dist7).

- Success criteria
  - [ ] A node mode for executables: boot, start distribution and run no entry, so a program can
    start copies of itself as nodes.
  - [ ] `peer`: `start/1`, `start_link/1` (its own executable in node mode, or `exec` programs such
    as OTP's `erl`), connections over distribution, `standard_io` and `{Ip, Port}`, `call/4,5`,
    `cast/4`, `send/3`, `stop/1`, `get_state/1`; refused and declined functions implemented.
  - [ ] `slave` starts nodes the same way; `pool`'s statistics collector per node.
- Tests
  - [ ] Golden `dist_peer`: a program starts peers, calls them, and stops them.

<a id="step-dist11"></a>

## DIST11. Distributed applications

Backlog: F38. Depends on: [DIST8](#step-dist8).

- Success criteria
  - [ ] `dist_ac` with the `distributed` kernel environment, `sync_nodes_mandatory`,
    `sync_nodes_optional`, `sync_nodes_timeout`.
  - [ ] Failover and takeover with OTP's start types (`{failover, Node}`, `{takeover, Node}`),
    `application:takeover/2`, `load/2` with nodes, `permit/2`.
  - [ ] The `application:takeover/2` row of `differences.md` is removed.
- Tests
  - [ ] Golden `dist_applications`: an application fails over to a second node and is taken back.

<a id="step-dist12"></a>

## DIST12. TLS distribution and other carriers

Backlog: F38. Depends on: [DIST5](#step-dist5).

- Success criteria
  - [ ] `inet_tls_dist` and `inet6_tls_dist` over Clause's `ssl` (`-proto_dist inet_tls`,
    `ssl_dist_optfile`, `ssl_dist_opt`), certificate checks as OTP's.
  - [ ] User carriers through the `-proto_dist` interface, if DIST0 selected them.
- Tests
  - [ ] Golden `dist_tls` between Clause nodes and with an OTP node (`host_otp`).

<a id="step-dist13"></a>

## DIST13. Close distribution

Backlog: F38. Depends on: [DIST0](#step-dist0)–[DIST12](#step-dist12).

- Success criteria
  - [ ] `docs/distribution.md`, `docs/libraries/library.md` rows and `differences.md` describe
    the delivered behavior.
  - [ ] Linux and macOS run the multi-node goldens.
  - [ ] Plan 21's MIM1 design updated for several nodes (replicated tables, schema on more than one
    node) or the single-node scope recorded.
- Tests
  - [ ] Full gate and `check-quality-all`.
