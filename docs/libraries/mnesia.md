# mnesia

Clause's `mnesia` application (plan 21, MIM1) exports the OTP 29 interface of `mnesia` (`mnesia-4.27`) and
`mnesia_frag_hash`. The work is split into steps:

- **MIM1A, done:**
  - starting and stopping, and the [configuration parameters](#configuration);
  - the RAM [schema](#schema): `create_table/2` with OTP's checks, `delete_table/1`, `table_info/2`, `system_info/1`,
    `schema/0,1`, `wait_for_tables/2` and `error_description/1`;
  - [dirty access](#dirty-access) to `set`, `ordered_set` and `bag` tables;
  - [indexes](#indexes).
- **Still to come**, raising `{not_implemented, Name, Arity}` until then:
  - transactions and activities (MIM1B);
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
    - `mnesia_monitor` ([clause_mnesia_monitor](../../library/mnesia/clause_mnesia_monitor.erl)).

Later steps add the lock manager, the transaction manager and the other servers.

`mnesia_monitor` does three things:

- **Owns the tables:** `mnesia_gvar`, the `schema` table, every table and every index table belong to it. They go
  when mnesia stops.
- **Reads the configuration:** see [configuration](#configuration).
- **Runs schema changes:** `create_table`, `delete_table`, `add_table_index` and `del_table_index` run one at a time.
  Each counts as one committed or failed transaction, as OTP's schema transactions do. Starting counts two commits.

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
- **What it covers:** answers before starting, system information, every `create_table` option and error, table
  properties, dirty access of each table type, indexes, counters, argument errors, schema changes, `schema/1`'s
  printout, error descriptions, and stopping and restarting.
