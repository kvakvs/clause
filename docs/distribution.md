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
Nodes connect through OTP's handshake with all its statuses, `allow/1` and dynamic names ([making
connections](#making-connections), DIST4b2a, DIST4b2b), over IPv4 or IPv6 (DIST4b2c), messages move
between them ([messages](#messages), DIST4b3a), processes monitor other nodes' processes and `net_adm:ping/1`
answers ([process monitors](#process-monitors), DIST4b3b1), and they link and send exit signals to each other
([links and exit signals](#links-and-exit-signals), DIST5a), and lead each other's processes ([group
leaders](#group-leaders), DIST5b1) and spawn processes on each other ([spawning](#spawning), DIST5b2), every
signal of a pair of processes in the order sent ([signal order](#signal-order), DIST5c). Clause and OTP 29 nodes
connect, ping, monitor, message, link, lead and spawn on each other both ways on either kind of epmd
([testing](#testing), DIST4b4, DIST5c). `net_kernel` changes tick times, reports and tunes connections, and
`auth` keeps cookies as OTP's ([managing connections](#managing-connections), [cookies](#cookies), DIST6).
`rpc`, `erpc` and the `gen` behaviours call other nodes, whose output reaches the caller's group leader
([remote calls](#remote-calls), DIST7a to DIST7c), and `net_adm:world/0,1` finds the nodes epmd names (DIST7d).
Nodes connect over TLS too, with OTP's options files and certificate checks ([TLS distribution](#tls-distribution),
DIST12a). The other libraries answer as OTP does without distribution (`{badrpc, nodedown}`, `not_alive`). The
rest of this file is the contract for distribution. Each section becomes current behavior when its
step lands.

## Scope

- **Peers:** OTP 29 nodes and other Clause nodes, in mixed clusters. Clause speaks OTP's protocol
  and keeps OTP's names, messages and inter-node protocols wherever another node observes them.
- **Carriers:**
  - TCP over IPv4 (`inet_tcp`, the default) and IPv6 (`inet6_tcp`).
  - TLS over IPv4 and IPv6 (`inet_tls`, `inet6_tls`) over Clause's [`ssl`](libraries/ssl.md)
    ([TLS distribution](#tls-distribution)).
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
    carrier runs a controller process (`clause_dist_controller`) over its socket through the `dist_ctrl_*`
    functions;
  - the TLS carriers `inet_tls_dist` and `inet6_tls_dist` and their supervisor `ssl_dist_sup` in `library/ssl`,
    with the same controller over an `ssl` socket.
- **Registered processes:** `net_sup`, `net_kernel`, `auth`, `erl_epmd` and `rex` start with
  distribution, as in OTP. The `global` and `pg` servers keep their OTP names and protocols ([pg](#pg)).

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
process-controller interface. Signals move over it since DIST4b3a ([messages](#messages)); the controller
reads and writes through the [controller data path](#controller-data-path). Node monitors hold entries
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

Current since DIST2d (`builtins/erlang/connections.cpp`), as OTP's. A connection has an output
queue of encoded packets, which messages fill since DIST4b3a ([messages](#messages)), also while it
is pending, and an up connection an input handler, first the controller.

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
  Data of an earlier connection id is dropped; an empty binary is a tick. Other data is read as a
  packet ([messages](#messages)) and its message delivered, or the controller killed, before the
  put is counted as received; `badarg` follows for data that is not a binary.
- **`dist_ctrl_input_handler(DHandle, InputHandler)`:** the controller makes any pid, of an ended
  process or of another node too, the process `dist_ctrl_put_data/2` takes data from; the
  controller then puts no more data. `notsup` unless the handle is a pair of a small integer and
  an entry's reference, else `badarg` unless the caller controls that up connection, the id is its
  current one and `InputHandler` is a pid.
- **`dist_get_stat(DHandle)`:** `{ok, Received, Sent, PendingBytes}` for any caller: the puts, the
  packets taken and the bytes queued, also while the connection is pending. OTP 29 counts 160 bytes
  more for each packet, 174 for one queued while the connection was pending, and Clause does the
  same ([differences](differences.md)). Zeros while the connection is idle; `badarg` for a handle
  that is not its entry's current one. Each connection starts at zero.

## Messages

Current since DIST4b3a (`builtins/erlang/dist_messages.cpp`), as OTP 29's, observed with a peer
speaking the protocol and with a test process as a connection's controller.

- **Packets:** 112 (pass-through), then the control message and the payload, each in the external
  format with its 131. Clause sends `SEND_SENDER` `{22, From, To}` to another node's pid, `SEND`
  `{2, '', To}` while its connection is not up yet, `REG_SEND` `{6, From, '', Name}` to
  `{Name, Node}` and `ALIAS_SEND` `{33, From, Alias}` to another node's reference.
- **Sends:** `!`, `send/2,3` and `send_nosuspend/2,3` queue the packet on the node's connection: an
  idle one auto-connects first ([node monitors](#node-monitors)) and the packet waits until
  `setnode/3`; an aborted connection drops it. With `noconnect` an idle connection is left alone
  (`noconnect`, `send_nosuspend/3` false). A node without a name drops every such message, as does
  a pid or reference of an earlier incarnation of this node. Another node's port is `badarg`.
- **The program's end** (the entry returned, `halt/0,1`) stops at once: packets still queued on a
  connection may be lost, as OTP 29's `halt` loses them. Its flush runs no process and kills the
  distribution ports; ten messages sent just before `halt()` never arrived in OTP 29.1.1, while a
  pause or `init:stop/0`, which takes the applications down first, let them all through.
- **Delivery:** `dist_ctrl_put_data/2` delivers `SEND`, `REG_SEND`, `SEND_SENDER`, `ALIAS_SEND` and
  their `_TT` forms (the trace token is ignored). The sender field is never checked; another node's
  receiver, a name no process has and a reference that is no alias get nothing. Process monitor
  signals ([process monitors](#process-monitors)) and link signals ([links and exit
  signals](#links-and-exit-signals)) are taken, group leaders set ([group leaders](#group-leaders)), spawns
  served and answered ([spawning](#spawning)), and the old `UNLINK` (4) is an invalid distribution message, as since OTP 26. Bytes after the payload are
  ignored.
- **Unreadable packets** kill the controller (`nodedown` reason `killed`) after OTP's report, `N`
  being the node's number in its printed pids:
  - fewer than three bytes: no report;
  - a first byte other than 112, or a control message without 131: the error report
    `** Got message from incompatible erlang on channel N`;
  - a control message that does not decode, or a payload that is missing, lacks its 131 or does not
    decode: the warning report `'Me' got a corrupted external term from 'Node' on distribution
    channel N`, the bytes from that term on (`<<...,131,255>>`, or `>>` for none) and
    `ATOM_CACHE_REF translations: none`;
  - a control message of no known operation, arity or receiver, or a monitor signal naming what
    OTP's `dist.c` refuses (below): the error report `Invalid distribution message: T`, `T` printed
    as `erlang:display/1` prints it.
- **`net_kernel`** logs `Net kernel got ~tw` for a message it does not expect, as OTP's: a
  `{dist_ctrlr, ...}` of a process that set up no connection, such as a test process calling
  `setnode/3`, among them.

## Funs

Current since DIST5d (`builtins/erlang/term_encoding.cpp`, `term_decoding.cpp`, `modules/code_server.cpp`), as
OTP 29's `external.c` and `erl_fun.c`, observed with crafted encodings and between Clause and OTP nodes.

- **Sent:** a local fun in a message, an exit reason, a `'DOWN'` reason or spawn arguments (`spawn(Node, Fun)`) is
  OTP's `NEW_FUN_EXT`: arity, new uniq, index, free value count, module, old index, old uniq, init's pid `<0.0.0>` as
  its creator, the captured values, and the size of it all. Clause's new uniq is the MD5 of the module name, its old
  uniq the first 24 bits of that and its old index the index (`fun_info/1`'s `new_uniq`, `uniq` and `index`), so the
  bytes differ from OTP's, which hashes the BEAM code. `term_to_binary/1,2` writes the same bytes. A native record
  still raises `notsup` there, and arrives as `notsup` in exit reasons.
- **Read:** a `NEW_FUN_EXT` names a local fun of the program by module, index, arity and new uniq, and its captured
  value count: between nodes of the same program, funs travel both ways and are called. Any other is a fun of
  another node's code, kept by the code server for the program's lifetime with the old index and old uniq of the
  first reading, as OTP's fun table keeps a stub. The size and the creator pid (which must be a pid) are not used;
  an old index or old uniq that is not an integer is `badarg`.
- **Another node's funs** print as OTP prints them, `#Fun<M.OldIndex.OldUniq>`; `fun_info/1` gives their items and
  `name` `[]`. Encoded again, in a message or `term_to_binary/1`, they are the bytes they were read from, so a fun
  that went to an OTP node and back is the node's own fun again. Calls fail as OTP's `error_handler:undefined_lambda/3`
  fails them: `{badfun, Fun}` when the program has the module, else `undef` with `{Fun, Args, []}` on top of the stack.
- **Comparison:** `=:=` wants the same fun (OTP's `eq` wants the same fun entry); `==` and the order compare module,
  index, old uniq, captured value count, then the captured values, as OTP's `erts_cmp`.
- An OTP node cannot call a Clause node's funs, nor Clause an OTP node's, since the uniq never matches; funs still
  travel there and back (golden `dist_funs`, `host_otp`; `external_funs` decodes crafted encodings).

## Process monitors

Current since DIST4b3b1 (`scheduler/remote_monitors.cpp`), as OTP 29's `dist.c`, observed with a
test process as a connection's controller and between two nodes.

- **Monitoring:** `monitor/2,3` of another node's pid or of `{Name, Node}` of another node queue
  `MONITOR_P` `{19, Watcher, Pid | Name, Ref}` on the node's connection, auto-connecting an idle one,
  also while it is pending. Options work as locally: `{tag, Tag}` replaces `'DOWN'`, `{alias, Mode}`
  makes the reference an alias that another node's process can answer to (`gen:call/3,4`).
- **'DOWN':** `{'DOWN', Ref, process, Item, Reason}`, Item the pid or `{Name, Node}`, comes from the
  node's `MONITOR_P_EXIT` `{21, Item, Watcher, Ref, Reason}` or `PAYLOAD_MONITOR_P_EXIT`
  `{28, Item, Watcher, Ref}` with the reason as payload. It comes with `noconnection` when the
  connection goes down or cannot be made, at once on a node that is not alive (`{Name, Node}` of
  another node is `badarg` there), and with `noproc` at once for a pid of an earlier incarnation of
  this node.
- **Ending:** `demonitor/1,2` and the watcher's end send `DEMONITOR_P` `{20, Watcher, Pid | Name,
  Ref}`; `flush` also finds a `'DOWN'` that came from another node. A reference of an earlier incarnation of this
  node monitors nothing (`demonitor/1` `true`, `info` `false`, `flush` still flushes); another node's is `badarg`
  (DIST5e).
- **Monitored here:** another node's `MONITOR_P` watches the process here it names by pid or
  registered name; one that is not there (no process, a port's name, a pid of an earlier
  incarnation) is answered with `noproc` at once. The process's end sends `MONITOR_P_EXIT` naming it
  as the monitor did, in the payload form when the node has `EXIT_PAYLOAD`. `DEMONITOR_P` ends the
  monitor. `process_info/2`'s `monitors` and `monitored_by` show the other node's pid or
  `{Name, Node}`.
- **Checks:** a `MONITOR_P` or `DEMONITOR_P` whose watcher is no pid of the connection's node, whose
  monitored process is no pid or atom here, or whose reference is no reference, and a
  `MONITOR_P_EXIT` whose monitored process is no pid or atom of another node or whose watcher is no
  pid here, are invalid distribution messages ([messages](#messages)). A `MONITOR_P_EXIT` of a
  monitor that already ended, or for a pid of an earlier incarnation, does nothing.
- **Nodes without OTP's monitor flags** (`DIST_MONITOR`, `DIST_MONITOR_NAME`) get no monitor
  signals; their monitors watch only the connection.
- **Ping:** `net_adm:ping/1` calls `{net_kernel, Node}` with `{is_auth, node()}`, which `net_kernel`
  answers `yes`: `pong`, else `disconnect_node/1` and `pang` (a wrong cookie fails the handshake with
  its log line first). `auth:is_auth/1` pings.

## Links and exit signals

Current since DIST5a (`scheduler/remote_links.cpp`), as OTP 29's `dist.c`, observed with a test process as a
connection's controller and between two nodes.

- **Linking:** `link/1` of another node's pid queues `LINK` `{1, From, To}` on its connection, auto-connecting an
  idle one, also while it is pending; linking again sends nothing. A node that cannot be reached, also on a node that
  is not alive, exits the link at once: `true`, and the caller gets `noconnection` (`{'EXIT', Pid, noconnection}`
  when trapping exits). A pid or port of an earlier incarnation of this node raises `noproc`, or sends
  `{'EXIT', Pid, noproc}` when trapping; another node's port is `badarg` for `link/1`, `unlink/1` and `exit/2`.
- **Unlinking:** `unlink/1` queues `UNLINK_ID` `{35, Id, From, To}` and keeps the link, unseen by
  `process_info/2`, until `UNLINK_ID_ACK` `{36, Id, To, From}` with that id; an exit that comes for it meanwhile
  is dropped. Linking again before the acknowledgement makes the link active again (`LINK` once more), and the old
  acknowledgement leaves it. Ids count up from 1 for the whole program; OTP's count per process.
- **Exits:** a linked process's end sends `EXIT` `{3, From, To, Reason}` to each linked process of other nodes,
  also for `normal`, or `PAYLOAD_EXIT` `{24, From, To}` with the reason as payload to a node with `EXIT_PAYLOAD`.
  `exit/2,3` of another node's pid sends `EXIT2` `{8, From, To, Reason}` or `PAYLOAD_EXIT2` `{26, From, To}`; to
  another node's alias nothing, as the node lacks `ALTACT_SIG`, and `priority` changes nothing.
- **Taken from another node:** its `LINK` links the process here, or is answered at once by an exit `noproc` for
  no process or a pid of an earlier incarnation; its `UNLINK_ID` (any id below 2^64) ends a link not being unlinked
  here and is always acknowledged; its link exits (`EXIT`, `PAYLOAD_EXIT` and their `_TT` forms) end the link and signal the process
  as a link does, dropped without a link; its exit signals (`EXIT2`, `PAYLOAD_EXIT2`, `_TT` forms) act as `exit/2`
  (`kill` ends a process that traps exits as `killed`), dropped for no process or an earlier incarnation. Exit
  signals reach a process running on another worker when its slice ends.
- **Connection loss:** every link to the node's processes exits `noconnection`, before the monitors' `'DOWN'`
  messages and the `nodedown` of node monitors, as OTP orders them.
- **Checks:** a link signal whose sender is no pid of the connection's node, whose receiver is no pid here (also a
  pid of an earlier incarnation for `LINK` and `EXIT2`), or an `UNLINK_ID` of id 0 or one that is no integer below
  2^64, is an invalid distribution message ([messages](#messages)).
- `process_info/2`'s `links` shows the other node's pids.

## Group leaders

Current since DIST5b1 (`builtins/erlang/dist_messages.cpp`), as OTP 29's.

- **Another node's leader:** a process's group leader may be another node's pid, kept in a buffer beside the
  process's links: `group_leader/0` and `process_info/2` return it, and the process's spawns inherit it.
  `standard_io` requests go to it through the I/O protocol (DIST7c, [I/O](#io-of-other-nodes-processes)).
- **`group_leader(Leader, Pid)`:** `Leader` a pid of any node. For another node's `Pid` it queues `GROUP_LEADER`
  `{7, Leader, Pid}` on that node's connection, auto-connecting an idle one, and returns `true`, also when the node
  cannot be reached or this node is not alive; a pid of an earlier incarnation of this node is `badarg`.
- **Taken from another node:** `GROUP_LEADER` makes its leader the group leader of the process here it names,
  once that process does not run on another worker; one naming another node's process does nothing. A
  `GROUP_LEADER` whose leader or member is no pid is an invalid distribution message ([messages](#messages)).

## Spawning

Current since DIST5b2 (`scheduler/remote_spawns.cpp`, `builtins/erlang/dist_spawns.cpp`,
`library/kernel/clause_dist_spawn.erl`), as OTP 29's `dist.c` and `erlang.erl`, observed with a test process as a
connection's controller and between two nodes.

- **Requests:** `spawn_request/2..5` naming another node queue `SPAWN_REQUEST` `{29, ReqId, From, GroupLeader,
  {Module, Function, Arity}, Options}` with the argument list as payload on its connection, auto-connecting an idle
  one, and return the request id. The requester keeps `{reply_tag, Tag}`, `{reply, Mode}`, `{monitor,
  MonitorOptions}` and `{link, LinkOptions}`; the node gets the other options, `monitor` and `link` first for those
  tuple forms. An invalid kept option replies `badopt` at once; a node that cannot be reached, also from a node that
  is not alive, replies `noconnection`, and an up node without the `SPAWN` flag `notsup`. Badarg for a node that is
  no atom, or an improper argument or option list.
- **Replies:** the node's `SPAWN_REPLY` `{31, ReqId, To, Flags, Result}` makes the link (flag 1) and the monitor
  (flag 2, the request id as its reference, with its tag and alias) active, then brings `{Tag, ReqId, ok, Pid}` or
  `{Tag, ReqId, error, Reason}` as `{reply, Mode}` asks. Until then `demonitor/1` of the id does nothing and
  `process_info/2`'s `monitors` leaves the monitor out. A lost connection fails pending requests with
  `noconnection`.
- **Abandoned or orphaned:** `spawn_request_abandon/1` of a pending request returns `true` (`false` once replied,
  for another process's or for another node's reference); its child, once spawned, gets the link's exit `abandoned`. A requester that ended has
  its child demonitored (`DEMONITOR_P`) and unlinked with the requester's exit reason. A reply to no pending request
  does nothing.
- **Blocking spawns:** `spawn/2,4`, `spawn_link/2,4`, `spawn_monitor/2,4` and `spawn_opt/3,5` naming another node
  continue in `clause_dist_spawn:spawn/5`, a request and its reply as OTP's `spawn_opt/5`: the pid, or `{Pid, Ref}`
  with `monitor`; `badopt` is `badarg`, as are the reply options; `noconnection` and `notsup` give OTP's useless
  pid, a process here (`clause_dist_spawn:crasher/6`, OTP's `erts_internal:crasher/6`) that logs the warning
  `** Can not start M:F,Args on Node **` (`(Options)` before `on` when there are any) and exits with that reason. `spawn(Node, Fun)` runs `erlang:apply(Fun, [])` there.
- **Served here:** another node's `SPAWN_REQUEST` starts `Module:Function(Args...)` with the request's group leader
  ([group leaders](#group-leaders)), linked to and monitored by the requester as its options ask, and queues its
  `SPAWN_REPLY` before the child can run, so the reply comes before anything the child sends. Options that are no
  proper list or not spawn options are answered `badopt`. A `SPAWN_REQUEST` whose id is no reference of another
  node, whose requester is no pid of another node, whose group leader is no pid or whose `{Module, Function,
  Arity}` is malformed, and a `SPAWN_REPLY` whose id or receiver is not of this node, whose flags are not below
  2^27 or whose result is no pid of another node or atom, are invalid distribution messages ([messages](#messages)).

## Signal order

Current since DIST5c, as OTP's. Every signal one process sends to another node (messages, links, unlinks,
monitors, exit signals, group leaders, spawn requests) is a packet on that node's one connection, queued in the
order sent, also while the connection is pending or being auto-connected; the packets of the other node are taken
in the order its controller puts them, and each is acted on before the next. So signals between a pair of
processes arrive in the order sent, as locally. While a connection is pending its packets use the forms of no
flags (`SEND` without the sender, `EXIT2` without payload), as OTP's do.

- **Aliases** of `alias/0,1` and of `monitor/3`'s `{alias, Mode}` take messages from other nodes as they take
  local ones: `reply` ends with the first, `explicit_unalias` with `unalias/1`, `reply_demonitor` ends its monitor.
- **Send options:** `erlang:send/3` with `noconnect` sends on an up or pending connection and returns `noconnect`
  for a node not connected; `nosuspend` changes nothing, as a connection is never busy here
  ([plan 30](../.agents/30-dist.md) Later); `send_nosuspend/2,3` return `true`, or `false` for `noconnect`.

## Remote calls

Current since DIST7a to DIST7c (`library/kernel/erpc.erl`, `library/kernel/rpc.erl`, `library/stdlib/gen.erl`), as
OTP 29's.

- **`erpc`:** calls, requests, multicalls and casts to another node are spawn requests of `erpc:execute_call/4` or
  `execute_cast/3` there ([spawning](#spawning)); results come back as the child's exit reason. A node that cannot
  be reached is `{erpc, noconnection}`, also a connection lost during the call; a time-out abandons the request,
  so no late reply arrives. A local call without a time-out still runs in the caller.
- **`rpc`:** `call`, `cast`, `multicall`, `async_call` and the yields convert `erpc`'s results to `{badrpc, Reason}`
  and bad nodes. `block_call` and `pinfo/2` call the node's `rex`; `server_call`, `multi_server_call` and `sbcast`
  monitor `{Name, Node}`; functions without a node list use `[node() | nodes()]`, and `parallel_eval/1` and
  `pmap/3` hand the calls to those nodes in turn. `rex` runs with distribution ([kernel_sup](#kernel_sup)), answers
  `features_request` with `[erpc]` and OTP's `{From, {call, M, F, A, Leader}}` with `{rex, Reply}`.
- **`gen`:** `gen_server`, `gen_statem` and `gen_event` calls to `{Name, Node}` or another node's pid monitor the
  server with an alias, so a reply after a time-out is dropped; a node that cannot be reached exits
  `{nodedown, Node}` inside the behaviour's `{Reason, {Module, call, Args}}`. `gen_server:multi_call/2,3,4` and
  `abcast/2,3` cover `[node() | nodes()]` without a node list; only `multi_call`'s monitors connect nodes.
- **What a node can run:** a distributed program keeps every export of the modules it links
  ([export liveness](executables.md#export-liveness)), so other nodes may call any of them; a module the program does
  not link is `undef` there.
- **I/O:** the called process's group leader is the caller's, so its output appears on the calling node
  ([I/O](#io-of-other-nodes-processes)).

## I/O of other nodes' processes

Current since DIST7c (`builtins/io.cpp`, `library/stdlib/io.erl`, `library/stdlib/clause_io.erl`), as OTP's.

- **`standard_io`** is the caller's group leader: the `io` builtins and `io:request/1,2` write this node's standard
  output while the leader is this node's standard I/O (the main process, or `user`), and
  otherwise send I/O protocol requests to the leader, another node's process or a local one such as a collecting
  process or an application master. `user` always names this node's standard I/O.
- **Standard I/O flag:** each process knows whether its leader is this node's standard I/O; spawns inherit it, and
  `group_leader/2`, `GROUP_LEADER` and spawn requests served here set it from the leader they name.
- **`user`:** `net_kernel` starts an I/O server registered as `user` with distribution (`clause_io:start_user/0`).
  It writes output in the order it comes and hands input requests to the standard input server, each in a process
  of its own. From then on `group_leader/0`, `process_info/2` and spawn requests to other nodes report `user` for a
  process led by this node's standard I/O, so other nodes' processes started from here answer to it.
- **Application masters** relay their group's I/O to the group leader they started under; `logger_std_h`'s
  `standard_io` writes to `user`, as OTP's.

## global

Current since DIST8a (`library/kernel/global.erl`, `clause_global_locker.erl`), OTP's protocol version 8 between
`global_name_server` processes, with OTP 29 nodes too.

- **Joining:** each visible connection starts a session: the servers exchange `init_connect` and `init_connect_ack`,
  and the lockers lock both partitions with a shared lock id (`lock_set` through the servers; this host's nodes are
  taken first, after a pause of 0.2 to 3 s). The servers then exchange their names, resolve conflicts, and tell the
  rest of their partitions about the new nodes (`new_nodes`) before the lock goes. Nodes of the other partition are
  connected (`in_sync`), so with `connect_all` the network becomes fully connected. A session out of step is
  cancelled (`cancel_connect`) and started again.
- **Names:** `register_name/2,3`, `re_register_name/2,3`, `unregister_name/1` and the external variants run in the
  registrar under global's lock on every known node; lookups read this node's replica. A name goes on every node when
  its process ends or its node goes down.
- **Conflicts:** when partitions holding the same name meet, the smaller node calls its registration's method on the
  node of its pid (`resolve_it/4` through `rpc:call/4`): `random_exit_name/3` keeps the pid of the smaller node and
  kills the other, `random_notify_name/3` sends the other `{global_name_conflict, Name}`, `notify_all_name/3`
  unregisters the name and tells both. A method that returns no pid unregisters the name; one that fails keeps the
  other partition's pid, as `rpc:call/4` turns the failure into `{badrpc, _}`.
- **Locks:** `set_lock/1,2,3`, `del_lock/1,2` and `trans/2,3,4` ask every node of the list; a refusal anywhere
  releases the nodes that granted it, and a retry sleeps as OTP's. A lock goes with its holder.
- **`sync/0,1`** returns once this node exchanged names with the nodes (within its global group); `disconnect/0`
  disconnects the nodes global knows and returns them; `info/0` and `get_locker/0` answer OTP's state record and the
  locker.
- **Overlapping partitions:** with `prevent_overlapping_partitions` (and `connect_all`), a node that loses a
  connection reports it (`lost_connection`) to the nodes it knows; each passes the report on once and asks the lost
  node to remove it (`remove_connection`), logging OTP's warning `'global' at node A requested disconnect from node B
  in order to prevent overlapping partitions`, and the removed node disconnects (`... disconnected node ...`).
  A node about to halt sends no reports (`prepare_shutdown`).
- **Not provided:** OTP's high-level trace for troubleshooting (`high_level_trace_start`, ...), which the server
  reports as unexpected calls.

## global_group

Current since DIST8b (`library/kernel/global_group.erl`), OTP's server and messages, with OTP 29 nodes too.

- **Configuration:** the `kernel` parameter `global_groups`, `[{Name, Nodes} | {Name, normal | hidden, Nodes}]`,
  read for this node's name and kept in a persistent term; this node's group is the one listing it (listed twice is
  `{'node defined twice', ...}`), the others are its other groups. Without the parameter, or while the node is not
  alive, there are no groups (`no_conf`) and every node takes part in `global`.
- **Group nodes:** at its start and at each nodeup of a group node, the server asks the node for its group
  (`global_group:get_own_nodes/0` through `erpc`); the same group makes it `sync` and lets `global` sync with it
  (`group_nodeup`), another one makes it `sync_error`. A connected node outside the group stays connected, but
  `global` keeps their names apart (`group_nodedown`, and `{disconnect_node, Node}` to the other node's server).
  `sync/0` checks the group again with `conf_check` messages (`config_ok`, `config_error`, logging OTP's
  `global_group: Could not synchronize with node ...` report).
- **Names:** `whereis_name/1,2`, `send/2,3` and `registered_names/1` answer this node's group from `global` and
  search the other groups, groups or nodes in a search process that asks each node's server (`find_name`,
  `registered_names`); in a group, the first node that can be reached answers for it.
- **Other requests:** `global_groups/0`, `own_nodes/0`, `info/0`, `monitor_nodes/1` (`{nodeup, Node}` and
  `{nodedown, Node}` of the group's nodes), `global_groups_changed/1`, `_added/1`, `_removed/1`, `ng_add_check/2,3`,
  OTP's test hooks, and `publish/2`, through which `net_kernel` publishes a hidden node to its group and a hidden
  group's nodes to their group only. The server also registers `global_group_check`, which passes replies of nodes
  older than OTP 25 on.

## pg

Current since DIST9 (`library/kernel/data_publisher.erl`, `pg.erl`), OTP 29's `data_publisher` messages between scope
servers of the same name, with OTP 29 nodes too.

- **Discovery:** a scope that is not `standalone` monitors nodes (`net_kernel:monitor_nodes/1`); at its start and at
  each `nodeup` it sends `{discover, Self, Version}` to the scope of its name on the node (`noconnect`). A scope
  answers each discover with its local data (`{local_data, Self, Version, Data}` as a cast), and discovers back a peer
  it did not know, monitoring it.
- **Members:** each node sends its own members only. A local join or leave goes to the peers as `{update, Self,
  Update}`; a peer's local data or update goes into the scope's ETS table as the difference to what the peer had, and
  notifies `monitor_scope/0,1` and `monitor/1,2` subscribers of remote members as of local ones. A peer's end, or the
  loss of its node's connection (`noconnection`), removes its members; `nodedown` itself changes nothing.
- **Older peers:** a peer of version 0 (OTP before 29) gets `pg`'s plain `join`, `leave` and `sync` messages, and its
  messages are translated back.
- **Default scope** (DIST13): with the kernel parameter `start_pg` set to `true` the scope `pg` runs as
  `kernel_safe_sup`'s child `pg`, as OTP's. Clause starts `kernel_sup` at the first call of a function of the default
  scope (`pg:join/2`, `get_members/1`, ...), where OTP starts it at boot ([differences](differences.md)). Without
  `start_pg` programs start it with `pg:start_link/0`, and calls without it fail as OTP's (`noproc`, or no members).

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
- **Calling a carrier** (DIST12a, DIST12b), as OTP's `net_kernel`: `listen(Name, Host)` with the name part as an
  atom, or `listen(Name)` when the module has no `listen/2`; a listen that raises is a `register/listen error`, and a
  module the program lacks `not supported`. Only the guide's mandatory callbacks are needed (`listen/1,2`, `accept/1`,
  `accept_connection/5`, `setup/5`, `close/1`, `select/1`, `address/0`); `childspecs/0`, `setopts/2` and `getopts/2`
  are used when exported, and connections without `mf_setopts`/`mf_getopts` answer `{error, enotsup}`. The
  handshake reads `f_recv`'s data as OTP's does from a list, and from a binary too
  ([differences](differences.md)). Golden `dist_user_carrier` runs a carrier of its own, in the guide's
  `gen_tcp` shape, between Clause and OTP nodes both ways.

## TLS distribution

Current since DIST12a (`library/ssl/inet_tls_dist.erl`, `inet6_tls_dist.erl`, `ssl_dist_sup.erl`), as OTP's
distribution over TLS:

- **Selecting it:** `--proto-dist inet_tls` (IPv4) or `--proto-dist inet6_tls` (IPv6), with the ssl options in a
  file (`--ssl-dist-optfile FILE`) or as options (`--ssl-dist-opt KEY VALUE`, repeated); OTP's `-proto_dist`,
  `-ssl_dist_optfile` and `-ssl_dist_opt` in args files mean the same. The program must link the carrier
  ([node start options](#node-start-options)). Every node of a cluster runs a TLS carrier: the handshake and the
  distribution packets, 4-byte length packets from the first one on, travel inside TLS as between OTP nodes, which
  connect to Clause nodes both ways.
- **Options file:** one term, `[{server, Options}, {client, Options}]`, in the file's `epp` encoding (latin1 without
  one), evaluated without bindings, so funs are written `fun Module:Function/Arity`. `ssl_dist_sup`, the carrier's
  child of `net_sup` (`childspecs/0`, started before the other children), reads it when distribution starts into the
  table `ssl_dist_opts`. A file that cannot be read, decoded, scanned or parsed fails the start with
  `{bad_ssl_dist_optfile, Reason}`, as OTP's.
- **`--ssl-dist-opt`:** `server_KEY VALUE` and `client_KEY VALUE` pairs for `certfile`, `cacertfile`, `keyfile`,
  `password`, `verify`, `verify_fun` (`{Module, Function, State}`), `crl_check`, `crl_cache` (a term), `reuse_sessions`,
  `secure_renegotiate`, `depth`, `hibernate_after`, `ciphers` and `versions` (one each), `ktls`, and the server's
  `dhfile` and `fail_if_no_peer_cert`. Another key raises `malformed_ssl_dist_opt` when a connection asks for its
  options.
- **A connection's options:** `{erl_dist, true}`, then those of `--ssl-dist-opt` and those of the file, with
  `{versions, ['tlsv1.3', 'tlsv1.2']}` first unless they name versions; a client also gets the node's host as
  `server_name_indication`, so `verify_peer` checks that the server's certificate names it.
  `get_ssl_client_options/0` and `get_ssl_server_options/1` give them.
- **Connections:** the carrier listens and looks nodes up as `inet_tcp_dist` does (`fam_listen/4`, `fam_setup/4`). An
  accepted socket runs the server's TLS handshake in a process of its own within the setup time; a failed one closes
  the socket (the ssl option `log_level` decides whether TLS alerts are logged). The connection's controller owns the
  ssl socket. Its process runs under `ssl_dist_sup`'s `tls_dist_connection_sup`, so distribution needs no started
  `ssl` application, and stopping `ssl` leaves it up. `net_kernel:node_info/2` gives the address with protocol
  `tls`; `net_kernel:setopts/2` refuses `active`, `deliver` and `packet`, as for TCP.
- **Certificate checks:** the ssl options decide what each side verifies. With an `allow/1` list, an accepted
  peer's certificate narrows it to the nodes whose host the certificate names, else to the peer's address when the
  certificate names that; when nothing is left the connection ends with `** Connection attempt from disallowed
  node(s) IP ** `. A peer without a certificate keeps the list. A server's
  `{verify_fun, {fun inet_tls_dist:verify_client/3, _}}` gets the allowed hosts and the client's address as its
  state, and wants the client's certificate to name one of them. `cert_nodes/1` gives the node names a certificate
  names, `allowed_hosts/1` the hosts of an allow list.
- **Declined:** kernel TLS offload (the `ktls` option, `set_ktls/1` and the other `ktls` functions, `dbg/0`): Clause's
  `ssl` runs TLS itself ([differences](differences.md#ssl)).

## Making connections

Current since DIST4b2a (`library/kernel/dist_util.erl`, `inet_tcp_dist.erl`, `net_kernel.erl`), as OTP's; the
statuses other than `ok`, `allow/1` and dynamic names since DIST4b2b.

- **Handshake:** protocol 6: the initiator's name (`N`, flags, creation, name), the acceptor's status, its
  challenge, the initiator's reply and the acceptor's ack. A digest is the MD5 of the cookie and the challenge as
  decimal text; both sides take the cookie `auth:get_cookie(Node)` gives for the other node.
- **Statuses:** the acceptor answers
  - `ok` for a node it holds no connection of;
  - `alive` for a node whose connection is up. The initiator answers `true`, since `net_kernel` only sets up
    a connection it does not hold; the acceptor then ends the old connection with nodedown reason
    `wait_pending` and goes on. Any other answer closes the new one;
  - for a node it is itself setting up a connection to (simultaneous connects): `ok_simultaneous` when the
    other node's name is greater, ending its own attempt and handing its `connect_node/1` callers to the
    accepted one, else `nok`. An initiator told `nok` waits for the other node's attempt, which takes its
    callers over; the setup timer ends the wait;
  - `nok` for a node another accepted handshake is pending for, and for this node's own name, logging
    `** Cannot get connection id for node N` (after an empty line), as OTP does;
  - `not_allowed` for a node outside the allow list (below);
  - `named:` with a name and a creation for a node asking for a dynamic name (below).
- **`allow/1`:** adds atoms to the allow list in order, each once, and answers `ok`; `error` for a
  non-list or a list with a non-atom, which adds nothing; `ignored` without distribution. `allowed/0` gives
  `{ok, Nodes}`. While the list is not empty, the acceptor refuses a node `dist_util:is_allowed/2` does not
  find in it (`** Connection attempt from disallowed node N ** `), checking a dynamic name request by its
  host, and `connect_node/1` refuses one before connecting (`** Connection attempt with disallowed node N ** `).
- **Dynamic names:** a node started as `--sname undefined` or `--name undefined` is hidden, listens nowhere and
  makes no auto-connects. Its first handshake offers `NAME_ME` and sends only its host; the acceptor names it
  `Name@Host`, with `Name` the base-36 digits of a 64-bit number derived from a random seed of the acceptor,
  the host and an attempt counter: a node asking again from that host gets the same name, unless one of that
  name is connected, which moves on to the next attempt. The creation is random the first time and one more
  each time the acceptor gives that name again. `net_kernel` names the node with `erlang:setnode/2`. When the
  connection that gave the name ends, `net_kernel` restarts its `net_sup` child of `kernel_sup`
  (`net_sup_dynamic` after `net_kernel:start/1,2`), as OTP does: the other connections end with
  `net_kernel_terminated`, the node is unnamed and the next `connect_node/1` names it again.
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
    handshake: `true`, or `false` once its process ended. A node no carrier takes and a name that is no node
    name answer `false`. A node monitor's auto-connect sets up the same way, and is aborted when it cannot, on a
    dynamic node and under the `kernel` parameter `dist_auto_connect` `never`.
  - Accepted connections: the documented `{accept, Acceptor, Controller, Family, Protocol}` from a carrier's
    acceptor starts `accept_connection/5` of the listening carrier of that family and protocol.
  - `disconnect/1` ends the connection's supervisor and controller with reason `disconnect` and answers `true`
    once both ended, so the connection is down; `false` without a connection, and for this node.
    `erlang:disconnect_node/1` is `net_kernel:disconnect/1` on an alive node and `ignored` on another, as OTP's.
  - Between `dist_util` and `net_kernel` (Clause's own messages): `{Pid, {accept_pending, Node | {name_me,
    Host}, Type}}` answered by `{Kernel, {accept_pending, Status}}` (`ok`, `ok_simultaneous`, `nok`, `alive` or
    `{named, Node, Creation}`); after `alive` and `true`, `{Pid, {replace, Node, Type}}` answered by `{Kernel,
    {replace, ok | nok}}`; on the initiator, `{Pid, {set_name, Node, Creation}}` answered by `{Kernel,
    {set_name, ok | error}}`; `{Pid, {nodeup, Node, Address, Type}}` answered by `{Kernel, {inserted,
    Intensity}}`, and the ticks `{Kernel, tick}`. Carriers receive `allow/1`'s list as `Allowed`, and a node
    waiting for its dynamic name passes itself to `setup/5` as `undefined@Host`.
- **Ticks:** a linked ticker spawned at `max` priority sends `net_kernel` `tick` every `net_ticktime` × 1000 /
  `net_tickintensity` milliseconds (the tick time rounded up to a multiple of the intensity, the `kernel` parameter
  `net_tickintensity` kept within 4 to 1000), and `net_kernel` ticks each up connection's supervisor. The
  supervisor reads `mf_getstat`: `net_tickintensity` ticks without input end the connection with
  `** Node N not responding **` / `** Removing (timedout) connection **` and the reason `net_tick_timeout`; a
  tick without output since the last one calls `mf_tick`, as does a hidden connection's tick without input, and
  the tick sent counts as output (DIST6).
- **Setup timer:** `dist_util:start_timer/1` links a process that ends the handshake with `setup_timer_timeout`
  unless a received handshake message resets it within `net_kernel:connecttime/0` (7 seconds, or the `kernel`
  parameter `net_setuptime` in seconds, at most 120, since DIST6); it is cancelled when the connection comes up.
- **`inet_tcp_dist`:** `accept/1` links an acceptor at `max` priority; `accept_connection/5` and `setup/5` spawn
  the supervisor with `dist_util:net_ticker_spawn_options/0`. Setup asks the `epmd_module`'s `port_please/2` and
  connects with `inet_dist_connect_options` under the carrier's own family, `binary`, `{active, false}` and
  `{packet, 2}`, over `nodelay` (`{nodelay, false}` when `dist_nodelay` is `false`); an address family in
  `inet_dist_connect_options` fails the connect, as in OTP. Host names the naming refuses log OTP's
  `** System running to use fully qualified hostnames **` lines. The
  controller is a process owning the socket: during the handshake it answers its supervisor's requests and
  switches to `{packet, 4}` before the connection is up; then it reads with `{active, once}` and puts each packet
  with `dist_ctrl_put_data/2` (an empty one is a tick), sends the queued packets on `dist_data` and sends ticks
  as empty packets. A closed socket, a failed send or refused data ends it with `connection_closed`, the other
  node's `nodedown` reason. Since DIST6 `mf_setopts` and `mf_getopts` ask the controller, which answers while the
  connection is up: `setopts/2` refuses `active`, `deliver` and `packet` (`{error, {badopts, Options}}`) as for
  listen sockets, and `getopts/2` reports `{active, true}` as OTP's port does while the controller reads with
  `{active, once}`.

## Managing connections

Current since DIST6 (`library/kernel/net_kernel.erl`, `dist_util.erl`), as OTP's `net_kernel`:

- **Tick time changes:** `set_net_ticktime(T, Period)` (`Period` 60 seconds by default) answers `unchanged` for the
  current tick time, `change_initiated`, or `{ongoing_change_to, T}` while a change runs; `get_net_ticktime/0`
  answers `T` or `{ongoing_change_to, T}`, `ignored` without distribution. The new tick time keeps the intensity
  and is rounded up to a multiple of it. An aux ticker sends `aux_tick` (the supervisors tick unless a visible
  connection has output pending) at once and every shorter interval of the two until `Period` is covered; a longer
  tick time takes effect at the start, a shorter one at the end of the period. A ticker that ends starts again.
- **Connection information:** `node_info(Node)` gives `{ok, [{owner, Pid}, {state, up | pending}, {address,
  #net_address{}}, {type, normal | hidden}, {in, N}, {out, N}]}`, the packet counts of an up connection read by its
  supervisor (`{Kernel, get_status}`), `0` for a pending one; `node_info(Node, Key)` one of them or
  `{error, invalid_key}`; `{error, bad_node}` without a connection, `badarg` without distribution (OTP's
  `sys_dist` table is missing then). `nodes_info/0` lists every connection; `i/0` prints them as OTP's table with
  totals, `i/1` one connection.
- **Socket options:** `getopts(Node, Options)` and `setopts(Node, Options)` go through the up connection's
  supervisor to the carrier's `mf_getopts` and `mf_setopts` (`{error, enotsup}` without them), `{error,
  noconnection}` without an up connection or when it ends first. `setopts(new, Options)` calls each listening
  carrier's `setopts/2` on its listen socket (its error is the answer; `enotsup` when the carrier has none), then
  puts each option in front of `inet_dist_listen_options` and `inet_dist_connect_options` in place of options of
  its key (an option that is no pair crashes `net_kernel`, as OTP's), sets `dist_nodelay` for `{nodelay, B}`, and
  answers once each pending connection's supervisor applied them when it came up. A node name or option list of
  another type is `function_clause`, also without distribution, which answers `ignored`.
- **`dist_auto_connect once`:** an up connection's end bars its node from auto-connects (node monitors abort at
  once); `connect_node/1` and an accepted connection lift the bar.
- **Passive connects:** `{passive_cnct, Node}` sets up a connection as an auto-connect does and answers once it is
  up, `true` at once for an up connection or this node, `false` when the setup fails or under `dist_auto_connect
  never`; for a barred node `passive_connect_monitor/2` waits up to `connecttime()` for the node to connect.
- **Disconnects:** `disconnect/1` and `async_disconnect/1` (a cast, not waiting) end up connections only; a pending
  connection answers `false`.
- **Other requests:** `verbose(Level)` answers the level before and logs OTP's info reports of connects,
  disconnects and exits at that level; `kernel_apply/3` answers `not_implemented`. The old spawn protocol's
  `{spawn, M, F, A, Leader}`, `{spawn_link, ...}` and `{spawn_opt, M, F, A, Options, Link, Leader}` calls spawn
  `spawn_func/6`, which answers the caller with its pid, sets the leader and applies the function (`do_spawn/3`).
  `{From, registered_send, Name, Message}` is passed to a registered name, `{From, badcookie, ...}` logs
  `** Got OLD cookie from N` and disconnects the node. Other calls are never answered, as OTP's.

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
  `inet_dist_listen_options` under the carrier's own options (an address family there is `badarg`) and over a
  backlog of 128; `address/0`, `select/1` (both parts and an IPv4 address for the host), `is_node_name/1` (an
  atom with one `@`) and `close/1` as OTP's. Since DIST4b2c (`library/kernel/inet6_tcp_dist.erl`):
  - `--proto-dist inet6_tcp` selects `inet6_tcp_dist`, the same carrier over IPv6: it listens on IPv6,
    registers with epmd as `inet6_tcp`, selects nodes whose host has an IPv6 address and gives `inet6` addresses.
    It runs over `inet_tcp_dist`'s `gen_*` functions, which take the driver `inet_tcp` or `inet6_tcp`, and
    `fam_address/1` and `fam_select/2`, which take the family. OTP's undocumented `fam_listen/4`,
    `fam_setup/4`, `getstat/1` and `tick/2` are stubs.
  - `setopts/2` and `getopts/2` set and read options of future connections on a listen socket through `inet`;
    `setopts/2` refuses `active`, `deliver` and `packet` options with `{error, {badopts, Refused}}`.
    `net_kernel:setopts/2` and `getopts/2`, which call them, come in DIST6.
  - `merge_options(Options, Forced)` gives `Forced`, then the options of `Options` whose key `Forced` has not.
    A key is a pair's first element, `mode` for `binary` and `list`, and `family` for `inet`, `inet6` and
    `local`; other options have none and always stay. `merge_options/3` appends the options of its third
    argument whose key `Options` has not.
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
  process), `global_group` ([global groups](#global_group)) and `kernel_safe_sup`, the supervisor other libraries add
  children to, holding the default `pg` scope under `start_pg` ([pg](#pg)). One-for-all without restarts, as OTP's. OTP's other children (`code_server`, `file_server_2`,
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
- **This node's cookie:** the `auth` server under `net_sup` starts with `--setcookie COOKIE`, else `--nocookie`
  (`nocookie`), else `.erlang.cookie` in the home directory (`init:get_argument(home)`: `HOME`, on Windows
  `HOMEDRIVE` and `HOMEPATH`, else the user profile), else in `filename:basedir(user_config, "erlang")`. The cookie
  is the file's printable characters (space to `~`) up to the first other one, blanks included; only line ends and
  blanks may follow (`Bad characters in cookie` otherwise, `Too short cookie string` for none), as OTP checks it
  since DIST6. A missing file is created in the home directory (the configuration directory without a home) with 20
  random letters `A` to `Z` and made readable by its owner alone (mode 400; read-only on Windows). On Unix, a file
  others may read stops `auth` with `Cookie file F must be accessible by owner only`, and a start without a home
  whose configuration directory cannot be read with `No home for cookie file`, so distribution fails to start.
  `--setcookie NODE COOKIE` forms, where the runtime passes them, set other nodes' cookies as OTP's (a node given
  twice keeps none).
- **Alive:** the `erlang` functions continue in `auth`'s. `get_cookie/0` is this node's cookie; `get_cookie(Node)`
  the cookie set for `Node`, else this node's. `set_cookie(C)` and `set_cookie(node(), C)` set this node's cookie,
  `set_cookie(Node, C)` the cookie of `Node`; each answers `true`. `auth:cookie/0,1`, `node_cookie/1,2` and
  `sync_cookie/0` (`true`; a cookie given for this node's name becomes its cookie) as OTP's; `is_auth/1` and
  `node_cookie/1,2` ping the node ([process monitors](#process-monitors)). Since DIST6 the server takes OTP's
  requests (`get_our_cookie`, `{get_cookie, Node}`, `{set_our_cookie, C}`, `{set_cookie, Node, C}`,
  `sync_cookie`, `echo` answered `hello`), `auth:set_cookie/1,2` and `get_cookie/1` take any term as OTP's (the
  `erlang` functions check atoms), and `print(Node, Format, Arguments)` casts `{print, Format, Arguments}` to
  `Node`'s `auth`, which logs it with `error_logger:error_msg/2`. OTP's answers to `badcookie` messages are left
  out: no runtime sends them.
- **`global`:** checks the `kernel` parameters `connect_all` and `prevent_overlapping_partitions` at its start as
  OTP's (`{invalid_parameter_value, Name, Value}` for a value that is no boolean); an unset `connect_all` becomes
  `false` only under the emulator flag `-connect_all false`.
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
| `--ssl-dist-optfile FILE` | `-ssl_dist_optfile` | The ssl options of TLS distribution ([TLS distribution](#tls-distribution)) |
| `--ssl-dist-opt KEY VALUE` | `-ssl_dist_opt` | One ssl option of TLS distribution, `server_KEY` or `client_KEY`; every one is kept |
| `--epmd bundled\|external\|none` | `-start_epmd` | What to do when no epmd runs ([epmd](#epmd)); default `bundled` |
| `--dist-listen true\|false` | `-dist_listen` | Accept incoming connections; `false` implies `--hidden` |
| `--env APP KEY VALUE` | `-App Key Value` | Sets `APP`'s environment parameter `KEY` to the term `VALUE` before applications start, over the `.app` resources (for example `--env kernel net_ticktime 20`). It works for every application, not only for distribution |

- `--env` values are parsed as OTP parses `-App Key Value`: when `application_controller` loads `APP` (kernel and
  stdlib when it starts), over the `.app` resource, merged in OTP's order. A value that is no term fails the load with
  `{bad_environment_value, Text}` after OTP's log line, and stops the program for kernel or stdlib.
- `init:get_argument/1` and `init:get_arguments/0` report these options under OTP's flag names:
  - `sname`, `name`, `setcookie`, `hidden`, `proto_dist`, `dist_listen`, `ssl_dist_optfile`, and `start_epmd`
    (`"false"` for `--epmd none`, `"true"` otherwise);
  - each `--ssl-dist-opt KEY VALUE` as `ssl_dist_opt` with `[KEY, VALUE]`;
  - `--env kernel net_ticktime 20` as `kernel` with `[["net_ticktime", "20"]]`.
- The `kernel` parameters distribution reads, as in OTP:
  - `net_ticktime`, `net_tickintensity`, `net_setuptime`;
  - `dist_auto_connect`, `connect_all`;
  - `inet_dist_listen_min`, `inet_dist_listen_max`, `inet_dist_use_interface`,
    `inet_dist_listen_options`, `inet_dist_connect_options`;
  - `epmd_module`, `erl_epmd_node_listen_port`;
  - `prevent_overlapping_partitions`, `global_groups`, `start_pg`;
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
- **Carrier modules (DIST12a):** `net_kernel` names a carrier only by the computed name `PROTO_dist`, so a program
  links a carrier other than the TCP ones when its code refers to the module, or when the manifest's
  `runtime_options` hold `--proto-dist PROTO`. A carrier the program lacks fails the start with OTP's log line
  `Protocol 'PROTO': not supported` and the failure below.
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
  - Test programs and epmds listen on loopback only, so the host's firewall never asks about them
    (DIST4b3a): every epmd and every program gets `ERL_EPMD_ADDRESS=127.0.0.1,::1`, and every node of a
    multi-node run, Clause's and OTP's alike, `inet_dist_use_interface` of 127.0.0.1 (`::1` under
    `--proto-dist inet6_tcp`; `nodes.loopback_flags`). Nodes that connect are named `@localhost`, and
    fixtures ask `erl_epmd:names(localhost)`: a lookup by this host's name reaches its network
    address, where no epmd listens (`{error, address}`, as in OTP).
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
- **OTP peers** (DIST4b4, DIST5c, DIST7a): `dist_ping`, `dist_connect`, `dist_signals` and `dist_rpc` repeat their first run with node `a` an OTP
  node on `clause-epmd` and with node `b` one on OTP's `epmd`; each node's stdout is the oracle's.
  An OTP node's stderr pattern is in OTP's own log format (a `Time level: message` line per report),
  and a node that waits for another's node status messages waits for its epmd marker name first,
  since an OTP node boots slower than a Clause program.
- **TLS** (DIST12a): `dist_tls` runs three nodes on TLS with options files and fixture certificates of its own
  (`data`), OTP nodes as a and as b, and IPv6 and `--ssl-dist-opt` runs with OTP nodes too; the runner listens on
  `::1` for `inet6_tls` as for `inet6_tcp`.
