%% Clause's peer (plan 21, docs/libraries/library.md#runtime-module-replacements): a Clause program cannot start
%% another node (no distribution and no erl to run), so starting checks OTP's options and then raises not_alive as
%% a non-distributed OTP node does, or notsup for an alternative connection. Names are made as OTP's, and the
%% requests to a peer's controlling process keep OTP's shapes. The peer server itself is declined.
-module(peer).
-export([
    call/4,
    call/5,
    cast/4,
    get_state/1,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    init/1,
    init_supervision/2,
    random_name/0,
    random_name/1,
    send/3,
    start/0,
    start/1,
    start_link/0,
    start_link/1,
    start_supervision/0,
    stop/1,
    supervision_child_spec/0,
    system_code_change/4,
    system_continue/3,
    system_get_state/1,
    system_replace_state/2,
    system_terminate/4,
    terminate/2
]).
-include("peer.types.hrl").

call(Dest, M, F, A) -> call(Dest, M, F, A, 5000).
call(Dest, M, F, A, Timeout) ->
    case gen_server:call(Dest, {call, M, F, A}, Timeout) of
        {ok, Reply} -> Reply;
        {Class, {Reason, Stack}} -> erlang:raise(Class, Reason, Stack);
        {error, Reason} -> erlang:error(Reason)
    end.
cast(Dest, M, F, A) -> gen_server:cast(Dest, {cast, M, F, A}).
get_state(Dest) -> gen_server:call(Dest, get_state).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
init(_) -> erlang:error({not_implemented, init, 1}).
init_supervision(_, _) -> erlang:error({not_implemented, init_supervision, 2}).
random_name() -> random_name("peer").
% A node name unique for this host: the prefix, a unique integer and the OS process id.
random_name(Prefix) -> lists:concat([Prefix, "-", erlang:unique_integer([positive]), "-", os:getpid()]).
send(Dest, To, Message) -> gen_server:cast(Dest, {send, To, Message}).
start() -> erlang:error({not_implemented, start, 0}).
start(Options) -> start_peer(Options).
start_link() -> start_link(#{name => random_name()}).
start_link(Options) -> start_peer(Options).

% OTP's option checks, then the answer of a node that cannot start peers.
start_peer(Options) ->
    Args = maps:get(args, Options, []),
    is_list(Args) orelse erlang:error({invalid_arg, Args}),
    [erlang:error({invalid_arg, Arg}) || Arg <- Args, not io_lib:char_list(Arg)],
    is_map_key(connection, Options) orelse (is_map_key(name, Options) andalso erlang:is_alive()) orelse
        erlang:error(not_alive),
    check_exec(maps:find(exec, Options)),
    erlang:error(notsup).

check_exec({ok, {Exec, Strings}}) ->
    io_lib:char_list(Exec) orelse erlang:error({exec, Exec}),
    [erlang:error({exec, S}) || S <- Strings, not io_lib:char_list(S)],
    ok;
check_exec({ok, Exec}) when is_list(Exec) ->
    io_lib:char_list(Exec) orelse erlang:error({exec, Exec}),
    ok;
check_exec(error) ->
    ok;
check_exec({ok, Exec}) ->
    erlang:error({exec, Exec}).
start_supervision() -> erlang:error({not_implemented, start_supervision, 0}).
stop(Dest) -> gen_server:stop(Dest).
supervision_child_spec() -> erlang:error({not_implemented, supervision_child_spec, 0}).
system_code_change(_, _, _, _) -> erlang:error({not_implemented, system_code_change, 4}).
system_continue(_, _, _) -> erlang:error({not_implemented, system_continue, 3}).
system_get_state(_) -> erlang:error({not_implemented, system_get_state, 1}).
system_replace_state(_, _) -> erlang:error({not_implemented, system_replace_state, 2}).
system_terminate(_, _, _, _) -> erlang:error({not_implemented, system_terminate, 4}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
