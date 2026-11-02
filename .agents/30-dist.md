# Plan 30: Network distribution

Distribution in the runtime and the libraries that depend on it, so a Clause executable is a
distributed Erlang node. Added 2026-10-30 as plan 11's phase DIST and moved here the same day (user
direction); formerly backlog item F38.

- **Today:** a Clause program is a non-distributed node until `net_kernel:start/1,2` (DIST4b1a) or
  `--sname`/`--name` (DIST4b1b), with OTP's cookies (DIST4b1c), under `kernel_sup` (DIST4b1d); started nodes
  connect over IPv4 or IPv6 (DIST4b2a to DIST4b2c), exchange messages (DIST4b3a), monitor each other's processes and
  ping (DIST4b3b1), link and send exit signals (DIST5a), lead each other's processes (DIST5b1) and spawn on each
  other (DIST5b2), every signal in order and with OTP 29 nodes (DIST5c), and send local funs to each other
  (DIST5d), and call each other through `rpc`, `erpc` (DIST7a) and `gen` (DIST7b), whose output reaches the caller's group
  leader (DIST7c); the program's end loses queued packets as
  OTP's `halt` (DIST4b3b2).
  Clause and OTP 29 nodes interoperate on either epmd (DIST4b4). `net_kernel` changes tick times, reports and tunes
  connections, bars nodes under `dist_auto_connect once` and connects passively; `auth` keeps cookies with OTP's
  requests and cookie-file checks (DIST6a, DIST6b).
  - The runtime has other nodes' pids, ports and references as terms (DIST1), a nameable node
    (`erlang:setnode/2`, DIST2a), connection entries with node status messages (`setnode/3`,
    DIST2b), node monitors with auto-connect (DIST2c) and the controller data path (`dist_ctrl_*`,
    `dist_get_stat/1`, DIST2d), which carries every signal since DIST4b3a and DIST5.
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
    connections come in DIST4b2a.
  - `--sname`/`--name` start distribution before the entry through `clause_dist_start:boot/3`; the distribution
    modules link with `clau --distribution`, manifest `distribution = true` or `runtime_options` naming the node,
    or a call of `net_kernel` (DIST4b1b).
  - `auth` under `net_sup` keeps the cookies (`--setcookie`, `--nocookie`, `~/.erlang.cookie` read or created);
    `erlang:get_cookie/0,1` and `set_cookie/1,2` continue in it while the node is alive (DIST4b1c).
  - `kernel_sup` (`kernel:init/1`) starts at the first use of `rex`, `global_name_server`, `global_group` or
    distribution, with OTP's children Clause has; its end halts the program as OTP's node (DIST4b1d); projects
    exclude services with `exclude_kernel_services` or `--exclude-kernel-service` (DIST4b1e).
  - Nodes connect over `inet_tcp_dist` through `dist_util`'s protocol 6 handshake with status `ok`, ticks keep
    connections up, `connect_node/1`, `hidden_connect_node/1`, auto-connect and `disconnect_node/1` as OTP's;
    a wrong cookie refuses the connection with OTP's log line (DIST4b2a).
  - Simultaneous connects, `alive` replacing a node's old connection, `nok` for this node's name, `allow/1` and
    `allowed/0`, and dynamic names (`--sname undefined`, `named:`, lost with the connection that gave them) as
    OTP's (DIST4b2b).
  - Process monitors by pid and by name: `MONITOR_P`, `DEMONITOR_P`, `MONITOR_P_EXIT` and its payload form,
    `'DOWN'` with `noconnection`; `net_adm:ping/1` through `net_kernel`'s `{is_auth, Node}` (DIST4b3b1).
  - The other libraries answer as OTP does without distribution: `net_kernel` (internals declined), `auth` (`nocookie` until started), `global` and
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

DIST0 to DIST6 and DIST7a to DIST7c are done, with DIST5's follow-ups DIST5d and DIST5e ([30-finished](30-finished.md)).

<a id="step-dist7"></a>

## DIST7. `rpc`, `erpc`, `gen` and I/O across nodes

Depends on: DIST6 ([30-finished](30-finished.md)).

Split before coding (2026-11-02); each sub-step extends the golden `dist_rpc` (Clause nodes, and OTP 29 nodes as a
and as b, `host_otp`).

- **DIST7d** `net_adm` with real nodes
  - [ ] `world/0,1`, `world_list/1,2` (epmd names of each host, pings, verbose lines), `host_file/0`,
        `localhost/0` (long names), `dns_hostname/1`.

<a id="step-dist8"></a>

## DIST8. `global` and `global_group` across nodes

Depends on: DIST6 ([30-finished](30-finished.md)).

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

Depends on: DIST6 ([30-finished](30-finished.md)).

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

Depends on: DIST5 ([30-finished](30-finished.md)).

- Success criteria
  - [ ] `inet_tls_dist` and `inet6_tls_dist` over Clause's `ssl` (`-proto_dist inet_tls`,
        `ssl_dist_optfile`, `ssl_dist_opt`), certificate checks as OTP's.
  - [ ] `benchmarks/bench_dist.erl`'s `dist_tls.` kernels run on Clause too: `tls_distribution/0` and
        `start_node/3` give Clause nodes the TLS flags (OTP only so far).
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
  Clause gets priority signals (then `exit/2` to another node's alias and `{link, [priority]}` take effect).
- Busy connections: OTP suspends senders past `dist_buf_busy_limit` and `nosuspend` sends fail then; Clause's
  output queue never is busy, so `nosuspend` changes nothing (DIST5c).
- `inet_tcp_dist`'s controller reads with `{active, once}` and one `inet:setopts/2` per packet, since
  Clause's sockets have no `{active, N}` (DIST4b2a); `{active, N}` or a runtime-side reader would cut that
  cost now that DIST5 moves real traffic.
