# Distribution

How a Clause executable becomes a distributed Erlang node. Decided in plan 30 step DIST0
(2026-10-30); the steps that implement it are in [plan 30](../.agents/30-dist.md).

**Today** a program is a non-distributed node, `nonode@nohost`. Pids, ports and references of
other nodes exist as terms ([terms](terms.md#identities-of-other-nodes), DIST1), the node can be
named ([this node](#this-node), DIST2a), connection entries, node status messages, node monitors
and the controller data path follow OTP's process-controller interface without moving any signal
yet ([connections](#connections), DIST2b to DIST2d). `erl_epmd` and `net_adm:names/0,1` speak to an
epmd, and Clause's epmd server runs as `clause-epmd` ([epmd](#epmd), DIST3a; written beside programs
with `-daemon`, `-names` and `-kill` in DIST3c) or inside the program when a registration finds none
(`--epmd bundled`, DIST3b); `--epmd external` starts an epmd program, and `--no-bundled-epmd` leaves
the bundled epmd out (DIST3d). `net_kernel:start/1,2` and `stop/0` start and end distribution in a
running program ([starting distribution](#starting-distribution), DIST4b1a), and `--sname`/`--name` before the
entry ([node start options](#node-start-options), DIST4b1b), with OTP's cookies ([cookies](#cookies), DIST4b1c).
Nodes connect through OTP's handshake ([making connections](#making-connections), DIST4b2a); no signal moves over a
connection yet.
The other libraries answer as OTP does without distribution (`{badrpc, nodedown}`, `pang`, `not_alive`). The
rest of this file is the contract for distribution. Each section becomes current behavior when its
step lands.

## Scope

- **Peers:** OTP 29 nodes and other Clause nodes, in mixed clusters. Clause speaks OTP's protocol
  and keeps OTP's names, messages and inter-node protocols wherever another node observes them.
- **Carriers:**
  - TCP over IPv4 (`inet_tcp`, the default) and IPv6 (`inet6_tcp`).
  - TLS (`inet_tls`, `inet6_tls`) over Clause's [`ssl`](libraries/ssl.md), later.
  - User-written carriers through `--proto-dist`, over OTP's `dist_util` interface
    ([carriers](#carriers)).
- **Node discovery:** epmd ([epmd](#epmd)): bundled inside the program, a standalone
  `clause-epmd`, or OTP's; custom epmd modules through the `kernel` parameter `epmd_module` (the
  `erl_epmd` callbacks); nodes without epmd (`erl_epmd_node_listen_port`).
- **Node kinds:** hidden nodes, dynamic node names (`--name undefined`), nodes that do not listen
  (`--dist-listen false`).
- **Left out unless a later step selects them:**
  - code loading over distribution (`-loader inet`) and `erl_boot_server`;
  - remote shells (`-remsh`);
  - tracing across nodes (`seq_trace` tokens, trace ports);
  - C nodes (`erl_interface`): they speak the same protocol but are not tested.

## Protocol

- Handshake protocol version 6 (OTP 23 and later). The challenge digest is the MD5 of the cookie
  and the challenge (`DFLAG_MANDATORY_25_DIGEST`).
- Packets carry a 2-byte length during the handshake and a 4-byte length after it, as with OTP's
  TCP carrier. Messages use the pass-through form (byte 112): the control message, then the payload,
  both in the external term format.
- Offered flags:
  - OTP 26's mandatory set (`DFLAG_DIST_MANDATORY`): `EXTENDED_REFERENCES`, `FUN_TAGS`,
    `NEW_FUN_TAGS`, `EXTENDED_PIDS_PORTS`, `UTF8_ATOMS`, `NEW_FLOATS`, `MAP_TAG`,
    `EXPORT_PTR_TAG`, `BIT_BINARIES`, `BIG_CREATION`, `HANDSHAKE_23`, `V4_NC`, `UNLINK_ID`.
  - Also `DIST_MONITOR`, `DIST_MONITOR_NAME`, `SPAWN`, `ALIAS`, `UNICODE_IO`, `SMALL_ATOM_TAGS`,
    `SEND_SENDER`, `BIG_SEQTRACE_LABELS`, `EXIT_PAYLOAD` and `MANDATORY_25_DIGEST`.
  - `PUBLISHED` unless the node is hidden, and `NAME_ME` when it asks for a dynamic name.
- Not offered for now, so peers never use them:
  - The atom cache (`DIST_HDR_ATOM_CACHE` and the older atom cache flags) and fragments
    (`FRAGMENTS`). Both are later optimizations: without fragments, a large message is sent in one
    piece and holds the connection until it is sent.
  - `ALTACT_SIG`: Clause handles priority signals as ordinary ones
    ([processes](processes.md)).
  - `NATIVE_RECORDS`, until `term_to_binary/1` supports native records
    ([native records](native-records.md)).
- A peer that lacks a mandatory flag is refused, as OTP refuses it.

## Runtime and libraries

The work is split as in OTP: the runtime moves signals, and Erlang code makes connections.

- **Runtime (C++):**
  - this node's name and creation;
  - pids, ports and references of other nodes as term kinds of their own;
  - one entry per connected node: state, controller process, flags, creation and connection id;
  - encoding of outgoing signals to other nodes, and decoding and delivery of incoming ones. Signals
    between two processes stay in order, as locally;
  - the signals a lost connection causes (`noconnection` exits and `'DOWN'` messages,
    `{nodedown, Node}`);
  - OTP's process-controller interface: `erlang:setnode/2,3`, the `dist_ctrl_*` functions,
    `dist_get_stat/1`, `erts_internal:new_connection/1`, `abort_pending_connection/2`,
    `get_creation/0`, `dynamic_node_name/0,1` and `get_dflags/0`;
  - the epmd server, shared by the bundled epmd and `clause-epmd` ([epmd](#epmd)).
- **Libraries (Erlang, written from OTP's documentation, never from its sources):**
  - `net_kernel`, `auth` and the `erl_epmd` client;
  - `dist_util` and the TCP carriers `inet_tcp_dist` and `inet6_tcp_dist` (in `library/kernel`). A
    carrier runs a controller process over `gen_tcp` through the `dist_ctrl_*` functions;
  - later `inet_tls_dist` and `inet6_tls_dist` in `library/ssl`.
- **Registered processes:** `net_sup`, `net_kernel`, `auth`, `erl_epmd` and `rex` start with
  distribution, as in OTP. The `global` and `pg` servers keep their OTP names and protocols.

## This node

Current since DIST2a (`builtins/erlang/node.cpp`, `scheduler/signals.cpp`), as in OTP:

- **Naming:** `erlang:setnode(Node, Creation)` names the node. `Node` is a node name (letters,
  digits, `-` and `_`, an `@`, then a host without another `@`) other than `nonode@nohost`;
  `Creation` a non-negative small integer, cut to 32 bits and raised to at least 4. A live process
  must be registered as `net_kernel`, and the node must have no name yet. Anything else is `badarg`.
- **Readers:** `node/0,1`, `nodes/1,2` (`this`), `ets:info/2` (`node`), monitors and sends by
  `{Name, Node}`, `spawn/2,4` and the encodings of this node's pids, ports and references use the
  name and creation. `is_alive/0` is true for a named node or a dynamic name
  (`erts_internal:dynamic_node_name/1`); `erts_internal:get_creation/0` is the creation, or
  `undefined` without a name. A decoded identity naming this node is local only under its creation.
- **Monitoring nodes:** `monitor_node/2,3` raise `notalive` for a node other than `nonode@nohost`
  while the node has no name. Monitoring this node does nothing; another node's monitor is its
  connection entry's ([node monitors](#node-monitors)).
- **End:** when the `net_kernel` process that named the node ends, its connections end
  ([connections](#connections)); after the last one the node is `nonode@nohost` with creation 0
  again, and can be named anew. `setnode/2` and this end send `nodeup` and `nodedown` for this node
  ([node status messages](#node-status-messages)).

## Connections

Current since DIST2b (`scheduler/connections.cpp`, `builtins/erlang/connections.cpp`), as OTP's
process-controller interface. No signal moves over a connection yet (DIST5); the controller reads
and writes through the [controller data path](#controller-data-path). Node monitors hold entries
too ([node monitors](#node-monitors)).

- **Entries:** one per other node, made by `erts_internal:new_connection(Node)` (no name needed),
  `monitor_node/2,3` or a `setnode/3` attempt, and kept while the program runs. An entry is idle, pending or up; up holds
  the controller process, the flags and the creation `setnode/3` gave.
- **Dist handles:** `{ConnId, Ref}`. Every handle of an entry holds the same reference, so handles
  of one entry compare equal. The connection id advances by one, within 24 bits, each time the
  entry turns pending.
- **`new_connection(Node)`:** the entry's handle; an idle entry turns pending. `badarg` unless
  `Node` is a node name other than this node's.
- **`abort_pending_connection(Node, DHandle)`:** `false` when the connection is up, else `true`;
  a pending connection of the handle's id turns idle, and its node monitors get `nodedown`. `badarg` unless the handle is one of `Node`'s
  entry.
- **`erlang:setnode(Node, DistCtrlr, {Flags, Creation})`:** brings the pending connection up and
  returns its handle. It sends `{dist_ctrlr, DistCtrlr, Node, Caller}` to `net_kernel` and
  `nodeup` to the subscribers. `badarg` unless `net_kernel` owns this node's name, `Node` is a
  node name other than this node's with a pending entry, `DistCtrlr` is a live local process that
  controls no connection and is not `net_kernel`, `Flags` is a non-negative integer of at most 64
  bits and `Creation` a non-negative small integer (cut to 32 bits). Flags without every mandatory
  one also write OTP's error report (`... attempted to enable connection to node 'N' which does not
  support all mandatory capabilities.`). A port controller is `badarg`: Clause's carriers run
  controller processes. `erts_internal:create_dist_channel/3` answers `{ok, DHandle}` or `badarg`.
- **`get_dflags/0`:** `{erts_dflags, Default, Mandatory, Addable, Rejectable, StrictOrder}` of the
  flags Clause offers ([protocol](#protocol)).
- **`nodes/0,1,2`:** `visible` and `hidden` are up entries with and without `DFLAG_PUBLISHED`,
  `connected` both, `known` every entry and this node, `this` this node. The list is OTP's order:
  pending, idle, hidden, then visible entries, each oldest change first, then this node. `nodes/2`
  gives the connection id of an up entry (`undefined` otherwise) and the node type (`known` for an
  entry that is not up).
- **Ends:** a controller's end takes its connection down: the entry turns idle and the subscribers
  get `nodedown` with the controller's exit reason (`{shutdown, R}` gives `R`, `normal`
  `connection_closed`). `net_kernel`'s end aborts the pending entries, the latest first, and kills the controllers,
  hidden before visible, the latest first (`nodedown` reason `killed`); after the last one this
  node's name ends with reason `net_kernel_terminated`, or `no_network` when that was
  `net_kernel`'s exit reason.

## Node monitors

Current since DIST2c (`scheduler/connections.cpp`, `builtins/erlang/node.cpp`), as OTP's:

- **`monitor_node(Node, true)`:** the caller's monitor of the node once more, held by its entry
  (made idle when there is none). Each monitor gets one `{nodedown, Node}` when the connection goes
  down or its pending connection is aborted, before the node status messages. `false` ends one of
  the caller's monitors and makes no entry. A process's end ends its monitors.
- **Auto-connect:** monitoring an idle entry makes it pending and sends
  `{auto_connect, Node, DHandle}` to `net_kernel`; without a process registered as `net_kernel` the
  connection is aborted at once, so the monitor gets its `nodedown`.
- **Checks:** `badarg` for a `monitor_node/3` option other than `allow_passive_connect` or an
  improper option list, then `notalive` ([this node](#this-node)), then `badarg` for a flag that is
  not a boolean.
- **`allow_passive_connect`:** with a connection that is not up, `monitor_node/3` holds no monitor
  and asks `net_kernel:passive_cnct(Node)` (a `{passive_cnct, Node}` call to `net_kernel`, `false`
  without one) in `clause_dist:passive_monitor/3`: `true` monitors again, `false` sends the caller
  `{nodedown, Node}` at once. An up connection takes the monitor as without the option.
- **`erlang:dmonitor_node(Node, Flag, Options)`:** asks `net_kernel` the same way when the options
  hold `allow_passive_connect`; otherwise the caller gets `{nodedown, Node}` at once. It returns
  `true` whatever the other arguments.
- **Text of identities:** `list_to_pid/1`, `list_to_port/1` and `list_to_ref/1` read another
  node's identity while its connection is up: the node part is the node name atom's number, as
  printed, and the identity gets the connection's creation. A pid's numbers are cut to 32 bits, a
  port's number to 64; a reference has one to five words below 2^32. Any other node part is
  `badarg`.

## Controller data path

Current since DIST2d (`builtins/erlang/connections.cpp`), as OTP's. An up connection has an output
queue of encoded packets, which the signal encoder fills from DIST5 on, and an input handler,
first the controller.

- **`dist_ctrl_get_data(DHandle)`:** the oldest queued packet as a list of binaries, `{Size,
  Packet}` with `get_size`, or `none`. **`dist_ctrl_get_data_notification(DHandle)`:** `ok`; the
  controller gets `dist_data` at once when a packet is queued, else when the next one is.
  **`dist_ctrl_get_opt(DHandle, get_size)`** and **`dist_ctrl_set_opt(DHandle, get_size, Bool)`**
  (which returns the value before) choose whether sizes come. Only the controller calls these:
  `notsup` for any other caller, then `badarg` for a handle that is not its connection's current
  one, another option or a value that is not a boolean.
- **`dist_ctrl_put_data(DHandle, Data)`:** data from the other node. A list continues in
  `erts_internal:dist_ctrl_put_data/2`, which makes iodata one binary (`badarg` otherwise). Checks:
  `badarg` unless the handle is a pair of a small integer and the reference of an entry, `notsup`
  unless the caller is the input handler of an up connection, `badarg` for another connection's.
  The put is then counted as received, and `badarg` follows for data that is not a binary. Data
  of an earlier connection id is dropped; an empty binary is a tick. Other data is decoded from
  DIST5 on and dropped until then.
- **`dist_ctrl_input_handler(DHandle, InputHandler)`:** the controller makes any pid, of an ended
  process or of another node too, the process `dist_ctrl_put_data/2` takes data from; the
  controller then puts no more data. `notsup` unless the handle is a pair of a small integer and
  an entry's reference, else `badarg` unless the caller controls that up connection, the id is its
  current one and `InputHandler` is a pid.
- **`dist_get_stat(DHandle)`:** `{ok, Received, Sent, PendingBytes}` for any caller: the puts, the
  packets taken and the bytes queued. Zeros while the connection is not up; `badarg` for a handle
  that is not its entry's current one. Each connection starts at zero.

## Node status messages

Current since DIST2b, as OTP's: `process_flag(monitor_nodes, Bool)`,
`process_flag({monitor_nodes, Options}, Bool)` and `net_kernel:monitor_nodes/1,2` over them.

- **Subscriptions:** one per process and option set, counted: `true` adds one and returns the count
  before, `false` ends the subscription and returns its count. Messages come in the order the
  subscriptions were first made, as many copies as the count. A process's end ends its
  subscriptions.
- **Options:** `[]` (or `#{list => true}` alone) gives `{nodeup, Node}` and `{nodedown, Node}` for
  visible nodes. A map gives `{nodeup | nodedown, Node, Info}` with `Info` a map, or a list with
  `list => true`, holding `connection_id` (`undefined` for this node), `nodedown_reason` (on
  `nodedown`) and `node_type` when asked; `node_type => visible | hidden | all` chooses the nodes.
  The list holds them in that order. Anything else is `badarg`.
- **`net_kernel:monitor_nodes/2`:** a list of `connection_id`, `nodedown_reason` and
  `{node_type, T}`, or a map of them. Errors as OTP's: `{bad_option_value, ...}` first, then
  `{unknown_options, ...}`, `{option_value_mismatch, [New, Old]}` for two node types and
  `{invalid_options, Options}` for anything but a list or a map; `error` for a flag that is not a
  boolean.

## Carriers

User carriers are supported through OTP's interface, current since DIST4b2a
(`library/kernel/dist_util.erl`, `include/dist.hrl`, `include/dist_util.hrl`):

- `--proto-dist Proto` selects the module `Proto_dist`, as OTP's `-proto_dist` does.
- Clause's `dist_util` exports OTP's functions: `handshake_we_started/1`,
  `handshake_other_started/1`, `strict_order_flags/0`, `rejectable_flags/0`, `start_timer/1`,
  `setup_timer/2`, `reset_timer/1`, `cancel_timer/1`, `is_node_name/1`, `split_node/1`,
  `is_allowed/2`, `shutdown/3,4` and `net_ticker_spawn_options/0`.
- `kernel/include/dist_util.hrl` and `kernel/include/dist.hrl` provide the `#hs_data{}` record and
  the `DFLAG_*` macros with OTP's names and fields; `kernel/include/net_address.hrl` (since DIST4b1a) the
  `#net_address{}` record a carrier's `listen/2` returns. The header is written by Clause, and its fields
  follow OTP's because carriers set them.
- Clause's own carriers are written over the same interface, so a third-party carrier and a built-in
  one run the same handshake code.

## Making connections

Current since DIST4b2a (`library/kernel/dist_util.erl`, `inet_tcp_dist.erl`, `net_kernel.erl`), as OTP's. The
statuses `ok_simultaneous`, `alive` (on the accepting side), `not_allowed` and `named:` come in DIST4b2b.

- **Handshake:** protocol 6: the initiator's name (`N`, flags, creation, name), the acceptor's status, its
  challenge, the initiator's reply and the acceptor's ack. A digest is the MD5 of the cookie and the challenge as
  decimal text; both sides take the cookie `auth:get_cookie(Node)` gives for the other node. The initiator
  answers `alive` with `true`, since `net_kernel` only sets up a connection it does not hold. The acceptor
  answers `nok` while it holds any connection of that node, or when the node has this node's name.
- **Flags:** the offer is `get_dflags/0`'s default with the carrier's `add_flags` (of the addable ones) and
  without its `reject_flags` (of the rejectable ones), and `PUBLISHED` unless this node is hidden
  (`net_kernel:publish_on_node/1`) or the request is `hidden`. A peer's `MANDATORY_25_DIGEST` stands for the
  OTP 25 set; a peer lacking a mandatory flag or one of `require_flags` is refused with an error report. The
  connection takes the flags both nodes offer, and is visible when both published.
- **Wrong cookie:** the acceptor logs `** Connection attempt from node N rejected. Invalid challenge reply. **`
  (`error_logger:error_msg/2`) and closes; the initiator's `connect_node/1` answers `false` without a log.
- **Up:** the supervisor calls `f_setopts_pre_nodeup`, `erlang:setnode/3` with the controller `f_getll` gives,
  tells `net_kernel`, then calls `f_setopts_post_nodeup` and `f_handshake_complete` (arity 3, or 4 with
  `#{creation, flags}`), and supervises the connection.
- **`net_kernel`:**
  - `connect_node/1` and `hidden_connect_node/1` answer `true` for this node and an up connection. Otherwise the
    first carrier whose `select/1` takes the node sets the connection up, and the callers wait for its
    handshake: `true`, or `false` once its process ended. A node no carrier takes, a name that is no node name
    and a node waiting for its dynamic name answer `false`. A node monitor's auto-connect sets up the same way,
    and is aborted when it cannot.
  - Accepted connections: the documented `{accept, Acceptor, Controller, Family, Protocol}` from a carrier's
    acceptor starts `accept_connection/5` of the listening carrier of that family and protocol.
  - `disconnect/1` ends the connection's supervisor and controller with reason `disconnect` and answers `true`
    once both ended, so the connection is down; `false` without a connection, and for this node.
    `erlang:disconnect_node/1` is `net_kernel:disconnect/1` on an alive node and `ignored` on another, as OTP's.
  - Between `dist_util` and `net_kernel` (Clause's own messages): `{Pid, {accept_pending, Node, Address,
    Type}}` answered by `{Kernel, {accept_pending, ok | nok_pending}}`, `{Pid, {nodeup, Node, Address, Type}}`
    answered by `{Kernel, {inserted, Intensity}}`, and the ticks `{Kernel, tick}`.
- **Ticks:** a linked ticker spawned at `max` priority sends `net_kernel` `tick` every `net_ticktime` × 1000 /
  `net_tickintensity` milliseconds, and `net_kernel` ticks each up connection's supervisor. The supervisor reads
  `mf_getstat`: `net_tickintensity` ticks without input end the connection with
  `** Node N not responding **` / `** Removing (timedout) connection **` and the reason `net_tick_timeout`; a
  tick without output since the last one calls `mf_tick`.
- **Setup timer:** `dist_util:start_timer/1` links a process that ends the handshake with `setup_timer_timeout`
  unless a received handshake message resets it within `net_kernel:connecttime/0` (7 seconds); it is cancelled
  when the connection comes up.
- **`inet_tcp_dist`:** `accept/1` links an acceptor at `max` priority; `accept_connection/5` and `setup/5` spawn
  the supervisor with `dist_util:net_ticker_spawn_options/0`. Setup asks the `epmd_module`'s `port_please/2` and
  connects with `{packet, 2}`, `nodelay` (unless `dist_nodelay` is `false`) and `inet_dist_connect_options`;
  host names the naming refuses log OTP's `** System running to use fully qualified hostnames **` lines. The
  controller is a process owning the socket: during the handshake it answers its supervisor's requests and
  switches to `{packet, 4}` before the connection is up; then it reads with `{active, once}` and puts each packet
  with `dist_ctrl_put_data/2` (an empty one is a tick), sends the queued packets on `dist_data` and sends ticks
  as empty packets. A closed socket, a failed send or refused data ends it with `connection_closed`, the other
  node's `nodedown` reason.

## epmd

Clause can provide epmd in two forms, built from the same C++ epmd code in the runtime. Both serve
OTP's epmd protocol: `ALIVE2` (with the `ALIVE2_X` reply), `PORT_PLEASE2`, `NAMES`, `DUMP`,
`KILL` and `STOP`. A registration ends when its node's connection to epmd closes, and `STOP` and
`KILL` are accepted only with `-relaxed_command_check`, as in OTP's epmd. `ERL_EPMD_PORT` (default
4369) and `ERL_EPMD_ADDRESS` work as for OTP's epmd.

- **Bundled epmd (built in by default):** the program's executable carries the epmd. It runs inside
  the program, on a runtime thread with no Erlang processes, and only while the program runs.
  - `clau --no-bundled-epmd`, or `bundled_epmd = false` in `[targets.options]`, leaves it out.
  - It serves every node on the host, not only its own program. When the program exits, the other
    nodes lose their registrations, and new connections to them fail until they register again.
    Clusters that outlive one program use `external` with `clause-epmd`.
- **`clause-epmd` executable (optional):** `clau --epmd-executable`, or `epmd_executable = true` in
  `[targets.options]`, also writes `clause-epmd` (`clause-epmd.exe` on Windows) beside the output.
  - It is a small standalone program with no Erlang code.
  - It takes a subset of OTP's `epmd` flags: `-daemon`, `-port P`, `-address LIST`,
    `-relaxed_command_check`, `-names` and `-kill`.
  - `-daemon` detaches it. On Windows that means a new process group, no console and no inherited
    handles; on POSIX, `setsid` after a double fork, with standard I/O on `/dev/null`.
  - `-names` and `-kill` print OTP's `epmd` messages and exit with its statuses.

The runtime option `--epmd MODE` chooses what happens when distribution starts, at startup or by
`net_kernel:start/1,2`:

| Mode | When an epmd already listens | When none listens |
| --- | --- | --- |
| `bundled` (default) | Register with it | Run the bundled epmd inside the program, then register |
| `external` | Register with it | Start `clause-epmd -daemon` (the program's directory first, then `PATH`) or OTP's `epmd -daemon` from `PATH`, then register; distribution fails to start if neither is found |
| `none` | Register with it | Distribution fails to start |

- A program built without the bundled epmd treats `bundled` as `none` and prints a warning once.
- When two programs start an epmd at the same time, the one that cannot bind gives way. The nodes
  retry their connection to epmd for a short time.
- Nodes without epmd stay possible through the `kernel` parameters `epmd_module` (a custom
  `erl_epmd` callback module) and `erl_epmd_node_listen_port`.

Current since DIST3a (`runtime/src/epmd/`, `library/kernel/erl_epmd.erl`):

- **Server:** `registry` keeps the names and `requests` answers each request as OTP's epmd does;
  `server` listens (every address of both families, or `-address` plus loopback) and moves the
  bytes. Observed from OTP 29's epmd and kept:
  - a registration whose highest version is 6 or more gets `ALIVE2_X_RESP`, others `ALIVE2_RESP`
    with a creation of 1 to 3; a name registered again gets its old creation plus one;
  - a refused registration (name taken) is answered with result 1 and creation 99, and its
    connection stays open; any byte on a registered connection closes it;
  - `NAMES` lists the newest name first; `DUMP` ends each line with a NUL byte and also lists
    remembered names (`old/unused name`);
  - `KILL` answers `NO` while names are registered, `STOP` is ignored, and other hosts may only
    ask `PORT_PLEASE2` and `NAMES`, unless `-relaxed_command_check`; with it, `STOP` of an unknown
    name answers `NOEXISTSTOPPED`, as OTP's;
  - malformed requests, unknown codes and bytes past a packet close the connection without reply;
    a connection without a registration closes after 60 seconds.
- **`clause-epmd`** (target `clause_epmd`) runs it in the foreground with `-port`, `-address` and
  `-relaxed_command_check`; the test build makes it for the [multi-node runner](#testing). DIST3c
  adds the rest below.
- **`erl_epmd`:** registers with `ALIVE2_REQ` (versions 6 to 5, type 77) and registers again a
  second after the connection breaks. `names/0,1` list the oldest name first, as OTP's client
  does.

Current since DIST3b (`runtime/src/epmd/bundled.cpp`, `runtime/src/builtins/epmd.cpp`):

- **Bundled epmd:** every executable carries it (the `clause_runtime_epmd` archive since DIST3d). It runs
  on a thread of its own, opened by the first registration that needs it, and it ends with the
  program; a `KILL` stops it, and the next registration that finds no epmd opens it again. It
  listens as `clause-epmd` would under the program's `ERL_EPMD_PORT`, `ERL_EPMD_ADDRESS` and
  `ERL_EPMD_RELAXED_COMMAND_CHECK`.
- **`--epmd bundled|none`:** when `erl_epmd`'s registration (also the re-registration after a
  lost epmd) finds no epmd (`econnrefused`), it asks `clause_dist:start_epmd/0`. Under `bundled`
  the bundled epmd opens; when another epmd holds the port, it gives way. The registration then
  connects again for up to a second. Under `none` the registration fails with `econnrefused`, as
  OTP's. `names/0,1` and `port_please/2,3` never start an epmd, as in OTP.

Current since DIST3c (`runtime/src/epmd/main.cpp`, `client.cpp`, `daemon_*.cpp`,
`compiler/src/linking/link.cpp`):

- **Beside programs:** `clau --epmd-executable` and `epmd_executable = true` link `clause-epmd` for
  the program's target from the archive `clause_epmd_program` beside the runtime archive, and
  publish it beside the executable with it.
- **`-names` and `-kill`:** as OTP's, only as the last argument (after `-port`). They ask the epmd on
  the loopback (IPv6, then IPv4) at `ERL_EPMD_PORT` or `-port`. `-names` prints `epmd: up and
  running on port P with data:` and the epmd's lines; `-kill` prints `Killed`, or `Killing not
  allowed - living nodes in database.` with status 1. Without an epmd both print `epmd: Cannot
  connect to local epmd` on standard error with status 1.
- **`-daemon`:** POSIX forks twice around `setsid`, ignores `SIGHUP`, moves to `/` and puts
  standard I/O on `/dev/null`; the parents exit with status 0 at once. Windows starts the program
  again with its arguments and `-detached` (`DETACHED_PROCESS`, `CREATE_NEW_PROCESS_GROUP`, no
  inherited handles), and that process serves with standard I/O on `NUL`. A daemon that cannot
  listen exits silently, as OTP's.

Current since DIST3d (`runtime/src/epmd/extension.cpp`, `launch_*.cpp`,
`library/kernel/clause_dist.erl`):

- **Without the bundled epmd:** the bundled epmd and the server are the `clause_runtime_epmd`
  archive beside the runtime archive, behind the startup descriptor's extension entry
  `CLAUSE_epmd_extension_v1` (as the crypto archive's). Every program lists it and links the
  archive unless `clau --no-bundled-epmd` or `bundled_epmd = false`; such a program answers a
  registration that finds no epmd as `--epmd none` does, after one warning on standard error
  (`clau: warning: this program was built without the bundled epmd ...`). CMake consumers of
  `Clause::generated_program` link the archive too.
- **`--epmd external`:** `erl_epmd` looks for `clause-epmd` in the program's directory
  (`init:script_name/0`), then for `clause-epmd` and OTP's `epmd` on `PATH`
  (`os:find_executable/1,2`), and starts it with `-daemon` (POSIX: standard I/O on `/dev/null`,
  waiting for its first process; Windows: detached, a new process group, not waited for). The
  registration then connects again for up to a second; without a program, or when it does not start,
  it fails with `econnrefused`. The epmd outlives the program, as OTP's does.

## Starting distribution

Current since DIST4b1a (`library/kernel/net_kernel.erl`, `erl_distribution.erl`, `inet_tcp_dist.erl`), as
OTP's `net_kernel:start/1,2` and `stop/0`. The started node connects to others as
[making connections](#making-connections) says.

- **Processes:** `net_kernel:start/1,2` starts the `net_sup` supervisor (`erl_distribution`, one-for-all, no
  restarts) with the `erl_epmd` server (the `kernel` parameter `epmd_module`, when that module has
  `start_link/0`), the `auth` server ([cookies](#cookies)) and then the `net_kernel` server, as `kernel_sup`'s
  `net_sup_dynamic` child ([kernel_sup](#kernel_sup)), and returns `{ok, NetSup}`; `{error, {already_started,
  NetSup}}` while it runs.
- **Options:** `start/2` takes `name_domain` (`longnames` by default), `net_ticktime` (the `kernel` parameter,
  60 by default; a value below 1 is 1), `net_tickintensity` (4), `dist_listen` and `hidden`; an invalid one
  raises `{invalid_option, Key, Value}`, a name that is no atom `invalid_name`, options that are no map
  `invalid_options`. `start/1` takes OTP's documented lists (`[Name]`, `[Name, NameDomain]`,
  `[Name, NameDomain, TickTime]`, `net_ticktime` being `TickTime` × 4 in seconds, rounded up).
- **Names:** a name part of letters, digits, `-` and `_`. A host part is taken as given; with `shortnames` it
  may hold no dot (`Can't set short node name!`). Without one (`a` or `a@`) the node takes this host's name,
  its first label for `shortnames`, its fully qualified name for `longnames` (`Can't set long node name!` when
  it has no dot). Other names log `Invalid node name!`. Each log line is an `error_logger:info_msg/1,2` report,
  followed by the failed server's crash report and `net_sup`'s start error, as in OTP.
- **Listening:** the first `--proto-dist` carrier (`inet_tcp` by default) checks the name with
  `is_node_name/1` (`Protocol 'inet_tcp': invalid node name: ...`), then `listen/2` registers the port with epmd
  under the `--epmd` mode ([epmd](#epmd)); a taken name logs `... the name N seems to be in use by another
  Erlang node`, other errors `... register/listen error: Reason`. `erlang:setnode/2` then names the node with
  the creation epmd gave. `dist_listen => false` listens nowhere and registers no name, takes a random creation
  and is hidden; the name `undefined` names no node yet (`erts_internal:dynamic_node_name/1`).
- **`inet_tcp_dist`:** `listen/1,2` listens with `gen_tcp` (IPv4, `{packet, 2}`) on the first free port from
  `inet_dist_listen_min` to `inet_dist_listen_max` (any port without them), on `inet_dist_use_interface`, with
  `inet_dist_listen_options`; `address/0`, `select/1` (both parts and an IPv4 address for the host),
  `is_node_name/1` (an atom with one `@`) and `close/1` as OTP's.
- **Failures:** a start that fails returns OTP's `{error, {{shutdown, {failed_to_start_child, net_kernel,
  {'EXIT', nodistribution}}}, Child}}`, `kernel_sup`'s answer with the `net_sup_dynamic` child record.
- **Requests:** `nodename/0`, `longnames/0`, `get_net_ticktime/0` and `get_state/0` (`started => dynamic`,
  `name`, `name_type`, `name_domain`) ask the server; without one they answer `ignored` (`#{started => no}`).
- **`stop/0`:** terminates and deletes `net_sup_dynamic`, whose `net_kernel` takes the name with it and whose `erl_epmd` closes the
  registration; `{error, not_found}` without distribution, `{error, not_allowed}` for a node the node start options
  named ([node start options](#node-start-options)).

## kernel_sup

Current since DIST4b1d (`library/kernel/kernel.erl`, `clause_kernel.erl`). OTP's node starts `kernel_sup` at boot;
Clause starts it at the first use of a kernel service: `rex` (`rpc`), `global_name_server` (`global`),
`global_group`, or distribution (`net_kernel:start/1,2`, `--sname`, `--name`). A program that uses none has none.

- **Children:** `kernel:init/1`'s, in OTP's start order: `rex`, `global_name_server`, `net_sup` (distribution of the
  node start options; otherwise `erl_distribution:start_link/0` answers `ignore` and the child stays without a
  process), `global_group` (a server without groups) and `kernel_safe_sup`, an empty supervisor other libraries add
  children to. One-for-all without restarts, as OTP's. OTP's other children (`code_server`, `file_server_2`,
  `inet_db`, `logger_sup`, `user`, ...) are runtime services in Clause ([differences](differences.md)).
- **`net_kernel:start/1,2`** adds the `net_sup_dynamic` child, and `stop/0` terminates and deletes it.
- **Opting out (DIST4b1e, user choice):** `exclude_kernel_services = [...]` in `[targets.options]` or
  `clau --exclude-kernel-service ID` leaves `rex`, `global_name_server`, `global_group` or `kernel_safe_sup` out of
  `kernel_sup` (`net_sup` cannot be excluded). `clau` generates `clause_kernel_services`, whose `services/0` tells
  `kernel:init/1` the included services and links their modules; an excluded one is not linked for `kernel_sup`.
  This mostly saves processes, not program size: `gen_server` already links `global` and `rpc`, so an excluded service usually stays linked and is only not started. An excluded server the program uses anyway (`global`, `rpc`) starts unlinked at its first use,
  as before `kernel_sup`.
- **End:** a keeper process links to `kernel_sup`; when `kernel_sup` ends, for example after `net_kernel` was
  killed, the program halts with OTP's slogan `Kernel pid terminated (application_controller)
  ("{application_terminated,kernel,shutdown}")`, exit 1. A first use racing another waits until the children
  started.

## Cookies

Current since DIST4b1c (`library/kernel/auth.erl`, `runtime/src/builtins/erlang/node.cpp`), as OTP's `auth`:

- **Without distribution:** `erlang:get_cookie/0,1` and `auth:get_cookie/0,1` answer `nocookie`;
  `erlang:set_cookie/1,2` and `auth:set_cookie/1,2` raise `distribution_not_started`, and
  `erlang:set_cookie(node(), C)` raises `badarg`. A cookie or node that is no atom is `badarg`.
- **This node's cookie:** the `auth` server under `net_sup` starts with `--nocookie` (`nocookie`), else
  `--setcookie COOKIE`, else `.erlang.cookie` in the home directory (`init:get_argument(home)`: `HOME`, on Windows
  `HOMEDRIVE` and `HOMEPATH`, else the user profile). The file's first line is the cookie, without its line end and
  trailing blanks. A missing file is created with 20 random letters `A` to `Z` and made readable by its owner alone
  (mode 400; read-only on Windows). On Unix, a file others may read stops `auth` with `Cookie file F must be
  accessible by owner only`, and a start without a home with `No home for cookie file`, so distribution fails to
  start.
- **Alive:** the `erlang` functions continue in `auth`'s. `get_cookie/0` is this node's cookie; `get_cookie(Node)`
  the cookie set for `Node`, else this node's. `set_cookie(C)` and `set_cookie(node(), C)` set this node's cookie,
  `set_cookie(Node, C)` the cookie of `Node`; each answers `true`. `auth:cookie/0,1`, `node_cookie/1,2` and
  `sync_cookie/0` (`true`) as OTP's; `is_auth/1` and `node_cookie/1,2` answer `no` until connections come (DIST4b2).
- **End:** the cookies end with `net_sup`; a later start reads the file again.

## Node start options

Runtime options ([executables](executables.md#runtime-options)) start distribution before the entry
runs. They can be given:

- on the command line;
- in `CLAUSE_FLAGS`;
- in the manifest, as `runtime_options = [...]` in `[targets.options]`. These are built into the
  executable as its defaults.

The command line wins over `CLAUSE_FLAGS`, which wins over the manifest.

| Option | OTP counterpart | Effect |
| --- | --- | --- |
| `--sname NAME` | `-sname` | Short node name `NAME@Host`; `NAME@HOST` is taken as given |
| `--name NAME` | `-name` | Long node name with the fully qualified host; `undefined` asks the first peer for a dynamic name |
| `--setcookie COOKIE` | `-setcookie` | This node's cookie; otherwise `~/.erlang.cookie` is read, or created as OTP creates it |
| `--nocookie` | `-nocookie` | No cookie until `erlang:set_cookie/1,2` |
| `--hidden` | `-hidden` | Hidden node |
| `--proto-dist PROTO` | `-proto_dist` | Carrier module `PROTO_dist`; default `inet_tcp` |
| `--epmd bundled\|external\|none` | `-start_epmd` | What to do when no epmd runs ([epmd](#epmd)); default `bundled` |
| `--dist-listen true\|false` | `-dist_listen` | Accept incoming connections; `false` implies `--hidden` |
| `--env APP KEY VALUE` | `-App Key Value` | Sets `APP`'s environment parameter `KEY` to the term `VALUE` before applications start, over the `.app` resources (for example `--env kernel net_ticktime 20`). It works for every application, not only for distribution |

- `--env` values are parsed as OTP parses `-App Key Value`: when `application_controller` loads `APP` (kernel and
  stdlib when it starts), over the `.app` resource, merged in OTP's order. A value that is no term fails the load with
  `{bad_environment_value, Text}` after OTP's log line, and stops the program for kernel or stdlib.
- `init:get_argument/1` and `init:get_arguments/0` report these options under OTP's flag names:
  - `sname`, `name`, `setcookie`, `hidden`, `proto_dist`, `dist_listen`, and `start_epmd`
    (`"false"` for `--epmd none`, `"true"` otherwise);
  - `--env kernel net_ticktime 20` as `kernel` with `[["net_ticktime", "20"]]`.
- The `kernel` parameters distribution reads, as in OTP:
  - `net_ticktime`, `net_tickintensity`, `net_setuptime`;
  - `dist_auto_connect`, `connect_all`;
  - `inet_dist_listen_min`, `inet_dist_listen_max`, `inet_dist_use_interface`,
    `inet_dist_listen_options`, `inet_dist_connect_options`;
  - `epmd_module`, `erl_epmd_node_listen_port`;
  - `prevent_overlapping_partitions`, `global_groups`;
  - `distributed`, `sync_nodes_mandatory`, `sync_nodes_optional`, `sync_nodes_timeout`.
- `net_kernel:start/1,2` starts distribution in a running program, as in OTP, under the same
  `--epmd` mode.

Current since DIST4b1b (`library/kernel/clause_dist_start.erl`, `erl_distribution.erl`,
`runtime/src/startup/startup.cpp`):

- **Linking (user choice, 2026-10-31):** the distribution modules join a program that calls `net_kernel`, or one
  built with `clau --distribution`, the manifest option `distribution = true`, or manifest `runtime_options`
  holding `--sname` or `--name`. Other programs stay small; given `--sname` or `--name`, they stop before the entry
  with `clau: runtime failure: --sname and --name need the distribution modules (link the program with
  --distribution)`, exit 70 ([differences](differences.md)).
- **Start:** with `--sname` or `--name`, the runtime runs `clause_dist_start:boot(Module, Function, Arguments)` as
  the main process instead of the entry. It starts `kernel_sup`, whose `net_sup` child `erl_distribution:start_link/0`
  starts as OTP's kernel does at boot: the name of `--sname` (`shortnames`) or `--name` (`longnames`), `--hidden`, `--dist-listen`,
  `--proto-dist` and the `kernel` parameters as for `net_kernel:start/2`; `undefined` asks the first peer for a
  name. It then calls the entry in its place, so the entry runs in the main process with its usual reports.
- **Such a node:** `get_state/0` says `started => static`, `stop/0` answers `{error, not_allowed}` and
  `start/1,2` `{error, {already_started, NetSup}}`. `net_sup` is `kernel_sup`'s `net_sup` child.
- **Failure:** a start that fails logs OTP's lines and reports, then halts with OTP's slogan
  `Kernel pid terminated (application_controller) ("{application_start_failure,kernel,{{shutdown,
  {failed_to_start_child,net_sup,...}},{kernel,start,[normal,[]]}}}")`, exit 1, before the entry runs.

## Testing

- **Multi-node cases:**
  - A case's `golden.json` lists its nodes. Each node has a name, its arguments, its kind (`clause`
    or `otp`), its expected stdout and exit status, and a stderr pattern. All nodes run the same
    program, with roles chosen by argument.
  - The runner gives each run its own epmd port (`ERL_EPMD_PORT`), so cases run in parallel and
    never touch a user's epmd. It starts a foreground `clause-epmd -port N` (built once by the test
    build; OTP's `epmd` for a run of `"epmd": "otp"`) and starts the nodes at once. It collects each
    node's output and stops everything at the end or on a timeout. Clause nodes run with
    `--epmd none` (DIST3b), and from DIST4 with a fixed cookie.
  - Single-node runs get an `ERL_EPMD_PORT` where nothing listens, so they never ask a user's epmd.
  - A node's own `env` (Clause) and `otp_flags` (OTP, as `ERL_FLAGS`) give it node start options (DIST4b1b).
  - Cases of the bundled and external modes start no runner epmd and check which epmd the nodes
    find. A run of `"epmd": "bundled"` (DIST3b) also checks that no epmd listens on its port after
    the nodes ended.
  - Host names in output are masked with `stdout_masks`.
  - A program linked with `--epmd-executable` finds the `clause-epmd` beside it in
    `CLAUSE_TEST_EPMD`; under the oracle that names OTP's `epmd` (DIST3c). The oracle gives
    single-node runs an `ERL_EPMD_PORT` where nothing listens, as the runner does (DIST3d).
- **Goldens** come from OTP nodes on OTP's `epmd` on the same kind of epmd port, generated once
  (`tests/compiler/executables/nodes.py`, used by `run.py` and `regenerate.py`).
- **Interop cases** mix `clause` and `otp` nodes, or use OTP's `epmd`. Their golden says
  `"host_otp": true`: they carry the CTest label `host_otp` and skip when no `escript` is found.
  Clause-only cases need no OTP: `epmd_names` runs on `clause-epmd` and on bundled epmds (two
  nodes racing to start one), and `epmd_interop` runs the same program on OTP's `epmd` and with an
  OTP node.
