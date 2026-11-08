%% OTP's mnesia_lib as other nodes call it (plan 21 step MIM1H3,
%% docs/libraries/mnesia-protocol.md#remote-queries-mim1h3): whether mnesia runs here, the node number, and the record
%% reads of a peer's dirty access and mnesia_rpc, and the variable a node loading a table from this one sets (MIM1H9). OTP's other internal functions are declined: no other node calls them.
-module(mnesia_lib).
-export([is_running/0, is_running_remote/0, running_nodes/1, get_node_number/0]).
-export([db_get/2, db_get/3, db_first/1, db_first/2, db_last/1, db_last/2]).
-export([db_next_key/2, db_next_key/3, db_prev_key/2, db_prev_key/3]).
-export([db_slot/2, db_slot/3, db_select_init/4, db_select_rev_init/4, db_select_cont/3, set/2]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    activate_debug_fun/5,
    active_tables/0,
    add/2,
    add_list/2,
    add_lsort/2,
    all_nodes/0,
    copy_file/2,
    copy_holders/1,
    core_file/0,
    coredump/0,
    coredump/1,
    create_counter/1,
    cs_to_nodes/1,
    cs_to_storage_type/2,
    db_chunk/2,
    db_erase/2,
    db_erase/3,
    db_erase_tab/1,
    db_erase_tab/2,
    db_fixtable/3,
    db_foldl/3,
    db_foldl/4,
    db_foldl/6,
    db_init_chunk/1,
    db_init_chunk/2,
    db_init_chunk/3,
    db_match_erase/2,
    db_match_erase/3,
    db_match_object/2,
    db_match_object/3,
    db_put/2,
    db_put/3,
    db_select/2,
    db_select/3,
    db_select_rev/2,
    db_select_rev/3,
    db_update_counter/3,
    db_update_counter/4,
    dbg_out/2,
    deactivate_debug_fun/3,
    del/2,
    dets_sync_close/1,
    dets_sync_open/2,
    dets_sync_open/3,
    dets_to_ets/6,
    dir/0,
    dir/1,
    dir_info/0,
    dirty_rpc_error_tag/1,
    disk_type/1,
    disk_type/2,
    dist_coredump/0,
    elems/2,
    ensure_loaded/1,
    error/2,
    error_desc/1,
    etype/1,
    eval_debug_fun/4,
    exists/1,
    fatal/2,
    fix_error/1,
    have_majority/2,
    have_majority/3,
    important/2,
    incr_counter/1,
    incr_counter/2,
    intersect/2,
    is_debug_compiled/0,
    is_running/1,
    is_string/1,
    key_search_all/3,
    key_search_delete/3,
    last_error/0,
    local_active_tables/0,
    lock_table/1,
    mkcore/1,
    not_active_here/1,
    other_val/2,
    overload_read/0,
    overload_read/1,
    overload_set/2,
    pad_name/3,
    random_time/2,
    read_counter/1,
    readable_indecies/1,
    remote_copy_holders/1,
    report_fatal/2,
    report_system_event/1,
    running_nodes/0,
    schema_cs_to_storage_type/2,
    scratch_debug_fun/0,
    search_delete/2,
    semantics/2,
    set_counter/2,
    set_local_content_whereabouts/1,
    set_remote_where_to_read/1,
    set_remote_where_to_read/2,
    show/1,
    show/2,
    sort_commit/1,
    storage_type_at_node/2,
    tab2dat/1,
    tab2dcd/1,
    tab2dcl/1,
    tab2dmp/1,
    tab2logtmp/1,
    tab2tmp/1,
    to_list/1,
    union/2,
    uniq/1,
    unlock_table/1,
    unset/1,
    val/1,
    validate_key/2,
    validate_record/2,
    vcore/0,
    vcore/1,
    verbose/2,
    view/0,
    view/1,
    view/2,
    warning/2
]).

% yes, no, starting or stopping.
is_running() ->
    clause_mnesia_monitor:is_running().

% A peer's question whether mnesia runs here: {Running, node()}.
is_running_remote() ->
    {is_running() =:= yes, node()}.

% The nodes of Nodes where mnesia runs, as their answers say.
running_nodes(Nodes) ->
    {Replies, _} = rpc:multicall(Nodes, ?MODULE, is_running_remote, []),
    [Node || {true, Node} <- Replies].

% The node and the process that answers, for a peer's crash report.
get_node_number() ->
    {node(), self()}.

%% Reads of this node's replica, by the table's storage type, as OTP's: a disc_only_copies table's last and previous
%% keys are its dets table's first and next.

