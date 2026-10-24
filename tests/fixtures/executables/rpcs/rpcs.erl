%% rpc, erpc, net_adm, net_kernel, auth and seq_trace (plan 21 step LIB25) on a node that is not distributed: calls
%% to this node run, other nodes are down, results and errors take OTP's shapes, and the network modules give
%% OTP's answers for a node that is not alive.
-module(rpcs).
-export([main/1]).
-export([fail/1, slow/1, echo/1, server/0]).
-compile(nowarn_deprecated_function).

-define(OTHER, 'other@host').

main(_) ->
    rpc_calls(),
    rpc_spread(),
    erpc_calls(),
    erpc_requests(),
    network(),
    tokens(),
    io:format("rpcs: ok~n").

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

attempt(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, strip(Reason)}
    end.

%% Exceptions without their stacks, whose frames differ between OTP and Clause.
strip({exception, Reason, Stack}) when is_list(Stack) -> {exception, Reason, stack};
strip({badrpc, {'EXIT', {Reason, Stack}}}) when is_list(Stack) -> {badrpc, {'EXIT', {Reason, stack}}};
strip({Reason, Stack}) when is_list(Stack), Stack =/= [], is_tuple(hd(Stack)) -> {Reason, stack};
strip(Other) -> Other.

fail(error) -> error(boom);
fail(exit) -> exit(bye);
fail(throw) -> throw(ball);
fail(exit_tuple) -> {'EXIT', returned}.

slow(Time) ->
    receive
    after Time -> slept
    end.

echo(Value) -> Value.

%% A registered process answering rpc:server_call/4 and rpc:multi_server_call/2,3.
server() ->
    receive
        {From, {ask, Question}} ->
            From ! {rpcs_server, node(), {answer, Question}},
            server();
        {From, quiet} ->
            From ! {other_wrapper, node(), ignored},
            server();
        stop ->
            ok
    end.

rpc_calls() ->
    show("call", [rpc:call(node(), lists, reverse, [[1, 2]]), rpc:call(?OTHER, lists, reverse, [[1, 2]])]),
    show("call_errors", [
        strip(rpc:call(node(), ?MODULE, fail, [error])),
        rpc:call(node(), ?MODULE, fail, [exit]),
        rpc:call(node(), ?MODULE, fail, [throw]),
        rpc:call(node(), ?MODULE, fail, [exit_tuple]),
        strip(rpc:call(node(), no_such_module, f, []))
    ]),
    show("timeouts", [rpc:call(node(), ?MODULE, slow, [300], 50), rpc:call(node(), ?MODULE, slow, [0], 1000)]),
    show("block_call", [
        rpc:block_call(node(), lists, sum, [[1, 2]]),
        rpc:block_call(node(), ?MODULE, fail, [exit]),
        rpc:block_call(node(), ?MODULE, slow, [0], 1000),
        rpc:block_call(?OTHER, lists, sum, [[1, 2]])
    ]),
    show("cast", [rpc:cast(node(), ?MODULE, echo, [x]), rpc:cast(?OTHER, ?MODULE, echo, [x])]),
    show("async", [
        rpc:yield(rpc:async_call(node(), lists, sum, [[4]])),
        rpc:yield(rpc:async_call(?OTHER, lists, sum, [[4]])),
        rpc:nb_yield(rpc:async_call(node(), ?MODULE, slow, [300]), 10),
        rpc:nb_yield(rpc:async_call(node(), lists, sum, [[5]]), 1000),
        rpc:nb_yield(rpc:async_call(?OTHER, lists, sum, [[5]]), 100),
        strip(element(2, rpc:nb_yield(rpc:async_call(node(), ?MODULE, fail, [error]), 1000)))
    ]),
    show("pinfo", [rpc:pinfo(self(), status), rpc:pinfo(self()) =:= process_info(self())]),
    show("call_badarg", attempt(fun() -> rpc:call("node", lists, sum, [[1]]) end)).

