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
- **Still to come**, raising `{not_implemented, Name, Arity}` until then:
  - the other schema changes (MIM1C);
  - disc storage (MIM1D);
  - events, text files, `qlc` tables and `info/0` (MIM1E);
  - checkpoints and backups (MIM1F);
  - fragments, `mnesia_frag_hash` and SNMP (MIM1G).

`mnesia` stays PARTIAL in [library_modules.py](../../tests/compiler/library/library_modules.py) until MIM1G. See the
[function inventory](../../library/mnesia/mnesia.terms) and [differences](../differences.md#mnesia).

## Design

mnesia is written in Erlang over `ets`, in its own structure. Where programs can observe something, it keeps OTP's
shape:

- **Tables:** each table is a public named `ets` table with `keypos` 2, which code may read directly.
- **Results and errors:** results and `{aborted, Reason}` terms are OTP's, and so are the exits of `abort/1` and of
  dirty functions (`{aborted, Reason}`).
- **Table definitions:** the `cstruct` record of `table_info(Tab, cstruct)` and of the schema table's records has
  OTP's fields, and so do the cookies and versions.
- **Registered names:** the processes Clause has use OTP's names.

The design keeps OTP's formats, so that a later step (MIM1H) can add more nodes.

### Processes

`application:start(mnesia)`, or `mnesia:start/0,1`, starts this tree ([clause_mnesia_app](../../library/mnesia/clause_mnesia_app.erl)):

- `mnesia_sup`:
  - `mnesia_event`, the `gen_event` manager for system events;
  - `mnesia_ext_sup`, for backend processes;
  - `mnesia_kernel_sup`:
    - `mnesia_monitor` ([clause_mnesia_monitor](../../library/mnesia/clause_mnesia_monitor.erl));
    - `mnesia_locker`, the lock manager ([clause_mnesia_locker](../../library/mnesia/clause_mnesia_locker.erl));
    - `mnesia_tm`, the transaction manager ([clause_mnesia_tm](../../library/mnesia/clause_mnesia_tm.erl)).

Later steps add the other servers.

`mnesia_monitor` does three things:

- **Owns the tables:** `mnesia_gvar`, the `schema` table, every table and every index table belong to it. They go
  when mnesia stops.
- **Reads the configuration:** see [configuration](#configuration).
- **Applies schema changes:** `create_table`, `delete_table`, `add_table_index` and `del_table_index` apply one at a
  time. Each runs in a schema transaction of its own process, as OTP's schema coordinator does:
  - it takes the schema's write lock, and the table's write lock (`delete_table`) or read lock (the index changes);
  - it counts as one committed or failed transaction; starting counts two commits;
  - inside any activity it returns `{aborted, nested_transaction}`.

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
- **Other rows:** `mnesia_status`, the configuration values, `{schema, tables}`, `{schema, local_tables}`,
  `{current, db_nodes}` and the transaction counters.

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

The schema lives in RAM: `use_dir` is `false`. Disc schemas come with MIM1D.

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
5. **Disc nodes:** a disc node that is not running gives `not_active`. Disc copies on this RAM node give
   `{bad_type, Tab, disc_copies, Node}`.

Behavior that follows OTP's:

- A table on other nodes only exists in the schema, with no replica here: `where_to_read` is `nowhere` and
  `storage_type` is `unknown`.
- A `read_only` table is written nowhere: dirty writes abort with `{no_exists, Tab}`.
- `{ram_copies, []}` with no other copies means a table on this node.
- An index listed twice gives two index tables, as in OTP.

`delete_table/1` removes the table, its index tables and its rows. `wait_for_tables/2` waits until each table has a
replica to read here, and returns `{timeout, Tables}` for the rest.

## Dirty access

The dirty functions run in the calling process and work on the table's `ets` table:

- **Reads** (`dirty_read`, `dirty_match_object`, `dirty_select`, `dirty_select_reverse`, `dirty_all_keys`, the walk
  functions, `dirty_index_read`, `dirty_index_match_object`) go to this node's replica. Without a replica they abort
  with `{no_exists, Args}`, and a failing read gives `{badarg, Args}`.
- **Writes** (`dirty_write`, `dirty_delete`, `dirty_delete_object`, `dirty_update_counter`) first check the record
  against the table's record name and arity. They then update the replica and its indexes.
- **Counters:** `dirty_update_counter` follows OTP's rules:
  - the table must be a set or ordered_set of three-field records;
  - a missing counter starts at the increment, or at zero;
  - a value that is not an integer, or that would go below zero, is replaced the same way.
- **Index use:** a match or one-clause select whose pattern leaves the key unbound but binds an indexed position
  uses that index. It returns the records in OTP's order: last key first for matches, the table's order for
  `ordered_set` selects.

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

- **Bookkeeping:** `index_info` and `commit_work` list the indexes newest first, as `{{Pos, ordered}, {ram, Table}}`.
  `table_info(Tab, index)` lists their positions in that order.
- **Upkeep:** every write and delete keeps the index tables current:
  - a set table's entry changes only when its value does;
  - a bag table's entry goes when no other record under the key holds its value.
- **Schema changes:** `add_table_index/2` fills a new index from the table's records. `del_table_index/2` removes
  every index of the position.
- **Errors:** both answer with OTP's reasons (`already_exists`, `no_exists`, `bad_type`), and each change gives the
  table definition its next major version.

## Tests

- **`mnesia_dirty` golden:** generated from OTP ([fixture](../../tests/fixtures/executables/mnesia_dirty/mnesia_dirty.erl)).
- **`mnesia_transactions` golden:** generated from OTP
  ([fixture](../../tests/fixtures/executables/mnesia_transactions/mnesia_transactions.erl)). It covers transaction
  results and abort terms, reads of own writes in each table type, walks, folds and index reads, nesting, the activity
  kinds, an access module, lock results and errors, conflicts between processes (waiting, restarts, retries running
  out, shared reads, a killed holder), schema transactions and select continuations.
- **What it covers:** answers before starting, system information, every `create_table` option and error, table
  properties, dirty access of each table type, indexes, counters, argument errors, schema changes, `schema/1`'s
  printout, error descriptions, and stopping and restarting.
