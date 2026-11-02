%% rpc, erpc and gen across nodes (plan 30 steps DIST7a and DIST7b, docs/distribution.md#remote-calls): a calls b, a
%% node no one runs and itself through rpc and erpc, b's rex serves block calls, server calls and broadcasts, and a
%% calls b's gen_server, gen_statem and gen_event servers. b ends through an rpc call of halt, which a sees as
%% {badrpc, nodedown}. Only exported functions travel, so b may be an OTP node.
-module(dist_rpc).
-export([main/1]).
-export([fail/1, slow/1, tell/2, where/1, finish/0, server/0]).
%% The callbacks of the gen servers, without -behaviour attributes since gen_server's and gen_event's share init/1.
-export([init/1, handle_call/3, handle_cast/2, callback_mode/0, handle_event/4, handle_event/2, handle_call/2]).

-define(NOBODY, 'nobody@localhost').

main(["a"]) ->
    B = 'drpb@localhost',
    wait_registered("drpb_ready"),
    show("ping", net_adm:ping(B)),
    rpc_calls(B),
    rpc_spread(B),
    rpc_servers(B),
    erpc_calls(B),
    erpc_requests(B),
    gen_calls(B),
    gen_spread(B),
    gen_others(B),
    show("halted", rpc:call(B, ?MODULE, finish, [])),
    done();
main(["b"]) ->
    register(drp_server, spawn(?MODULE, server, [])),
    {ok, _} = gen_server:start({local, drp_gs}, ?MODULE, gs, []),
    {ok, _} = gen_statem:start({local, drp_sm}, ?MODULE, sm, []),
    {ok, _} = gen_event:start({local, drp_ev}),
    ok = gen_event:add_handler(drp_ev, ?MODULE, ev),
    _Ready = mark("drpb_ready"),
    receive
    after infinity -> ok
    end.

fail(error) -> error(boom);
fail(exit) -> exit(bye);
fail(throw) -> throw(ball);
fail(exit_tuple) -> {'EXIT', returned}.

slow(Time) ->
    receive
    after Time -> slept
    end.

tell(To, What) ->
    To ! {told, node(), What},
    ok.

where(X) -> {X, node()}.

%% b's end: its standard output is its own user's, whatever the caller's group leader is.
finish() ->
    io:format(user, "dist_rpc: ok~n", []),
    erlang:halt().

%% A registered process answering rpc:server_call/4 and rpc:multi_server_call/2,3, and forwarding broadcasts.
server() ->
    receive
        {From, {ask, Question}} ->
            From ! {drp_server, node(), {answer, Question}},
            server();
        {forward, To, What} ->
            To ! {forwarded, node(), What},
            server()
    end.

init(gs) -> {ok, gs};
init(sm) -> {ok, idle, 0};
init(ev) -> {ok, 0}.

handle_call(ping, _, State) -> {reply, {pong, node()}, State};
handle_call({sleep, Time}, _, State) -> {reply, slow(Time), State}.

handle_cast({forward, To}, State) ->
    To ! {cast, node()},
    {noreply, State}.

callback_mode() -> handle_event_function.

handle_event({call, From}, get, State, Count) -> {keep_state, Count + 1, [{reply, From, {State, Count, node()}}]};
handle_event({call, From}, {go, Next}, _, Count) -> {next_state, Next, Count, [{reply, From, ok}]};
handle_event({call, From}, {sleep, Time}, _, _) -> {keep_state_and_data, [{reply, From, slow(Time)}]}.

handle_event(_, Count) -> {ok, Count + 1}.

handle_call(count, Count) -> {ok, {events, Count, node()}, Count}.

rpc_calls(B) ->
    show("call", [
        rpc:call(B, erlang, node, []), rpc:call(B, lists, reverse, [[1, 2]]), rpc:call(?NOBODY, erlang, node, [])
    ]),
    show("call_errors", [
        strip(rpc:call(B, ?MODULE, fail, [error])),
        rpc:call(B, ?MODULE, fail, [exit]),
        rpc:call(B, ?MODULE, fail, [throw]),
        rpc:call(B, ?MODULE, fail, [exit_tuple]),
        strip(rpc:call(B, no_such_module, f, []))
    ]),
    show("timeouts", [rpc:call(B, ?MODULE, slow, [300], 50), rpc:call(B, ?MODULE, slow, [0], 5000)]),
    show("block_call", [
        rpc:block_call(B, erlang, node, []),
        rpc:block_call(B, ?MODULE, fail, [exit]),
        rpc:block_call(B, ?MODULE, slow, [300], 50),
        rpc:block_call(?NOBODY, erlang, node, [])
    ]),
    show("cast", [rpc:cast(B, ?MODULE, tell, [self(), cast]), rpc:cast(?NOBODY, ?MODULE, tell, [self(), lost])]),
    show("cast_told", receive_one()),
    Slow = rpc:async_call(B, ?MODULE, slow, [300]),
    show("async", [
        rpc:yield(rpc:async_call(B, erlang, node, [])),
        rpc:yield(rpc:async_call(?NOBODY, erlang, node, [])),
        rpc:nb_yield(Slow, 10),
        rpc:yield(Slow),
        rpc:nb_yield(rpc:async_call(B, lists, sum, [[5]]), 5000)
    ]),
    show("late_replies", receive_all()).

rpc_spread(B) ->
    show("multicall", [
        rpc:multicall(erlang, node, []),
        rpc:multicall([node(), B, ?NOBODY], erlang, node, []),
        rpc:multicall([B], ?MODULE, fail, [exit]),
        rpc:multicall([B], ?MODULE, slow, [300], 50)
    ]),
    show("parallel", [
        rpc:parallel_eval([{erlang, node, []}, {erlang, node, []}, {erlang, node, []}]),
        rpc:pmap({?MODULE, where}, [], [1, 2, 3])
    ]),
    Self = self(),
    show("everywhere", rpc:eval_everywhere([B, ?NOBODY], ?MODULE, tell, [Self, everywhere])),
    show("everywhere_told", receive_one()),
    show("abcast", rpc:abcast([B, ?NOBODY], drp_server, {forward, Self, abcast})),
    show("abcast_forwarded", receive_one()),
    show("sbcast", [
        rpc:sbcast([B, ?NOBODY], drp_server, {forward, Self, sbcast}),
        rpc:sbcast([B], nobody, hi)
    ]),
    show("sbcast_forwarded", receive_one()).

rpc_servers(B) ->
    register(drp_server, spawn(?MODULE, server, [])),
    show("server_call", [
        rpc:server_call(B, drp_server, drp_server, {ask, q}),
        rpc:server_call(?NOBODY, drp_server, drp_server, {ask, q}),
        rpc:server_call(B, nobody, drp_server, {ask, q})
    ]),
    show("multi_server_call", [
        rpc:multi_server_call(drp_server, {ask, all}),
        rpc:multi_server_call([B, ?NOBODY], drp_server, {ask, two}),
        rpc:multi_server_call([B], nobody, hi)
    ]),
    show("pinfo", rpc:pinfo(rpc:call(B, erlang, whereis, [drp_server]), registered_name)).

erpc_calls(B) ->
    show("erpc_call", [
        erpc:call(B, erlang, node, []),
        erpc:call(B, fun erlang:node/0),
        erpc:call(B, fun erlang:node/0, 5000),
        erpc:call(node(), erlang, node, [], #{always_spawn => true})
    ]),
    show("erpc_errors", [
        attempt(fun() -> erpc:call(?NOBODY, erlang, node, []) end),
        attempt(fun() -> erpc:call(B, ?MODULE, fail, [error]) end),
        attempt(fun() -> erpc:call(B, ?MODULE, fail, [exit]) end),
        attempt(fun() -> erpc:call(B, ?MODULE, fail, [throw]) end),
        attempt(fun() -> erpc:call(B, ?MODULE, slow, [300], 50) end),
        attempt(fun() -> erpc:call(B, no_such_module, f, []) end)
    ]),
    show("erpc_multicall", [
        [strip_result(R) || R <- erpc:multicall([node(), B, ?NOBODY], ?MODULE, fail, [error])],
        erpc:multicall([node(), B, ?NOBODY], erlang, node, []),
        erpc:multicall([B], ?MODULE, slow, [300], 50)
    ]),
    Self = self(),
    show("erpc_cast", [
        erpc:cast(B, ?MODULE, tell, [Self, erpc_cast]),
        erpc:cast(?NOBODY, ?MODULE, tell, [Self, lost]),
        erpc:multicast([B, ?NOBODY], ?MODULE, tell, [Self, multicast])
    ]),
    show("erpc_told", lists:sort([receive_one(), receive_one()])).

erpc_requests(B) ->
    R1 = erpc:send_request(B, lists, sum, [[1, 2]]),
    show("receive_response", erpc:receive_response(R1)),
    R2 = erpc:send_request(B, ?MODULE, slow, [100]),
    show("wait_response", [erpc:wait_response(R2), erpc:wait_response(R2, 5000)]),
    R3 = erpc:send_request(?NOBODY, erlang, node, []),
    show("other_node", attempt(fun() -> erpc:receive_response(R3, 5000) end)),
    R4 = erpc:send_request(B, ?MODULE, slow, [300]),
    show("response_timeout", attempt(fun() -> erpc:receive_response(R4, 20) end)),
    C0 = erpc:reqids_new(),
    C1 = erpc:send_request(B, erlang, node, [], one, C0),
    C2 = erpc:send_request(node(), erlang, node, [], two, C1),
    C3 = erpc:send_request(?NOBODY, erlang, node, [], three, C2),
    show("collected", lists:sort([collect(C3), collect(C3), collect(C3)])),
    show("late_responses", receive_all()).

gen_calls(B) ->
    Pid = rpc:call(B, erlang, whereis, [drp_gs]),
    show("gen_call", [
        gen_server:call({drp_gs, B}, ping),
        gen_server:call(Pid, ping),
        gen_server:call({drp_gs, B}, {sleep, 0}, 5000),
        gen_server:receive_response(gen_server:send_request({drp_gs, B}, ping), 5000)
    ]),
    show("gen_call_errors", [
        attempt(fun() -> gen_server:call({drp_gs, B}, {sleep, 300}, 50) end),
        attempt(fun() -> gen_server:call({nobody, B}, ping) end),
        attempt(fun() -> gen_server:call({drp_gs, ?NOBODY}, ping) end),
        attempt(fun() -> gen_server:call({drp_gs, ?NOBODY}, ping, 5000) end),
        gen_server:receive_response(gen_server:send_request({drp_gs, ?NOBODY}, ping), 5000)
    ]),
    show("gen_late_replies", receive_all()).

gen_spread(B) ->
    {ok, _} = gen_server:start({local, drp_gs}, ?MODULE, gs, []),
    show("multi_call", [
        gen_server:multi_call(drp_gs, ping),
        gen_server:multi_call([B, ?NOBODY], drp_gs, ping),
        gen_server:multi_call([B], nobody, ping),
        gen_server:multi_call([B], drp_gs, {sleep, 300}, 50)
    ]),
    Self = self(),
    show("cast", gen_server:cast({drp_gs, B}, {forward, Self})),
    show("cast_forwarded", receive_one()),
    show("abcast", [
        gen_server:abcast(drp_gs, {forward, Self}),
        gen_server:abcast([B, ?NOBODY], drp_gs, {forward, Self})
    ]),
    show("abcast_forwarded", lists:sort([receive_one(), receive_one(), receive_one()])),
    show("gen_late_replies", receive_all()).

gen_others(B) ->
    show("gen_statem", [
        gen_statem:call({drp_sm, B}, get),
        gen_statem:call({drp_sm, B}, {go, busy}),
        gen_statem:call({drp_sm, B}, get, 5000),
        attempt(fun() -> gen_statem:call({drp_sm, B}, {sleep, 300}, 50) end),
        attempt(fun() -> gen_statem:call({drp_sm, ?NOBODY}, get) end)
    ]),
    show("gen_event", [
        gen_event:notify({drp_ev, B}, one),
        gen_event:sync_notify({drp_ev, B}, two),
        gen_event:call({drp_ev, B}, ?MODULE, count),
        gen_event:which_handlers({drp_ev, B}),
        attempt(fun() -> gen_event:call({drp_ev, ?NOBODY}, ?MODULE, count) end)
    ]),
    show("gen_late_replies", receive_all()).

%% One response of the collection, its label first.
collect(Collection) ->
    try erpc:receive_response(Collection, 5000, false) of
        {Result, Label, _} -> {Label, Result}
    catch
        error:{Reason, Label, _} -> {Label, Reason}
    end.

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

attempt(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, strip(Reason)}
    end.

strip_result({Class, Reason}) -> {Class, strip(Reason)}.

%% Exceptions without their stacks, whose frames differ between OTP and Clause.
strip({exception, Reason, Stack}) when is_list(Stack) -> {exception, Reason, stack};
strip({badrpc, {'EXIT', {Reason, Stack}}}) when is_list(Stack) -> {badrpc, {'EXIT', {Reason, stack}}};
strip(Other) -> Other.

receive_one() ->
    receive
        Message -> Message
    after 5000 -> none
    end.

%% Messages that arrive within 400 ms: late replies the calls should have dropped.
receive_all() ->
    receive
        Message -> [Message | receive_all()]
    after 400 -> []
    end.

wait_registered(Name) ->
    {ok, Names} = net_adm:names(localhost),
    case lists:keymember(Name, 1, Names) of
        true ->
            ok;
        false ->
            timer:sleep(10),
            wait_registered(Name)
    end.

% Register the epmd name Name while the returned socket stays open: this node is ready.
mark(Name) ->
    Port = list_to_integer(os:getenv("ERL_EPMD_PORT", "4369")),
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, Port, [binary, {active, false}]),
    Bytes = list_to_binary(Name),
    Request = <<120, 1:16, 77, 0, 6:16, 5:16, (byte_size(Bytes)):16, Bytes/binary, 0:16>>,
    ok = gen_tcp:send(Socket, [<<(byte_size(Request)):16>>, Request]),
    {ok, <<_, 0, _/binary>>} = gen_tcp:recv(Socket, 0, 5000),
    Socket.

done() -> io:format("dist_rpc: ok~n").
