# Plan 30: Network distribution

Distribution in the runtime and the libraries that depend on it, so a Clause executable is a
distributed Erlang node. Added 2026-10-30 as plan 11's phase DIST and moved here the same day (user
direction); formerly backlog item F38.

- **Today:** a Clause program is a non-distributed node until `net_kernel:start/1,2` (DIST4b1a) or
  `--sname`/`--name` (DIST4b1b), with OTP's cookies (DIST4b1c), under `kernel_sup` (DIST4b1d).
  - The runtime has other nodes' pids, ports and references as terms (DIST1), a nameable node
    (`erlang:setnode/2`, DIST2a), connection entries with node status messages (`setnode/3`,
    DIST2b), node monitors with auto-connect (DIST2c) and the controller data path (`dist_ctrl_*`,
    `dist_get_stat/1`, DIST2d), but moves no signal: the output queue stays empty and put data is
    dropped until DIST5 encodes and decodes signals.
  - Clause's epmd server runs as `clause-epmd`; `erl_epmd` and `net_adm:names/0,1` speak to any
    epmd (DIST3a); a registration that finds no epmd opens the program's bundled epmd under
    `--epmd bundled` (default) or fails under `--epmd none` (DIST3b); `clau --epmd-executable`
    writes `clause-epmd` with `-daemon`, `-names` and `-kill` beside programs (DIST3c);
    `--epmd external` starts an epmd program and `--no-bundled-epmd` leaves the bundled epmd out
    (DIST3d).
  - The node start options and `--env APP KEY VALUE` are init flags under OTP's names, and
    `application_controller` applies `--env` as OTP's `-App Key Value` (DIST4a1); `--args-file` reads
    a release's `vm.args` as the [args files](../docs/executables.md#args-files) contract says (DIST4a2, DIST4a3a); the manifest's
    `runtime_options` are built into executables as their defaults (DIST4a3b); `--config` and `-config` read
    `sys.config` files as OTP's boot does (DIST4a4).
  - `net_kernel:start/1,2` and `stop/0` start and end distribution in a running program: `erl_distribution`'s
    `net_sup`, `inet_tcp_dist`'s listen and epmd registration, OTP's names, options and errors (DIST4b1a);
    connections come in DIST4b2.
  - `--sname`/`--name` start distribution before the entry through `clause_dist_start:boot/3`; the distribution
    modules link with `clau --distribution`, manifest `distribution = true` or `runtime_options` naming the node,
    or a call of `net_kernel` (DIST4b1b).
  - `auth` under `net_sup` keeps the cookies (`--setcookie`, `--nocookie`, `~/.erlang.cookie` read or created);
    `erlang:get_cookie/0,1` and `set_cookie/1,2` continue in it while the node is alive (DIST4b1c).
  - `kernel_sup` (`kernel:init/1`) starts at the first use of `rex`, `global_name_server`, `global_group` or
    distribution, with OTP's children Clause has; its end halts the program as OTP's node (DIST4b1d); projects
    exclude services with `exclude_kernel_services` or `--exclude-kernel-service` (DIST4b1e).
  - The other libraries answer as OTP does without distribution: `rpc`/`erpc` (`{badrpc, nodedown}`,
    `noconnection`), `net_adm` (`pang`), `net_kernel` (internals declined), `auth` (`nocookie` until started), `global` and
    `global_group` (one node, locker internals declined), `pg` (no peers), `peer`/`slave`
    (`not_alive`), `pool` (collector declined), `application` (`takeover/2` exits `noproc`, no
    `dist_ac`).
- **Goal:** a Clause executable is a distributed Erlang node that connects to OTP 29 nodes and to
  other Clause nodes over OTP's protocol, and those libraries behave as OTP's distributed ones.
