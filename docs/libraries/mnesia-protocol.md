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
  `move_table_copy/3`. Clause writes `{op, add_table_copy, Storage, Node, Cs}`, `{op, del_table_copy, Storage, Node,
  Cs}` and `{op, move_table_copy, From, To, Cs}` until MIM1H9 observes them.
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
