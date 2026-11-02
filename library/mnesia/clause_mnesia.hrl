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
