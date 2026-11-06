%% Records and macros shared by Clause's mnesia modules (plan 21 step MIM1A, docs/libraries/mnesia.md).

%% A table definition as OTP's schema, backups and other nodes exchange it: the same fields in the same order, so that
%% `table_info(Tab, cstruct)` and the schema's records stay OTP's.
-record(cstruct, {
    name,
    type = set,
    ram_copies = [],
    disc_copies = [],
    disc_only_copies = [],
    external_copies = [],
    load_order = 0,
    access_mode = read_write,
    majority = false,
    index = [],
    snmp = [],
    local_content = false,
    record_name = {bad_record_name},
    attributes = [key, val],
    user_properties = [],
    frag_properties = [],
    storage_properties = [],
    cookie,
    version = {{2, 0}, []}
}).

%% A table's index work as `table_info(Tab, index_info)` shows it: the table type and one
%% {{Position, ordered}, {ram, IndexTable}} per index, the newest first.
-record(index, {setorbag, pos_list = []}).

%% The global variables table: {Name, Value} rows, {Tab, Item} for each table's properties.
-define(GVAR, mnesia_gvar).

%% A transaction's id, as `get_activity_id/0` and `system_info(held_locks)` show it (MIM1B): a serial number, older
%% transactions smaller, and the process running it.
-record(tid, {counter, pid}).

%% A transaction's opaque state: its store of locks and writes, the stores of the transactions it is nested in and its
%% nesting level (MIM1B).
-record(tidstore, {store, up_stores = [], level = 1}).

%% The reason of a refused lock, which restarts the transaction (MIM1B): the lock wanted and the holder that refused it.
-record(cyclic, {node = node(), oid, op, lock, lucky}).

%% The first term of each log file in mnesia's directory, as OTP's (MIM1D): what the log holds and its format version.
-record(log_header, {log_kind, log_version, mnesia_version, node, now}).

%% A commit's disc writes in LATEST.LOG, as OTP's (MIM1D): {{Tab, Key}, Value, Operation} items of each storage.
-record(commit, {
    node, decision, ram_copies = [], disc_copies = [], disc_only_copies = [], ext = [], schema_ops = []
}).

%% A transaction's outcome as nodes exchange it (MIM1H4): its tid, committed, aborted or unclear, and the nodes that
%% take part.
-record(decision, {tid, outcome, disc_nodes, ram_nodes}).