rpc_spread() ->
    show("multicall", [
        rpc:multicall([node(), ?OTHER], lists, sum, [[1, 2]]),
        rpc:multicall(lists, sum, [[3]]),
        rpc:multicall([node()], ?MODULE, fail, [exit]),
        rpc:multicall([node()], ?MODULE, slow, [300], 50)
    ]),
    show("parallel", [
        rpc:parallel_eval([{lists, sum, [[1]]}, {?MODULE, echo, [two]}]),
        rpc:pmap({lists, sum}, [], [[1], [2, 3]]),
        rpc:pmap({?MODULE, echo}, [], [a, b, c])
    ]),
    show("everywhere", [rpc:eval_everywhere(?MODULE, echo, [x]), rpc:eval_everywhere([?OTHER], ?MODULE, echo, [x])]),
    register(rpcs_server, spawn(?MODULE, server, [])),
    register(rpcs_inbox, self()),
    show("abcast", [rpc:abcast(rpcs_inbox, hello), rpc:abcast([node(), ?OTHER], nobody, hello)]),
    show("sbcast", [rpc:sbcast(rpcs_inbox, hi), rpc:sbcast([node(), ?OTHER], nobody, hi)]),
    show("inbox", [receive_all()]),
    show("server_call", [
        rpc:server_call(node(), rpcs_server, rpcs_server, {ask, q}),
        rpc:server_call(?OTHER, rpcs_server, rpcs_server, {ask, q}),
        rpc:server_call(node(), nobody, rpcs_server, {ask, q})
    ]),
    show("multi_server_call", [
        rpc:multi_server_call(rpcs_server, {ask, all}),
        rpc:multi_server_call([node(), ?OTHER], rpcs_server, {ask, two}),
        rpc:multi_server_call(nobody, hi)
    ]),
    rpcs_server ! stop,
    unregister(rpcs_inbox).

receive_all() ->
    receive
        Message -> [Message | receive_all()]
    after 50 -> []
    end.

