%% OTP's mnesia_recover as other nodes call it (plan 21 step MIM1H4,
%% docs/libraries/mnesia-protocol.md#connecting-mim1h4): requests to this node's recovery server, connecting to other
%% nodes, and whether this node logged another's mnesia down. The server registered as mnesia_recover is
%% clause_mnesia_recover. OTP's other internal functions are declined: no other node calls them.
-module(mnesia_recover).
-export([call/1, connect_nodes/1, has_mnesia_down/1]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    allow_garb/0,
    code_change/3,
    disconnect/1,
    dump_decision_tab/0,
    get_master_node_info/0,
    get_master_node_tables/0,
    get_master_nodes/1,
    get_mnesia_downs/0,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    incr_trans_tid_serial/0,
    init/0,
    init/1,
    log_decision/1,
    log_dump_overload/1,
    log_master_nodes/3,
    log_mnesia_down/1,
    log_mnesia_up/1,
    mnesia_down/1,
    next_check_overload/0,
    next_garb/0,
    note_decision/2,
    note_log_decision/2,
    outcome/2,
    start/0,
    still_pending/1,
    sync/0,
    sync_trans_tid_serial/1,
    terminate/2,
    wait_for_decision/2,
    what_happened/3
]).

call(Msg) ->
    clause_mnesia_recover:call(Msg).

% {NewlyConnected, AlreadyConnected}.
connect_nodes(Nodes) ->
    clause_mnesia_recover:connect_nodes(Nodes).

has_mnesia_down(Node) ->
    clause_mnesia_recover:has_mnesia_down(Node).

allow_garb() -> erlang:error({not_implemented, allow_garb, 0}).
code_change(_, _, _) -> erlang:error({not_implemented, code_change, 3}).
disconnect(_) -> erlang:error({not_implemented, disconnect, 1}).
dump_decision_tab() -> erlang:error({not_implemented, dump_decision_tab, 0}).
get_master_node_info() -> erlang:error({not_implemented, get_master_node_info, 0}).
get_master_node_tables() -> erlang:error({not_implemented, get_master_node_tables, 0}).
get_master_nodes(_) -> erlang:error({not_implemented, get_master_nodes, 1}).
get_mnesia_downs() -> erlang:error({not_implemented, get_mnesia_downs, 0}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
incr_trans_tid_serial() -> erlang:error({not_implemented, incr_trans_tid_serial, 0}).
init() -> erlang:error({not_implemented, init, 0}).
init(_) -> erlang:error({not_implemented, init, 1}).
log_decision(_) -> erlang:error({not_implemented, log_decision, 1}).
log_dump_overload(_) -> erlang:error({not_implemented, log_dump_overload, 1}).
log_master_nodes(_, _, _) -> erlang:error({not_implemented, log_master_nodes, 3}).
log_mnesia_down(_) -> erlang:error({not_implemented, log_mnesia_down, 1}).
log_mnesia_up(_) -> erlang:error({not_implemented, log_mnesia_up, 1}).
mnesia_down(_) -> erlang:error({not_implemented, mnesia_down, 1}).
next_check_overload() -> erlang:error({not_implemented, next_check_overload, 0}).
next_garb() -> erlang:error({not_implemented, next_garb, 0}).
note_decision(_, _) -> erlang:error({not_implemented, note_decision, 2}).
note_log_decision(_, _) -> erlang:error({not_implemented, note_log_decision, 2}).
outcome(_, _) -> erlang:error({not_implemented, outcome, 2}).
start() -> erlang:error({not_implemented, start, 0}).
still_pending(_) -> erlang:error({not_implemented, still_pending, 1}).
sync() -> erlang:error({not_implemented, sync, 0}).
sync_trans_tid_serial(_) -> erlang:error({not_implemented, sync_trans_tid_serial, 1}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
wait_for_decision(_, _) -> erlang:error({not_implemented, wait_for_decision, 2}).
what_happened(_, _, _) -> erlang:error({not_implemented, what_happened, 3}).
