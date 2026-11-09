# mnesia's files and protocol, as observed

What OTP 29's `mnesia` (4.27) writes to its directory and sends between nodes, observed by running scenarios on OTP
nodes (plan 21 step MIM1H; its rules are in `.agents/21-lib.md`). Clause's [mnesia](mnesia.md) is written from these
observations and OTP's documentation, never from OTP's sources. Each step of MIM1H adds the tables of what it observed.

## Directory files (MIM1H1)

Each file is a `disk_log` (halt log) of Erlang terms unless named a `dets` table. A log starts with
`{log_header, Kind, Version, MnesiaVersion, Node, Now}`.

| File | Header kind and version | Terms after the header |
| --- | --- | --- |
| `FALLBACK.BUP`, backups | `backup_log`, `"1.2"` | `{schema, Tab, CreateList}` items, the schema's first, then the records |
| `schema.DAT` | `dets` `set`, key position 2 | `{schema, Tab, CreateList}`: the schema's own list has a cookie but no `majority` or `storage_properties` |
| `LATEST.LOG`, `PREVIOUS.LOG` | `trans_log`, `"4.3"` | `#commit{}` records, `{decision, Tid, Outcome, Nodes, RamNodes}` records and `{master_nodes, Tab, Nodes}` entries |
| `DECISION_TAB.LOG` | `decision_tab`, `"1.0"` | one `{decision_list, Decisions}`: `{trans_tid, serial, N}` and `{master_nodes, Tab, Nodes}`, in OTP's `ets` order |
| `Tab.DCD` | `dcd_log`, `"1.0"` | a `disc_copies` table's records; a new table's file has no terms at all, not even a header |
| `Tab.DCL` | `dcl_log`, `"1.0"` | the table's log items since the `DCD` file |
| `Tab.DAT` | `dets`, the table's type, key position 2 | a `disc_only_copies` table's records |

- **Log items:** `{{Tab, Key}, Value, Operation}` with `{{Tab, Key}, Record, write}`, `{{Tab, Key}, {Tab, Key}, delete}`,
  `{{Tab, Key}, Record, delete_object}` and `{{Tab, Key}, {RecordName, Incr}, update_counter}`. A transaction's items
  come in the order of OTP's transaction store.
- **Commits:** `{commit, Node, Decision, RamCopies, DiscCopies, DiscOnlyCopies, Ext, SchemaOps}`. A dirty write or a
  transaction on one node is `presume_commit` with its items in the field of the table's storage, `RamCopies` empty.
  A schema transaction is a commit whose decision is `{decision, Tid, presume_abort, [Node], []}` and whose
  `SchemaOps` holds `{op, Op, ...}` tuples, followed by `{decision, Tid, committed, [], []}` (MIM1H2).
- **When files change:**
  - `create_schema/1` writes `FALLBACK.BUP`; the first start turns it into `schema.DAT`.
  - Schema changes rewrite `schema.DAT` at once and stay in `LATEST.LOG` until the next dump.
  - `set_master_nodes/1,2` appends `{master_nodes, Tab, Nodes}` to `LATEST.LOG`.
  - A dump (`dump_log/0`, the thresholds, a start) renames `LATEST.LOG` to `PREVIOUS.LOG`, moves its items into `DCL`
    files (or a new `DCD` file once the `DCL` file outgrows `1/dc_dump_limit` of it), and then rewrites
    `DECISION_TAB.LOG` if the log held anything. A dump of an empty log writes no decision log.
  - Stopping does not dump: `LATEST.LOG` keeps the commits since the last dump.
  - Starting dumps the log. With `dump_disc_copies_at_startup` `true` (the default), each `disc_copies` table that the
    log touched or that has a `DCL` file gets a new `DCD` file and loses its `DCL` file; with `false`, the log's items
    are appended to the `DCL` files.
  - Starting from a fallback deletes the other files, writes `schema.DAT`, a `disc_copies` table's records as `write`
    items of a `DCL` file after a `DCD` file with only a header, and `disc_only_copies` tables' `DAT` files. No
    `DECISION_TAB.LOG` is left.
- **Transaction serial:** a fresh start's own transactions take serials 1 and 2. A start continues from
  `DECISION_TAB.LOG`'s serial, or past the transactions of the schema commits in `LATEST.LOG`; transactions that wrote
  only RAM tables are not remembered.

## Schema transactions (MIM1H2)

On one node, a schema transaction logs `{commit, Node, {decision, Tid, presume_abort, [Node], []}, [], Disc, DiscOnly,
[], Ops}` and then `{decision, Tid, committed, [], []}`. `Cs` below is the table's create list as in `schema.DAT`, with
the new version.

| Operation | `{op, ...}` tuples |
| --- | --- |
| `create_table/2` | `{op, create_table, Cs}`; a fragmented table's fragments, the last first, then the base table |
| `delete_table/1`, `del_table_copy/2` of the last copy | `{op, delete_table, Cs}` with the table's last definition |
| `add_table_index/2`, `del_table_index/2` | `{op, add_index, {Pos, ordered}, Cs}`, `{op, del_index, Pos, Cs}` |
| `transform_table/3,4` | `{op, transform, Fun or ignore, Cs}` |
| `change_table_copy_type/3` | `{op, change_table_copy_type, Node, From, To, Cs}` |
| access mode, load order, majority | `{op, change_table_access_mode, Cs, Old, New}`, `change_table_load_order`, `change_table_majority` alike |
| `write_table_property/2`, `delete_table_property/2` | `{op, write_property, Cs, Prop}`, `{op, delete_property, Cs, Key}` |
| `snmp_open_table/2`, `snmp_close_table/1` | `{op, add_snmp, Ustruct, Cs}`, `{op, del_snmp, Cs}` |
| `dump_tables/1` | `{op, dump_table, Size, Cs}` per table |
| `change_table_frag/2` | `{op, change_table_frag, activate or deactivate or {add_frag, Nodes} or del_frag, BaseCs}`, then `create_table` or `delete_table` of the fragment, then `{op, rec, Storage, {{Frag, Key}, [Record], write}}` and `{op, rec, Storage, {{Frag, Key}, [{Frag, Key}], delete}}` per moved record |
| `restore/2` | `{op, clear_table, Cs}` or `{op, restore_recreate, BackupCs}` per table, the restored records as the commit's `Disc` and `DiscOnly` writes |

- **Dumped at once:** after `delete_table`, `transform` and `change_table_copy_type`, the log is dumped, so none of them
  stays in `LATEST.LOG`.
- **Not schema transactions:** `clear_table/1` is a `presume_commit` commit of `{{Tab, '_'}, '_', clear_table}`. A
  transaction that writes a table with `majority` `true` logs its commit under its decision, and only
  `{decision, Tid, committed, [], []}` when it wrote no disc table. `set_master_nodes` logs `{master_nodes, Tab,
  Nodes}`.
