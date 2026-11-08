%% OTP's mnesia_bup as other nodes call it (plan 21 step MIM1H10a,
%% docs/libraries/mnesia-protocol.md#disc-nodes-mim1h10a): the fallback receiver that a node installing a fallback here
%% spawns, as create_schema/1 over several nodes does. Fallbacks are clause_mnesia_backup's. OTP's other internal
%% functions are declined: no other node calls them.
-module(mnesia_bup).
-export([fallback_receiver/2]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    create_schema/1,
    create_schema/2,
    do_traverse_backup/7,
    fallback_bup/0,
    fallback_exists/0,
    fallback_to_schema/0,
    install_fallback/1,
    install_fallback/2,
    install_fallback_master/2,
    iterate/4,
    local_uninstall_fallback/2,
    lookup_schema/2,
    make_initial_backup/3,
    read_schema/2,
    refresh_cookie/2,
    schema2bup/1,
    tm_fallback_start/1,
    trav_apply/5,
    traverse_backup/4,
    traverse_backup/6,
    uninstall_fallback/0,
    uninstall_fallback/1,
    uninstall_fallback_master/2
]).

% The receiver of Master's fallback, spawned with OTP's #fallback_args{}.
fallback_receiver(Master, Args) ->
    clause_mnesia_backup:fallback_receiver(Master, Args).

create_schema(_) -> erlang:error({not_implemented, create_schema, 1}).
create_schema(_, _) -> erlang:error({not_implemented, create_schema, 2}).
do_traverse_backup(_, _, _, _, _, _, _) -> erlang:error({not_implemented, do_traverse_backup, 7}).
fallback_bup() -> erlang:error({not_implemented, fallback_bup, 0}).
fallback_exists() -> erlang:error({not_implemented, fallback_exists, 0}).
fallback_to_schema() -> erlang:error({not_implemented, fallback_to_schema, 0}).
install_fallback(_) -> erlang:error({not_implemented, install_fallback, 1}).
install_fallback(_, _) -> erlang:error({not_implemented, install_fallback, 2}).
install_fallback_master(_, _) -> erlang:error({not_implemented, install_fallback_master, 2}).
iterate(_, _, _, _) -> erlang:error({not_implemented, iterate, 4}).
local_uninstall_fallback(_, _) -> erlang:error({not_implemented, local_uninstall_fallback, 2}).
lookup_schema(_, _) -> erlang:error({not_implemented, lookup_schema, 2}).
make_initial_backup(_, _, _) -> erlang:error({not_implemented, make_initial_backup, 3}).
read_schema(_, _) -> erlang:error({not_implemented, read_schema, 2}).
refresh_cookie(_, _) -> erlang:error({not_implemented, refresh_cookie, 2}).
schema2bup(_) -> erlang:error({not_implemented, schema2bup, 1}).
tm_fallback_start(_) -> erlang:error({not_implemented, tm_fallback_start, 1}).
trav_apply(_, _, _, _, _) -> erlang:error({not_implemented, trav_apply, 5}).
traverse_backup(_, _, _, _) -> erlang:error({not_implemented, traverse_backup, 4}).
traverse_backup(_, _, _, _, _, _) -> erlang:error({not_implemented, traverse_backup, 6}).
uninstall_fallback() -> erlang:error({not_implemented, uninstall_fallback, 0}).
uninstall_fallback(_) -> erlang:error({not_implemented, uninstall_fallback, 1}).
uninstall_fallback_master(_, _) -> erlang:error({not_implemented, uninstall_fallback_master, 2}).
