%% Two nodes' mnesia connecting (plan 21 step MIM1H4, docs/libraries/mnesia-protocol.md#connecting-mim1h4): node a
%% negotiates a protocol with node b's monitor and connects to b's mnesia_recover, which exchange their transaction
%% serials; b refuses an unknown protocol and answers a decision query; then b stops, starts again and is connected
%% again, and a network partition between both running nodes is detected on both when they meet again. Each step shows
%% both nodes' system events, protocol and recovery variables, links between monitors and mnesia_down records. Only
%% exported functions travel, so either node may be an OTP node.
-module(mnesia_connect).
-export([main/1]).
-export([start/0, stop/0, listen/0, heard/0, state/1, next_tid/0, finish/0]).

main(["a"]) ->
    B = 'mrcb@localhost',
    wait_registered("mrcb_ready"),
    pong = net_adm:ping(B),
    ok = start(),
    ok = rpc:call(B, ?MODULE, start, []),
    show("before", both(B)),
    show("negotiate", mnesia_monitor:negotiate_protocol([B, 'nobody@localhost'])),
    show("negotiated", both(B)),
    show("connect", mnesia_recover:connect_nodes([B])),
    show("connected", both(B)),
    show("connect_again", mnesia_recover:connect_nodes([B])),
    show("reject", reject(B)),
    show("what_decision", what_decision(B)),
    show("b_stop", rpc:call(B, ?MODULE, stop, [])),
    show("down", wait_event({mnesia_down, B})),
    show("after_stop", both(B)),
    show("negotiate_stopped", mnesia_monitor:negotiate_protocol([B])),
    show("b_start", rpc:call(B, ?MODULE, start, [])),
    show("reconnect", mnesia_recover:connect_nodes([B])),
    show("reconnected", both(B)),
    true = erlang:disconnect_node(B),
    show("partition", wait_event({mnesia_down, B})),
    timer:sleep(500),
    pong = net_adm:ping(B),
    show("rejoin", wait_event({inconsistent_database, running_partitioned_network, B})),
    timer:sleep(500),
    show("rejoined", both(B)),
    show("finished", rpc:call(B, ?MODULE, finish, [])),
    io:format("mnesia_connect: ok~n");
main(["b"]) ->
    _Ready = mark("mrcb_ready"),
    receive
    after infinity -> ok
    end.

%% mnesia started with a process that keeps its system events.
start() ->
    ok = mnesia:start(),
    listen().

stop() ->
    mnesia:stop().

listen() ->
    Self = self(),
    Pid = spawn(fun() -> listening(Self) end),
    receive
        {Pid, subscribed} -> ok
    end.

listening(Parent) ->
    is_pid(whereis(mrc_events)) andalso unregister(mrc_events),
    register(mrc_events, self()),
    {ok, _} = mnesia:subscribe(system),
    Parent ! {self(), subscribed},
    events([]).

events(Events) ->
    receive
        {mnesia_system_event, Event} ->
            events([Event | Events]);
        {From, heard} ->
            From ! {heard, lists:reverse(Events)},
            events([])
    end.

%% The system events since the last look.
heard() ->
    mrc_events ! {self(), heard},
    receive
        {heard, Events} -> Events
    after 1000 -> no_listener
    end.

wait_event(Event) ->
    wait_event(Event, 50).

wait_event(Event, 0) ->
    {missing, Event, heard()};
wait_event(Event, N) ->
    Events = heard(),
    case lists:member(Event, Events) of
        true ->
            Events;
        false ->
            timer:sleep(100),
            [mrc_events ! {mnesia_system_event, E} || E <- Events],
            wait_event(Event, N - 1)
    end.

%% Both nodes' state, each with its events since the last look.
both(B) ->
    [{a, state(B)}, {b, rpc:call(B, ?MODULE, state, [node()])}].

%% What this node knows of the other: system events, protocol variables, recovery nodes, monitor links, mnesia_down
%% records and the next transaction's number; only the events while its mnesia is stopped.
state(Other) ->
    Events = heard(),
    case mnesia:system_info(is_running) of
        yes -> [{events, Events} | running_state(Other)];
        Status -> [{events, Events}, {is_running, Status}]
    end.

running_state(Other) ->
    [
        {protocol_version, gvar(protocol_version)},
        {protocol, gvar({protocol, Other})},
        {recover_nodes, gvar(recover_nodes)},
        {db_nodes, gvar({current, db_nodes})},
        {links, monitor_links()},
        {downs, lists:sort([{mnesia_down, N} || {mnesia_down, {node, N}, _, _} <- ets:tab2list(mnesia_decision)])},
        {has_down, mnesia_recover:has_mnesia_down(Other)},
        {remote_down, mnesia_monitor:has_remote_mnesia_down(Other)},
        {next_tid, next_tid()}
    ].

gvar(Name) ->
    try
        ets:lookup_element(mnesia_gvar, Name, 2)
    catch
        error:badarg -> missing
    end.

monitor_links() ->
    {links, Links} = process_info(whereis(mnesia_monitor), links),
    lists:sort([node(P) || P <- Links, node(P) =/= node()]).

next_tid() ->
    {atomic, {_, {tid, N, _}, _}} = mnesia:transaction(fun mnesia:get_activity_id/0),
    N.

%% b's monitor refuses a protocol it does not know.
reject(B) ->
    {B, {reject, Mon, Version, Protocol}} =
        gen_server:call({mnesia_monitor, B}, {negotiate_protocol, self(), "1.0", [{7, 0}]}),
    {B, {reject, node(Mon), Version, Protocol}}.

%% b's recovery server answers a question about a transaction it never saw, and takes the transaction's serial.
what_decision(B) ->
    Before = rpc:call(B, ?MODULE, next_tid, []),
    Tid = {tid, Before + 100, self()},
    gen_server:cast({mnesia_recover, B}, {what_decision, node(), {decision, Tid, unclear, [], [node()]}}),
    timer:sleep(300),
    rpc:call(B, ?MODULE, next_tid, []) - Before.

%% b's end, which a sees as {badrpc, nodedown}.
finish() ->
    io:format(user, "mnesia_connect: ok~n", []),
    erlang:halt().

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

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
