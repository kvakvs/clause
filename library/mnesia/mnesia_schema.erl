%% OTP's mnesia_schema as other nodes call it (plan 21 step MIM1H3,
%% docs/libraries/mnesia-protocol.md#remote-queries-mim1h3): where this node would take its schema from, for
%% create_schema/1 elsewhere, and its directory's files deleted, for delete_schema/1 elsewhere. OTP's other internal
%% functions are declined: no other node calls them.
-module(mnesia_schema).
-export([remote_read_schema/0, delete_schema2/0]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    add_backend_type/2,
    add_index_plugin/3,
    add_snmp/2,
    add_table_copy/3,
    add_table_index/2,
    arrange_restore/3,
    attr_tab_to_pos/2,
    attr_to_pos/2,
    backend_types/0,
    change_table_access_mode/2,
    change_table_copy_type/3,
    change_table_frag/2,
    change_table_load_order/2,
    change_table_majority/2,
    check_duplicates/2,
    check_keys/3,
    create_table/1,
    cs2list/1,
    del_snmp/1,
    del_table_copy/2,
    del_table_index/2,
    delete_backend_type/1,
    delete_cstruct/2,
    delete_index_plugin/1,
    delete_schema/1,
    delete_table/1,
    delete_table_property/2,
    do_add_backend_type/2,
    do_add_index_plugin/3,
    do_change_table_copy_type/3,
    do_create_table/1,
    do_delete_backend_type/1,
    do_delete_index_plugin/1,
    do_delete_table/1,
    do_delete_table_property/2,
    do_read_table_property/2,
    do_write_table_property/2,
    dump_tables/1,
    ensure_active/1,
    ensure_no_schema/1,
    get_create_list/1,
    get_initial_schema/3,
    get_table_properties/1,
    get_tid_ts_and_lock/2,
    incr_version/1,
    index_plugins/0,
    info/0,
    info/1,
    init/1,
    init_backends/0,
    insert_cstruct/3,
    insert_schema_ops/2,
    is_remote_member/1,
    list2cs/1,
    list2cs/2,
    lock_schema/0,
    make_create_table/1,
    make_delete_table/2,
    merge_schema/0,
    merge_schema/1,
    move_table/3,
    normalize_cs/2,
    opt_create_dir/2,
    pick/4,
    prepare_commit/3,
    purge_dir/2,
    purge_tmp_files/0,
    ram_delete_table/2,
    read_cstructs_from_disc/0,
    read_nodes/0,
    restore/1,
    restore/2,
    restore/3,
    schema_coordinator/3,
    schema_transaction/1,
    set_where_to_read/3,
    transform_table/4,
    undo_prepare_commit/2,
    unlock_schema/0,
    verify/3,
    version/0,
    vsn_cs2list/1,
    write_table_property/2
]).

% {ok, ram | default | fallback | disc, CreateList} with the schema's create list, or {error, Reason}.
remote_read_schema() ->
    clause_mnesia_disc:read_schema().

% ok, or {error, {"Mnesia still running", node()}}.
delete_schema2() ->
    clause_mnesia_disc:purge_local().