db_get(Tab, Key) -> db_get(storage_type(Tab), Tab, Key).

db_get(ram_copies, Tab, Key) -> ets:lookup(Tab, Key);
db_get(disc_copies, Tab, Key) -> ets:lookup(Tab, Key);
db_get(disc_only_copies, Tab, Key) -> dets:lookup(Tab, Key).

db_first(Tab) -> db_first(storage_type(Tab), Tab).

db_first(ram_copies, Tab) -> ets:first(Tab);
db_first(disc_copies, Tab) -> ets:first(Tab);
db_first(disc_only_copies, Tab) -> dets:first(Tab).

db_last(Tab) -> db_last(storage_type(Tab), Tab).

db_last(ram_copies, Tab) -> ets:last(Tab);
db_last(disc_copies, Tab) -> ets:last(Tab);
db_last(disc_only_copies, Tab) -> dets:first(Tab).

db_next_key(Tab, Key) -> db_next_key(storage_type(Tab), Tab, Key).

db_next_key(ram_copies, Tab, Key) -> ets:next(Tab, Key);
db_next_key(disc_copies, Tab, Key) -> ets:next(Tab, Key);
db_next_key(disc_only_copies, Tab, Key) -> dets:next(Tab, Key).

db_prev_key(Tab, Key) -> db_prev_key(storage_type(Tab), Tab, Key).

db_prev_key(ram_copies, Tab, Key) -> ets:prev(Tab, Key);
db_prev_key(disc_copies, Tab, Key) -> ets:prev(Tab, Key);
db_prev_key(disc_only_copies, Tab, Key) -> dets:next(Tab, Key).

db_slot(Tab, Slot) -> db_slot(storage_type(Tab), Tab, Slot).

db_slot(ram_copies, Tab, Slot) -> ets:slot(Tab, Slot);
db_slot(disc_copies, Tab, Slot) -> ets:slot(Tab, Slot);
db_slot(disc_only_copies, Tab, Slot) -> dets:slot(Tab, Slot).

% The first chunk of a peer's select/4 on this node's replica, of the storage the peer knows it by (MIM1H6).
db_select_init(disc_only_copies, Tab, Spec, N) -> dets:select(Tab, Spec, N);
db_select_init(_, Tab, Spec, N) -> ets:select(Tab, Spec, N).

db_select_rev_init(disc_only_copies, Tab, Spec, N) -> dets:select(Tab, Spec, N);
db_select_rev_init(_, Tab, Spec, N) -> ets:select_reverse(Tab, Spec, N).

% The next chunk of a continuation db_select_init/4 or db_select_rev_init/4 gave; an ets continuation that went to
% another node and back gets its compiled Spec again.
db_select_cont(_, '$end_of_table', _) -> '$end_of_table';
db_select_cont(disc_only_copies, Cont, _Spec) -> dets:select(Cont);
db_select_cont(_, Cont, Spec) -> ets:select(ets:repair_continuation(Cont, Spec)).

% Where this node keeps the table; aborts with no_exists, or node_not_running when mnesia is not running.
storage_type(Tab) ->
    clause_mnesia_monitor:val({Tab, storage_type}).

% A global variable, as a node loading a new copy from this one sets {mnesia_loader, active_trans} (MIM1H9).
set(Name, Value) ->
    ets:insert(mnesia_gvar, {Name, Value}).