erpc_calls() ->
    show("erpc_call", [
        erpc:call(node(), lists, reverse, [[1, 2]]),
        erpc:call(node(), fun() -> fun_result end),
        erpc:call(node(), fun() -> spawned end, 1000),
        erpc:call(node(), lists, sum, [[1]], #{always_spawn => true})
    ]),
    show("erpc_errors", [
        attempt(fun() -> erpc:call(?OTHER, lists, sum, [[1]]) end),
        attempt(fun() -> erpc:call(node(), ?MODULE, fail, [error]) end),
        attempt(fun() -> erpc:call(node(), ?MODULE, fail, [exit]) end),
        attempt(fun() -> erpc:call(node(), ?MODULE, fail, [throw]) end),
        attempt(fun() -> erpc:call(node(), ?MODULE, fail, [error], 1000) end),
        attempt(fun() -> erpc:call(node(), ?MODULE, fail, [exit], 1000) end),
        attempt(fun() -> erpc:call(node(), ?MODULE, slow, [300], 50) end),
        attempt(fun() -> erpc:call(node(), no_such_module, f, []) end),
        attempt(fun() -> erpc:call(node(), not_a_fun) end),
        attempt(fun() -> erpc:call("node", lists, sum, [[1]]) end),
        attempt(fun() -> erpc:call(node(), lists, sum, [[1]], -1) end)
    ]),
    show("erpc_multicall", [
        [strip_result(R) || R <- erpc:multicall([node(), ?OTHER, node()], ?MODULE, fail, [error])],
        erpc:multicall([node(), ?OTHER], lists, sum, [[1]]),
        erpc:multicall([node()], fun() -> from_fun end),
        erpc:multicall([node()], ?MODULE, slow, [300], 50),
        erpc:multicall([node()], ?MODULE, fail, [throw], #{always_spawn => true}),
        attempt(fun() -> erpc:multicall([node()], "m", f, []) end)
    ]),
    show("erpc_cast", [
        erpc:cast(node(), ?MODULE, echo, [x]),
        erpc:cast(?OTHER, fun() -> x end),
        erpc:multicast([node(), ?OTHER], ?MODULE, echo, [x]),
        erpc:multicast([node()], fun() -> x end),
        attempt(fun() -> erpc:cast(node(), "m", f, []) end)
    ]).

strip_result({Class, Reason}) -> {Class, strip(Reason)}.

erpc_requests() ->
    R1 = erpc:send_request(node(), lists, sum, [[1, 2]]),
    show("receive_response", erpc:receive_response(R1)),
    R2 = erpc:send_request(node(), ?MODULE, slow, [100]),
    show("wait_response", [erpc:wait_response(R2), erpc:wait_response(R2, 1000)]),
    R3 = erpc:send_request(node(), fun() -> checked end),
    Message =
        receive
            {'DOWN', _, process, _, _} = Down -> Down
        end,
    show("check_response", [erpc:check_response(Message, R3), erpc:check_response(other, R3)]),
    R4 = erpc:send_request(?OTHER, lists, sum, [[1]]),
    show("other_node", attempt(fun() -> erpc:receive_response(R4, 1000) end)),
    R5 = erpc:send_request(node(), ?MODULE, slow, [300]),
    show("response_timeout", attempt(fun() -> erpc:receive_response(R5, 20) end)),
    C0 = erpc:reqids_new(),
    C1 = erpc:send_request(node(), lists, sum, [[1]], one, C0),
    C2 = erpc:send_request(node(), fun() -> two end, two, C1),
    C3 = erpc:reqids_add(erpc:send_request(node(), ?MODULE, echo, [three]), three, C2),
    show("collection", [erpc:reqids_size(C3), lists:sort([L || {_, L} <- erpc:reqids_to_list(C3)])]),
    {First, Second, Third} = collect3(C3),
    show("collected", lists:sort([First, Second, Third])),
    show("empty", [
        erpc:receive_response(C0, 10, true),
        erpc:wait_response(C0, 0, true),
        erpc:check_response(x, C0, true)
    ]),
    show("collection_errors", [
        attempt(fun() -> erpc:reqids_add(R1, again, erpc:reqids_add(R1, once, C0)) end),
        attempt(fun() -> erpc:reqids_size(not_a_map) end),
        attempt(fun() -> erpc:reqids_to_list(#{a => b}) end),
        attempt(fun() -> erpc:receive_response(not_a_request) end)
    ]),
    C4 = erpc:send_request(node(), ?MODULE, slow, [300], late, C0),
    show("collection_timeout", attempt(fun() -> erpc:receive_response(C4, 20, true) end)),
    C5 = erpc:send_request(?OTHER, lists, sum, [[1]], far, C0),
    show("collection_error", attempt(fun() -> erpc:receive_response(C5, 1000, true) end)).

%% The three responses in any order, each with its label.
collect3(C) ->
    {A, LA, C1} = erpc:receive_response(C, 1000, true),
    {{response, B}, LB, C2} = erpc:wait_response(C1, 1000, true),
    {Last, LC, C3} = take_last(C2),
    0 = erpc:reqids_size(C3),
    {{LA, A}, {LB, B}, {LC, Last}}.

take_last(C) ->
    receive
        Message ->
            case erpc:check_response(Message, C, true) of
                {{response, Result}, Label, Rest} -> {Result, Label, Rest};
                no_response -> take_last(C)
            end
    end.

network() ->
    show("net_adm", [
        net_adm:ping(?OTHER),
        net_adm:ping(node()),
        net_adm:names(),
        net_adm:names("localhost"),
        net_adm:localhost() =:= element(2, inet:gethostname()),
        net_adm:world_list([localhost]),
        net_adm:dns_hostname("no.such.host.invalid")
    ]),
    show("net_kernel", [
        net_kernel:get_state(),
        net_kernel:monitor_nodes(true),
        net_kernel:monitor_nodes(true, [nodedown_reason]),
        net_kernel:monitor_nodes(true, [bad]),
        net_kernel:monitor_nodes(perhaps),
        net_kernel:monitor_nodes(false),
        net_kernel:get_net_ticktime(),
        net_kernel:set_net_ticktime(10),
        net_kernel:connect_node(?OTHER),
        net_kernel:allow([?OTHER]),
        net_kernel:allowed(),
        net_kernel:getopts(?OTHER, [nodelay]),
        net_kernel:setopts(new, [{nodelay, true}]),
        net_kernel:stop()
    ]),
    show("auth", [
        auth:is_auth(?OTHER),
        auth:cookie(),
        attempt(fun() -> auth:cookie(monster) end),
        attempt(fun() -> auth:node_cookie(?OTHER, monster) end),
        attempt(fun() -> auth:node_cookie([?OTHER, monster]) end)
    ]),
    show("nodes", [node(), nodes(), is_alive()]).

tokens() ->
    show("empty_token", [seq_trace:get_token(), seq_trace:get_token(label), seq_trace:get_token(send)]),
    show("set", [seq_trace:set_token(label, 3), seq_trace:set_token(send, true), seq_trace:set_token(print, true)]),
    show("components", [seq_trace:get_token(label), seq_trace:get_token(send), seq_trace:get_token('receive')]),
    show("print", [seq_trace:print(hello), seq_trace:print(3, hello), seq_trace:get_system_tracer()]),
    show("clear", [element(2, seq_trace:set_token([])), seq_trace:get_token()]),
    show("reset", [seq_trace:reset_trace(), seq_trace:get_token()]),
    show("bad_token", attempt(fun() -> seq_trace:set_token(bogus, 1) end)).