add_backend_type(_, _) -> erlang:error({not_implemented, add_backend_type, 2}).
add_index_plugin(_, _, _) -> erlang:error({not_implemented, add_index_plugin, 3}).
add_snmp(_, _) -> erlang:error({not_implemented, add_snmp, 2}).
add_table_copy(_, _, _) -> erlang:error({not_implemented, add_table_copy, 3}).
add_table_index(_, _) -> erlang:error({not_implemented, add_table_index, 2}).
arrange_restore(_, _, _) -> erlang:error({not_implemented, arrange_restore, 3}).
attr_tab_to_pos(_, _) -> erlang:error({not_implemented, attr_tab_to_pos, 2}).
attr_to_pos(_, _) -> erlang:error({not_implemented, attr_to_pos, 2}).
backend_types() -> erlang:error({not_implemented, backend_types, 0}).
change_table_access_mode(_, _) -> erlang:error({not_implemented, change_table_access_mode, 2}).
change_table_copy_type(_, _, _) -> erlang:error({not_implemented, change_table_copy_type, 3}).
change_table_frag(_, _) -> erlang:error({not_implemented, change_table_frag, 2}).
change_table_load_order(_, _) -> erlang:error({not_implemented, change_table_load_order, 2}).
change_table_majority(_, _) -> erlang:error({not_implemented, change_table_majority, 2}).
check_duplicates(_, _) -> erlang:error({not_implemented, check_duplicates, 2}).
check_keys(_, _, _) -> erlang:error({not_implemented, check_keys, 3}).
create_table(_) -> erlang:error({not_implemented, create_table, 1}).
cs2list(_) -> erlang:error({not_implemented, cs2list, 1}).
del_snmp(_) -> erlang:error({not_implemented, del_snmp, 1}).
del_table_copy(_, _) -> erlang:error({not_implemented, del_table_copy, 2}).
del_table_index(_, _) -> erlang:error({not_implemented, del_table_index, 2}).
delete_backend_type(_) -> erlang:error({not_implemented, delete_backend_type, 1}).
delete_cstruct(_, _) -> erlang:error({not_implemented, delete_cstruct, 2}).
delete_index_plugin(_) -> erlang:error({not_implemented, delete_index_plugin, 1}).
delete_schema(_) -> erlang:error({not_implemented, delete_schema, 1}).
delete_table(_) -> erlang:error({not_implemented, delete_table, 1}).
delete_table_property(_, _) -> erlang:error({not_implemented, delete_table_property, 2}).
do_add_backend_type(_, _) -> erlang:error({not_implemented, do_add_backend_type, 2}).
do_add_index_plugin(_, _, _) -> erlang:error({not_implemented, do_add_index_plugin, 3}).
do_change_table_copy_type(_, _, _) -> erlang:error({not_implemented, do_change_table_copy_type, 3}).
do_create_table(_) -> erlang:error({not_implemented, do_create_table, 1}).
do_delete_backend_type(_) -> erlang:error({not_implemented, do_delete_backend_type, 1}).
do_delete_index_plugin(_) -> erlang:error({not_implemented, do_delete_index_plugin, 1}).
do_delete_table(_) -> erlang:error({not_implemented, do_delete_table, 1}).
do_delete_table_property(_, _) -> erlang:error({not_implemented, do_delete_table_property, 2}).
do_read_table_property(_, _) -> erlang:error({not_implemented, do_read_table_property, 2}).
do_write_table_property(_, _) -> erlang:error({not_implemented, do_write_table_property, 2}).
dump_tables(_) -> erlang:error({not_implemented, dump_tables, 1}).
ensure_active(_) -> erlang:error({not_implemented, ensure_active, 1}).
ensure_no_schema(_) -> erlang:error({not_implemented, ensure_no_schema, 1}).
get_create_list(_) -> erlang:error({not_implemented, get_create_list, 1}).
get_initial_schema(_, _, _) -> erlang:error({not_implemented, get_initial_schema, 3}).
get_table_properties(_) -> erlang:error({not_implemented, get_table_properties, 1}).
get_tid_ts_and_lock(_, _) -> erlang:error({not_implemented, get_tid_ts_and_lock, 2}).
incr_version(_) -> erlang:error({not_implemented, incr_version, 1}).
index_plugins() -> erlang:error({not_implemented, index_plugins, 0}).
info() -> erlang:error({not_implemented, info, 0}).
info(_) -> erlang:error({not_implemented, info, 1}).
init(_) -> erlang:error({not_implemented, init, 1}).
init_backends() -> erlang:error({not_implemented, init_backends, 0}).
insert_cstruct(_, _, _) -> erlang:error({not_implemented, insert_cstruct, 3}).
insert_schema_ops(_, _) -> erlang:error({not_implemented, insert_schema_ops, 2}).
is_remote_member(_) -> erlang:error({not_implemented, is_remote_member, 1}).
list2cs(_) -> erlang:error({not_implemented, list2cs, 1}).
list2cs(_, _) -> erlang:error({not_implemented, list2cs, 2}).
lock_schema() -> erlang:error({not_implemented, lock_schema, 0}).
make_create_table(_) -> erlang:error({not_implemented, make_create_table, 1}).
make_delete_table(_, _) -> erlang:error({not_implemented, make_delete_table, 2}).
merge_schema() -> erlang:error({not_implemented, merge_schema, 0}).
merge_schema(_) -> erlang:error({not_implemented, merge_schema, 1}).
move_table(_, _, _) -> erlang:error({not_implemented, move_table, 3}).
normalize_cs(_, _) -> erlang:error({not_implemented, normalize_cs, 2}).
opt_create_dir(_, _) -> erlang:error({not_implemented, opt_create_dir, 2}).
pick(_, _, _, _) -> erlang:error({not_implemented, pick, 4}).
prepare_commit(_, _, _) -> erlang:error({not_implemented, prepare_commit, 3}).
purge_dir(_, _) -> erlang:error({not_implemented, purge_dir, 2}).
purge_tmp_files() -> erlang:error({not_implemented, purge_tmp_files, 0}).
ram_delete_table(_, _) -> erlang:error({not_implemented, ram_delete_table, 2}).
read_cstructs_from_disc() -> erlang:error({not_implemented, read_cstructs_from_disc, 0}).
read_nodes() -> erlang:error({not_implemented, read_nodes, 0}).
restore(_) -> erlang:error({not_implemented, restore, 1}).
restore(_, _) -> erlang:error({not_implemented, restore, 2}).
restore(_, _, _) -> erlang:error({not_implemented, restore, 3}).
schema_coordinator(_, _, _) -> erlang:error({not_implemented, schema_coordinator, 3}).
schema_transaction(_) -> erlang:error({not_implemented, schema_transaction, 1}).
set_where_to_read(_, _, _) -> erlang:error({not_implemented, set_where_to_read, 3}).
transform_table(_, _, _, _) -> erlang:error({not_implemented, transform_table, 4}).
undo_prepare_commit(_, _) -> erlang:error({not_implemented, undo_prepare_commit, 2}).
unlock_schema() -> erlang:error({not_implemented, unlock_schema, 0}).
verify(_, _, _) -> erlang:error({not_implemented, verify, 3}).
version() -> erlang:error({not_implemented, version, 0}).
vsn_cs2list(_) -> erlang:error({not_implemented, vsn_cs2list, 1}).
write_table_property(_, _) -> erlang:error({not_implemented, write_table_property, 2}).
