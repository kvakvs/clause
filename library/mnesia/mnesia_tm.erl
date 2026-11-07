%% OTP's mnesia_tm as other nodes call it (plan 21 step MIM1H7,
%% docs/libraries/mnesia-protocol.md#remote-locks-mim1h7): a peer's transaction fixes a table here before it walks it,
%% through mnesia_rpc. The server registered as mnesia_tm is clause_mnesia_tm. OTP's other internal functions are
%% declined: no other node calls them.
-module(mnesia_tm).
-export([fixtable/3]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    block_tab/1,
    commit_participant/6,
    dirty/2,
    display_info/2,
    do_snmp/2,
    do_update_op/3,
    get_info/1,
    get_transactions/0,
    get_transactions_count/0,
    info/1,
    init/1,
    mnesia_down/1,
    new_cr_format/1,
    non_transaction/5,
    prepare_checkpoint/1,
    prepare_checkpoint/2,
    prepare_snmp/3,
    put_activity_id/1,
    put_activity_id/2,
    start/0,
    sync/0,
    system_code_change/4,
    system_continue/3,
    system_terminate/4,
    transaction/6,
    unblock_tab/1
]).

% A peer's fix of a table while its transaction walks it, or the fix's end: this node, as OTP's answers. Clause's
% tables need no fix to walk.
fixtable(_Tab, _Lock, _Requester) ->
    node().

block_tab(_) -> erlang:error({not_implemented, block_tab, 1}).
commit_participant(_, _, _, _, _, _) -> erlang:error({not_implemented, commit_participant, 6}).
dirty(_, _) -> erlang:error({not_implemented, dirty, 2}).
display_info(_, _) -> erlang:error({not_implemented, display_info, 2}).
do_snmp(_, _) -> erlang:error({not_implemented, do_snmp, 2}).
do_update_op(_, _, _) -> erlang:error({not_implemented, do_update_op, 3}).
get_info(_) -> erlang:error({not_implemented, get_info, 1}).
get_transactions() -> erlang:error({not_implemented, get_transactions, 0}).
get_transactions_count() -> erlang:error({not_implemented, get_transactions_count, 0}).
info(_) -> erlang:error({not_implemented, info, 1}).
init(_) -> erlang:error({not_implemented, init, 1}).
mnesia_down(_) -> erlang:error({not_implemented, mnesia_down, 1}).
new_cr_format(_) -> erlang:error({not_implemented, new_cr_format, 1}).
non_transaction(_, _, _, _, _) -> erlang:error({not_implemented, non_transaction, 5}).
prepare_checkpoint(_) -> erlang:error({not_implemented, prepare_checkpoint, 1}).
prepare_checkpoint(_, _) -> erlang:error({not_implemented, prepare_checkpoint, 2}).
prepare_snmp(_, _, _) -> erlang:error({not_implemented, prepare_snmp, 3}).
put_activity_id(_) -> erlang:error({not_implemented, put_activity_id, 1}).
put_activity_id(_, _) -> erlang:error({not_implemented, put_activity_id, 2}).
start() -> erlang:error({not_implemented, start, 0}).
sync() -> erlang:error({not_implemented, sync, 0}).
system_code_change(_, _, _, _) -> erlang:error({not_implemented, system_code_change, 4}).
system_continue(_, _, _) -> erlang:error({not_implemented, system_continue, 3}).
system_terminate(_, _, _, _) -> erlang:error({not_implemented, system_terminate, 4}).
transaction(_, _, _, _, _, _) -> erlang:error({not_implemented, transaction, 6}).
unblock_tab(_) -> erlang:error({not_implemented, unblock_tab, 1}).
