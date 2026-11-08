# mnesia

Clause's `mnesia` application (plan 21, MIM1) exports the OTP 29 interface of `mnesia` (`mnesia-4.27`) and
`mnesia_frag_hash`. The work is split into steps:

- **MIM1A, done:**
  - starting and stopping, and the [configuration parameters](#configuration);
  - the RAM [schema](#schema): `create_table/2` with OTP's checks, `delete_table/1`, `table_info/2`, `system_info/1`,
    `schema/0,1`, `wait_for_tables/2` and `error_description/1`;
  - [dirty access](#dirty-access) to `set`, `ordered_set` and `bag` tables;
  - [indexes](#indexes).
- **MIM1B, done:**
  - [transactions](#transactions), nested or not, with OTP's results, abort terms and restarts;
  - [locks](#locks) and their queue;
  - [activities](#activities) and access modules;
  - schema changes as schema transactions.
- **MIM1C, done:** the other [schema changes](#schema-changes): `clear_table/1`, `transform_table/3,4`, table copies
  and their storage types, user properties, access mode, load order and majority; master nodes, `force_load_table/1`,
  `change_config/2` and `set_debug_level/1`.
- **MIM1D, done:** [disc storage](#disc-storage): `create_schema/1,2` and `delete_schema/1`, the disc schema,
  `disc_copies` and `disc_only_copies` tables, the transaction log with `dump_log/0`, `sync_log/0` and its thresholds,
  `dump_tables/1`, and storage type changes of tables and of the schema.
- **MIM1E, done:**
  - [events](#events): `subscribe/1`, `unsubscribe/1` and `report_event/1`, the event handler's reports and the debug
    levels;
  - [text files](#text-files): `load_textfile/1` and `dump_to_textfile/1`;
  - [query tables](#query-tables): `table/1,2`;
  - [the report](#the-report) of `info/0` and `system_info/0`, and `lkill/0`, `kill/0` and `ms/0`.
- **MIM1F, done:** [checkpoints](#checkpoints), [backups](#backups) through the default backup module `mnesia_backup`,
  traversals, [restores](#restoring) and [fallbacks](#fallbacks).
- **MIM1G, done:** [fragmented tables](#fragmented-tables) with `mnesia_frag_hash` and the access module
  `mnesia_frag`, and [SNMP indexes](#snmp).
- **Declined:** `add_backend_type/2`, which needs external storage backends (`mnesia_backend_type` modules); Clause
  keeps tables in `ets` and `dets` only. It raises `{not_implemented, add_backend_type, 2}`.
- **MIM1H, optional, in steps:** several nodes over distribution, Clause and OTP nodes in one database. MIM1H1, done:
  the [same files](#disc-storage) as OTP's in mnesia's directory, so that OTP and Clause nodes take turns on one
  directory. MIM1H2, done: [schema transactions](#transaction-log) logged as OTP's operations. MIM1H3, done: OTP's
  [process names](#processes) and the [queries other nodes make](#queries-from-other-nodes). MIM1H4, done:
  [connecting nodes](#connecting-nodes). MIM1H5, done: [merging schemas](#merging-schemas). MIM1H6, done:
  [dirty access](#dirty-access) to tables held on another node. MIM1H7, done: [locks](#locks) on other nodes. MIM1H8,
  done: [commits](#transactions) over several nodes. MIM1H9, done: [table copies](#schema-changes) and loads over
  several nodes. MIM1H10a, done: [disc nodes](#disc-nodes) created and started together. MIM1H10b, done: master
  nodes, majority and copies between disc nodes. What OTP writes and sends is recorded in
  [mnesia's files and protocol](mnesia-protocol.md).

Every other documented function works. See the [function inventory](../../library/mnesia/mnesia.terms) and
[differences](../differences.md#mnesia).

## Design

mnesia is written in Erlang over `ets`, `dets` and `disk_log`, in its own structure. Where programs can observe
something, it keeps OTP's shape:

- **Tables:** each `ram_copies` and `disc_copies` table is a public named `ets` table with `keypos` 2, which code may
  read directly; each `disc_only_copies` table is a `dets` table of the same name.
- **Files:** the files of mnesia's directory have OTP's names and terms.
- **Results and errors:** results and `{aborted, Reason}` terms are OTP's, and so are the exits of `abort/1` and of
  dirty functions (`{aborted, Reason}`).
- **Table definitions:** the `cstruct` record of `table_info(Tab, cstruct)` and of the schema table's records has
  OTP's fields, and so do the cookies and versions.
- **Registered names:** the processes Clause has use OTP's names.

The design keeps OTP's formats, so that a later step (MIM1H) can add more nodes.

### Processes

`application:start(mnesia)`, or `mnesia:start/0,1`, starts this tree ([clause_mnesia_app](../../library/mnesia/clause_mnesia_app.erl)):

- `mnesia_sup`:
  - `mnesia_event`, the `gen_event` manager for system events, with the `event_module` parameter's handler. The
    default handler ([clause_mnesia_event](../../library/mnesia/clause_mnesia_event.erl)) reports them as OTP's does
    (see [events](#events));
  - `mnesia_ext_sup`, for backend processes;
  - `mnesia_kernel_sup`:
    - `mnesia_monitor` ([clause_mnesia_monitor](../../library/mnesia/clause_mnesia_monitor.erl));
    - `mnesia_subscr`, which keeps the [event subscriptions](#events)
      ([clause_mnesia_subscr](../../library/mnesia/clause_mnesia_subscr.erl));
    - `mnesia_locker`, the lock manager ([clause_mnesia_locker](../../library/mnesia/clause_mnesia_locker.erl));
    - `mnesia_recover` ([clause_mnesia_recover](../../library/mnesia/clause_mnesia_recover.erl)), which connects to
      other nodes' mnesia (see [connecting nodes](#connecting-nodes));
    - `mnesia_tm`, the transaction manager ([clause_mnesia_tm](../../library/mnesia/clause_mnesia_tm.erl));
    - `mnesia_rpc`, which runs other nodes' calls ([mnesia_rpc](../../library/mnesia/mnesia_rpc.erl), see
      [queries from other nodes](#queries-from-other-nodes));
    - `mnesia_checkpoint_sup`, with a process for each [checkpoint](#checkpoints);
    - `mnesia_controller` and `mnesia_late_loader`
      ([clause_mnesia_controller](../../library/mnesia/clause_mnesia_controller.erl)): the controller answers the table
      definitions other nodes fetch, and learns after a [merge](#merging-schemas) which tables they hold.

Each child has OTP's name, order and modules, as `supervisor:which_children/1` shows them.

`mnesia_monitor` does four things:

- **Owns the tables:** `mnesia_gvar`, the `schema` table, every table and every index table belong to it. They go
  when mnesia stops; `dets` tables and the log close.
- **Reads the configuration:** see [configuration](#configuration).
- **Keeps the directory:** it loads a disc schema at start and runs every [dump](#transaction-log) of the
  transaction log, so that dumps and schema changes never overlap.
- **Applies schema changes:** each change runs in a schema transaction of its own process, as OTP's schema
  coordinator does:
  - the transaction takes OTP's locks and makes OTP's checks in OTP's order
    ([clause_mnesia_change](../../library/mnesia/clause_mnesia_change.erl)), then the monitor applies the change;
  - it counts as one committed or failed transaction; starting counts two commits;
  - inside any activity it returns `{aborted, nested_transaction}`.

### Queries from other nodes

Another node's mnesia, OTP's or Clause's, calls OTP's internal module names on this node (plan 21 step MIM1H3). Clause
provides those modules, with the functions other nodes call and OTP's answers
([mnesia's files and protocol](mnesia-protocol.md#remote-queries-mim1h3)); their other functions are declined:

- [mnesia_lib](../../library/mnesia/mnesia_lib.erl): `is_running/0`, `is_running_remote/0`, `get_node_number/0`, and the
  record reads `db_get`, `db_first`, `db_last`, `db_next_key`, `db_prev_key`, `db_slot`, and the select chunks
  `db_select_init`, `db_select_rev_init` and `db_select_cont` (MIM1H6);
- [mnesia_index](../../library/mnesia/mnesia_index.erl): the index reads `dirty_read2/3` and `dirty_match_object/3`
  (MIM1H6);
- [mnesia_tm](../../library/mnesia/mnesia_tm.erl): `fixtable/3`, which a transaction walking a table here calls
  (MIM1H7);
- [mnesia_schema](../../library/mnesia/mnesia_schema.erl): `remote_read_schema/0`, where a start would take the schema
  from, and `delete_schema2/0`;
- [mnesia_controller](../../library/mnesia/mnesia_controller.erl): `get_remote_cstructs/0`, the table definitions;
- [mnesia_rpc](../../library/mnesia/mnesia_rpc.erl): the server for `{apply, M, F, Args}` calls, and `call/4`.

`create_schema/1` and `delete_schema/1` ask each node through these queries, so they work with OTP nodes too.

### Connecting nodes

Clause's mnesia connects to other nodes' mnesia, OTP's or Clause's, as OTP's does (plan 21 step MIM1H4,
[mnesia's files and protocol](mnesia-protocol.md#connecting-mim1h4)):

- **Protocol:** `mnesia_monitor:negotiate_protocol/1` agrees on protocol `{8,6}` (or `{8,5}`) with each node's
  monitor, which links the two monitors ([clause_mnesia_connect](../../library/mnesia/clause_mnesia_connect.erl)).
- **Recovery nodes:** `mnesia_recover:connect_nodes/1` adds the nodes whose mnesia runs to `recover_nodes`, and the
  nodes exchange their transaction serials, so that transaction numbers keep growing across nodes
  ([clause_mnesia_recover](../../library/mnesia/clause_mnesia_recover.erl)).
- **Going down:** when a linked node's mnesia stops or its connection breaks, the node leaves `recover_nodes`, this node
  records its `mnesia_down` (`mnesia_recover:has_mnesia_down/1`) and reports the `{mnesia_down, Node}` system event.
- **Partitions:** two running nodes that recorded each other down report `{inconsistent_database,
  running_partitioned_network, Node}` when they meet again, and `starting_partitioned_network` when they connect.

Each node keeps the outcomes of the transactions it took part in ([transactions](#transactions), MIM1H8), and a disc
node logs them until each disc node taking part is certain ([disc nodes](#disc-nodes), MIM1H10a); a decision another
node sends in `{decisions, ...}` is not taken yet.

### Merging schemas

`change_config(extra_db_nodes, Nodes)`, and a start with the `extra_db_nodes` parameter, merge this node's schema with
those of the nodes that run mnesia, OTP's or Clause's, as OTP's does (plan 21 step MIM1H5,
[mnesia's files and protocol](mnesia-protocol.md#schema-merge-mim1h5)):

- **The merge:** after [connecting](#connecting-nodes), a schema transaction locks the schema on every node, takes the
  other node's table definitions and commits the merged ones on both sides with OTP's two-phase schema commit
  ([clause_mnesia_merge](../../library/mnesia/clause_mnesia_merge.erl),
  [clause_mnesia_commit](../../library/mnesia/clause_mnesia_commit.erl)). The lock manager and the transaction manager
  answer other nodes' requests in OTP's raw form.
- **After it:** both nodes list each other in `db_nodes` and `running_db_nodes`; each node's tables are read and
  written on the node that holds them, and the controllers report `{mnesia_up, Node}`. A table defined on both nodes
  with another cookie fails the merge with `{error, {merge_schema_failed, Message}}`, OTP's message.
- **A node going down:** its tables stay defined but are read nowhere until it comes back; a node starting again with
  `extra_db_nodes` takes the running nodes' definitions and loads each replica that a running node holds too from it
  ([table copies](#schema-changes), MIM1H9), while a RAM table of its own comes back empty.
- **Schema changes after it** reach every running node's schema ([transactions](#transactions), MIM1H8).
- **Not yet:** a node asked to commit another node's restore, fragment change or SNMP change votes no (MIM1H11,
  MIM1H12).

### Disc nodes

Nodes whose schemas are on disc, OTP's or Clause's, are created and start together as OTP's (plan 21 step MIM1H10a,
[mnesia's files and protocol](mnesia-protocol.md#disc-nodes-mim1h10a)):

- **Creating:** `create_schema/1,2` over several nodes installs the same fallback on each, under the global lock of the
  schema, through OTP's fallback receiver (`mnesia_bup:fallback_receiver/2`,
  [clause_mnesia_backup](../../library/mnesia/clause_mnesia_backup.erl)), which Clause nodes run for OTP's
  `create_schema/1` too.
- **Starting:** a node connects to the schema's other nodes as a [merge](#merging-schemas) does, which commits only the
  definitions that differ. A replica that another node holds too then loads from a running node whose replica is
  active; from its own files when no other node holds the table on disc or this node logged each of them going down
  before it stopped (`initial`); empty when the table is held in RAM only (`ram_only`); otherwise it waits until
  another node loaded the table, and loads it from there ([clause_mnesia_loader](../../library/mnesia/clause_mnesia_loader.erl)).
  Until its load ends it is read nowhere and `wait_for_tables/2` waits for it.
- **Disc replicas** go over nodes as RAM ones do; a `disc_copies` receiver writes its `DCD` file, and two
  `disc_only_copies` replicas exchange `dets` chunks.
- **Logs:** a commit over disc nodes is logged under its decision on each of them, and each node's `mnesia_recover`
  tells the others once it is certain; the other nodes' mnesia coming up and going down is logged and kept in
  `DECISION_TAB.LOG`, which the next start reads to decide its loads.
- **Schema changes** that need a table's replicas on disc nodes, as `delete_table/1` and index changes, abort while
  one of those nodes is down, with OTP's `not_active` reason.
- **Master nodes** decide the loads at start (plan 21 step MIM1H10b,
  [protocol](mnesia-protocol.md#master-nodes-majority-and-copies-mim1h10b)): a table loads only from its master nodes,
  from this node's files when it is one of them (`local_master`), and otherwise waits for one.
  `force_load_table/1` loads a waiting replica from this node's files (`forced_by_user`).
- **Majority:** a write lock on a `majority` table aborts with `{no_majority, Tab}` unless more than half of its
  replicas are active.
- **Copies between disc nodes:** `add_table_copy/3`, `move_table_copy/3` and `change_table_copy_type/3` work for
  another node's disc replicas, and `del_table_copy(schema, Node)` removes a stopped disc node, whose files
  `delete_schema/1` then removes.
- **Not yet:** a waiting replica is not adopted when the other node comes up without loading it (OTP's
  `{adopt_orphan, Node}` load); partitions of disc nodes (MIM1H10c).

### Global variables

`mnesia_gvar` is a public `set` of `{Name, Value}` rows, as in OTP.

- **Table properties:** each table has `{{Tab, Item}, Value}` rows:
  - from its definition: `cstruct`, `attributes`, `arity`, `record_name`, `setorbag`, `index`, `wild_pattern`,
    `record_validation`, `cookie`, `version` and the node lists;
  - from its replica on this node: `storage_type`, `where_to_read`, `where_to_write`, `where_to_commit`,
    `where_to_wlock`, `active_replicas`, `load_node`, `load_reason`, `index_info`, `commit_work`, and
    `{index, Pos}` for each index table.
- **Answers built from these rows:** `table_info(Tab, Item)` reads them, `table_info(Tab, all)` lists them, and
  `schema/1` prints them with `size`, `memory` and `master_nodes`. Both list the same items as OTP, in the same order.
- **User properties:** each also has a `{{Tab, user_property, Key}, Property}` row, which
  `read_table_property/2` reads.
- **Other rows:** `mnesia_status`, the configuration values, `{schema, tables}`, `{schema, local_tables}`,
  `{current, db_nodes}` and the transaction counters.

Master nodes are `{master_nodes, {tab, Tab}, Nodes}` rows of the `mnesia_decision` table, as in OTP.

A missing row gives OTP's answer:

- a table's `where_to_read` is `nowhere`, and its `where_to_write` and `active_replicas` are `[]`;
- any other missing row aborts with `{node_not_running, Node}` when mnesia is not running, and with
  `{no_exists, Name}` when it is.

## Configuration

The documented configuration parameters are read when mnesia starts. Each comes from the application environment, or
else from its documented default. A value of the wrong type stops the start with `{bad_config, Name, Value}`.

`mnesia:start/1` takes `{Name, Value}` pairs and sets them before starting. A bad value returns
`{error, {bad_type, Name, Value}}`, as in OTP.

`system_info/1` answers the configuration items even when mnesia is not running. Other items then answer as OTP's
do:

- `tables` and `local_tables` abort with `{node_not_running, Node}`;
- `transaction_commits` and the other counters abort with `badarg`;
- `transactions` aborts with OTP's `badmatch`.

## Schema

The schema lives in RAM, or on disc when mnesia's directory holds one (see [disc storage](#disc-storage)).

`create_table/2` checks a definition in OTP's order, with OTP's reasons
([clause_mnesia_schema](../../library/mnesia/clause_mnesia_schema.erl)):

1. **Option keys:** an unknown key, or anything that is not a `{Key, Value}` pair, gives `badarg`. A repeated key gives
   `{badarg, Tab, "Duplicate keys", Keys}`.
2. **`storage_properties`:** `ets` and `dets` options that mnesia sets itself are refused.
3. **Existing table:** `already_exists`.
4. **The definition's checks:**
   - node lists, then the table name, the type and `record_name`;
   - attributes: at least two atoms, never `snmp`, none twice;
   - index positions: after the key, `bag` or `ordered`;
   - `local_content`, `access_mode`, `majority`, the SNMP structure, `user_properties`, `cookie` and `version`.
5. **Disc nodes:** a disc node that is not running gives `not_active`. Disc copies on this node with a RAM schema
   give `{bad_type, Tab, disc_copies, Node}`.

Behavior that follows OTP's:

- A table on other nodes only exists in the schema, with no replica here: `where_to_read` is `nowhere` and
  `storage_type` is `unknown`.
- A `read_only` table is written nowhere: dirty writes abort with `{no_exists, Tab}`.
- `{ram_copies, []}` with no other copies means a table on this node.
- An index listed twice gives two index tables, as in OTP.

`delete_table/1` removes the table, its index tables and its rows. `wait_for_tables/2` waits until each table has a
replica to read here, and returns `{timeout, Tables}` for the rest.

## Schema changes

Each change takes OTP's locks: the schema's write lock first, then the table's write lock where OTP takes it, which
also aborts with `{no_exists, Tab}` for a table that is `read_only` or has no replica here. Then come OTP's checks:
`{no_exists, {Tab, cstruct}}` for an unknown table, `{no_exists, Tab}` without an active replica, and the definition's
checks of `create_table/2` for the changed definition. Each change gives the definition its next major version.

- **`clear_table/1`:** a transaction of its own, not a schema transaction. It takes the table's write lock and
  clears the table and its indexes at the commit. Inside a dirty activity it is a transaction; inside a transaction it
  aborts with `nested_transaction`.
- **`transform_table/3,4`:** new attributes and record name; the indexes keep their positions and are rebuilt.
  - The function sees each record in key order. A record it returns unchanged stays, and one with the new record name
    and the same key replaces it.
  - A wrong arity, key or record name, or a failing function, aborts with
    `{"Bad transform function", Tab, Fun, Node, Reason}`. `mnesia_event` then prints OTP's
    `Transform function failed` report.
  - `ignore` changes the definition only. For a `bag` key, only the records the function changes stay, as in OTP.
- **Table copies:**
  - `add_table_copy/3` adds a node first in its storage's list. A `ram_copies` replica on a node outside the running
    nodes only changes the definition. A replica on a running node of a table held on another running node, OTP's or
    Clause's, is loaded from that node with OTP's transfer before the commit (plan 21 step MIM1H9,
    [mnesia's files and protocol](mnesia-protocol.md#replicas-and-loading-mim1h9),
    [clause_mnesia_loader](../../library/mnesia/clause_mnesia_loader.erl)); this node sends its replicas to other
    nodes the same way, from a process its controller starts.
  - `del_table_copy/2` deletes the table with its last replica. Deleting the schema's copy on another node also removes
    that node's replicas of every table, and deletes the tables it alone held.
  - `move_table_copy/3` needs both nodes running, so on one node it always aborts with `not_active`. Over two nodes it
    adds the copy, loaded as above, then deletes the old one, as OTP's operations do (MIM1H9).
  - `change_table_copy_type/3` to `unknown` deletes the replica. The other changes are described under
    [storage types](#storage-types).
- **Table properties:**
  - `write_table_property/2` and `delete_table_property/2` change `user_properties`.
  - `change_table_access_mode/2`, `change_table_load_order/2` and `change_table_majority/2` change their fields.
    `read_only` writes the table nowhere, so writes abort with `{no_exists, Tab}`.
- **Not schema transactions:**
  - `set_master_nodes/1,2` keep the master nodes that hold a replica. With mnesia stopped they check and log them
    against `schema.DAT`, return `{error, "No schema file exists"}` with only a fallback, and `ok` without a
    directory ([disc nodes](#disc-nodes)).
  - `force_load_table/1` answers `yes` for a table loaded here, and loads a replica that waits for another node from
    this node's files. For a table without a replica here, it waits for one, as OTP's does.
  - `change_config/2`:
    - `dc_dump_limit` takes effect at once;
    - `extra_db_nodes` [merges the schemas](#merging-schemas) and returns the given nodes that run mnesia with this
      one.
  - `set_debug_level/1` sets the `debug` parameter and returns the previous level. While mnesia runs it also
    changes the event handler's subscriptions (see [events](#events)).

## Disc storage

Disc storage keeps OTP's files in mnesia's directory, the `dir` parameter (default `Mnesia.<node()>`)
([clause_mnesia_disc](../../library/mnesia/clause_mnesia_disc.erl)):

| File | Contents |
| --- | --- |
| `FALLBACK.BUP` | the backup that `create_schema/1,2` writes, installed at the next start |
| `schema.DAT` | a `dets` table of the schema's `{schema, Tab, CreateList}` records |
| `LATEST.LOG` | a `disk_log` of `#commit{}` records with each commit's disc writes, `{master_nodes, Tab, Nodes}` entries, and the decisions and other nodes' `mnesia_up` and `mnesia_down` records of [disc nodes](#disc-nodes), after a `#log_header{}` |
| `PREVIOUS.LOG` | the log a dump is moving into the tables' files |
| `DECISION_TAB.LOG` | the `{decision_list, Decisions}` that each dump finding entries in the log writes: the transaction serial `{trans_tid, serial, N}`, the master nodes and the other nodes seen going down |
| `Tab.DCD`, `Tab.DCL` | a `disc_copies` table's records, and its writes since the `DCD` file was written |
| `Tab.DAT`, `Tab_Pos.DAT` | a `disc_only_copies` table's `dets` table, and the `dets` table of its index at `Pos` |

### The disc schema

- **Creating:** `create_schema/1,2` makes the directory and writes the fallback, with OTP's checks and errors: a
  repeated node, a node that has a schema or does not answer, a bad schema property, and `schema_location` `ram`
  (`{has_no_disc, Node}`).
- **Deleting:** `delete_schema/1` removes mnesia's files from the directory, which stays. It fails while mnesia runs.
- **Using it:** `system_info(use_dir)` follows `schema_location`: `disc` always uses the directory, `opt_disc` when it
  holds a fallback or a schema, `ram` never. `system_info(fallback_activated)` is `true` while a fallback waits.
- **Starting:** the fallback, if any, becomes `schema.DAT`. Each table then gets its records: a `disc_copies` table
  from its `DCD` and `DCL` files, a `disc_only_copies` table from its `dets` file, a RAM table from a dump by
  `dump_tables/1`. Their `load_reason` is `local_only`, the schema's `initial`; a table that other nodes hold too loads
  once the start connected to them ([disc nodes](#disc-nodes), MIM1H10a). The decision log's master nodes are
  set, and transactions continue its serial. The log since the last dump is applied again: its committed writes, also
  those of OTP's schema transactions, and its master nodes. Then, with `dump_disc_copies_at_startup` (the default), the
  `disc_copies` tables the log touched or that have a `DCL` file get new `DCD` files; without it, the log's writes go
  to their `DCL` files. The log starts afresh, and the decision log is written when the log held entries.
- **Schema changes:** each change writes the changed tables' records to `schema.DAT` at once and is logged as OTP's
  [schema transaction](#transaction-log); it counts two log writes, as OTP's do. A new `disc_copies` table starts with
  a `DCD` file without terms, as OTP's; a deleted table's files go.
- **The same files as OTP's:** an OTP node starts on a directory a Clause node wrote and the reverse, with the records,
  indexes, master nodes, fallbacks and schema transactions left in the log found again (plan 21 steps MIM1H1,
  MIM1H2).

### Transaction log

- **Logging:** a commit that writes a `disc_copies` or `disc_only_copies` table appends one `#commit{}` record to
  `LATEST.LOG` before the tables change, so `system_info(transaction_log_writes)` counts it. Dirty writes and
  transactions are logged, `ets` activities and RAM tables are not. `sync_transaction/1,2,3` and `sync_dirty/1,2`
  write their record before going on; the others hand it to the log. `set_master_nodes/1,2` logs each table's master
  nodes, which the next start sets again.
- **Schema transactions (MIM1H2):** each logs OTP's commit, `{op, ...}` tuples with the tables' create lists under
  a `{decision, Tid, presume_abort, [Node], []}`, then `{decision, Tid, committed, [], []}`: `create_table`,
  `delete_table`, `add_index`, `del_index`, `transform` (with its function), `change_table_copy_type`, the access
  mode, load order and majority with their old and new values, `write_property`, `delete_property`, `add_snmp`,
  `del_snmp`, `dump_table` with the table's size, `change_table_frag` with the fragments created or deleted and a
  `rec` operation per moved record, and a restore's `clear_table` and `restore_recreate` with the restored records as
  the commit's writes. As OTP does, the log is dumped right after `delete_table`, `transform` and
  `change_table_copy_type`, and before them, so that no older write replays over the table. A start replays cleared,
  restored and moved records in the log's order. A transaction that writes a `majority` table logs under its decision
  the same way, even when it wrote only RAM tables.
- **Dumps:** `dump_log/0` returns `dumped`, and a dump also follows every `dump_log_write_threshold` writes and every
  `dump_log_time_threshold` milliseconds. A dump moves the log's writes into each table's `DCL` file, or writes a new
  `DCD` file when the `DCL` file has grown to `1/dc_dump_limit` of it. `disc_only_copies` tables are synced. The log
  goes to `PREVIOUS.LOG` while it is dumped, and a dump that found entries writes `DECISION_TAB.LOG`.
- **Errors:** `sync_log/0` syncs the log, and returns `{error, no_such_log}` with a RAM schema. Without a running
  mnesia both return `{error, {node_not_running, Node}}`.
- **`dump_tables/1`:** writes RAM tables to `DCD` files, which the next start loads. It aborts with
  `{"Only allowed on ram_copies", Tab, Nodes}` for disc tables, `{bad_type, schema}`, `{no_exists, Tab}`, and
  `{has_no_disc, Node}` with a RAM schema.

### Storage types

`change_table_copy_type/3` moves a replica between storage types:

- **Tables:** the records move between `ets` and `dets`, the indexes are built again for the new storage, and the
  disc files follow. A RAM table goes to disc only on a disc schema (`{has_no_disc, Node}`) and without a dump by
  `dump_tables/1` (`{combine_error, Tab, Node, "Table dump exists"}`). `ordered_set` tables cannot be
  `disc_only_copies`.
- **The schema:** to `disc_copies` it writes a new directory, and needs `schema_location` `opt_disc`
  (`{combine_error, schema, Node, "schema_location must be opt_disc"}`). Back to `ram_copies` it removes the
  directory's files, and aborts with `{"Disc resident tables", Tabs, Node}` while this node has disc tables.

### Disc-only tables

A `disc_only_copies` table works on its `dets` table, as OTP's:

- **Walks:** `dets` has no order, so `last/1` is `first/1`, `prev/2` is `next/2`, and reverse selects run forward.
- **Indexes:** each index is a `dets` `bag` of `{Value, Key}`, listed in `index_info` as
  `{{Pos, bag}, {dets, {Tab, index, {Pos, bag}}}}`. It is built again at each start.
- **Information:** `table_info(Tab, memory)` is the file's size in bytes, as `dets` gives it.

## Dirty access

The dirty functions run in the calling process and work on the table's `ets` table:

- **Reads** (`dirty_read`, `dirty_match_object`, `dirty_select`, `dirty_select_reverse`, `dirty_all_keys`, the walk
  functions, `dirty_index_read`, `dirty_index_match_object`) go to the replica `where_to_read` names, this node's when
  it has one. Without a replica they abort with `{no_exists, Args}`, and a failing read gives `{badarg, Args}`.
- **Writes** (`dirty_write`, `dirty_delete`, `dirty_delete_object`, `dirty_update_counter`) first check the record
  against the table's record name and arity. They then update the replica and its indexes.
- **Counters:** `dirty_update_counter` follows OTP's rules:
  - the table must be a set or ordered_set of three-field records;
  - a missing counter starts at the increment, or at zero;
  - a value that is not an integer, or that would go below zero, is replaced the same way.
- **Index use:** a match or one-clause select whose pattern leaves the key unbound but binds an indexed position
  uses that index. It returns the records in OTP's order: last key first for matches, the table's order for
  `ordered_set` selects.
- **Tables held on another node** (plan 21 step MIM1H6, [mnesia's files and
  protocol](mnesia-protocol.md#remote-reads-and-dirty-writes-mim1h6)): a read runs as OTP's call of `mnesia_lib`,
  `mnesia` or `mnesia_index` on the node `where_to_read` names, through that node's
  [mnesia_rpc](#queries-from-other-nodes), so the holder may be an OTP node. A write goes to each other replica's
  `mnesia_tm` as OTP's commit: synchronously, waiting for the holder's result, in a `sync_dirty` activity and whenever
  this node holds no replica, else without an answer. The holder applies it and reports its table events with the
  writer's activity id. A write whose holder goes while it waits returns `{'EXIT', {aborted, {node_not_running,
  Node}}}`, as OTP's; `select/4` chunks in dirty activities are read on the holder too.

## Transactions

A transaction runs in the calling process ([clause_mnesia_tm](../../library/mnesia/clause_mnesia_tm.erl)). Its
activity is `{AccessModule, {tid, Counter, Pid}, #tidstore{}}` under `mnesia_activity_state` in the process
dictionary, as in OTP:

- **Store:** `mnesia_tm` gives each transaction an `ets` bag of its locks and of its writes and deletes. Reads merge
  these into the table's records ([clause_mnesia_written](../../library/mnesia/clause_mnesia_written.erl)), so a
  transaction sees its own writes, with OTP's order of results.
- **Commit:** `mnesia_tm` applies the writes to the tables and their indexes, then releases the locks. A transaction
  whose process dies holds no locks afterwards.
- **Results:** `{atomic, Value}`, or `{aborted, Reason}` with OTP's reasons: `abort/1`'s reason, `{throw, Value}`,
  an exit's reason, and `{Reason, Stack}` for an error.
- **Nesting:** an inner transaction works on a copy of the outer store, which its commit hands back. Its abort leaves
  the outer transaction running. Inner commits and aborts count too.
- **Restarts:** a refused lock (`#cyclic{}`) restarts the outermost transaction with the same tid, after OTP's growing
  pause, counting `transaction_restarts`. When the retries run out the result is `{aborted, nomore}`.
- **Other nodes** (plan 21 step MIM1H8, [mnesia's files and protocol](mnesia-protocol.md#two-phase-commit-mim1h8)):
  a transaction writing tables held on other nodes, OTP's or Clause's, commits there with OTP's two-phase protocols
  ([clause_mnesia_commit](../../library/mnesia/clause_mnesia_commit.erl)): `sym_trans` for tables replicated on the
  same nodes, `asym_trans` for tables of different nodes and majority tables, and for every schema change, which
  reaches every running node's schema. This node takes part in other nodes' commits the same way
  ([clause_mnesia_participant](../../library/mnesia/clause_mnesia_participant.erl)): `mnesia_tm` votes on and applies
  `sym_trans` commits, a process of its own each `asym_trans` commit, with the transaction's table events. A node
  going down before it votes restarts the transaction, which commits on the nodes still running; a coordinator going
  down before deciding aborts its transaction on the participants unless another one knows it committed. Each node
  keeps the outcomes of the transactions it took part in and answers other nodes' `what_happened` questions from them.

`select/4` and `select_reverse/4` return OTP's `#mnesia_select{}` continuation. In a transaction with writes to the
table, each chunk merges them as OTP's does, chunk boundaries included.

## Locks

`mnesia_locker` keeps read and write locks on records, whole tables (`{Tab, '______WHOLETABLE_____'}`) and global
items ([clause_mnesia_locker](../../library/mnesia/clause_mnesia_locker.erl)):

- **Conflicts:** a request that conflicts only with younger transactions waits in the queue. One that conflicts with
  an older transaction is refused, and its transaction restarts. A free lock also waits behind older queued requests
  for its table, and for its record when it is a read.
- **Release:** a transaction's end releases its locks, and the queued requests are tried again, youngest first.
- **Results:** `lock/2` returns what OTP's does: the nodes for a new write lock, `[]` for a record write lock held
  already, the majority flag for a table write lock held already, the records for a record read lock, `ok` for a new
  table read lock.
- **Sticky locks:** `s_write/1` and the other sticky operations need a replica here; the table's first one also takes
  its read lock and its sticky item.
- **Information:** `system_info(held_locks)`, `lock_queue` and `transactions` give OTP's terms.
- **Other nodes** (plan 21 step MIM1H7, [mnesia's files and protocol](mnesia-protocol.md#remote-locks-mim1h7)): a
  lock is taken by the lock manager of the node that keeps it, OTP's or Clause's, with OTP's raw messages: a read lock
  on the node `where_to_read` names, which sends the records read with its grant, a write lock on each node of
  `where_to_write`, a global lock on each listed node running with this one, an index read as one `ix_read` request.
  The transaction remembers those nodes: its commit sends their transaction managers `simple_commit`, its abort their
  lock managers `release_tid`, and a restart waits for each of them to release its locks. Conflicts follow the same
  rules across nodes, by tid. When a transaction's process dies, the other nodes are told to abort it; when a node goes
  down, a transaction waiting for its grant restarts, and the node's transactions lose their locks here.

## Activities

`activity/2,3,4`, `async_dirty/1,2`, `sync_dirty/1,2` and `ets/1,2` run a fun with mnesia's functions mapped as in OTP
([clause_mnesia_access](../../library/mnesia/clause_mnesia_access.erl)):

- **Dirty activities** use the dirty functions; `ets` works on the tables directly.
- **Inside a transaction** a dirty activity is a nested transaction, whose abort reason becomes an exit.
- **Access modules:** each function calls `AccessModule:F(ActivityId, Opaque, ...)`. The default module `mnesia`
  exports these arities, so other access modules can call back. `table_info/2` goes through the access module too.
- **Errors:** an error in a dirty activity exits with `{Reason, Stack}`; a returned `{aborted, Reason}` aborts.

## Indexes

Each index is an `ordered_set` table of `{{Value, Key}}` ([clause_mnesia_index](../../library/mnesia/clause_mnesia_index.erl)):

- **Bookkeeping:** `index_info` and `commit_work` list the indexes newest first, as `{{Pos, ordered}, {ram, Table}}`
  (a `disc_only_copies` table's as [`dets` tables](#disc-only-tables)).
  `table_info(Tab, index)` lists their positions in that order.
- **Upkeep:** every write and delete keeps the index tables current:
  - a set table's entry changes only when its value does;
  - a bag table's entry goes when no other record under the key holds its value.
- **Schema changes:** `add_table_index/2` fills a new index from the table's records. `del_table_index/2` removes
  every index of the position.
- **Errors:** both answer with OTP's reasons (`already_exists`, `no_exists`, `bad_type`), and each change gives the
  table definition its next major version.

## Events

`mnesia_subscr` keeps the subscriptions ([clause_mnesia_subscr](../../library/mnesia/clause_mnesia_subscr.erl)). It links
each subscriber and drops its subscriptions when it exits.

- **Subscribing:** `subscribe/1` and `unsubscribe/1` return `{ok, Node}`, or OTP's errors:
  - `{badarg, What}` for an unknown category;
  - `{already_exists, What}` for a second subscription of the kind; a table's simple and detailed subscriptions count
    as one;
  - `{no_exists, Tab}` for an unknown table, `{not_active_local, Tab}` for a table without a replica here;
  - `{node_not_running, Node}` without a running mnesia;
  - unsubscribing from a table that does not exist gives `badarg`.
- **Where the subscribers are kept**, as in OTP:
  - the `subscribers` and `activity_subscribers` variables;
  - a `{subscribers, Simple, Detailed}` entry in the table's `commit_work`, and its `subscribers` property;
  - each change of `commit_work`, an index's too, puts its entries in OTP's order;
  - `system_info(subscribers)` lists the event manager first.
- **System events:** `{mnesia_system_event, Event}` goes to each system subscriber and to the event manager:
  - `{mnesia_user, Event}` from `report_event/1`;
  - `{mnesia_info, Format, Args}` and the other reports;
  - `{mnesia_down, Node}` when mnesia stops.

  Without a running mnesia, `report_event/1` starts an event manager for that event alone, which prints it.
- **Activity events:** `{mnesia_activity_event, {complete, Tid}}` when a transaction that wrote commits, schema
  transactions included. Reads, aborts, inner transactions and dirty activities send none.
- **Table events:** sent while a commit or a dirty update applies, before the table changes.
  - Simple: `{write, Record, Activity}`, `{delete, {Tab, Key}, Activity}` and `{delete_object, Record, Activity}`, the
    record named after the table.
  - Detailed: `{write, Tab, Record, OldRecords, Activity}` and `{delete, Tab, What, OldRecords, Activity}`.
  - The activity is `{dirty, Pid}` for a dirty update and the tid for a transaction. `ets` activities send none.
  - A counter update is a write, with the record it replaced.
  - `clear_table/1` reports the table's schema record deleted and written again.
- **Schema events:** each schema change reports the changed table's `{schema, Tab, Definition}` record:
  - written twice for a new table or a new table copy, before and after it is stored;
  - deleted for a deleted table, with `undefined` as the old records of a detailed event;
  - written once for any other change.
- **The handler:** the default handler reports as OTP's:
  - it prints info reports, once when one comes again, and user events, as `Mnesia(Node): ...`;
  - it logs warnings and errors through `error_logger`;
  - it prints the table events it subscribes to at the debug levels: the schema's at `debug`, every local table's at
    `trace`, from `set_debug_level/1` or from the start;
  - a process registered globally as `mnesia_global_logger` gets a copy.

## Text files

[clause_mnesia_text](../../library/mnesia/clause_mnesia_text.erl) reads and writes OTP's text files: a
`{tables, [{Tab, Options}]}` term, then records named after their tables.

- **`load_textfile/1`:**
  - mnesia starts first, on a new disc schema when its directory has none;
  - each listed table is created, or takes the records when it exists; a table that cannot be created is reported, and
    its records left out;
  - the records are written in one transaction, whose result it returns.
- **Errors**, with OTP's messages: `{error, open}`, `{error, bad_header}`, `{error, undefined_object}` for a record of
  an unlisted table, and `{error, read}` for a term that does not parse. A table entry that is not a pair throws
  `{error, badtab}`, or fails with `function_clause` for a triple, as in OTP.
- **`dump_to_textfile/1`:** writes this node's tables other than the schema, with their record names and attributes,
  then their records. Without a running mnesia it returns `error`.

## Query tables

`table/1,2` give `qlc` a table ([clause_mnesia_qlc](../../library/mnesia/clause_mnesia_qlc.erl)):

- **Options:** `{lock, Lock}`, `{n_objects, N}` and `{traverse, select | {select, MatchSpec}}`; `qlc` takes the others.
- **Reading:** in the caller's activity, in chunks of `select/4`. A filter on the key reads through `read/3`, and one
  on an indexed position through `index_read/3`.
- **Information:** `qlc:info/1` prints OTP's form of the query.
- **Errors:** outside an activity the query aborts with `no_transaction`. A cursor's process takes the activity of the
  process that made it.
- **Fixing:** a dirty traversal of a `set` or `bag` table fixes it with `safe_fixtable`.

## The report

[clause_mnesia_info](../../library/mnesia/clause_mnesia_info.erl) prints OTP's report.

- **`info/0`:**
  - the locks held and wanted, and the coordinators of running transactions;
  - each active table with its size and memory, or file size for a `disc_only_copies` table;
  - then the report of `system_info/0`.
- **`system_info/0`:** prints and returns whether mnesia runs:
  - the version, the debug level, the directory, the fallback, and the running and stopped nodes;
  - the master node tables, the tables of each storage, and the tables written at the same replicas;
  - the transaction and lock counts.

  Without a running mnesia, both print only the first lines.
- **`lkill/0`:** kills mnesia's processes at once, as OTP's debug function does, and the application ends as
  `exited: normal`. `kill/0` does so on each node, and `ms/0` lists OTP's names that they end.

## Checkpoints

A checkpoint keeps its tables as they were when it was activated
([clause_mnesia_checkpoint](../../library/mnesia/clause_mnesia_checkpoint.erl)).

- **Activating:** `activate_checkpoint/1` returns `{ok, Name, Nodes}`; the default name is `{Integer, node()}`. OTP's
  errors:
  - `{badarg, Arg}` for an unknown argument, and `{already_exists, Name}` for a name in use;
  - `combine_error` for a table in both `min` and `max`, for no table at all, and for any `ram_overrides_dump` list,
    which OTP accepts only for tables in both lists;
  - `{"Cannot prepare checkpoint (replica not available)", [Tab, Name]}` for a table without an active replica.
- **Retainers:** each checkpoint is a process under `mnesia_checkpoint_sup` with a retainer for each table. Before an
  update changes a key for the first time since activation, the retainer keeps the key's records; a cleared table
  keeps each of its keys. The schema keeps its records in the same way.
- **RAM tables:** a `ram_copies` table, unless `ram_overrides_dump` names it, keeps its records from its dump
  ([`dump_tables/1`](#transaction-log)) and retains nothing, as in OTP. A backup of it holds its dumped records only.
- **Bookkeeping**, as in OTP: `system_info(checkpoints)`, `table_info(Tab, checkpoints)`, the
  `{Tab, {retainer, Name}}` variables and a `{checkpoints, Names}` entry first in the table's `commit_work`.
- **Deactivating:** `deactivate_checkpoint/1` returns `ok`, or `{error, {no_exists, Name}}`. A checkpoint also ends when
  its table's replica here goes, with the `{mnesia_checkpoint_deactivated, Name}` system event and OTP's error report.

## Backups

[clause_mnesia_backup](../../library/mnesia/clause_mnesia_backup.erl) writes and reads backups through a backup module,
by default `mnesia_backup` ([mnesia_backup](../../library/mnesia/mnesia_backup.erl)), which keeps OTP's callbacks and its
`disk_log` file.

- **Contents:** a `#log_header{}` of kind `backup_log`, the schema's `{schema, Tab, CreateList}` items, then each
  table's records named after the table, and `{Tab, Key}` deletes.
- **`backup_checkpoint/2,3`:** the schema first, then each table of the checkpoint: its records now, then each
  retained key as a delete followed by the records at activation, so that restoring them in order gives the
  checkpoint's state. Without the schema in the checkpoint, each table's definition is written instead.
  - Arguments: a backup module, or `{module, Mod}`, `{scope, global | local}`, `{tables, Tabs}` and
    `{incremental, PreviousCheckpoint}`.
  - An incremental backup holds the keys changed since the previous checkpoint: their records now, then the records
    the newer checkpoint retained.
  - Errors as OTP's: `{badarg, Arg}`, and `{error, {'EXIT', Reason}}` when the backup fails. A backup module whose
    `abort_write/1` fails gets OTP's error report.
- **`backup/1,2`:** a checkpoint of every table, backed up and deactivated. A table without a replica here makes it
  fail.
- **`traverse_backup/4,6`:** passes each schema item and record to the fun, and writes what it returns to the target,
  or nothing with the target module `read_only`. The result is `{ok, Acc}` or OTP's errors, such as
  `{"Backup traversal failed", Reason}`.

## Restoring

`restore/2` restores tables from a backup in one schema transaction and returns `{atomic, Tabs}`:

- **Options:** each table is cleared (the default), kept, recreated with the backup's definition, or skipped:
  `{clear_tables, Tabs}`, `{keep_tables, Tabs}`, `{recreate_tables, Tabs}`, `{skip_tables, Tabs}` and
  `{default_op, Op}`; `{module, Mod}` names the backup module.
- **Records:** the backup's records of the restored tables are written in order, and its deletes applied, under the
  tables' record names. The schema's own definition is never restored.
- **Errors:** `{aborted, Reason}` for a bad argument (`{badarg, {clear_tables, schema}}` and others) and for a
  backup that cannot be read, before any transaction.

## Fallbacks

- **Installing:** `install_fallback/1,2` copies a backup to `FALLBACK.BUP`. The backup's schema must hold this node's
  disc schema. Arguments: a backup module, `{module, Mod}`, `{scope, global | local}`, and `{mnesia_dir, Dir}` with
  a local scope. Errors are OTP's `{"Cannot install fallback", Reason}`, such as `{already_exists, Node}` for a second
  fallback.
- **Uninstalling:** `uninstall_fallback/0,1` removes the fallback.
- **Starting:** the next start replaces the directory's database with the fallback: its schema, and each local
  table's records in the table's files, a `disc_copies` table's as `DCL` writes after an empty `DCD` file, as OTP's.
  The start's dump then merges them, unless `dump_disc_copies_at_startup` is `false`. The decision log goes.

## Fragmented tables

A table with `frag_properties` is split into fragments: the first is the table itself, fragment N the table
`Tab_fragN` ([mnesia_frag](../../library/mnesia/mnesia_frag.erl)).

- **Hashing:** [mnesia_frag_hash](../../library/mnesia/mnesia_frag_hash.erl) is OTP's linear hashing over `erlang:phash2/2`,
  with OTP's `#hash_state{}` and fragment numbers; states from before OTP R13 hash with `erlang:phash/2`. The base
  table's `frag_properties` hold the hash module and state, and `mnesia_gvar` holds them as `{Tab, frag_hash}`.
- **Creating:** `create_table/2` with `frag_properties` checks them as OTP does (`n_fragments`, `node_pool`,
  `n_ram_copies`, `n_disc_copies`, `n_disc_only_copies`, `foreign_key`, `hash_module`, `hash_state`) and creates each
  fragment, with its replicas spread over the node pool. `delete_table/1` of the base table deletes every fragment.
- **Access:** with `mnesia_frag` as the activity's access module, a record goes to the fragment of its key, or of its
  foreign key; reads without a key, selects, folds, walks and index reads go through every fragment. `table_info/2`
  in such an activity also answers `base_table`, `n_fragments`, `node_pool`, `foreign_key`, `foreigners`, the
  replica counts, `frag_names`, `frag_dist`, `frag_size`, `frag_memory`, and `size` and `memory` over all fragments.
- **`change_table_frag/2`:** `{activate, Props}`, `deactivate`, `{add_frag, NodesOrDist}` and `del_frag`, which
  move the records that hash to another fragment, and `{add_node, Node}` and `{del_node, Node}`. Tables with a
  foreign key follow their foreign table's fragments. Errors as OTP's.
- **Majority:** `change_table_majority/2` of a base table changes every fragment; of another fragment it aborts with
  `bad_type`, as in OTP.

## SNMP

A table with an snmp structure, `[{key, Types}]` given to `create_table/2` or `snmp_open_table/2`, keeps an ordered
index of its rows by SNMP row index, as OTP's `mnesia_snmp_hook` does
([clause_mnesia_snmp](../../library/mnesia/clause_mnesia_snmp.erl)):

- **Row indexes:** an `integer` key is `[Key]`, a `string` key its length then its characters, a `fix_string` key its
  characters; a tuple key joins its parts. A written key that makes no row index aborts with
  `{bad_snmp_key, {Tab, Key}, Types}`.
- **Lookups:** `snmp_get_row/2`, `snmp_get_next_index/2` and `snmp_get_mnesia_key/2`, which see a transaction's own
  writes. Counter updates leave the index alone, as in OTP.
- **`snmp_close_table/1`** removes the index.

## Tests

- **`mnesia_dirty` golden:** generated from OTP ([fixture](../../tests/fixtures/executables/mnesia_dirty/mnesia_dirty.erl)).
- **`mnesia_transactions` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_transactions/mnesia_transactions.erl)). It covers transaction
  results and abort terms, reads of own writes in each table type, walks, folds and index reads, nesting, the activity
  kinds, an access module, lock results and errors, conflicts between processes (waiting, restarts, retries running
  out, shared reads, a killed holder), schema transactions and select continuations.
- **`mnesia_schema_changes` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_schema_changes/mnesia_schema_changes.erl)). It covers each change of
  MIM1C before starting and while running: results and abort terms, table copies on this node and on a node outside the
  schema, transform failures with their reports, properties, master nodes, configuration changes and their
  transaction counts.
- **`mnesia_disc` golden:** generated from OTP ([fixture](../../tests/fixtures/executables/mnesia_disc/mnesia_disc.erl)).
  It covers creating and deleting the disc schema with their errors, the directory's files, disc tables of each type
  and their indexes, transaction log writes and dumps, `dump_tables/1`, storage type changes of tables and of the
  schema, `schema_location` and stopping and starting mnesia with the data found again.
- **`mnesia_events` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_events/mnesia_events.erl)). It covers system, activity and table
  subscriptions with their events and errors, simple and detailed table events of dirty updates and transactions,
  schema events, the commit work's order, subscribers that exit, the debug levels' printouts, `mnesia_down` at stop,
  `qlc` queries over `table/1,2` with `qlc:info/1`, text files with their errors, the report of `info/0` and
  `system_info/0`, and `lkill/0` and `kill/0`.
- **`mnesia_backups` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_backups/mnesia_backups.erl)). It covers checkpoints with their
  errors and bookkeeping, backups of the database and of checkpoints (full, incremental, of some tables, of a RAM
  schema), retained records of cleared tables, a checkpoint ended by its table's deletion, traversals that read and
  rewrite backups, restores with each option and their errors, and fallbacks installed, uninstalled and taken at the
  next start.
- **`mnesia_fragments` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_fragments/mnesia_fragments.erl)). It covers `mnesia_frag_hash`'s
  states and fragment numbers, fragmented tables created with their errors, access through `mnesia_frag`, the
  fragment information, fragments added and deleted, activation, node pools, foreign keys, and SNMP indexes with their
  lookups in and outside transactions.
- **`mnesia_dir_exchange` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_dir_exchange/mnesia_dir_exchange.erl)), with two nodes taking
  turns on one directory: a disc schema created, its tables written and their log partly dumped, master nodes set; a
  start without dumping `disc_copies` tables, records and tables changed; a backup installed as a fallback and started
  from. Each turn shows the directory's files and their terms; node b leaves schema transactions and a cleared table
  in the log for node a's start. Its runs pair Clause nodes with OTP nodes both ways, so that each reads the other's
  files.
- **`mnesia_schema_ops` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_schema_ops/mnesia_schema_ops.erl)). Each schema operation runs
  between a start and a stop, and the log's terms after it are compared with OTP's, then the records the next start
  replays, and the directory's files after the last dump.
- **`mnesia_remote_queries` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_remote_queries/mnesia_remote_queries.erl)). Node a asks node b
  each query before b's mnesia ever ran, with a fallback, while it runs on a disc schema and after it stops, reads b's
  records through `db_get` and b's `mnesia_rpc`, lists b's processes and supervisor children, and checks b with
  `create_schema/1` and `delete_schema/1`. Its runs pair Clause nodes with OTP nodes both ways.
- **`mnesia_connect` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_connect/mnesia_connect.erl)). Node a negotiates with node b's
  monitor and connects to b, b refuses an unknown protocol and answers a decision query; b stops and starts and is
  connected again, and a partition between the running nodes is reported on both when they meet. Each step shows both
  nodes' system events, protocol and recovery variables, monitor links, `mnesia_down` records and transaction numbers.
  Its runs pair Clause nodes with OTP nodes both ways.
- **`mnesia_merge` golden:** generated from OTP ([fixture](../../tests/fixtures/executables/mnesia_merge/mnesia_merge.erl)).
  Node a merges with node b, first failing on a table both define with other cookies, then after b deletes it; b
  stops, and starts again merging with a. Each step shows both nodes' db nodes and each table's readers, writers,
  replicas, storage, cookie and version. Its runs pair Clause nodes with OTP nodes both ways.
- **`mnesia_remote_dirty` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_remote_dirty/mnesia_remote_dirty.erl)). After a merge, each node
  dirty-writes and reads the other node's table and counter table with every dirty function, and the holder reports
  the table events; then chunked selects, folds and writes in dirty activities, failing reads and refused writes, a
  write whose holder goes while it waits, and reads once the holder is gone. Its runs pair Clause nodes with OTP nodes
  both ways.
- **`mnesia_remote_locks` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_remote_locks/mnesia_remote_locks.erl)). After a merge, each node's
  transactions take every kind of lock on the other node's table and read it, abort and nest; an older transaction
  waits in the other node's queue, a younger one is refused and restarts, a killed transaction's locks go, and a holder
  going down ends a wait and releases its transactions' locks. Its runs pair Clause nodes with OTP nodes both ways.
- **`mnesia_two_phase` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_two_phase/mnesia_two_phase.erl)). After a merge, replicated tables
  are created from either node; each node's transactions write replicated tables, tables of different nodes, a majority
  table and only the other node's table, in sync, nested and aborted transactions, with `clear_table/1` and dirty
  writes, and both nodes hold the same records, the other node reporting the table events. Index, transform, property,
  load order, access mode and table changes reach the other node's schema; a clashing `ets` name makes the other node
  refuse a table; both nodes answer `what_happened`. One run ends with the participant going down before it votes, the
  other with the coordinator going down before this node votes. Its runs pair Clause nodes with OTP nodes both ways.
- **`mnesia_replicas` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_replicas/mnesia_replicas.erl)). After a merge, copies are added
  on the other node and on this one by each node, a large table among them, deleted and moved both ways, with their
  errors; then b starts again and loads its replica of a replicated table from a. Each step shows both nodes' readers,
  writers, active replicas, load node and reason, and whether they hold the same records. Its runs pair Clause nodes
  with OTP nodes both ways.
- **`mnesia_disc_nodes` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_disc_nodes/mnesia_disc_nodes.erl)). a creates the schema on both
  nodes, which start together; tables of each placement are written in transactions; schema changes wait for b while
  it is down; b starts again and loads from a; then a starts alone after b stopped last, loads what only it holds on
  disc and waits for the rest, which it loads from b once b is back. Each step shows both nodes' readers, writers,
  load nodes and reasons, and what each node's files hold. Its runs pair Clause nodes with OTP nodes both ways.
- **`mnesia_disc_masters` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_disc_masters/mnesia_disc_masters.erl)). Two disc nodes set master
  nodes while running and while stopped, refuse majority writes with one node down, start alone and force their
  loads; then replicas are added, moved and changed between the nodes, b leaves the schema while stopped, its files
  go, and it joins again as a RAM node made a disc node. Its runs pair Clause nodes with OTP nodes both ways.
- **What `mnesia_dirty` covers:** answers before starting, system information, every `create_table` option and error, table
  properties, dirty access of each table type, indexes, counters, argument errors, schema changes, `schema/1`'s
  printout, error descriptions, and stopping and restarting.
