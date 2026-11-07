%% OTP's mnesia_index as other nodes call it (plan 21 step MIM1H6,
%% docs/libraries/mnesia-protocol.md#remote-reads-and-dirty-writes-mim1h6): the index reads of a peer's dirty access
%% on this node's replica, through mnesia_rpc. OTP's other internal functions are declined: no other node calls them.
-module(mnesia_index).
-export([dirty_read2/3, dirty_match_object/3]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    add_index/6,
    clear_index/4,
    db_get/2,
    db_match_erase/2,
    db_put/2,
    del_index_table/3,
    del_object_index/5,
    del_transient/2,
    del_transient/3,
    delete_index/4,
    dirty_read/3,
    dirty_select/3,
    ext_index_instances/1,
    get_index_table/2,
    get_index_table/3,
    index_info/2,
    index_vals_f/3,
    init_indecies/3,
    init_index/2,
    read/5,
    tab2filename/2
]).

% The records of this node's replica whose attribute at Pos is Value.
dirty_read2(Tab, Value, Pos) ->
    clause_mnesia_index:read(Tab, Value, Pos).

% The records of this node's replica matching Pattern, found through the index at Pos.
dirty_match_object(Tab, Pattern, Pos) ->
    clause_mnesia_index:match_object(Tab, Pattern, Pos).

add_index(_, _, _, _, _, _) -> erlang:error({not_implemented, add_index, 6}).
clear_index(_, _, _, _) -> erlang:error({not_implemented, clear_index, 4}).
db_get(_, _) -> erlang:error({not_implemented, db_get, 2}).
db_match_erase(_, _) -> erlang:error({not_implemented, db_match_erase, 2}).
db_put(_, _) -> erlang:error({not_implemented, db_put, 2}).
del_index_table(_, _, _) -> erlang:error({not_implemented, del_index_table, 3}).
del_object_index(_, _, _, _, _) -> erlang:error({not_implemented, del_object_index, 5}).
del_transient(_, _) -> erlang:error({not_implemented, del_transient, 2}).
del_transient(_, _, _) -> erlang:error({not_implemented, del_transient, 3}).
delete_index(_, _, _, _) -> erlang:error({not_implemented, delete_index, 4}).
dirty_read(_, _, _) -> erlang:error({not_implemented, dirty_read, 3}).
dirty_select(_, _, _) -> erlang:error({not_implemented, dirty_select, 3}).
ext_index_instances(_) -> erlang:error({not_implemented, ext_index_instances, 1}).
get_index_table(_, _) -> erlang:error({not_implemented, get_index_table, 2}).
get_index_table(_, _, _) -> erlang:error({not_implemented, get_index_table, 3}).
index_info(_, _) -> erlang:error({not_implemented, index_info, 2}).
index_vals_f(_, _, _) -> erlang:error({not_implemented, index_vals_f, 3}).
init_indecies(_, _, _) -> erlang:error({not_implemented, init_indecies, 3}).
init_index(_, _) -> erlang:error({not_implemented, init_index, 2}).
read(_, _, _, _, _) -> erlang:error({not_implemented, read, 5}).
tab2filename(_, _) -> erlang:error({not_implemented, tab2filename, 2}).
