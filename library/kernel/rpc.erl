%% Clause's rpc (plan 21 step LIB25, docs/libraries/library.md): OTP's remote procedure calls over erpc, with {badrpc, Reason}
%% results. The rex server, which block calls, sbcast/2,3 and server messages need, starts under kernel_sup at the first use. A
%% Clause program is one node, so other nodes are down: calls to them give {badrpc, nodedown} and multicalls list
%% them as bad nodes.
-module(rpc).
-behaviour(gen_server).
-export([
    start/0,
    start_link/0,
    stop/0,
    call/4,
    call/5,
    block_call/4,
    block_call/5,
    server_call/4,
    cast/4,
    abcast/2,
    abcast/3,
    sbcast/2,
    sbcast/3,
    eval_everywhere/3,
    eval_everywhere/4,
    multi_server_call/2,
    multi_server_call/3,
    multicall/3,
    multicall/4,
    multicall/5,
    async_call/4,
    yield/1,
    nb_yield/2,
    nb_yield/1,
    parallel_eval/1,
    pmap/3,
    pinfo/1,
    pinfo/2
]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2, code_change/3]).
-export([proxy_user_flush/0]).

-include("rpc.types.hrl").

-define(NAME, rex).
-define(MAX_INT_TIMEOUT, 4294967295).
-define(IS_VALID_TMO(T), (T == infinity orelse (is_integer(T) andalso 0 =< T andalso T =< ?MAX_INT_TIMEOUT))).

%% An erpc result as rpc gives it.
-define(RPCIFY(Erpc),
    try Erpc of
        {'EXIT', _} = BadRpc -> {badrpc, BadRpc};
        Result -> Result
    catch
        Class:Reason -> rpcify_exception(Class, Reason)
    end
).

%% The rex server.

start() -> gen_server:start({local, ?NAME}, ?MODULE, [], []).

start_link() -> gen_server:start_link({local, ?NAME}, ?MODULE, [], []).

stop() -> gen_server:call(?NAME, stop, infinity).

% The server starts with kernel_sup at the first use of a kernel service (plan 30 step DIST4b1d).
ensure_started() ->
    case whereis(?NAME) of
        undefined -> ok = clause_kernel:ensure_started();
        _ -> ok
    end.

