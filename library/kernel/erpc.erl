%% Clause's erpc (plan 21 step LIB25, docs/libraries/library.md): calls run in processes spawned with spawn_request/5 on this
%% node, with OTP's results and errors ({erpc, Reason}, {exception, Reason, Stack}, ...). A Clause program is one
%% node: a request to another node gets the spawn reply OTP gets when it cannot connect, noconnection, and casts
%% to it are dropped.
-module(erpc).
-export([
    call/2,
    call/3,
    call/4,
    call/5,
    cast/2,
    cast/4,
    send_request/2,
    send_request/4,
    send_request/6,
    receive_response/1,
    receive_response/2,
    receive_response/3,
    wait_response/1,
    wait_response/2,
    wait_response/3,
    check_response/2,
    check_response/3,
    multicall/2,
    multicall/3,
    multicall/4,
    multicall/5,
    multicast/2,
    multicast/4,
    reqids_new/0,
    reqids_size/1,
    reqids_add/3,
    reqids_to_list/1
]).
-export([execute_call/4, execute_call/3, execute_cast/3, is_arg_error/4, trim_stack/4, call_result/4]).

-include("erpc.types.hrl").

-define(MAX_INT_TIMEOUT, 4294967295).
-define(IS_VALID_TMO_INT(T), (is_integer(T) andalso 0 =< T andalso T =< ?MAX_INT_TIMEOUT)).
-define(IS_CUT_FRAME(F),
    (element(1, F) == ?MODULE andalso (element(2, F) == execute_call orelse element(2, F) == execute_cast))
).

%% Calls.