- **Not seen on one node:** the operations of `add_table_copy/3`, `del_table_copy/2` of another copy and
  `move_table_copy/3`, observed over two nodes in [replicas and loading](#replicas-and-loading-mim1h9).
- **Serial at start:** a start whose log held schema transactions takes one serial more before its own two.

## Remote queries (MIM1H3)

What a node asks another node's mnesia through `rpc:call/4`, or through the other node's `mnesia_rpc` server, and what
it answers. A node that is not running mnesia answers too, as long as the node itself runs.

| Query | Answer |
| --- | --- |
| `mnesia_lib:is_running/0` | `yes`, `no`, `starting` or `stopping` |
| `mnesia_lib:is_running_remote/0` | `{IsRunning, node()}`, `IsRunning` `true` only for `yes`; `delete_schema/1` asks each node this |
| `mnesia_lib:get_node_number/0` | `{node(), Pid}`, the answering process |
| `mnesia_schema:remote_read_schema/0` | `{ok, Source, CreateList}`: `ram` while running, with the schema table's list; `fallback` or `disc` with the list in `FALLBACK.BUP` or `schema.DAT`, as written (no `majority` or `storage_properties`); `default` without a directory to use, with a new RAM schema on no nodes; `{error, "No schema file exists"}`. `create_schema/1` asks each node this and refuses any other source than `default` |
| `mnesia_schema:delete_schema2/0` | `ok`, the directory's mnesia files deleted, or `{error, {"Mnesia still running", Node}}` |
| `mnesia_controller:get_remote_cstructs/0` | `{cstructs, Cstructs, RunningNodes}`: a `#cstruct{}` record per table, newest first, the schema last; without mnesia a `{badmatch, {error, {node_not_running, Node}}}` failure |
| `mnesia_lib:db_get/2` | the records under the key in this node's replica; `{aborted, {no_exists, {Tab, storage_type}}}` or `{aborted, {node_not_running, Node}}` exits |

- **The schema table's list:** while running, the schema table keeps the schema's own list as it was read from disc, so
  `remote_read_schema/0` answers it without `majority` and `storage_properties` on a disc schema. Backups write the
  whole definition instead.
- **`mnesia_rpc`:** a `gen_server` registered as `mnesia_rpc` answers `{apply, M, F, Args}` calls. `mnesia_lib`'s
  `db_get`, `db_first`, `db_last`, `db_next_key` and `db_prev_key` of a `ram_copies` or `disc_copies` table are
  answered by the server itself; any other apply runs in a process of its own. A throw answers its value, any other
  failure `{badrpc, {'EXIT', Reason}}` without a stack. `mnesia_rpc:call/4` uses the server only when the nodes agreed
  on a protocol after `{8,4}` (`{protocol, Node}` in `mnesia_gvar`), and `rpc:call/4` otherwise.
- **Processes:** `mnesia_kernel_sup` starts `mnesia_monitor`, `mnesia_subscr`, `mnesia_locker`, `mnesia_recover`,
  `mnesia_tm`, `mnesia_rpc`, `mnesia_checkpoint_sup`, `mnesia_controller` and `mnesia_late_loader` in this order, each
  registered under its name. `supervisor:which_children/1` lists each with its module and `gen_server` (`mnesia_monitor`
  and `proc_lib` for `mnesia_locker`, `mnesia_tm` and `mnesia_late_loader`); a checkpoint's process shows as
  `{undefined, Pid, worker, [mnesia_checkpoint_sup, mnesia_checkpoint, supervisor]}`.

## Connecting (MIM1H4)

How two nodes' mnesia connect, observed with both nodes on a RAM schema. Neither node merges the other's schema here:
`{current, db_nodes}` stays the node itself (merging is MIM1H5).

| Message | From, to | Answer or effect |
| --- | --- | --- |
| `{negotiate_protocol, Nodes}` | a local caller of `mnesia_monitor:negotiate_protocol/1`, to `mnesia_monitor` | a process of the monitor's asks each node's monitor through `rpc:multicall(Nodes, mnesia_monitor, call, [Msg])`; the reply lists the nodes that accepted |
| `{negotiate_protocol, Mon, Version, Protocols}` | the asking node's negotiator, to `mnesia_monitor` by `gen_server:call` | `{Node, {accept, MonPid, Version, Protocol}}`, linking `Mon`; or `{Node, {reject, MonPid, Version, Protocol}}`. `Version` is mnesia's (`"4.27"`), `Protocols` `[{8,6}, {8,5}, {8,4}, {8,3}]`; a monitor accepts its own protocol when the asker lists it, `{8,5}` when the asker lists it first |
| `{connect_nodes, Nodes}` | `mnesia_recover:connect_nodes/1`, to `mnesia_recover` | negotiates with the nodes not yet in `recover_nodes`, asks each `mnesia_lib:is_running/0`, adds those that run (`yes` or `starting`) to `recover_nodes` and replies `{NewNodes, AlreadyConnected}` |
| `{decisions, Node, [{trans_tid, serial, N}]}` | the connecting node's `mnesia_recover`, to the new nodes' by `gen_server:abcast` | the receiver adds `Node` first in `recover_nodes`; a serial larger than its own makes its serial `N + 1` |
| `{what_decision, Node, Decision}` | a node unsure of a transaction, to `mnesia_recover` by cast | the serial of the decision's tid is taken as above; the reply is `{decisions, Node, [D]}` with this node's decision on the tid, or the asked one when it has none |

- **Variables:** after a negotiation both nodes have `{protocol, Other}` = `{{8,6}, false}` in `mnesia_gvar` (the flag says
  that the protocol needs conversion); the asking node also sets `protocol_version` to `{8,6}`. The monitors stay
  linked.
- **Going down:** when the other node's mnesia stops, or the connection breaks (`noconnection`), the link's `EXIT`
  reaches the monitor. Then the node leaves `recover_nodes` and `{current, db_nodes}`, the running node records
  `{mnesia_down, {node, Node}, Date, Time}` in `mnesia_decision` (and `{mnesia_down, Node, Date, Time}` in
  `LATEST.LOG` on a disc schema), and reports the `{mnesia_down, Node}` system event. A node whose mnesia is stopped
  answers no negotiation (`[]`).
- **Partitions:** `mnesia_recover:has_mnesia_down/1` is whether such a record exists;
  `mnesia_monitor:has_remote_mnesia_down/1` answers `{true, node()}` when it does and the schema has no master nodes.
  When a node comes up (`nodeup`) that a running node recorded down, the running node asks it
  `has_remote_mnesia_down`; when that node recorded this one down too, it reports
  `{inconsistent_database, running_partitioned_network, Node}`, which the default event handler logs as an error.
  `connect_nodes` makes the same check with `starting_partitioned_network`. Records stay until the nodes merge (MIM1H5).

## Schema merge (MIM1H5)

How two running nodes with RAM schemas merge them, observed with `mnesia:change_config(extra_db_nodes, [B])` on node a
and with node b starting again with `extra_db_nodes` naming a. The merge is a schema transaction of a process the
merging node starts: it locks the schema on every node and commits the merged definitions on both sides with OTP's
`asym_trans` commit. In the tables, a is the merging node.

| Message | From, to | Answer or effect |
| --- | --- | --- |
| `{merging_schema, A}` | a, to `mnesia_controller` on a and on the nodes given, by cast | nothing observable |
| `{connect_nodes, Nodes}` | a, to its own `mnesia_recover` | as when [connecting](#connecting-mim1h4); only nodes not yet running with a are merged, so a second call merges nothing |
| `{Pid, {write, Tid, {schema, '______WHOLETABLE_____'}}}` | the transaction, to `mnesia_locker` of a, then of each running node and each node to merge | `{mnesia_locker, Node, granted}`; an ended transaction sends `{release_tid, Tid}` to each locker when it aborts |
| `mnesia_controller:get_remote_cstructs/0` | the transaction, to the first node to merge, by `rpc` | `{cstructs, Cstructs, RunningNodes}` ([remote queries](#remote-queries-mim1h3)) |
| `{Pid, {ask_commit, asym_trans, Tid, #commit{}, DiscNodes, RamNodes}}` | the transaction, to each other node's `mnesia_tm` | a participant process of that node answers `{mnesia_tm, Node, {vote_yes, Tid, Participant}}`; the commit's `decision` is `#decision{tid = Tid, outcome = presume_abort}` with the same node lists, and its `schema_ops` are the merge's operations below |
| `{Tid, pre_commit}` | the transaction, to each participant | `{mnesia_tm, Node, {acc_pre_commit, Tid, Participant, true}}` |
| `{Tid, committed}` | the transaction, to each participant | the participant applies the operations and answers `{mnesia_tm, Node, {schema_commit, Tid, Participant}}` |
| `{Tid, simple_commit}` | a transaction that only locked, to that node's `mnesia_tm` | the node's locks of the transaction go |
| `{im_running, A, Friends}` | a, after a merge, to the controllers of each side, naming the other side; after a transaction that merged nothing, to its own controller naming itself | the controller casts `{adopt_orphans, node(), Tabs}` to each of `Friends`, `Tabs` being the tables it holds, without the schema |
| `{adopt_orphans, Node, Tabs}` | a controller, to another node's or its own | `{log_mnesia_up, Node}` to its `mnesia_recover` (which removes Node's `mnesia_down` record), then the `{mnesia_up, Node}` system event; `Tabs` are read and written on Node |
| `{schema_is_merged, [], late_merge, []}` | a, after the merges, to the controllers of a and the connected nodes, by `gen_server:multi_call` | `ignore` |
| `disc_load_intents` | a node loading its tables at start, to each running node's controller, under the lock `{'______GLOBAL_____', mnesia_late_disc_load}` | `{ok, Node, Tabs}`, the tables that node holds |
| `{i_have_tab, Tab, Node}` | a node that loaded its replica, to the other controllers | `Tab` is read and written on Node |

- **Operations:** the commit's `schema_ops` are `{op, announce_im_running, A, SchemaList, Running, RemoteRunning}`
  (`SchemaList` the other side's schema definition as a list, `Running` a's running nodes, `RemoteRunning` the other
  side's), then `{op, merge_schema, List}` for each definition the other node sent, in its order, merged with a's, then
  for each table only a has. On every node, the nodes of the other side join `{current, db_nodes}` (first) at the
  commit, so `running_db_nodes` lists them.
- **The schema's definition:** its node lists are the other side's followed by a's, its cookie stays a's, and its
  version becomes `{{Major + 1, 0}, {A, erlang:timestamp()}}`, `Major` the larger of the two. Another table known on
  both nodes must have the same cookie; otherwise the transaction throws OTP's message and `change_config/2` returns
  `{error, {merge_schema_failed, "Bad cookie in table definition Tab: A = CsA, B = CsB\n"}}`, the cstructs printed with
  `~w`. The nodes stay connected (`recover_nodes`) but not running together.
- **Table information:** a table held on the other node has that node as `where_to_read`, `where_to_write` and
  `active_replicas`, storage type `unknown` and size 0 here. A node joining a table's replicas comes first in
  `active_replicas` and last in `where_to_write`; `where_to_commit` lists `{Node, Storage}` in node order. When the other
  node's mnesia goes down, its replicas are no longer active: its tables are read `nowhere` and written on no node,
  while their definitions stay.
- **Nothing to merge:** `change_config(extra_db_nodes, Nodes)` runs the schema transaction even when no node is left
  to merge (it counts as a committed transaction); the node's own `im_running` follows only a merge.
- **Starting again:** a node started with `extra_db_nodes` merges at start: its new schema takes the definitions of
  the running nodes, including its own tables there, so a RAM table of its own comes back empty and is loaded at once.
  Both nodes report `{mnesia_up, Node}` of each other; the merging node also reports its own after the second
  transaction, which merges nothing.
- **Serials:** a node asked to commit another node's transaction, or to end it (`ask_commit`, `simple_commit`), takes
  its serial as when [connecting](#connecting-mim1h4).

## Remote reads and dirty writes (MIM1H6)

How a node reads and dirty-writes a table that only another node holds, observed after a [merge](#schema-merge-mim1h5)
of two RAM schemas. In the tables, a reads and writes, b holds the table.

| Message or call | From, to | Answer or effect |
| --- | --- | --- |
| `{apply, M, F, Args}` | a's caller, to b's `mnesia_rpc` by `gen_server:call` ([remote queries](#remote-queries-mim1h3)) | `M:F(Args)` on b's replica; a read that fails there answers `{badrpc, {'EXIT', Reason}}` |
| `{Ref, {sync_dirty, {dirty, Pid}, #commit{}, Tab}}` | a's caller, to b's `mnesia_tm` | b's `mnesia_tm` applies the commit, reports its table events with the activity id `{dirty, Pid}` and sends `{mnesia_tm, b, {dirty_res, Result}}` to `Ref`, an alias of the caller |
| `{Pid, {async_dirty, {dirty, Pid}, #commit{}, Tab}}` | a's caller, to b's `mnesia_tm` | b applies the commit and answers nothing |

- **Reads:** each dirty read is one `apply` on the node `where_to_read` names, the same call that a node holding the
  table makes locally:

  | Read | `M:F(Args)` |
  | --- | --- |
  | `dirty_read/2` | `mnesia_lib:db_get(Tab, Key)` |
  | `dirty_first/1`, `dirty_last/1`, `dirty_next/2`, `dirty_prev/2` | `mnesia_lib:db_first(Tab)`, `db_last(Tab)`, `db_next_key(Tab, Key)`, `db_prev_key(Tab, Key)` |
  | `dirty_slot/2` | `mnesia_lib:db_slot(Tab, Slot)` |
  | `dirty_match_object/2` | `mnesia:remote_dirty_match_object(Tab, Pattern)` |
  | `dirty_select/2`, `dirty_select_reverse/2`, `dirty_all_keys/1` | `mnesia:remote_dirty_select(Tab, Spec)`, `remote_dirty_select_reverse(Tab, Spec)`; `dirty_all_keys/1` selects the keys |
  | `dirty_index_read/3` | `mnesia_index:dirty_read2(Tab, Value, Pos)` |
  | `dirty_index_match_object/3` with the key unbound | `mnesia_index:dirty_match_object(Tab, Pattern, Pos)` |
  | `select/4`, `select_reverse/4` in a dirty activity | `mnesia_lib:db_select_init(Storage, Tab, Spec, N)` or `db_select_rev_init(...)`, then `db_select_cont(Storage, Cont, Spec)` per chunk; `Storage` is the replica's storage on b, and b repairs the `ets` continuation with `Spec` |

  Folds and `all_keys/1` in a dirty activity are built from these reads.
- **Failures:** a read that fails on b (`{badrpc, {'EXIT', _}}`) aborts with `{badarg, Args}`, `Args` the apply's
  arguments, once a has checked that b still holds the table; a table that nobody holds aborts with
  `{no_exists, Args}`. `table_info/2` answers from a's own variables: `size` and `memory` are 0, `storage_type`
  `unknown`. An `ets` activity reads a's own `ets` tables and fails with `badarg`.
- **Writes:** `dirty_write`, `dirty_delete`, `dirty_delete_object` and `dirty_update_counter` check the record on a,
  then send b a `presume_commit` commit whose one item, as in [the log](#directory-files-mim1h1), is in the field of
  the table's storage on b (`{{Tab, Key}, {RecordName, Incr}, update_counter}` for a counter). The commit goes
  `sync_dirty` in a `sync_dirty` activity and whenever a holds no replica of the table, even in an `async_dirty`
  activity; only a node that holds a replica itself sends the other replicas `async_dirty` commits in an
  `async_dirty` activity. A write's result is b's `dirty_res`: `ok`, or a counter's new value.
- **The holder going:** when b goes while a waits for its `dirty_res`, the write returns
  `{'EXIT', {aborted, {node_not_running, b}}}` instead of failing. Once a recorded b's `mnesia_down`, the table is
  read `nowhere`: reads abort with `{no_exists, Args}`, writes with `{no_exists, Tab}`.

## Remote locks (MIM1H7)

How a transaction locks a table that only another node holds, observed after a [merge](#schema-merge-mim1h5) of two
RAM schemas. In the tables, a's transaction locks a table that b holds. Requests go to each lock manager as raw
messages `{Pid, Request}`, and it answers `{mnesia_locker, Node, Reply}` to `Pid`, the transaction's process. a's own
lock manager answers a read of a record held on a `{granted, lookup_in_client}`, and the transaction reads the record
itself.

| Request to b's `mnesia_locker` | Lock | Grant |
| --- | --- | --- |
| `{read, Tid, {Tab, Key}}` | read lock of the record, on the node that `where_to_read` names | `{granted, Records}`, the records read under the lock |
| `{read, Tid, {Tab, '______WHOLETABLE_____'}}` | read lock of the table: `lock/2`, `match_object`, `select`, walks, folds, `all_keys` | `{granted, ok}`; the reads follow through b's `mnesia_rpc` ([remote reads](#remote-reads-and-dirty-writes-mim1h6)) |
| `{write, Tid, Oid}` | write lock of a record or table, on each node of `where_to_wlock` | `granted` |
| `{read_write, Tid, {Tab, Key}}` | `read/3` with `write`: write lock and read | `{granted, Records}` |
| `{ix_read, Tid, Tab, Value, Pos}` | `index_read/3`: read lock of the table and the index read | `{granted, Records, ['______WHOLETABLE_____']}` |
| `{read, Tid, {'______GLOBAL_____', Item}}`, `{write, ...}` | `lock({global, Item, Nodes}, Kind)`, on each listed node | `{granted, ok}`, `granted` |
| `{sync_release_tid, Tid}` | a restarting transaction's locks | `{tid_released, Tid}` |

| Message | From, to | Effect |
| --- | --- | --- |
| `{not_granted, #cyclic{}}` | b's lock manager, to a younger transaction asking for a lock that an older one holds | `#cyclic{node = b, oid = Oid, op = Op, lock = Lock, lucky = HolderTid}`; the transaction restarts with the same tid |
| `{Tid, simple_commit}` | a's transaction at its commit, to b's `mnesia_tm`, when it only took locks on b | b releases the transaction's locks and takes its serial as when [connecting](#connecting-mim1h4) |
| `{release_tid, Tid}` | a's aborting transaction, to each lock manager it used | the locks go |
| `{what_happened, unclear, Tid}` | a's `mnesia_tm`, when a transaction's process dies, to b's `mnesia_recover` by `gen_server:call` | `{ok, unclear}` for a transaction b decided nothing about |
| `{Tid, {do_abort, {mnesia_down, a}}}` | a's `mnesia_tm`, after that question, to b's `mnesia_tm` | b releases the transaction's locks |
| `{apply, mnesia_tm, fixtable, [Tab, true, Pid]}` | a's transaction before it walks the table or reads `select/4` chunks, to b's `mnesia_rpc` | b answers its node name; a's `mnesia_tm` sends `[Tab, false, Pid]` once the transaction ends |

- **Waiting:** an older transaction (smaller tid) asking for a lock that a younger one holds waits in b's queue:
  `system_info(lock_queue)` on b lists `{Oid, Op, Pid, Tid, HolderTid}` with a's process and tid. It is granted, in the
  grant form above, when the holder ends. A tid's counter is its node's; a lock request does not exchange serials, so
  an older transaction is the one whose node counted fewer transactions.
- **Restarts:** a refused transaction restarts with the same tid: it asks a's `mnesia_tm` to restart, sends
  `sync_release_tid` to every lock manager it used, its own node's included, waits for each `tid_released`, and runs
  its fun again after a pause. The restarts count in a's `transaction_restarts`, not b's.
- **Ends:** a commit sends `simple_commit` to the other nodes' `mnesia_tm` and releases its own node's locks; an abort
  sends `release_tid` to each lock manager, its own node's included. A nested transaction's locks stay until the outer
  one ends.
- **Sticky locks:** a sticky lock of a table held elsewhere aborts with `{not_local, Tab}` before any request. A node
  whose table is stuck to it asks its own lock manager `{test_set_sticky, Tid, Oid, Lock}` (`not_stuck` the first
  time, `granted` once stuck) and sends it `{stick, Oid, Node}`; other nodes' locks of that table are granted as usual.
- **A node going down:** a transaction waiting for b's grant when b goes down is told so (`{mnesia_down, b}`), releases
  its locks on every node and restarts; once b's tables are read nowhere it aborts with `{no_exists, Tab}`. a's lock
  manager drops the locks of b's transactions.

## Two-phase commit (MIM1H8)

How transactions and schema changes commit on several nodes, observed after a [merge](#schema-merge-mim1h5) of two RAM
schemas. In the tables, a coordinates: a's transaction process sends each other node holding a written table one
`#commit{}` and gathers the answers. Each commit's items are `{{Tab, Key}, Value, Op}` in the field of the table's
storage on that node, as in [the log](#directory-files-mim1h1).

| Message | From, to | Answer or effect |
| --- | --- | --- |
| `{Pid, {ask_commit, Protocol, Tid, #commit{}, DiscNodes, RamNodes}}` | a's transaction, to each other node's `mnesia_tm` | see each protocol below; the node takes the transaction's serial as when [connecting](#connecting-mim1h4) |
| `{mnesia_tm, b, {vote_yes, Tid}}` | b's `mnesia_tm`, to a (`sym_trans`, `sync_sym_trans`) | a sends `{Tid, do_commit}` to b's `mnesia_tm` and commits its own part; b applies its commit, reports its table events and completed activity with a's tid, and releases the transaction's locks |
| `{mnesia_tm, b, {committed, Tid}}` | b's `mnesia_tm`, to a, after `do_commit` (`sync_sym_trans`) | a waits for it before the transaction returns |
| `{mnesia_tm, b, {vote_yes, Tid, Participant}}` | a process b's `mnesia_tm` starts, to a (`asym_trans`) | a sends `{Tid, pre_commit}` to `Participant` |
| `{mnesia_tm, b, {acc_pre_commit, Tid, Participant, SchemaOps}}` | the participant, to a | `SchemaOps` is `true` when the commit has schema operations; a sends `{Tid, committed}` to the participant and commits its own part |
| `{mnesia_tm, b, {schema_commit, Tid, Participant}}` | the participant, after applying schema operations, to a | a waits for it before the schema change returns; a commit of records only is not confirmed |
| `{mnesia_tm, b, {vote_no, Tid, {aborted, Reason}}}` | b, to a | the transaction aborts with `Reason` and releases its locks on every node |
| `{Tid, {do_abort, Reason}}` | a, to b's `mnesia_tm` or participant | b drops the commit and releases the transaction's locks |

- **Protocols:** writes of tables replicated on the same nodes commit with `sym_trans` (`sync_sym_trans` in
  `sync_transaction/1` and `clear_table/1`); writes of tables on different nodes, of a table with `majority`, and every
  schema change commit with `asym_trans`, also in a sync transaction. A transaction that only read ends with
  `simple_commit` ([remote locks](#remote-locks-mim1h7)).
- **Commits and node lists:** a node gets a commit when the first item for it comes, in front of those before it; a's
  own commit is always there, empty when a holds none of the written tables. `RamNodes` lists the commits' nodes in
  that order, and `DiscNodes` those whose schema is on disc. An `asym_trans` commit's `decision` is
  `#decision{tid = Tid, outcome = presume_abort, disc_nodes = DiscNodes, ram_nodes = RamNodes}`, a `sym_trans` one's
  `presume_commit`.
- **Schema changes:** every running node gets the schema operations of a change as `schema_ops`, whether it holds the
  table or not, after the schema's write lock on each of them; a new table is also write locked on its running nodes,
  the last first. A node holding a new replica creates its `ets` table before voting, and an `ets` table of that name
  makes it vote no with `{system_limit, Tab, {"Failed to create ets table", badarg}}`. A new table's replicas on the
  running nodes are active at once: each node lists itself first in `active_replicas` and `where_to_write`. A table made
  `read_only` lists its active replicas in reverse, and made writable again, its own node first. Each change of a
  definition brings its table to the front of `system_info(tables)` on every node.
- **Outcomes:** each node taking part keeps the transaction's outcome; `{what_happened, Default, Tid}` to its
  `mnesia_recover` answers `{ok, committed}` for both protocols, `{ok, Default}` for a transaction it never saw.
  Participants do not count the transactions in `transaction_commits`.
- **A participant going down** before it votes: the transaction counts a failure and a restart, and commits on the
  nodes still running. **The coordinator going down** before the participant voted, after it voted or after the
  pre-commit: the participant aborts the transaction, unless another node taking part knows it committed, and its
  locks go.

## Replicas and loading (MIM1H9)

How a node gets a replica's records from another node, observed after a [merge](#schema-merge-mim1h5) of two RAM
schemas. In the tables, b gets the replica and a holds the table. The receiver is the process that needs the records:
the participant of b in `add_table_copy/3` and `move_table_copy/3` (before it votes), the coordinator itself when it
gets the copy, or a loader process of b at start.

| Message | From, to | Answer or effect |
| --- | --- | --- |
| `mnesia_lib:set({mnesia_loader, active_trans}, Tid)` | b's receiver of a new copy, to a, by `rpc` | sets the variable on a; a sender of a table locked by the transaction locks it under that tid |
| `{send_table, Tab, Receiver, Storage, Reason}` | b's receiver, to a's `mnesia_controller` by `gen_server:cast` | a's controller starts a sender; `Reason` is `{add_table_copy, Tid}` for a new copy and `load` at start |
| `{Sender, {first, Size}}` | the sender, to the receiver | `Size` is the table's size; the receiver makes its replica's table and answers `{Receiver, more}` |
| `mnesia_controller:call({update_where_to_write, [add, Tab, b], Sender})` | the sender, to b by `rpc`, then to its own controller | the table is written on b from then on (`true`) |
| `{Sender, {more, Records}}`, or `{Sender, {more_z, zlib:compress(term_to_binary(Records))}}` with `send_compressed` above 0 | the sender, to the process that answered `more` | all chunks follow at once, each of about `max_transfer_size` bytes of records, then `{Sender, no_more}`; the receiver acknowledges each with `more`, which the sender of a RAM replica does not wait for |
| `{block_table, [Tab], Sender}`, `{add_active_replica, [Tab, b, Storage, AccessMode], Sender}` | the sender, to b's `mnesia_controller` by `gen_server:call`, the second also to its own | `ok`, `true`: b's replica is active on both nodes |
| `{Sender, {no_more, nobin}}` | the sender, to the receiver | the receiver answers `{Pid, no_more}`, `Pid` any process of its loader |

- **Commits meanwhile:** before the first chunk, the sender subscribes the receiver to the table's simple events, so
  that it gets the commits made while the records go; the sender ends the subscription after the receiver's
  `no_more`.
- **Copies:** the operations are `{op, add_table_copy, Storage, Node, Cs}` and `{op, del_table_copy, Storage, Node,
  Cs}`, `Cs` the new definition. `move_table_copy/3` commits `{op, add_table_copy, Storage, To, Cs1}`, `{op,
  sync_trans}` and `{op, del_table_copy, Storage, From, Cs2}`, `Cs1` with both nodes (`To` first) and `Cs2` without
  `From`, both of the same version. At `sync_trans` each participant sends `{sync_trans, Participant}` to the
  coordinator and waits for its `{sync_trans, Coordinator}` before it prepares the rest.
- **Properties:** the new replica's node lists itself first in `active_replicas` and `where_to_write`, `load_node` is
  the sender's node and `load_reason` `{dumper, {add_table_copy, Tid}}`; the sender's node lists the new node first in
  `active_replicas` and last in `where_to_write`. A node whose copy goes keeps its `load_node` and `load_reason`, and
  reads and writes the table on the other copies.
- **At start:** a node starting with `extra_db_nodes` loads each replica that a running node holds too from it, with
  `load_reason` `{active_remote, Node}`; a replica that no running node holds comes back empty, `load_reason`
  `local_only`.

## Disc nodes (MIM1H10a)

How nodes whose schemas are on disc are created, start, load their tables and log their commits, observed with two
nodes a and b, each with its own directory.

### Creating the schema

`create_schema([A, B])` on a asks each node `mnesia_schema:remote_read_schema/0` ([remote
queries](#remote-queries-mim1h3)), then a process of a installs the same fallback on every node:

| Message | From, to | Answer or effect |
| --- | --- | --- |
| `global:set_lock({{mnesia_table_lock, schema}, Master}, Nodes)` | the master, on all the nodes | held until the end, then `global:del_lock/2` |
| `spawn_link(Node, mnesia_bup, fallback_receiver, [Master, Args])` | the master, on each node, a included | the receiver registers as `mnesia_fallback`; `Args` is OTP's `#fallback_args{}`: `{fallback_args, Opaque, global, mnesia_backup, true, undefined, undefined, undefined, [], [], keep_tables}`, `Opaque` a file name in the master's directory |
| `{Master, {start, Header, SchemaItems}}` | the master, to each receiver | the receiver writes the master's backup header and `{schema, schema, CreateList}` items to a temporary file of its directory and answers `{Receiver, ok}` |
| `{Master, swap}` | the master, to each receiver | the file becomes `FALLBACK.BUP`; `{Receiver, ok}` |
| `{Master, stop}` | the master, to each receiver | the receiver exits with reason `stopped`; the master, linked and trapping exits, waits for each exit. A receiver that exits otherwise while the master waits fails the installation (`{error, {"Cannot install fallback", {'EXIT', Pid, Reason}}}`) |

### Starting

- **Connecting:** a node starting on a disc schema connects to the schema's other nodes and its `extra_db_nodes` as
  a [merge](#schema-merge-mim1h5) does. A merge sends `{op, merge_schema, List}` only for the definitions that differ
  from the merging node's (or that it lacks), so two nodes with the same schema commit `announce_im_running` alone. A
  participant whose commit holds no other operation answers `{acc_pre_commit, Tid, Participant, false}` and sends no
  `schema_commit`; with other operations it answers `true` and confirms them.
- **The late loader:** after the merge, the starting node takes the write lock `{'______GLOBAL_____',
  mnesia_late_disc_load}` on the running nodes and calls `disc_load_intents` on their controllers. A node answers
  `{ok, Node, Tabs}`, `Tabs` its loaded tables and those it will load from its own disc; a table it waits for is not
  among them.
- **Where each replica loads from:**

  | Replica | `load_reason` |
  | --- | --- |
  | active on a running node (its `adopt_orphans` or the sender's `add_active_replica` said so) | `{active_remote, Node}`, transferred from it |
  | on this node only | `local_only` |
  | RAM, the table on no other node's disc | `ram_only`: empty |
  | on disc, no other node holding it on disc, or this node logged each of those down before it started | `initial`: from its files |
  | RAM with a disc copy elsewhere, or on disc while another disc holder may have seen it last | waits |

  A replica that waits keeps its `storage_type` and stays among `local_tables`; it is read `nowhere`, written on no
  node, its `size` is `undefined`, and `wait_for_tables/2` times out on it. A node that loaded a table on its own
  casts `{i_have_tab, Tab, Node}` to the other controllers; a node waiting for the table then loads it from Node. A
  running node whose own replica waits when the other node comes up loads it from its disc as `{adopt_orphan,
  Node}` once no other node will.
- **While a replica loads,** it is read `nowhere` even after the other node's replica is active, until its own load
  ends; `wait_for_tables/2` waits for it.

### Disc replicas over nodes

- A `disc_copies` replica is sent as records, as a RAM one. The receiver writes them to a new `DCD` file of its
  directory, with its own header.
- Between two `disc_only_copies` replicas the records go as dets chunks: `{Sender, {first, Size, {ErtsVersion,
  Format}}}`, `Format` the table's `bchunk_format`, then `{Sender, {more, Chunks}}` with the chunks of `dets:bchunk/2`
  (its format first) and `{Sender, no_more}`. Each chunk goes to the process that answered `more`; the receiver
  answers `more` after each chunk and fills its dets file with `dets:init_table/3` (`{format, bchunk}`). OTP's
  receiver also takes records after `{Sender, {first, Size}}`.

### Logs on disc nodes

- **Commits over nodes:** an `asym_trans` commit is logged on each disc node as its `#commit{}` under the
  transaction's `#decision{tid = Tid, outcome = presume_abort, disc_nodes = Disc, ram_nodes = Ram}`, with this node's
  RAM writes as well as its disc writes, then `{decision, Tid, committed, Unsure, []}`, `Unsure` the other disc nodes
  not yet certain. Each node casts `{im_certain, Node, {decision, Tid, committed, Unsure, []}}` to the other disc
  nodes' `mnesia_recover` once it committed; a node hearing it logs the decision again without that node, so that
  each node logs two decisions per commit of two nodes. A `sym_trans` commit is logged as on one node
  (`presume_commit`), and a commit of RAM tables only is not logged.
- **Schema transactions** commit with the same records; a merge's commit holds only its `merge_schema` operations,
  and a merge that announces the running nodes alone logs only its decisions. A schema transaction while the other
  node is down names this node alone (`[A]`) and logs one decision.
- **Nodes up and down:** `{mnesia_up, Node, Date, Time}` is logged when another node's mnesia comes up with this
  one (its `adopt_orphans`), `{mnesia_down, Node, Date, Time}` when it goes. A dump keeps the down records that no
  later up record removed in `DECISION_TAB.LOG`'s `decision_list`, as `{mnesia_down, Node, Date, Time}`; the next
  start reads them back to decide its loads, and `mnesia_decision` keeps them as `{mnesia_down, {node, Node}, Date,
  Time}`.

### Schema changes with a disc node down

| Change | With b down |
| --- | --- |
| `create_table/2` with a copy on b | `{aborted, {not_active, Tab, b}}` |
| `create_table/2` of a RAM table on b only | commits; b gets it at its next start ([merge](#schema-merge-mim1h5)) |
| `delete_table/1`, `add_table_index/2`, `del_table_index/2` of a table with a replica on b | `{aborted, {not_active, "All replicas on diskfull nodes are not active yet", Tab, [b]}}` |

## Master nodes, majority and copies (MIM1H10b)

Observed with two disc nodes a and b, as [disc nodes](#disc-nodes-mim1h10a).

### Master nodes

- **Setting them:** `set_master_nodes(Tab, Nodes)` and `set_master_nodes(Nodes)` check the table and the nodes holding
  it as on one node, and log `{master_nodes, Tab, Nodes}` (`[]` clears them). On a node whose mnesia is stopped they
  check the definitions of `schema.DAT` and append the entries to `LATEST.LOG`, which the next start reads; with only a
  fallback in the directory they return `{error, "No schema file exists"}`, and without a directory `ok`.
- **Loads at start:** a table with master nodes loads only from them. When this node is one of them, its replica
  loads from its files, or empty for a RAM one, with `load_reason` `local_master`, even when another node's replica
  is active; otherwise from a running master whose replica is active (`{active_remote, Node}`), else it waits, even
  for a replica on disc here, until a master loaded it. A table held on this node alone still loads `local_only`.
- **`force_load_table(Tab)`** of a replica that waits loads it from this node's files (or empty) with `load_reason`
  `forced_by_user`, and answers `yes`; of a loaded table `yes`; `{error, {no_exists, Tab}}` for an unknown table and
  `{error, {bad_type, schema}}` for the schema.

### Majority

A write lock on a table with `majority` aborts the transaction with `{aborted, {no_majority, Tab}}` unless more than
half of the table's replicas are active: with two replicas, one node alone has no majority. Read locks and dirty
writes are not checked; a read with a write lock (`read(Tab, Key, write)`) is. A schema transaction that locks such a
table, as `del_table_copy(schema, Node)` locks each table of the node, aborts the same way.

### Copies between disc nodes

- `add_table_copy/3` and `move_table_copy/3` of a disc replica on another node: the receiver gets the records as
  [table transfers](#replicas-and-loading-mim1h9) do and writes its `DCD` or `dets` file before the commit; its
  `load_reason` is `{dumper, {add_table_copy, Tid}}`. A node may move a replica to itself.
- `change_table_copy_type/3` of another node's replica commits `{op, change_table_copy_type, Node, From, To, List}`;
  that node converts its replica as for its own change, and checks it the same way. A replica converted from
  `disc_only_copies` to `ram_copies` or `disc_copies` takes `load_reason` `{dumper, change_table_copy_type}`; other
  conversions keep theirs.
- A schema transaction's decision names the schema's disc nodes as its operations leave them: changing a RAM node's
  schema to disc names that node among the disc nodes, and the node then writes its directory.
- **A node leaving:** `del_table_copy(schema, B)` with b's mnesia stopped removes b from every definition; a table that
  only b held goes. While b runs it aborts with `{active, "Mnesia is running", B}`. `delete_schema([B])` then removes
  b's files; b starts on a RAM schema, joins with `change_config(extra_db_nodes, [A])` as a RAM node, and
  `change_table_copy_type(schema, B, disc_copies)` from a makes it a disc node again.

### Certainty

A coordinator casts `{im_certain, Node, Decision}` to the other disc nodes right after it sends `committed`, before
its own part of the commit; a participant sends it before its `schema_commit`. A node that dumps its log while a
decision is not yet certain keeps that decision in `DECISION_TAB.LOG`.

A coordinator logs its commit, then its committed decision naming the other disc nodes, then the decision again
without each node whose `im_certain` came. A participant hears the coordinator's `im_certain` about when it logs its
own decision, so it logs `[A]` then `[]`, or `[]` twice.

## Partitions and orphans (MIM1H10c)

Observed with two disc nodes a and b, as [disc nodes](#disc-nodes-mim1h10a); the connection broke with
`erlang:disconnect_node/1` while both ran.

### Partitions

- **Apart:** each node logs the other's mnesia down and goes on alone: its tables are read and written on it only,
  and a transaction on it commits on it alone.
- **Meeting again:** when the nodes connect again, each asks the other's `has_remote_mnesia_down`, as when
  [connecting](#connecting-mim1h4), and reports `{inconsistent_database, running_partitioned_network, Node}` when the
  other node logged it down too and has no master nodes for the schema. The nodes stay apart: `running_db_nodes`
  lists each node alone and writes stay on each side.
- **Starting again:** a node that starts while the other runs reports `{inconsistent_database,
  starting_partitioned_network, Node}`, unless it has master nodes for the schema; the running node reports nothing.
  The starting node merges with the running one, both drop their down records, and its replicas load from the
  running node (`{active_remote, Node}`): what it wrote apart is gone.

### Orphans

A replica that waits at start (see [starting](#starting)) is an orphan once another node comes up while no node will
load it:

- **Who adopts:** when a node comes up (its `adopt_orphans`), the smallest of the table's disc holders adopts the
  replicas that wait there, when the node that came up holds the table on disc, no replica is active and every disc
  holder runs. The load reason names the adopting node, `{adopt_orphan, A}`, whether a was the node starting or the
  node running.
- **Loading:** an adopted table loads from the adopting node's files through its `mnesia_late_loader`, which takes the
  lock of late disc loads, asks the other running nodes' `disc_load_intents` and leaves them the tables they will load
  from their own disc; those load from them once they announce them (`i_have_tab`).
- **Master nodes:** a table with master nodes on the adopting node is loaded by them instead: the controller sends
  `{Pid, {maybe_async_late_disc_load, Tabs, {adopt_orphan, A}}}` to each running master's `mnesia_late_loader`, which
  loads them from its files with that reason; the adopting node then loads them from the master.

### Commit protocol

A transaction commits with `asym_trans` when the tables it writes differ in `where_to_commit`, nodes and storage alike
(`disc_copies` and `ram_copies` tables on the same nodes, for one), or have a majority; it then logs its commit under
`{decision, Tid, presume_abort, DiscNodes, RamNodes}` even when this node commits alone (one node, or a partition),
with its RAM writes beside the disc ones when it wrote on disc. Tables committing on the same nodes with the same
storage use `sym_trans`.

## Checkpoints over nodes (MIM1H11a)

Observed with two disc nodes a and b. A checkpoint is OTP's `#checkpoint_args{}`, which nodes pass to each other:
`{checkpoint_args, Name, AllowRemote, RamOverridesDump, Nodes, Node, Now, Cookie, Min, Max, PendingTab, WaitForOld,
IsActivated, IgnoreNew, Retainers, Iterators, Supervisor, Pid}`, its retainers `{retainer, Name, Tab, Store, Writers,
ReallyRetain}`. `RamOverridesDump` is the list of tables, `[]` without the option.

### Activating

- **Writers:** a `max` table's retainer is written by each active replica (`[a, b]` for a table on both, in the order
  of `active_replicas`); a `min` table's by the replica on the activating node, else by the first active one unless
  `{allow_remote, false}`. The checkpoint's nodes are the writers of all its retainers, observed greater node first
  (`[b, a]`), which `activate_checkpoint/1` returns.
- **Preparing:** the activating node calls `mnesia_tm:prepare_checkpoint(Cp)` on each node of the checkpoint (an rpc),
  with the stores `undefined` and its process fields unset; each answers `{ok, Name, IgnoreNew, Node}`. Then it calls
  `mnesia_checkpoint:call(Name, collect_pending)` on each, answered `{ok, PendingTids}`, then
  `mnesia_checkpoint:call(Name, {activate, Pending})`, answered `activated`. A node that is not a writer of any
  retainer has no part: neither a process nor the checkpoint's rows.
- **Each node's part:** a process under `mnesia_checkpoint_sup` and the gvar rows `{checkpoint, Name}`, `checkpoints`
  and a `{Tab, {retainer, Name}}` row for each retainer of the checkpoint. A node writing a table's retainer has an
  ets store `{ets, Ref}` in its row, `{Tab, checkpoints}` and the `{checkpoints, Names}` entry of the table's commit
  work; a RAM table's row there says `false` (its dump is kept, nothing retained). On a node not writing it, the row
  keeps the store it was given (`undefined` at activation) and the table has no checkpoint entries.
- **Retaining:** each node retains its own replica's updates, `{Tab, Key, OldRecords}` in its store for the first
  change of a key; the transactions pending at activation are not retained. Clause's node waits for the other nodes'
  commits applying on it to end before it makes its stores.

### Nodes coming and going

- **A node goes:** each node's checkpoints drop it from their retainers' writers (`{mnesia_down, Node}` to each
  checkpoint process); a checkpoint left with a retainer without writers ends there with the error report "Checkpoint
  'Name' has been deactivated, last table copy deleted." and the system event `{mnesia_checkpoint_deactivated, Name}`,
  before the `{mnesia_down, Node}` event. The node that went loses its checkpoints with its processes.
- **A node gets a replica:** the sender of a replica (a start loading from a running node, or `add_table_copy/3`)
  calls `mnesia_checkpoint:tm_add_copy(Tab, Node)` after it made the replica active. Each checkpoint of the table
  that keeps it with most redundancy then calls `mnesia_checkpoint:tm_prepare(Cp)` on the node (an rpc, its current
  `#checkpoint_args{}`), tells its other nodes `{add_retainer, R, Node}` with the new writers appended, and the node
  too (an rpc of `mnesia_checkpoint:cast/2`), then sends each record retained so far as
  `{retain, {dirty, send_retainer}, Tab, Key, OldRecords}`. Messages to another node's checkpoint go through
  `erpc:cast(Node, mnesia_checkpoint, cast, [Name, Msg])`, which answers `{ok, Pid}`. A checkpoint keeping the table
  with least redundancy does not take the new copy.
- **Retaining after joining:** OTP's node that joined a checkpoint this way keeps none of its own later updates, and
  an update drops a key it had been sent; Clause's retains them ([differences](../differences.md#mnesia)).
- **A replica goes:** the node losing its replica of a table (`del_table_copy/2`, or the table deleted) drops itself
  from the retainer's writers on each node of its checkpoints (`{del_retainer, R, Node}`) and its store; a retainer
  left without writers ends the checkpoint with the same report and event. The other node's row keeps its store
  reference with the remaining writers.
- **Deactivating:** `deactivate_checkpoint/1` on a node holding the checkpoint calls
  `mnesia_checkpoint:remote_deactivate(Name)` on each of its nodes, answered `deactivated`; without the checkpoint
  here it returns `{error, {no_exists, Name}}`.

### Backups over nodes

A backup of a checkpoint runs where it is asked and needs the checkpoint there: elsewhere it fails with
`{error, {'EXIT', {error, {error, {no_exists, Name}}}}}`. Each table is read where its retainer is written, here when
this node writes it. Otherwise the backup spawns `mnesia_log:tab_copier(Self, B, Tab)` on a writer, with OTP's
`#backup_args{}` (`{backup_args, Name, Module, Opaque, Scope, PrevName, Tables, Cookie}`), links it and receives:

- it sends `{Self, more, N}`, starting from 0, for each chunk; the copier answers `{Copier, {more, N + 1, Items}}`;
- the copier's chunks: the table's records now (or the keys changed since the previous checkpoint), then an empty
  chunk, then the retained deletes and records;
- after the last chunk is asked for, the copier unlinks from the receiver and answers `{Copier, {last, {ok, dummy}}}`,
  then ends.

The receiver writes each chunk to the backup with the table's name as the records' first element.

## Fallbacks and restores over nodes (MIM1H11b)

Observed with two disc nodes a and b. A fallback's arguments are OTP's `#fallback_args{}`:
`{fallback_args, Opaque, Scope, Module, UseDefaultDir, MnesiaDir, FallbackBup, FallbackTmp, SkipTables, KeepTables,
DefaultOp}`.

- **Installing globally:** `install_fallback/1,2` with the default `{scope, global}` reads the backup's schema; the
  installing node must be one of its disc nodes. Under the global lock `{{mnesia_table_lock, schema}, Pid}` on those
  nodes it spawns `mnesia_bup:fallback_receiver(Master, FA)` on each, itself included, and sends each in turn
  `{Master, {start, Header, SchemaItems}}`, then `{Master, {records, Items}}` for each chunk of records, then
  `{Master, swap}`, each answered `{Receiver, ok}` before the next goes; then `{Master, stop}`. A receiver registers as
  `mnesia_fallback` and answers `{Receiver, {error, {already_exists, Node}}}` when its node holds a fallback or
  installs one; the installation then fails with the first refusal OTP's master hears, so with fallbacks on both
  nodes the node named differs between runs.
- **Local scope:** `{scope, local}` installs on this node alone.
- **Uninstalling globally:** `uninstall_fallback/0` reads this node's `FALLBACK.BUP` (`{error, enoent}` without one,
  a fallback elsewhere left alone), then under the same lock spawns `mnesia_bup:local_uninstall_fallback(Master, FA)`
  on each disc node of the fallback's schema. Each, registered as `mnesia_fallback`, answers `{Pid, started}`; after
  all started the master sends `{Master, do_uninstall}`, and each removes its fallback and answers `{Pid, ok}`.
- **Starting:** both nodes stopped with a global fallback start from it, each node's tables with the backup's records.
  A node with a fallback installed that hears another node's mnesia go down stops too, reporting "(ignoring core) **
  FATAL ** A fallback is installed and Mnesia must be restarted. Forcing shutdown after mnesia_down from Node...":
  stopping both nodes one after the other may report it on the second.

## Events and fragments over nodes (MIM1H12)

Observed with two disc nodes a and b.

- **Subscriptions:** `subscribe({table, Tab, simple | detailed})` needs a replica here: a table held only on another
  node answers `{error, {not_active_local, Tab}}`, a second subscription to the same table and kind
  `{error, {already_exists, What}}`; the others answer `{ok, node()}`.
- **Events:** each node reports the table events of the updates its own replicas apply, whichever node's transaction
  or dirty write made them, with that activity's id (`{tid, N, Pid}` or `{dirty, Pid}` of the other node's process),
  and an activity's `complete` event where it commits. A transaction on several nodes reports its events on each
  node that applies it, in the order that node applies them.
- **Fragmented tables:** fragments are placed over the node pool as on one node, each replica on the node holding
  fewest; any node reads and writes them through `mnesia_frag`, on whichever node holds a fragment. `frag_size` and
  `frag_memory` ask the node a fragment is read from (a fragment without a replica here is not 0).
- **Changing fragments:** `add_frag` and `del_frag` are schema transactions on every running node: the coordinator
  reads the records that move, a fragment held elsewhere read there, and commits `{op, change_table_frag, FragOp,
  BaseDef}`, the new fragment's `create_table` or the last one's `delete_table`, and an `{op, rec, Storage, {{Frag,
  Key}, Records, write | delete}}` for each record moving; each node moves the records of the fragments it holds.
- **Removing a node:** `del_table_copy(schema, Node)` deletes the tables only Node held, fragments among them (a
  fragmented table whose base table only Node held is gone), and takes Node out of each fragmented table's node pool
  (`{op, change_table_frag, {del_node, Node}, Def}` where Node holds no replica).
- **Restoring:** `restore/2` is a schema transaction committed on every node running mnesia (`asym_trans`): each node's
  commit holds the operations (`{op, clear_table, Def}` and `{op, restore_recreate, Def}`, a deleted table's
  `delete_table`) and the backup's records of the tables that node holds after the restore, in the commit's field of
  their storage there. A node clears or makes its tables first, then writes the records. The result lists the
  tables, the last of the backup's schema first.
