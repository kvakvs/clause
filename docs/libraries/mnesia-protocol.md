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