call(N, Fun) -> call(N, Fun, #{timeout => infinity}).

call(N, Fun, TimeoutOrOptions) when is_function(Fun, 0) -> call(N, erlang, apply, [Fun, []], TimeoutOrOptions);
call(_, _, _) -> error({?MODULE, badarg}).

call(N, M, F, A) -> call(N, M, F, A, #{timeout => infinity}).

%% A local call without a time-out runs in the caller, as OTP optimizes it.
call(N, M, F, A, #{timeout := infinity, always_spawn := false}) when
    N =:= node(), is_atom(M), is_atom(F), is_list(A)
->
    case local_call(M, F, A) of
        {ok, Return} -> Return;
        {Class, Reason} -> erlang:Class(Reason)
    end;
call(N, M, F, A, #{timeout := T, always_spawn := AlwaysSpawn}) when
    is_atom(N), is_atom(M), is_atom(F), is_list(A), is_boolean(AlwaysSpawn)
->
    Timeout = timeout_value(T),
    Res = make_ref(),
    ReqId = request(N, execute_call, [Res, M, F, A], [{reply, error_only}, monitor], spawn_reply),
    receive
        {spawn_reply, ReqId, error, Reason} -> result(spawn_reply, ReqId, Res, Reason);
        {'DOWN', ReqId, process, _, Reason} -> result(down, ReqId, Res, Reason)
    after Timeout -> result(timeout, ReqId, Res, undefined)
    end;
call(_, _, _, _, #{timeout := _, always_spawn := _}) ->
    error({?MODULE, badarg});
call(N, M, F, A, #{} = Options) ->
    call(N, M, F, A, maps:merge(#{timeout => infinity, always_spawn => false}, Options));
call(N, M, F, A, T) ->
    call(N, M, F, A, #{timeout => T, always_spawn => false}).

%% The call in this process, as OTP's local optimization runs it.
local_call(M, F, A) ->
    try execute_call(M, F, A) of
        {return, Return} -> {ok, Return}
    catch
        throw:Thrown -> {throw, Thrown};
        exit:Reason -> {exit, {exception, Reason}};
        error:Reason:Stack -> {error, error_reason(Reason, Stack, M, F, A)}
    end.

error_reason(Reason, Stack, M, F, A) ->
    case is_arg_error(Reason, M, F, A) of
        true -> {?MODULE, Reason};
        false -> {exception, Reason, trim_stack(Stack, M, F, A)}
    end.

%% A spawn request on this node; for another node the reply OTP gets without a connection.
request(N, Function, Args, Options, _) when N =:= node() ->
    spawn_request(?MODULE, Function, Args, Options);
request(N, _, _, _, ReplyTag) when is_atom(N) ->
    ReqId = make_ref(),
    self() ! {ReplyTag, ReqId, error, noconnection},
    ReqId.

%% Requests.

send_request(N, F) when is_atom(N), is_function(F, 0) -> send_request(N, erlang, apply, [F, []]);
send_request(_, _) -> error({?MODULE, badarg}).

send_request(N, M, F, A) when is_atom(N), is_atom(M), is_atom(F), is_list(A) ->
    Res = make_ref(),
    ReqId = request(N, execute_call, [Res, M, F, A], [{reply, error_only}, monitor], spawn_reply),
    [Res | ReqId];
send_request(N, F, L, C) when is_atom(N), is_function(F, 0), is_map(C) ->
    send_request(N, erlang, apply, [F, []], L, C);
send_request(_, _, _, _) ->
    error({?MODULE, badarg}).

send_request(N, M, F, A, L, C) when is_atom(N), is_atom(M), is_atom(F), is_list(A), is_map(C) ->
    Res = make_ref(),
    ReqId = request(N, execute_call, [Res, M, F, A], [{reply, error_only}, monitor], spawn_reply),
    maps:put(ReqId, [Res | L], C);
send_request(_, _, _, _, _, _) ->
    error({?MODULE, badarg}).

receive_response([Res | ReqId] = RId) when is_reference(Res), is_reference(ReqId) -> receive_response(RId, infinity);
receive_response(_) -> error({?MODULE, badarg}).

receive_response([Res | ReqId], Tmo) when is_reference(Res), is_reference(ReqId) ->
    Timeout = timeout_value(Tmo),
    receive
        {spawn_reply, ReqId, error, Reason} -> result(spawn_reply, ReqId, Res, Reason);
        {'DOWN', ReqId, process, _, Reason} -> result(down, ReqId, Res, Reason)
    after Timeout -> result(timeout, ReqId, Res, undefined)
    end;
receive_response(_, _) ->
    error({?MODULE, badarg}).

receive_response(Collection, Tmo, Delete) when map_size(Collection) == 0, is_boolean(Delete) ->
    _ = timeout_value(Tmo),
    no_request;
receive_response(Collection, Tmo, Delete) when is_map(Collection), is_boolean(Delete) ->
    Timeout = timeout_value(Tmo),
    receive
        {spawn_reply, ReqId, error, Reason} when is_map_key(ReqId, Collection), is_reference(ReqId) ->
            collection_result(spawn_reply, ReqId, Reason, Collection, false, Delete);
        {'DOWN', ReqId, process, _, Reason} when is_map_key(ReqId, Collection), is_reference(ReqId) ->
            collection_result(down, ReqId, Reason, Collection, false, Delete)
    after Timeout -> collection_result(timeout, ok, ok, Collection, false, Delete)
    end;
receive_response(_, _, _) ->
    error({?MODULE, badarg}).

wait_response([Res | ReqId] = RId) when is_reference(Res), is_reference(ReqId) -> wait_response(RId, 0);
wait_response(_) -> error({?MODULE, badarg}).

wait_response([Res | ReqId], WaitTime) when is_reference(Res), is_reference(ReqId) ->
    Timeout = timeout_value(WaitTime),
    receive
        {spawn_reply, ReqId, error, Reason} -> result(spawn_reply, ReqId, Res, Reason);
        {'DOWN', ReqId, process, _, Reason} -> {response, result(down, ReqId, Res, Reason)}
    after Timeout -> no_response
    end;
wait_response(_, _) ->
    error({?MODULE, badarg}).

wait_response(Collection, WaitTime, Delete) when map_size(Collection) == 0, is_boolean(Delete) ->
    _ = timeout_value(WaitTime),
    no_request;
wait_response(Collection, WaitTime, Delete) when is_map(Collection), is_boolean(Delete) ->
    Timeout = timeout_value(WaitTime),
    receive
        {spawn_reply, ReqId, error, Reason} when is_map_key(ReqId, Collection), is_reference(ReqId) ->
            collection_result(spawn_reply, ReqId, Reason, Collection, true, Delete);
        {'DOWN', ReqId, process, _, Reason} when is_map_key(ReqId, Collection), is_reference(ReqId) ->
            collection_result(down, ReqId, Reason, Collection, true, Delete)
    after Timeout -> no_response
    end;
wait_response(_, _, _) ->
    error({?MODULE, badarg}).

check_response({spawn_reply, ReqId, error, Reason}, [Res | ReqId]) when is_reference(Res), is_reference(ReqId) ->
    result(spawn_reply, ReqId, Res, Reason);
check_response({'DOWN', ReqId, process, _, Reason}, [Res | ReqId]) when is_reference(Res), is_reference(ReqId) ->
    {response, result(down, ReqId, Res, Reason)};
check_response(_, [Res | ReqId]) when is_reference(Res), is_reference(ReqId) ->
    no_response;
check_response(_, _) ->
    error({?MODULE, badarg}).

check_response(_, Collection, Delete) when map_size(Collection) == 0, is_boolean(Delete) ->
    no_request;
check_response({spawn_reply, ReqId, error, Reason}, Collection, Delete) when
    is_reference(ReqId), is_map_key(ReqId, Collection), is_boolean(Delete)
->
    collection_result(spawn_reply, ReqId, Reason, Collection, true, Delete);
check_response({'DOWN', ReqId, process, _, Reason}, Collection, Delete) when
    is_reference(ReqId), is_map_key(ReqId, Collection), is_boolean(Delete)
->
    collection_result(down, ReqId, Reason, Collection, true, Delete);
check_response(_, Collection, Delete) when is_map(Collection), is_boolean(Delete) ->
    no_response;
check_response(_, _, _) ->
    error({?MODULE, badarg}).

%% Request id collections: #{ReqId => [Res | Label]}.

reqids_new() -> maps:new().

reqids_size(Collection) ->
    try
        maps:size(Collection)
    catch
        _:_ -> error({?MODULE, badarg})
    end.

reqids_add([_ | ReqId], _, Collection) when is_reference(ReqId), is_map_key(ReqId, Collection) ->
    error({?MODULE, badarg});
reqids_add([Res | ReqId], Label, Collection) when is_reference(Res), is_reference(ReqId), is_map(Collection) ->
    maps:put(ReqId, [Res | Label], Collection);
reqids_add(_, _, _) ->
    error({?MODULE, badarg}).

reqids_to_list(Collection) when is_map(Collection) ->
    try
        maps:fold(
            fun
                (ReqId, [Res | Label], Acc) when is_reference(ReqId), is_reference(Res) ->
                    [{[Res | ReqId], Label} | Acc];
                (_, _, _) ->
                    throw(badarg)
            end,
            [],
            Collection
        )
    catch
        throw:badarg -> error({?MODULE, badarg})
    end;
reqids_to_list(_) ->
    error({?MODULE, badarg}).

%% Multicalls: the local node's call runs in the caller after the requests are sent.

multicall(Ns, Fun) -> multicall(Ns, Fun, #{timeout => infinity}).

multicall(Ns, Fun, TimeoutOrOptions) when is_function(Fun, 0) ->
    multicall(Ns, erlang, apply, [Fun, []], TimeoutOrOptions);
multicall(_, _, _) ->
    error({?MODULE, badarg}).

multicall(Ns, M, F, A) -> multicall(Ns, M, F, A, #{timeout => infinity}).

multicall(Ns, M, F, A, #{} = Options) ->
    try
        true = is_atom(M),
        true = is_atom(F),
        true = is_list(A),
        Tag = make_ref(),
        Timeout = timeout_value(maps:get(timeout, Options, infinity)),
        LocalCall =
            case maps:get(always_spawn, Options, false) of
                true -> always_spawn;
                false -> allow_local_call
            end,
        mcall_receive_replies(Tag, mcall_send_requests(Tag, Ns, M, F, A, LocalCall, Timeout))
    catch
        error:Error when Error =/= internal_error -> error({?MODULE, badarg})
    end;
multicall(Ns, M, F, A, T) ->
    multicall(Ns, M, F, A, #{timeout => T}).

mcall_send_requests(Tag, Ns, M, F, A, LocalCall, Timeout) ->
    mcall_send_requests(Tag, Ns, M, F, A, [], deadline(Timeout), LocalCall, 0).

mcall_send_requests(_, [], M, F, A, RIDs, Deadline, local_call, Count) ->
    {ok, RIDs, #{local_call => local_call(M, F, A)}, Count, Deadline};
mcall_send_requests(_, [], _, _, _, RIDs, Deadline, _, Count) ->
    {ok, RIDs, #{}, Count, Deadline};
mcall_send_requests(Tag, [N | Ns], M, F, A, RIDs, infinity, allow_local_call, Count) when N == node() ->
    mcall_send_requests(Tag, Ns, M, F, A, [local_call | RIDs], infinity, local_call, Count);
mcall_send_requests(Tag, [N | Ns], M, F, A, RIDs, Deadline, LocalCall, Count) when is_atom(N) ->
    RID = request(
        N, execute_call, [Tag, M, F, A], [{reply, error_only}, {reply_tag, Tag}, {monitor, [{tag, Tag}]}], Tag
    ),
    mcall_send_requests(Tag, Ns, M, F, A, [RID | RIDs], Deadline, LocalCall, Count + 1);
mcall_send_requests(_, _, _, _, _, RIDs, _, _, Count) ->
    {badarg, RIDs, #{}, Count, expired}.

mcall_receive_replies(Tag, {Sent, RIDs, Replies, Count, Deadline}) ->
    All = mcall_receive_replies(Tag, RIDs, Replies, Count, Deadline),
    case Sent of
        ok -> mcall_map_replies(RIDs, All, []);
        _ -> error(Sent)
    end.

mcall_receive_replies(_, _, Replies, 0, _) ->
    Replies;
mcall_receive_replies(Tag, RIDs, Replies, Count, Deadline) ->
    receive
        {Tag, ReqId, error, Reason} ->
            Reply = mcall_result(spawn_reply, ReqId, Tag, Reason),
            mcall_receive_replies(Tag, RIDs, Replies#{ReqId => Reply}, Count - 1, Deadline);
        {Tag, ReqId, process, _, Reason} ->
            Reply = mcall_result(down, ReqId, Tag, Reason),
            mcall_receive_replies(Tag, RIDs, Replies#{ReqId => Reply}, Count - 1, Deadline)
    after time_left(Deadline) ->
        case RIDs of
            [] -> Replies;
            _ -> mcall_receive_replies(Tag, [], Replies, mcall_abandon(RIDs, Replies, Count), expired)
        end
    end.

mcall_result(Type, ReqId, Tag, Reason) ->
    try
        {ok, result(Type, ReqId, Tag, Reason)}
    catch
        Class:Error -> {Class, Error}
    end.

mcall_abandon([], _, Count) ->
    Count;
mcall_abandon([local_call | RIDs], Replies, Count) ->
    mcall_abandon(RIDs, Replies, Count);
mcall_abandon([RID | RIDs], Replies, Count) ->
    Left =
        case maps:is_key(RID, Replies) orelse not call_abandon(RID) of
            true -> Count;
            false -> Count - 1
        end,
    mcall_abandon(RIDs, Replies, Left).

mcall_map_replies([], _, Results) ->
    Results;
mcall_map_replies([RID | RIDs], Replies, Results) ->
    mcall_map_replies(RIDs, Replies, [maps:get(RID, Replies, {error, {?MODULE, timeout}}) | Results]).

%% Casts.

multicast(N, Fun) -> multicast(N, erlang, apply, [Fun, []]).

multicast(Nodes, M, F, A) ->
    try
        true = is_atom(M),
        true = is_atom(F),
        true = is_list(A),
        lists:foreach(fun(Node) -> cast_request(Node, M, F, A) end, Nodes)
    catch
        error:_ -> error({?MODULE, badarg})
    end.

cast(N, Fun) -> cast(N, erlang, apply, [Fun, []]).

cast(Node, M, F, A) when is_atom(Node), is_atom(M), is_atom(F), is_list(A) ->
    cast_request(Node, M, F, A);
cast(_, _, _, _) ->
    error({?MODULE, badarg}).

cast_request(Node, M, F, A) when Node =:= node() ->
    _ = spawn_request(?MODULE, execute_cast, [M, F, A], [{reply, no}]),
    ok;
cast_request(Node, _, _, _) when is_atom(Node) ->
    ok.

%% What spawned requests run.

execute_call(Ref, M, F, A) ->
    Reply =
        try
            {Ref, return, apply(M, F, A)}
        catch
            throw:Reason ->
                {Ref, throw, Reason};
            exit:Reason ->
                {Ref, exit, Reason};
            error:Reason:Stack ->
                case is_arg_error(Reason, M, F, A) of
                    true -> {Ref, error, {?MODULE, Reason}};
                    false -> {Ref, error, Reason, trim_stack(Stack, M, F, A)}
                end
        end,
    exit(Reply).

execute_call(M, F, A) -> {return, apply(M, F, A)}.

execute_cast(M, F, A) ->
    try
        apply(M, F, A)
    catch
        error:Reason:Stack ->
            case is_arg_error(Reason, M, F, A) of
                true -> error({?MODULE, Reason});
                false -> error({exception, {Reason, trim_stack(Stack, M, F, A)}})
            end
    end.

call_result(Type, ReqId, Res, Reason) -> result(Type, ReqId, Res, Reason).

%% A system_limit from the argument list itself rather than the call.
is_arg_error(system_limit, _, _, A) ->
    try
        apply(?MODULE, nonexisting, A),
        false
    catch
        error:system_limit -> true;
        _:_ -> false
    end;
is_arg_error(_, _, _, _) ->
    false.

%% The stack up to the called function, without erpc's own frames.
trim_stack([Cut | _], M, F, A) when ?IS_CUT_FRAME(Cut) ->
    [{M, F, A, []}];
trim_stack([{M, F, A, _} = Frame, Cut | _], M, F, A) when ?IS_CUT_FRAME(Cut) ->
    [Frame];
trim_stack(Stack, M, F, A) ->
    try
        trim(Stack, M, F, A)
    catch
        throw:use_all -> Stack
    end.

trim([], _, _, _) ->
    throw(use_all);
trim([{M, F, Arity, _} = Frame, Cut | _], M, F, A) when ?IS_CUT_FRAME(Cut), Arity == length(A) ->
    [Frame];
trim([Cut | _], M, F, A) when ?IS_CUT_FRAME(Cut) ->
    [{M, F, length(A), []}];
trim([Frame | Rest], M, F, A) ->
    [Frame | trim(Rest, M, F, A)].

%% Results.

call_abandon(ReqId) ->
    case spawn_request_abandon(ReqId) of
        true -> true;
        false -> erlang:demonitor(ReqId, [info])
    end.

result(down, _, Res, {Res, return, Return}) ->
    Return;
result(down, _, Res, {Res, throw, Throw}) ->
    throw(Throw);
result(down, _, Res, {Res, exit, Exit}) ->
    exit({exception, Exit});
result(down, _, Res, {Res, error, Error, Stack}) ->
    error({exception, Error, Stack});
result(down, _, Res, {Res, error, {?MODULE, _} = Error}) ->
    error(Error);
result(down, _, _, noconnection) ->
    error({?MODULE, noconnection});
result(down, _, _, Reason) ->
    exit({signal, Reason});
result(spawn_reply, _, _, Reason) ->
    error({?MODULE, Reason});
result(timeout, ReqId, Res, _) ->
    case call_abandon(ReqId) of
        true ->
            error({?MODULE, timeout});
        false ->
            receive
                {spawn_reply, ReqId, error, Reason} -> result(spawn_reply, ReqId, Res, Reason);
                {'DOWN', ReqId, process, _, Reason} -> result(down, ReqId, Res, Reason)
            after 0 -> error({?MODULE, badarg})
            end
    end.

collection_result(timeout, _, _, Collection, _, _) ->
    try
        maps:foreach(fun abandon/2, Collection)
    catch
        throw:badarg -> error({?MODULE, badarg})
    end,
    error({?MODULE, timeout});
collection_result(Type, ReqId, Reason, Collection, Wrap, Delete) ->
    Info =
        case Delete of
            true -> maps:take(ReqId, Collection);
            false -> {maps:get(ReqId, Collection), Collection}
        end,
    case Info of
        {[Res | Label], Rest} when is_reference(Res) ->
            try result(Type, ReqId, Res, Reason) of
                Result when Wrap -> {{response, Result}, Label, Rest};
                Result -> {Result, Label, Rest}
            catch
                Class:Error -> erlang:Class({Error, Label, Rest})
            end;
        _ ->
            error({?MODULE, badarg})
    end.

abandon(ReqId, [Res | _]) when is_reference(ReqId), is_reference(Res) ->
    case call_abandon(ReqId) of
        true ->
            ok;
        false ->
            receive
                {spawn_reply, ReqId, error, _} -> ok;
                {'DOWN', ReqId, process, _, _} -> ok
            after 0 -> ok
            end
    end;
abandon(_, _) ->
    throw(badarg).

%% Time-outs.

timeout_value(infinity) ->
    infinity;
timeout_value(Timeout) when ?IS_VALID_TMO_INT(Timeout) ->
    Timeout;
timeout_value({abs, Timeout}) when is_integer(Timeout) ->
    case Timeout - erlang:monotonic_time(millisecond) of
        Left when Left < 0 -> 0;
        Left when Left > ?MAX_INT_TIMEOUT -> error({?MODULE, badarg});
        Left -> Left
    end;
timeout_value(_) ->
    error({?MODULE, badarg}).

deadline(infinity) -> infinity;
deadline(T) -> erlang:monotonic_time() + erlang:convert_time_unit(T, millisecond, native).

time_left(infinity) ->
    infinity;
time_left(expired) ->
    0;
time_left(Deadline) ->
    case Deadline - erlang:monotonic_time() of
        Left when Left =< 0 -> 0;
        Left -> erlang:convert_time_unit(Left - 1, native, millisecond) + 1
    end.
