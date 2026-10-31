# Plan 30: Network distribution

Distribution in the runtime and the libraries that depend on it, so a Clause executable is a
distributed Erlang node. Added 2026-10-30 as plan 11's phase DIST and moved here the same day (user
direction); formerly backlog item F38.

- **Today:** a Clause program is a non-distributed node.
  - The runtime has other nodes' pids, ports and references as terms (DIST1), a nameable node
    (`erlang:setnode/2`, DIST2a), connection entries with node status messages (`setnode/3`,
    DIST2b), node monitors with auto-connect (DIST2c) and the controller data path (`dist_ctrl_*`,
    `dist_get_stat/1`, DIST2d), but moves no signal: the output queue stays empty and put data is
    dropped until DIST5 encodes and decodes signals.
  - Clause's epmd server runs as `clause-epmd`; `erl_epmd` and `net_adm:names/0,1` speak to any
    epmd (DIST3a); a registration that finds no epmd opens the program's bundled epmd under
    `--epmd bundled` (default) or fails under `--epmd none` (DIST3b); `clau --epmd-executable`
    writes `clause-epmd` with `-daemon`, `-names` and `-kill` beside programs (DIST3c).
  - The other libraries answer as OTP does without distribution: `rpc`/`erpc` (`{badrpc, nodedown}`,
    `noconnection`), `net_adm` (`pang`), `net_kernel` (`start/1,2` and
    internals declined), `auth` (`nocookie`), `global` and
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

<a id="step-dist2"></a>
<a id="step-dist2b"></a>
<a id="step-dist2c"></a>
<a id="step-dist2d"></a>

<a id="step-dist3"></a>
<a id="step-dist3a"></a>

<a id="step-dist3b"></a>

<a id="step-dist3c"></a>
<a id="step-dist3d"></a>

## DIST3d. Programs without the bundled epmd, and the external mode

Depends on: DIST3c ([30-finished](30-finished.md)). Split from DIST3c on 2026-10-31.

- Success criteria
  - [ ] `clau --no-bundled-epmd` and manifest `bundled_epmd = false` leave the bundled epmd out of
        the executable (its objects in an archive beside the runtime that only programs carrying it
        link, e.g. through a startup-descriptor extension entry as the crypto archive's); such a
        program treats `--epmd bundled` as `none` with one warning.
  - [ ] `--epmd external`: start `clause-epmd -daemon` (the program's directory, then `PATH`) or
        OTP's `epmd -daemon` from `PATH` when no epmd listens; distribution fails to start when
        neither is found.
- Tests
  - [ ] Golden of the external mode with `clause-epmd` found beside the program (authored where
        OTP's Windows `epmd -daemon` does not stay up); a program without the bundled epmd under
        `--epmd bundled`.

<a id="step-dist4"></a>
<a id="step-dist4a"></a>

## DIST4a. Node start options

Depends on: DIST0 ([30-finished](30-finished.md)).

- Success criteria
  - [ ] Runtime options of [node start options](../docs/distribution.md#node-start-options):
        `--sname`, `--name`, `--setcookie`, `--nocookie`, `--hidden`, `--proto-dist`,
        `--dist-listen` (`--epmd` is DIST3b's); parsed and kept until DIST4b starts
        distribution with them.
  - [ ] `--env APP KEY VALUE` for every application: the value parsed as a term, applied over the
        `.app` resources before applications start; invalid terms stop the program as other invalid
        runtime options do.
  - [ ] Manifest `runtime_options` in `[targets.options]` built into the executable as defaults;
        precedence manifest < `CLAUSE_FLAGS` < command line.
  - [ ] `init:get_argument/1` and `get_arguments/0` report them under OTP's flag names
        (`sname`, `setcookie`, `kernel` with `[["net_ticktime", "20"]]`, ...); the
        `init:get_arguments/0` row of `differences.md` narrowed.
- Tests
  - [ ] Golden `runtime_env_options`: `--env` values seen by `application:get_env/2` and the
        reported arguments (authored runs for Clause-only spellings, OTP's `-App Key Value` for the
        rest).

<a id="step-dist4b"></a>

## DIST4b. Handshake, TCP carrier and first connections

Depends on: DIST2d and DIST3b ([30-finished](30-finished.md)), [DIST4a](#step-dist4a).

- Success criteria
  - [ ] `dist_util` with OTP's exports and `kernel/include/dist_util.hrl`, `dist.hrl` with OTP's
        `#hs_data{}` fields and `DFLAG_*` macros (written by Clause): the handshake of protocol 6
        (`send_name`, challenge and reply digests from the cookie,
        `ok`/`ok_simultaneous`/`nok`/`not_allowed`/`alive`/`named` statuses, simultaneous connects,
        `DFLAG_NAME_ME` dynamic names), ticks and `net_ticktime`.
  - [ ] `inet_tcp_dist` and `inet6_tcp_dist` over that interface (`listen`, `accept`,
        `accept_connection`, `setup`, `close`, `select`, `address`, `is_node_name`), a controller
        process over `gen_tcp` using the `dist_ctrl_*` BIFs.
  - [ ] First data path: `SEND_SENDER`, `REG_SEND`, `MONITOR_P`/`DEMONITOR_P`/`MONITOR_P_EXIT`
        (enough for `gen:call/3` to `{net_kernel, Node}`), pass-through framing, the offered flags of
        [protocol](../docs/distribution.md#protocol).
    - Encoded packets join DIST2d's output queue (`DataPath::output`), sending `dist_data` to a
      controller that asked; `dist_get_stat/1`'s pending bytes as OTP counts them.
    - `dist_ctrl_put_data/2` decodes what it takes, drops data of an earlier connection id, and
      for data it cannot read logs OTP's `** Got message from incompatible erlang on channel ...`
      and ends the connection, as observed.
  - [ ] Start: distribution from the DIST4a options before the entry runs, `net_kernel:start/1,2`,
        `stop/0`, `connect_node/1`; `auth` cookies (`erlang:set_cookie/1,2`, `get_cookie/0,1`,
        `~/.erlang.cookie` read or created as OTP does); `net_adm:ping/1` answers `pong`.
  - [ ] `net_kernel:start/1,2` leaves `differences.md`.
- Tests
  - [ ] Golden `dist_ping`: two Clause nodes register, ping and exchange messages to pids and to
        `{Name, Node}`; wrong cookie gives `pang` and OTP's log line.
  - [ ] The same with one OTP node (`host_otp`), both directions.

<a id="step-dist5"></a>

## DIST5. Every signal across nodes

Depends on: [DIST4b](#step-dist4b).

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
