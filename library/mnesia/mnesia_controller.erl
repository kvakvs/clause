%% OTP's mnesia_controller as other nodes call it (plan 21 step MIM1H3,
%% docs/libraries/mnesia-protocol.md#remote-queries-mim1h3): this node's table definitions and running nodes, which a
%% node merging its schema with this one fetches, and the requests of another node's sender loading a table here (MIM1H9). The server registered as mnesia_controller is
%% clause_mnesia_controller. OTP's other internal functions are declined: no other node calls them.
-module(mnesia_controller).
-export([get_remote_cstructs/0, get_cstructs/0, call/1]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    add_active_replica/2,
    add_active_replica/3,
    add_active_replica/4,
    async_dump_log/1,
    block_controller/0,
    block_table/1,
    cast/1,
    change_table_access_mode/1,
    change_table_majority/1,
    code_change/3,
    connect_nodes/1,
    connect_nodes/2,
    connect_nodes2/3,
    create_table/1,
    del_active_replica/2,
    dump_and_reply/2,
    force_load_table/1,
    get_disc_copy/1,
    get_info/1,
    get_network_copy/3,
    get_workers/1,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    i_have_tab/2,
    info/0,
    init/1,
    load_and_reply/2,
    master_nodes_updated/2,
    merge_schema/0,
    mnesia_down/1,
    release_schema_commit_lock/0,
    schedule_late_disc_load/2,
    send_and_reply/2,
    snapshot_dcd/1,
    start/0,
    start_remote_sender/5,
    sync_and_block_table_whereabouts/4,
    sync_del_table_copy_whereabouts/2,
    sync_dump_log/1,
    terminate/2,
    unannounce_add_table_copy/2,
    unblock_controller/0,
    unblock_table/1,
    update/1,
    wait_for_schema_commit_lock/0,
    wait_for_tables/2,
    wait_for_tables_init/3
]).

get_remote_cstructs() ->
    get_cstructs().

% {cstructs, Cstructs, RunningNodes}; without a running mnesia it fails with OTP's badmatch.
% A request to this node's controller, as a sender on another node makes it (MIM1H9).
call(Request) ->
    clause_mnesia_controller:call(Request).

get_cstructs() ->
    {cstructs, _, _} = clause_mnesia_controller:call(get_cstructs).

add_active_replica(_, _) -> erlang:error({not_implemented, add_active_replica, 2}).
add_active_replica(_, _, _) -> erlang:error({not_implemented, add_active_replica, 3}).
add_active_replica(_, _, _, _) -> erlang:error({not_implemented, add_active_replica, 4}).
async_dump_log(_) -> erlang:error({not_implemented, async_dump_log, 1}).
block_controller() -> erlang:error({not_implemented, block_controller, 0}).
block_table(_) -> erlang:error({not_implemented, block_table, 1}).
cast(_) -> erlang:error({not_implemented, cast, 1}).
change_table_access_mode(_) -> erlang:error({not_implemented, change_table_access_mode, 1}).
change_table_majority(_) -> erlang:error({not_implemented, change_table_majority, 1}).
code_change(_, _, _) -> erlang:error({not_implemented, code_change, 3}).
connect_nodes(_) -> erlang:error({not_implemented, connect_nodes, 1}).
connect_nodes(_, _) -> erlang:error({not_implemented, connect_nodes, 2}).
connect_nodes2(_, _, _) -> erlang:error({not_implemented, connect_nodes2, 3}).
create_table(_) -> erlang:error({not_implemented, create_table, 1}).
del_active_replica(_, _) -> erlang:error({not_implemented, del_active_replica, 2}).
dump_and_reply(_, _) -> erlang:error({not_implemented, dump_and_reply, 2}).
force_load_table(_) -> erlang:error({not_implemented, force_load_table, 1}).
get_disc_copy(_) -> erlang:error({not_implemented, get_disc_copy, 1}).
get_info(_) -> erlang:error({not_implemented, get_info, 1}).
get_network_copy(_, _, _) -> erlang:error({not_implemented, get_network_copy, 3}).
get_workers(_) -> erlang:error({not_implemented, get_workers, 1}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
i_have_tab(_, _) -> erlang:error({not_implemented, i_have_tab, 2}).
info() -> erlang:error({not_implemented, info, 0}).
init(_) -> erlang:error({not_implemented, init, 1}).
load_and_reply(_, _) -> erlang:error({not_implemented, load_and_reply, 2}).
master_nodes_updated(_, _) -> erlang:error({not_implemented, master_nodes_updated, 2}).
merge_schema() -> erlang:error({not_implemented, merge_schema, 0}).
mnesia_down(_) -> erlang:error({not_implemented, mnesia_down, 1}).
release_schema_commit_lock() -> erlang:error({not_implemented, release_schema_commit_lock, 0}).
schedule_late_disc_load(_, _) -> erlang:error({not_implemented, schedule_late_disc_load, 2}).
send_and_reply(_, _) -> erlang:error({not_implemented, send_and_reply, 2}).
snapshot_dcd(_) -> erlang:error({not_implemented, snapshot_dcd, 1}).
start() -> erlang:error({not_implemented, start, 0}).
start_remote_sender(_, _, _, _, _) -> erlang:error({not_implemented, start_remote_sender, 5}).
sync_and_block_table_whereabouts(_, _, _, _) ->
    erlang:error({not_implemented, sync_and_block_table_whereabouts, 4}).
sync_del_table_copy_whereabouts(_, _) ->
    erlang:error({not_implemented, sync_del_table_copy_whereabouts, 2}).
sync_dump_log(_) -> erlang:error({not_implemented, sync_dump_log, 1}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
unannounce_add_table_copy(_, _) -> erlang:error({not_implemented, unannounce_add_table_copy, 2}).
unblock_controller() -> erlang:error({not_implemented, unblock_controller, 0}).
unblock_table(_) -> erlang:error({not_implemented, unblock_table, 1}).
update(_) -> erlang:error({not_implemented, update, 1}).
wait_for_schema_commit_lock() -> erlang:error({not_implemented, wait_for_schema_commit_lock, 0}).
wait_for_tables(_, _) -> erlang:error({not_implemented, wait_for_tables, 2}).
wait_for_tables_init(_, _, _) -> erlang:error({not_implemented, wait_for_tables_init, 3}).