activate_debug_fun(_, _, _, _, _) -> erlang:error({not_implemented, activate_debug_fun, 5}).
active_tables() -> erlang:error({not_implemented, active_tables, 0}).
add(_, _) -> erlang:error({not_implemented, add, 2}).
add_list(_, _) -> erlang:error({not_implemented, add_list, 2}).
add_lsort(_, _) -> erlang:error({not_implemented, add_lsort, 2}).
all_nodes() -> erlang:error({not_implemented, all_nodes, 0}).
copy_file(_, _) -> erlang:error({not_implemented, copy_file, 2}).
copy_holders(_) -> erlang:error({not_implemented, copy_holders, 1}).
core_file() -> erlang:error({not_implemented, core_file, 0}).
coredump() -> erlang:error({not_implemented, coredump, 0}).
coredump(_) -> erlang:error({not_implemented, coredump, 1}).
create_counter(_) -> erlang:error({not_implemented, create_counter, 1}).
cs_to_nodes(_) -> erlang:error({not_implemented, cs_to_nodes, 1}).
cs_to_storage_type(_, _) -> erlang:error({not_implemented, cs_to_storage_type, 2}).
db_chunk(_, _) -> erlang:error({not_implemented, db_chunk, 2}).
db_erase(_, _) -> erlang:error({not_implemented, db_erase, 2}).
db_erase(_, _, _) -> erlang:error({not_implemented, db_erase, 3}).
db_erase_tab(_) -> erlang:error({not_implemented, db_erase_tab, 1}).
db_erase_tab(_, _) -> erlang:error({not_implemented, db_erase_tab, 2}).
db_fixtable(_, _, _) -> erlang:error({not_implemented, db_fixtable, 3}).
db_foldl(_, _, _) -> erlang:error({not_implemented, db_foldl, 3}).
db_foldl(_, _, _, _) -> erlang:error({not_implemented, db_foldl, 4}).
db_foldl(_, _, _, _, _, _) -> erlang:error({not_implemented, db_foldl, 6}).
db_init_chunk(_) -> erlang:error({not_implemented, db_init_chunk, 1}).
db_init_chunk(_, _) -> erlang:error({not_implemented, db_init_chunk, 2}).
db_init_chunk(_, _, _) -> erlang:error({not_implemented, db_init_chunk, 3}).
db_match_erase(_, _) -> erlang:error({not_implemented, db_match_erase, 2}).
db_match_erase(_, _, _) -> erlang:error({not_implemented, db_match_erase, 3}).
db_match_object(_, _) -> erlang:error({not_implemented, db_match_object, 2}).
db_match_object(_, _, _) -> erlang:error({not_implemented, db_match_object, 3}).
db_put(_, _) -> erlang:error({not_implemented, db_put, 2}).
db_put(_, _, _) -> erlang:error({not_implemented, db_put, 3}).
db_select(_, _) -> erlang:error({not_implemented, db_select, 2}).
db_select(_, _, _) -> erlang:error({not_implemented, db_select, 3}).
db_select_rev(_, _) -> erlang:error({not_implemented, db_select_rev, 2}).
db_select_rev(_, _, _) -> erlang:error({not_implemented, db_select_rev, 3}).
db_update_counter(_, _, _) -> erlang:error({not_implemented, db_update_counter, 3}).
db_update_counter(_, _, _, _) -> erlang:error({not_implemented, db_update_counter, 4}).
dbg_out(_, _) -> erlang:error({not_implemented, dbg_out, 2}).
deactivate_debug_fun(_, _, _) -> erlang:error({not_implemented, deactivate_debug_fun, 3}).
del(_, _) -> erlang:error({not_implemented, del, 2}).
dets_sync_close(_) -> erlang:error({not_implemented, dets_sync_close, 1}).
dets_sync_open(_, _) -> erlang:error({not_implemented, dets_sync_open, 2}).
dets_sync_open(_, _, _) -> erlang:error({not_implemented, dets_sync_open, 3}).
dets_to_ets(_, _, _, _, _, _) -> erlang:error({not_implemented, dets_to_ets, 6}).
dir() -> erlang:error({not_implemented, dir, 0}).
dir(_) -> erlang:error({not_implemented, dir, 1}).
dir_info() -> erlang:error({not_implemented, dir_info, 0}).
dirty_rpc_error_tag(_) -> erlang:error({not_implemented, dirty_rpc_error_tag, 1}).
disk_type(_) -> erlang:error({not_implemented, disk_type, 1}).
disk_type(_, _) -> erlang:error({not_implemented, disk_type, 2}).
dist_coredump() -> erlang:error({not_implemented, dist_coredump, 0}).
elems(_, _) -> erlang:error({not_implemented, elems, 2}).
ensure_loaded(_) -> erlang:error({not_implemented, ensure_loaded, 1}).
error(_, _) -> erlang:error({not_implemented, error, 2}).
error_desc(_) -> erlang:error({not_implemented, error_desc, 1}).
etype(_) -> erlang:error({not_implemented, etype, 1}).
eval_debug_fun(_, _, _, _) -> erlang:error({not_implemented, eval_debug_fun, 4}).
exists(_) -> erlang:error({not_implemented, exists, 1}).
fatal(_, _) -> erlang:error({not_implemented, fatal, 2}).
fix_error(_) -> erlang:error({not_implemented, fix_error, 1}).
have_majority(_, _) -> erlang:error({not_implemented, have_majority, 2}).
have_majority(_, _, _) -> erlang:error({not_implemented, have_majority, 3}).
important(_, _) -> erlang:error({not_implemented, important, 2}).
incr_counter(_) -> erlang:error({not_implemented, incr_counter, 1}).
incr_counter(_, _) -> erlang:error({not_implemented, incr_counter, 2}).
intersect(_, _) -> erlang:error({not_implemented, intersect, 2}).
is_debug_compiled() -> erlang:error({not_implemented, is_debug_compiled, 0}).
is_running(_) -> erlang:error({not_implemented, is_running, 1}).
is_string(_) -> erlang:error({not_implemented, is_string, 1}).
key_search_all(_, _, _) -> erlang:error({not_implemented, key_search_all, 3}).
key_search_delete(_, _, _) -> erlang:error({not_implemented, key_search_delete, 3}).
last_error() -> erlang:error({not_implemented, last_error, 0}).
local_active_tables() -> erlang:error({not_implemented, local_active_tables, 0}).
lock_table(_) -> erlang:error({not_implemented, lock_table, 1}).
mkcore(_) -> erlang:error({not_implemented, mkcore, 1}).
not_active_here(_) -> erlang:error({not_implemented, not_active_here, 1}).
other_val(_, _) -> erlang:error({not_implemented, other_val, 2}).
overload_read() -> erlang:error({not_implemented, overload_read, 0}).
overload_read(_) -> erlang:error({not_implemented, overload_read, 1}).
overload_set(_, _) -> erlang:error({not_implemented, overload_set, 2}).
pad_name(_, _, _) -> erlang:error({not_implemented, pad_name, 3}).
random_time(_, _) -> erlang:error({not_implemented, random_time, 2}).
read_counter(_) -> erlang:error({not_implemented, read_counter, 1}).
readable_indecies(_) -> erlang:error({not_implemented, readable_indecies, 1}).
remote_copy_holders(_) -> erlang:error({not_implemented, remote_copy_holders, 1}).
report_fatal(_, _) -> erlang:error({not_implemented, report_fatal, 2}).
report_system_event(_) -> erlang:error({not_implemented, report_system_event, 1}).
running_nodes() -> erlang:error({not_implemented, running_nodes, 0}).
schema_cs_to_storage_type(_, _) -> erlang:error({not_implemented, schema_cs_to_storage_type, 2}).
scratch_debug_fun() -> erlang:error({not_implemented, scratch_debug_fun, 0}).
search_delete(_, _) -> erlang:error({not_implemented, search_delete, 2}).
semantics(_, _) -> erlang:error({not_implemented, semantics, 2}).
set_counter(_, _) -> erlang:error({not_implemented, set_counter, 2}).
set_local_content_whereabouts(_) ->
    erlang:error({not_implemented, set_local_content_whereabouts, 1}).
