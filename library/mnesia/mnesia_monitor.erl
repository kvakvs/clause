%% OTP's mnesia_monitor as other nodes call it (plan 21 step MIM1H4,
%% docs/libraries/mnesia-protocol.md#connecting-mim1h4): requests to this node's monitor, as a peer negotiating a
%% protocol makes them, the negotiation with other nodes, and whether this node logged another's mnesia down. The
%% server registered as mnesia_monitor is clause_mnesia_monitor. OTP's other internal functions are declined: no other
%% node calls them.
-module(mnesia_monitor).
-export([call/1, negotiate_protocol/1, has_remote_mnesia_down/1]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    cast/1,
    close_dets/1,
    close_log/1,
    code_change/3,
    detect_inconcistency/2,
    detect_partitioned_network/2,
    disconnect/1,
    do_check_type/2,
    get_env/1,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    init/0,
    init/1,
    mktab/2,
    mnesia_down/2,
    needs_protocol_conversion/1,
    negotiate_protocol_impl/2,
    open_dets/2,
    open_log/1,
    patch_env/2,
    protocol_version/0,
    reopen_log/3,
    set_env/2,
    start/0,
    start_proc/4,
    sync_log/1,
    terminate/2,
    terminate_proc/3,
    unsafe_close_dets/1,
    unsafe_close_log/1,
    unsafe_create_external/4,
    unsafe_mktab/2,
    unsafe_open_dets/2,
    use_dir/0
]).

% A request to this node's monitor; {error, {node_not_running, node()}} when it is not running or ends meanwhile.
call(Msg) ->
    case whereis(mnesia_monitor) of
        undefined ->
            {error, {node_not_running, node()}};
        Pid ->
            try
                gen_server:call(Pid, Msg, infinity)
            catch
                exit:_ -> {error, {node_not_running, node()}}
            end
    end.

% The nodes of Nodes that agreed on a protocol with this one.
negotiate_protocol(Nodes) ->
    clause_mnesia_monitor:negotiate_protocol(Nodes).

% {true, node()} when this node logged Node's mnesia down and the schema has no master nodes, else {false, node()}.
has_remote_mnesia_down(Node) ->
    clause_mnesia_connect:has_remote_mnesia_down(Node).

cast(_) -> erlang:error({not_implemented, cast, 1}).
close_dets(_) -> erlang:error({not_implemented, close_dets, 1}).
close_log(_) -> erlang:error({not_implemented, close_log, 1}).
code_change(_, _, _) -> erlang:error({not_implemented, code_change, 3}).
detect_inconcistency(_, _) -> erlang:error({not_implemented, detect_inconcistency, 2}).
detect_partitioned_network(_, _) -> erlang:error({not_implemented, detect_partitioned_network, 2}).
disconnect(_) -> erlang:error({not_implemented, disconnect, 1}).
do_check_type(_, _) -> erlang:error({not_implemented, do_check_type, 2}).
get_env(_) -> erlang:error({not_implemented, get_env, 1}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
init() -> erlang:error({not_implemented, init, 0}).
init(_) -> erlang:error({not_implemented, init, 1}).
mktab(_, _) -> erlang:error({not_implemented, mktab, 2}).
mnesia_down(_, _) -> erlang:error({not_implemented, mnesia_down, 2}).
needs_protocol_conversion(_) -> erlang:error({not_implemented, needs_protocol_conversion, 1}).
negotiate_protocol_impl(_, _) -> erlang:error({not_implemented, negotiate_protocol_impl, 2}).
open_dets(_, _) -> erlang:error({not_implemented, open_dets, 2}).
open_log(_) -> erlang:error({not_implemented, open_log, 1}).
patch_env(_, _) -> erlang:error({not_implemented, patch_env, 2}).
protocol_version() -> erlang:error({not_implemented, protocol_version, 0}).
reopen_log(_, _, _) -> erlang:error({not_implemented, reopen_log, 3}).
set_env(_, _) -> erlang:error({not_implemented, set_env, 2}).
start() -> erlang:error({not_implemented, start, 0}).
start_proc(_, _, _, _) -> erlang:error({not_implemented, start_proc, 4}).
sync_log(_) -> erlang:error({not_implemented, sync_log, 1}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
terminate_proc(_, _, _) -> erlang:error({not_implemented, terminate_proc, 3}).
unsafe_close_dets(_) -> erlang:error({not_implemented, unsafe_close_dets, 1}).
unsafe_close_log(_) -> erlang:error({not_implemented, unsafe_close_log, 1}).
unsafe_create_external(_, _, _, _) -> erlang:error({not_implemented, unsafe_create_external, 4}).
unsafe_mktab(_, _) -> erlang:error({not_implemented, unsafe_mktab, 2}).
unsafe_open_dets(_, _) -> erlang:error({not_implemented, unsafe_open_dets, 2}).
use_dir() -> erlang:error({not_implemented, use_dir, 0}).