%% The state maps the monitor of each call's process to the caller.
init([]) ->
    process_flag(trap_exit, true),
    {ok, #{}}.

handle_call({call, Mod, Fun, Args, Leader}, To, State) ->
    Call = fun() ->
        set_group_leader(Leader),
        gen_server:reply(To, execute_call(Mod, Fun, Args))
    end,
    {_, Monitor} = spawn_monitor(Call),
    {noreply, State#{Monitor => To}};
handle_call({block_call, Mod, Fun, Args, Leader}, _, State) ->
    Mine = group_leader(),
    set_group_leader(Leader),
    Reply = execute_call(Mod, Fun, Args),
    group_leader(Mine, self()),
    {reply, Reply, State};
handle_call(stop, _, State) ->
    {stop, normal, stopped, State};
handle_call(_, _, State) ->
    {noreply, State}.

handle_cast({cast, Mod, Fun, Args, Leader}, State) ->
    _ = spawn(fun() ->
        set_group_leader(Leader),
        erpc:execute_cast(Mod, Fun, Args)
    end),
    {noreply, State};
handle_cast(_, State) ->
    {noreply, State}.

handle_info({'DOWN', Monitor, process, _, normal}, State) ->
    {noreply, maps:remove(Monitor, State)};
handle_info({'DOWN', Monitor, process, _, Reason}, State) ->
    case maps:take(Monitor, State) of
        {To, Rest} ->
            gen_server:reply(To, {badrpc, {'EXIT', Reason}}),
            {noreply, Rest};
        error ->
            {noreply, State}
    end;
handle_info({From, {sbcast, Name, Message}}, State) ->
    From ! {?NAME, node(), delivered(Name, Message)},
    {noreply, State};
handle_info({From, {send, Name, Message}}, State) ->
    case delivered(Name, {From, Message}) of
        {nonexisting_name, _} = Missing -> From ! {?NAME, node(), Missing};
        _ -> ok
    end,
    {noreply, State};
handle_info({From, features_request}, State) ->
    From ! {features_reply, node(), [erpc]},
    {noreply, State};
handle_info(_, State) ->
    {noreply, State}.

terminate(_, _) -> ok.

code_change(_, State, _) -> {ok, State}.

delivered(Name, Message) ->
    try Name ! Message of
        _ -> node()
    catch
        error:_ -> {nonexisting_name, Name}
    end.

set_group_leader(Leader) when is_pid(Leader) -> group_leader(Leader, self());
set_group_leader(_) -> ok.

%% Kept for OTP's rex_proxy_user, which forwards to user once it starts: Clause's standard I/O needs no user.
proxy_user_flush() ->
    receive
        Message -> user ! Message
    end,
    proxy_user_flush().

execute_call(Mod, Fun, Args) ->
    try erpc:execute_call(Mod, Fun, Args) of
        {return, Return} -> Return
    catch
        throw:Result ->
            Result;
        exit:Reason ->
            {badrpc, {'EXIT', Reason}};
        error:Reason:Stack ->
            case erpc:is_arg_error(Reason, Mod, Fun, Args) of
                true -> {badrpc, {'EXIT', Reason}};
                false -> {badrpc, {'EXIT', {Reason, erpc:trim_stack(Stack, Mod, Fun, Args)}}}
            end
    end.

%% Calls.

call(N, M, F, A) -> call(N, M, F, A, infinity).

call(N, M, F, A, T) -> ?RPCIFY(erpc:call(N, M, F, A, T)).

block_call(N, M, F, A) -> block_call(N, M, F, A, infinity).

block_call(N, M, F, A, Timeout) when is_atom(N), is_atom(M), is_list(A), ?IS_VALID_TMO(Timeout) ->
    server_request(N, {block_call, M, F, A, group_leader()}, Timeout).

%% A call to rex on `Node`: this node's server, or nodedown for another.
server_request(Node, Request, Timeout) when Node =:= node() ->
    ensure_started(),
    try gen_server:call(?NAME, Request, Timeout) of
        Reply -> Reply
    catch
        exit:{timeout, _} -> {badrpc, timeout};
        exit:Reason -> {badrpc, {'EXIT', Reason}}
    end;
server_request(_, _, _) ->
    {badrpc, nodedown}.

rpcify_exception(throw, {'EXIT', _} = BadRpc) -> {badrpc, BadRpc};
rpcify_exception(throw, Return) -> Return;
rpcify_exception(exit, {exception, Exit}) -> {badrpc, {'EXIT', Exit}};
rpcify_exception(exit, {signal, Reason}) -> {badrpc, {'EXIT', Reason}};
rpcify_exception(exit, Reason) -> exit(Reason);
rpcify_exception(error, {exception, Error, Stack}) -> {badrpc, {'EXIT', {Error, Stack}}};
rpcify_exception(error, {erpc, badarg}) -> error(badarg);
rpcify_exception(error, {erpc, noconnection}) -> {badrpc, nodedown};
rpcify_exception(error, {erpc, timeout}) -> {badrpc, timeout};
rpcify_exception(error, {erpc, notsup}) -> {badrpc, notsup};
rpcify_exception(error, {erpc, Error}) -> {badrpc, {'EXIT', Error}};
rpcify_exception(error, Reason) -> error(Reason).

%% Sends {self(), Msg} to Name on Node and waits for {ReplyWrapper, Node, Reply}.
server_call(Node, Name, ReplyWrapper, Message) when is_atom(Node), is_atom(Name) ->
    case Node =:= node() of
        false ->
            {error, nodedown};
        true ->
            Ref = erlang:monitor(process, {Name, Node}),
            {Name, Node} ! {self(), Message},
            receive
                {'DOWN', Ref, _, _, _} ->
                    {error, nodedown};
                {ReplyWrapper, Node, Reply} ->
                    erlang:demonitor(Ref, [flush]),
                    Reply
            end
    end.

cast(Node, Mod, Fun, Args) ->
    try
        ok = erpc:cast(Node, Mod, Fun, Args)
    catch
        error:{erpc, badarg} -> error(badarg)
    end,
    true.

abcast(Name, Message) -> abcast([node()], Name, Message).

abcast([Node | Rest], Name, Message) ->
    try
        erlang:send({Name, Node}, Message)
    catch
        error:_ -> ok
    end,
    abcast(Rest, Name, Message);
abcast([], _, _) ->
    abcast.

sbcast(Name, Message) -> sbcast([node()], Name, Message).

sbcast(Nodes, Name, Message) ->
    ensure_started(),
    receive_nodes(?NAME, send_nodes(Nodes, ?NAME, {sbcast, Name, Message}, [])).

eval_everywhere(Mod, Fun, Args) -> eval_everywhere([node()], Mod, Fun, Args).

eval_everywhere(Nodes, Mod, Fun, Args) ->
    lists:foreach(fun(Node) -> cast(Node, Mod, Fun, Args) end, Nodes),
    abcast.

multi_server_call(Name, Message) -> multi_server_call([node()], Name, Message).

multi_server_call(Nodes, Name, Message) when is_list(Nodes), is_atom(Name) ->
    receive_nodes(Name, send_nodes(Nodes, Name, Message, [])).

send_nodes([Node | Rest], Name, Message, Monitors) when is_atom(Node) ->
    Monitor = start_monitor(Node, Name),
    catch {Name, Node} ! {self(), Message},
    send_nodes(Rest, Name, Message, [Monitor | Monitors]);
send_nodes([_ | Rest], Name, Message, Monitors) ->
    send_nodes(Rest, Name, Message, Monitors);
send_nodes([], _, _, Monitors) ->
    Monitors.

%% Another node is down at once, as on an OTP node that is not alive.
start_monitor(Node, Name) when Node =:= node() ->
    {Node, erlang:monitor(process, {Name, Node})};
start_monitor(Node, Name) ->
    Ref = make_ref(),
    self() ! {'DOWN', Ref, process, {Name, Node}, noconnection},
    {Node, Ref}.

receive_nodes(Name, Nodes) -> receive_nodes(Name, Nodes, [], []).

receive_nodes(_, [], BadNodes, Replies) ->
    {Replies, BadNodes};
receive_nodes(Name, [{N, R} | Rest], BadNodes, Replies) ->
    receive
        {'DOWN', R, _, _, _} ->
            receive_nodes(Name, Rest, [N | BadNodes], Replies);
        {?NAME, N, {nonexisting_name, _}} ->
            erlang:demonitor(R, [flush]),
            receive_nodes(Name, Rest, [N | BadNodes], Replies);
        {Name, N, Reply} ->
            erlang:demonitor(R, [flush]),
            receive_nodes(Name, Rest, BadNodes, [Reply | Replies])
    end.

%% Multicalls.

multicall(M, F, A) -> multicall(M, F, A, infinity).

multicall(Nodes, M, F, A) when is_list(Nodes) -> multicall(Nodes, M, F, A, infinity);
multicall(M, F, A, Timeout) -> multicall([node()], M, F, A, Timeout).

multicall(Nodes, M, F, A, Timeout) ->
    Results =
        try
            erpc:multicall(Nodes, M, F, A, Timeout)
        catch
            error:{erpc, badarg} -> error(badarg)
        end,
    multicall_results(Nodes, Results, [], []).

multicall_results([], [], Ok, Bad) ->
    {lists:reverse(Ok), lists:reverse(Bad)};
multicall_results([_ | Ns], [{ok, {'EXIT', _} = Exit} | Rs], Ok, Bad) ->
    multicall_results(Ns, Rs, [{badrpc, Exit} | Ok], Bad);
multicall_results([_ | Ns], [{ok, Return} | Rs], Ok, Bad) ->
    multicall_results(Ns, Rs, [Return | Ok], Bad);
multicall_results([N | Ns], [{error, {erpc, Reason}} | Rs], Ok, Bad) when Reason == timeout; Reason == noconnection ->
    multicall_results(Ns, Rs, Ok, [N | Bad]);
multicall_results([_ | Ns], [{Class, Reason} | Rs], Ok, Bad) ->
    multicall_results(Ns, Rs, [rpcify_exception(Class, Reason) | Ok], Bad).

%% Asynchronous calls and parallel evaluation.

async_call(Node, Mod, Fun, Args) ->
    try
        erpc:send_request(Node, Mod, Fun, Args)
    catch
        error:{erpc, badarg} -> error(badarg)
    end.

yield(Key) -> ?RPCIFY(erpc:receive_response(Key)).

nb_yield(Key) -> nb_yield(Key, 0).

nb_yield(Key, Timeout) ->
    try erpc:wait_response(Key, Timeout) of
        no_response -> timeout;
        {response, {'EXIT', _} = BadRpc} -> {value, {badrpc, BadRpc}};
        {response, Result} -> {value, Result}
    catch
        Class:Reason -> {value, rpcify_exception(Class, Reason)}
    end.

parallel_eval(Calls) -> [yield(async_call(node(), M, F, A)) || {M, F, A} <- Calls].

%% Evaluated from the end of the list, as OTP does; check/2 restores the order.
pmap({M, F}, Extra, List) when is_atom(M), is_atom(F) ->
    check(parallel_eval([{M, F, [Arg | Extra]} || Arg <- lists:reverse(List)]), []).

check([{badrpc, _} | _], _) -> exit(badrpc);
check([X | Rest], Acc) -> check(Rest, [X | Acc]);
check([], Acc) -> Acc.

pinfo(Pid) when node(Pid) =:= node() -> process_info(Pid);
pinfo(Pid) -> call(node(Pid), erlang, process_info, [Pid]).

pinfo(Pid, Item) when node(Pid) =:= node() -> process_info(Pid, Item);
pinfo(Pid, Item) -> block_call(node(Pid), erlang, process_info, [Pid, Item]).