set_remote_where_to_read(_) -> erlang:error({not_implemented, set_remote_where_to_read, 1}).
set_remote_where_to_read(_, _) -> erlang:error({not_implemented, set_remote_where_to_read, 2}).
show(_) -> erlang:error({not_implemented, show, 1}).
show(_, _) -> erlang:error({not_implemented, show, 2}).
sort_commit(_) -> erlang:error({not_implemented, sort_commit, 1}).
storage_type_at_node(_, _) -> erlang:error({not_implemented, storage_type_at_node, 2}).
tab2dat(_) -> erlang:error({not_implemented, tab2dat, 1}).
tab2dcd(_) -> erlang:error({not_implemented, tab2dcd, 1}).
tab2dcl(_) -> erlang:error({not_implemented, tab2dcl, 1}).
tab2dmp(_) -> erlang:error({not_implemented, tab2dmp, 1}).
tab2logtmp(_) -> erlang:error({not_implemented, tab2logtmp, 1}).
tab2tmp(_) -> erlang:error({not_implemented, tab2tmp, 1}).
to_list(_) -> erlang:error({not_implemented, to_list, 1}).
union(_, _) -> erlang:error({not_implemented, union, 2}).
uniq(_) -> erlang:error({not_implemented, uniq, 1}).
unlock_table(_) -> erlang:error({not_implemented, unlock_table, 1}).
unset(_) -> erlang:error({not_implemented, unset, 1}).
val(_) -> erlang:error({not_implemented, val, 1}).
validate_key(_, _) -> erlang:error({not_implemented, validate_key, 2}).
validate_record(_, _) -> erlang:error({not_implemented, validate_record, 2}).
vcore() -> erlang:error({not_implemented, vcore, 0}).
vcore(_) -> erlang:error({not_implemented, vcore, 1}).
verbose(_, _) -> erlang:error({not_implemented, verbose, 2}).
view() -> erlang:error({not_implemented, view, 0}).
view(_) -> erlang:error({not_implemented, view, 1}).
view(_, _) -> erlang:error({not_implemented, view, 2}).
warning(_, _) -> erlang:error({not_implemented, warning, 2}).