- **Contract:** [distribution.md](../docs/distribution.md), decided in DIST0 (2026-10-30, user
  choices):
  - OTP 29 wire compatibility (protocol 6, OTP 26's mandatory flags); OTP's split: the runtime
    moves signals behind the process-controller interface, Erlang library code connects.
  - epmd (replanned 2026-10-30, user): a bundled epmd inside the program by default, an optional
    standalone `clause-epmd` executable, and `--epmd bundled|external|none` choosing what happens
    when no epmd runs.
  - User carriers through OTP's `dist_util` interface and `#hs_data{}` record.
  - Node options `--sname`, `--name`, `--setcookie`, `--hidden`, `--proto-dist`, ... and a general
    `--env APP KEY VALUE`, from the command line, `CLAUSE_FLAGS` and the manifest's
    `runtime_options`.
  - The atom cache and fragments are later optimizations ([Later](#later)).
  - Multi-node goldens with a per-case epmd port; OTP interop cases labeled `host_otp`.
- **Afterwards:** revisit plan 21's MIM1 (`mnesia`) design, which assumes one node.

## Working rules for plan 30 steps

- Commit each step when its gate is green: subject `[plan30] DISTn. Title`, e.g.
  `[plan30] DIST4b. Handshake, TCP carrier and first connections`.
- The plan 11 gate applies (`check-quality` first, then fast CTest; full CTest when the phase
  closes). Steps that change only library code gate on their own goldens and the library checks
  first (AGENTS.md testing strategy).
- A step that grows beyond one reviewable change is split into lettered sub-steps (`DIST5a`,
  `DIST5b`) before coding; prefer bullet lists, nested where needed.
- When a step is done, remove it from this file and add a 1-2 line bullet with its commit hash to
  `30-finished.md` (created with the first finished step, linked from `00-finished.md`).

## Steps


## DIST4b2. Handshake and TCP carrier

Split 2026-10-31 into DIST4b2a (handshake with status `ok`, first connections), DIST4b2b (the other
statuses and dynamic names) and DIST4b2c (IPv6 and carrier options).

## DIST4b2a. Handshake and first connections

Depends on: DIST4b1e ([30-finished](30-finished.md)).

- Success criteria
  - [ ] `kernel/include/dist.hrl` (`DFLAG_*`) and `kernel/include/dist_util.hrl` (`#hs_data{}`, `?shutdown`
        macros) with OTP's names and fields, written by Clause from OTP's documentation.
  - [ ] `dist_util` with OTP's exports: the protocol 6 handshake on both sides with status `ok`
        (`send_name`, challenge, reply and ack digests from `auth`'s cookies, mandatory flags,
        `setnode/3`), the connection supervisor loop with ticks (`net_ticktime`, `net_tick_timeout`);
        other statuses refuse the connection until DIST4b2b.
  - [ ] `inet_tcp_dist`: `accept/1`, `accept_connection/5`, `setup/5` and a controller process over
        `gen_tcp` (handshake requests, then `dist_data` out and input through `dist_ctrl_put_data/2`).
  - [ ] `net_kernel`: the acceptor, pending and up connections, `connect_node/1`,
        `hidden_connect_node/1`, node monitors' auto-connect, the ticker, `disconnect/1` with
        `erlang:disconnect_node/1` over it as OTP's; `nodes/0,1`, `nodeup`/`nodedown` and
        `monitor_node/2` over real connections; a wrong cookie refuses the connection with OTP's log line.
- Tests
  - [ ] Golden `dist_connect`: two Clause nodes connect (visible and hidden), see each other in
        `nodes/0,1`, receive `nodeup`/`nodedown` with reasons, `monitor_node/2`, an idle connection
        stays up over several tick times; a wrong cookie gives `false` and OTP's log line.

## DIST4b2b. Handshake statuses and dynamic names

Depends on: [DIST4b2a](#dist4b2a-handshake-and-first-connections).

- Success criteria
  - [ ] Simultaneous connects (`ok_simultaneous`, `nok`), `alive` with the initiator's `true`/`false`,
        `not_allowed` with `net_kernel:allow/1` and `allowed/0`, `named:` dynamic names
        (`DFLAG_NAME_ME`, a node started without a name takes the first peer's), name clashes.
- Tests
  - [ ] Golden `dist_connect` cases: both ends connect at once, a restarted node meets its old
        connection, `allow/1` refuses a node, a dynamic name.

## DIST4b2c. IPv6 carrier and carrier options

Depends on: [DIST4b2b](#dist4b2b-handshake-statuses-and-dynamic-names).

- Success criteria
  - [ ] `inet6_tcp_dist` over `inet_tcp_dist`'s family-generic functions (`gen_*`, `fam_*`);
        `getopts/2`, `setopts/2`, `merge_options/2,3`, `nodelay/0`, the kernel parameters
        `inet_dist_connect_options` and `dist_nodelay`.
- Tests
  - [ ] Golden `dist_connect` case over `--proto-dist inet6_tcp`.

<a id="step-dist4b3"></a>

## DIST4b3. First data path and ping

Depends on: [DIST4b2c](#dist4b2c-ipv6-carrier-and-carrier-options).

- Success criteria
  - [ ] `SEND_SENDER`, `REG_SEND`, `MONITOR_P`/`DEMONITOR_P`/`MONITOR_P_EXIT` (enough for `gen:call/3` to
        `{net_kernel, Node}`), pass-through framing, the offered flags of
        [protocol](../docs/distribution.md#protocol).
    - Encoded packets join DIST2d's output queue (`DataPath::output`), sending `dist_data` to a
      controller that asked; `dist_get_stat/1`'s pending bytes as OTP counts them.
    - `dist_ctrl_put_data/2` decodes what it takes, drops data of an earlier connection id, and
      for data it cannot read logs OTP's `** Got message from incompatible erlang on channel ...`
      and ends the connection, as observed.
  - [ ] `net_adm:ping/1` answers `pong`; messages to remote pids and to `{Name, Node}`.
- Tests
  - [ ] Golden `dist_ping`: two Clause nodes register, ping and exchange messages to pids and to
        `{Name, Node}`; wrong cookie gives `pang` and OTP's log line.

<a id="step-dist4b4"></a>

## DIST4b4. OTP interoperability

Depends on: [DIST4b3](#step-dist4b3).

- Success criteria
  - [ ] Clause and OTP 29 nodes connect, ping and exchange messages in both directions over OTP's and
        Clause's epmd; whatever differs from OTP's wire behavior found on the way is fixed or recorded.
- Tests
  - [ ] `dist_ping` and `dist_connect` runs with one OTP node (`host_otp`), both directions.

<a id="step-dist5"></a>

## DIST5. Every signal across nodes

Depends on: [DIST4b4](#step-dist4b4).

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
  - [ ] On a node that is not alive, as OTP: `link/1` to another node's pid returns `true` and sends
        `{'EXIT', Pid, noconnection}`, `monitor/2` returns a reference, `group_leader/2` returns
        `true` (DIST1 left them `badarg`, recorded in `differences.md`).
  - [ ] Auto-connect: signals to an unconnected node wait while `net_kernel` connects
        (`dist_auto_connect`); `erlang:send/3` `noconnect` and `nosuspend`.
- Tests
  - [ ] Golden `dist_signals`: remote spawn, links, monitors, exits, group leaders, aliases and a
        node going down, between Clause nodes and with an OTP node (`host_otp`).

<a id="step-dist6"></a>

## DIST6. `net_kernel` and `auth` complete

Depends on: [DIST5](#step-dist5).

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

Depends on: [DIST6](#step-dist6).

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

Depends on: [DIST6](#step-dist6).

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

Depends on: [DIST6](#step-dist6).

- Success criteria
  - [ ] Scope processes discover peers on `nodeup`, exchange memberships, relay joins and leaves,
        drop a node's members on `nodedown`, as OTP's `pg` does on the wire.
  - [ ] `monitor_scope/0,1` and `monitor/1,2,3` notify about remote members.
  - [ ] The `differences.md` row on `pg` and `global` is removed.
- Tests
  - [ ] Golden `dist_pg` between Clause nodes and with an OTP node (`host_otp`).

<a id="step-dist10"></a>

## DIST10. Starting nodes: `peer`, `slave`, `pool`

Depends on: [DIST7](#step-dist7).

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

Depends on: [DIST8](#step-dist8).

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

Depends on: [DIST5](#step-dist5).

- Success criteria
  - [ ] `inet_tls_dist` and `inet6_tls_dist` over Clause's `ssl` (`-proto_dist inet_tls`,
        `ssl_dist_optfile`, `ssl_dist_opt`), certificate checks as OTP's.
  - [ ] A user carrier written against OTP's `dist_util` interface (the carrier guide's
        `gen_tcp`-based example shape, own code) runs with `--proto-dist`.
- Tests
  - [ ] Golden `dist_tls` between Clause nodes and with an OTP node (`host_otp`).
  - [ ] Golden `dist_user_carrier` with the user carrier.

<a id="step-dist13"></a>

## DIST13. Close distribution

Depends on: [DIST2b](#step-dist2b)–[DIST12](#step-dist12).

- Success criteria
  - [ ] `docs/distribution.md`, `docs/libraries/library.md` rows and `differences.md` describe
        the delivered behavior.
  - [ ] Linux and macOS run the multi-node goldens.
  - [ ] Plan 21's MIM1 design updated for several nodes (replicated tables, schema on more than one
        node) or the single-node scope recorded.
- Tests
  - [ ] Full gate and `check-quality-all`.

<a id="later"></a>

## Later (optional optimizations, user 2026-10-30)

- Fragments (`DFLAG_FRAGMENTS`): large messages split so they do not hold a connection; receiving
  and sending.
- The distribution header atom cache (`DFLAG_DIST_HDR_ATOM_CACHE`), with its strict ordering
  needs.
- `DFLAG_NATIVE_RECORDS` once `term_to_binary/1` supports native records; `DFLAG_ALTACT_SIG` if
  Clause gets priority signals.
