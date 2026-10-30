# Distribution

How a Clause executable becomes a distributed Erlang node. Decided in plan 30 step DIST0
(2026-10-30); the steps that implement it are in [plan 30](../.agents/30-dist.md).

**Today** a program is a non-distributed node, `nonode@nohost`. It has only local pids, ports and
references, and the libraries answer as OTP does without distribution (`{badrpc, nodedown}`,
`pang`, `not_alive`). The rest of this file is the contract for distribution. Each section becomes
current behavior when its step lands.

## Scope

- **Peers:** OTP 29 nodes and other Clause nodes, in mixed clusters. Clause speaks OTP's protocol
  and keeps OTP's names, messages and inter-node protocols wherever another node observes them.
- **Carriers:**
  - TCP over IPv4 (`inet_tcp`, the default) and IPv6 (`inet6_tcp`).
  - TLS (`inet_tls`, `inet6_tls`) over Clause's [`ssl`](libraries/ssl.md), later.
  - User-written carriers through `--proto-dist`, over OTP's `dist_util` interface
    ([carriers](#carriers)).
- **Node discovery:** epmd ([epmd](#epmd)), custom epmd modules through the `kernel` parameter
  `epmd_module` (the `erl_epmd` callbacks), and epmd-less nodes (`erl_epmd_node_listen_port`,
  `--no-epmd` with a carrier that needs no epmd).
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
    `get_creation/0`, `dynamic_node_name/0,1` and `get_dflags/0`.
- **Libraries (Erlang, written from OTP's documentation, never from its sources):**
  - `net_kernel`, `auth` and the `erl_epmd` client;
  - `dist_util` and the TCP carriers `inet_tcp_dist` and `inet6_tcp_dist` (in `library/kernel`). A
    carrier runs a controller process over `gen_tcp` through the `dist_ctrl_*` functions;
  - later `inet_tls_dist` and `inet6_tls_dist` in `library/ssl`.
- **Registered processes:** `net_sup`, `net_kernel`, `auth`, `erl_epmd` and `rex` start with
  distribution, as in OTP. The `global` and `pg` servers keep their OTP names and protocols.

## Carriers

User carriers are supported through OTP's interface:

- `--proto-dist Proto` selects the module `Proto_dist`, as OTP's `-proto_dist` does.
- Clause's `dist_util` exports OTP's functions: `handshake_we_started/1`,
  `handshake_other_started/1`, `strict_order_flags/0`, `rejectable_flags/0`, `start_timer/1`,
  `setup_timer/2`, `reset_timer/1`, `cancel_timer/1`, `is_node_name/1`, `split_node/1`,
  `is_allowed/2`, `shutdown/3,4` and `net_ticker_spawn_options/0`.
- `kernel/include/dist_util.hrl` and `kernel/include/dist.hrl` provide the `#hs_data{}` record and
  the `DFLAG_*` macros with OTP's names and fields. The header is written by Clause, and its fields
  follow OTP's because carriers set them.
- Clause's own carriers are written over the same interface, so a third-party carrier and a built-in
  one run the same handshake code.

## epmd

- A node starts epmd when it starts distribution with epmd (the default, `--start-epmd true`) and
  nothing listens on the epmd port.
- The node does not run epmd inside itself, because epmd must outlive it. It starts a detached copy
  of its own executable with the runtime option `--epmd-daemon`.
- That copy runs the runtime's C++ epmd and nothing else: no schedulers and no Erlang code. It
  serves OTP's epmd protocol: `ALIVE2` (with the `ALIVE2_X` reply), `PORT_PLEASE2`, `NAMES`,
  `DUMP`, `KILL` and `STOP`. A registration ends when its node's connection to epmd closes.
- The copy stops only when it is killed or asked to stop (`KILL`). OTP's epmd allows `STOP` and
  `KILL` only with `-relaxed_command_check`, and Clause's daemon does the same.
- **Detaching:**
  - Windows: a new process group, no console and no inherited handles.
  - POSIX: `setsid` after a double fork, with standard I/O on `/dev/null`.
- An epmd that already runs is used as it is, whether OTP's or another Clause program's.
- When two nodes start one at the same time, the second daemon cannot bind and exits quietly. Nodes
  retry the connection to epmd for a short time.
- `ERL_EPMD_PORT` (default 4369) and `ERL_EPMD_ADDRESS` work as for OTP's epmd.
- `prog --epmd-daemon` also runs the daemon by hand. `--epmd-daemon --foreground` keeps it attached
  to the console; the test runner uses it this way.

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
| `--start-epmd true\|false` | `-start_epmd` | Start epmd when none runs; default `true` |
| `--no-epmd` | `-no_epmd` | No epmd and no `erl_epmd` process (carriers that need none) |
| `--dist-listen true\|false` | `-dist_listen` | Accept incoming connections; `false` implies `--hidden` |
| `--env APP KEY VALUE` | `-App Key Value` | Sets `APP`'s environment parameter `KEY` to the term `VALUE` before applications start, over the `.app` resources (for example `--env kernel net_ticktime 20`). It works for every application, not only for distribution |
| `--epmd-daemon` | `epmd -daemon` | Runs epmd instead of the program ([epmd](#epmd)) |

- `init:get_argument/1` and `init:get_arguments/0` report these options under OTP's flag names:
  - `sname`, `name`, `setcookie`, `hidden`, `proto_dist`, `start_epmd`, `no_epmd`, `dist_listen`;
  - `--env kernel net_ticktime 20` as `kernel` with `[["net_ticktime", "20"]]`.
- The `kernel` parameters distribution reads, as in OTP:
  - `net_ticktime`, `net_tickintensity`, `net_setuptime`;
  - `dist_auto_connect`, `connect_all`;
  - `inet_dist_listen_min`, `inet_dist_listen_max`, `inet_dist_use_interface`,
    `inet_dist_listen_options`, `inet_dist_connect_options`;
  - `epmd_module`, `erl_epmd_node_listen_port`;
  - `prevent_overlapping_partitions`, `global_groups`;
  - `distributed`, `sync_nodes_mandatory`, `sync_nodes_optional`, `sync_nodes_timeout`.
- `net_kernel:start/1,2` starts distribution in a running program, as in OTP. It needs a running
  epmd unless the node runs without one.

## Testing

- **Multi-node cases:**
  - A case's `golden.json` lists its nodes. Each node has a name, its arguments, its kind (`clause`
    or `otp`), its expected stdout and exit status, and a stderr pattern. All nodes run the same
    program, with roles chosen by argument.
  - The runner gives each case its own epmd port (`ERL_EPMD_PORT`), so cases run in parallel and
    never touch a user's epmd. It starts a foreground `--epmd-daemon` on that port, starts the nodes
    with a fixed cookie, collects each node's output, and stops everything at the end or on a
    timeout.
  - Host names in output are masked with `stdout_masks`.
- **Goldens** come from OTP nodes (`erl -sname`) on the same kind of epmd port, generated once.
- **Interop cases** mix `clause` and `otp` nodes. They carry the CTest label `host_otp` and skip
  when no `erl` is found. Clause-only cases need no OTP.
