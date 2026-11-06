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
