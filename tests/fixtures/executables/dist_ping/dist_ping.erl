%% Messages across nodes (plan 30 step DIST4b3a). Node a sends node b's echo server messages to {Name, Node}, to its
%% pid and to an alias, a large term and a long run of messages; b answers each. Node a also controls a connection to
%% a node that does not exist (setnode/3 with its own process): the packets its sends queue, the connection's
%% statistics, the data it puts and the unreadable data that kills it. Process monitors and pings (plan 30 step
%% DIST4b3b1): the monitor signals on that connection, then pings of b, monitors of b's processes and calls to b's
%% server.
-module(dist_ping).
-export([main/1]).

% The flags Clause offers: OTP's own add the atom cache, whose packets this test does not read.
-define(FLAGS, 16#D075F5FBC).
-define(FAKE, 'fake@nohost').

main(["a"]) ->
    ok = net_kernel:monitor_nodes(true, #{node_type => hidden, nodedown_reason => true}),
    queued(),
    puts(),
    fake_monitors(),
    faults(),
    B = peer("dpb"),
    wait_registered("dpb_ready"),
    pings(B),
    monitors(B),
    messages(B),
    done();
main(["b"]) ->
    register(echo, self()),
    _Ready = mark("dpb_ready"),
    echo(0).

%% The echo server: a reply to a pid, a message through an alias, a gen call, processes to monitor, and a stop. A
%% program's end does not wait for its messages to other nodes, so b answers the stop and ends once a's node is gone.
echo(Count) ->
    receive
        {'$gen_call', From, Request} ->
            gen:reply(From, {echoed, Request}),
            echo(Count + 1);
        {spawn, Name, Pid} ->
            Child = spawn(fun child/0),
            Name =:= none orelse register(Name, Child),
            Pid ! {spawned, Child},
            echo(Count + 1);
        {dead, Pid} ->
            {Dead, Ref} = spawn_monitor(fun() -> ok end),
            receive
                {'DOWN', Ref, _, _, _} -> Pid ! {dead, Dead}
            end,
            echo(Count + 1);
        {Pid, Message} when is_pid(Pid) ->
            Pid ! {reply, Message, self()},
            echo(Count + 1);
        {alias, Alias, Message} ->
            Alias ! {via_alias, Message},
            echo(Count + 1);
        {alias_late, Alias, Pid} ->
            Alias ! late,
            Pid ! done,
            echo(Count + 1);
        {stop, Pid} ->
            show("echoed", Count),
            true = monitor_node(node(Pid), true),
            Pid ! stopped,
            receive
                {nodedown, _} -> done()
            end
    end.

% A process of b that a's processes monitor: it tells who monitors it, and ends with the reason it is given.
child() ->
    receive
        {monitored_by, Pid} ->
            {monitored_by, Watchers} = process_info(self(), monitored_by),
            Pid ! {monitored_by, Watchers},
            child();
        {exit, Reason} ->
            exit(Reason)
    end.

%% Pings of b: a wrong cookie fails the handshake (b logs it), then b answers; a node that is not there does not.
pings(B) ->
    true = erlang:set_cookie(B, wrong),
    show("wrong cookie", [net_adm:ping(B), nodes()]),
    true = erlang:set_cookie(B, erlang:get_cookie()),
    show("ping", [net_adm:ping(B), nodes() =:= [B], net_adm:ping(B)]),
    show("no node", net_adm:ping('nobody@localhost')).

%% Monitors of b's processes: by pid and by name, with a tag, ended by demonitor/2 or the watcher's end, of a process
%% that is gone; gen calls to b's server.
monitors(B) ->
    Child = spawned(none, B),
    Ref = monitor(process, Child),
    Named = spawned(named_child, B),
    NamedRef = monitor(process, {named_child, B}),
    {monitors, Monitors} = process_info(self(), monitors),
    show("monitors", lists:sort(Monitors) =:= lists:sort([{process, Child}, {process, {named_child, B}}])),
    Child ! {monitored_by, self()},
    show("monitored by", next() =:= {monitored_by, [self()]}),
    Child ! {exit, {shutdown, by_pid}},
    show("by pid", down(Ref, Child)),
    Named ! {exit, by_name},
    show("by name", down(NamedRef, {named_child, B})),
    Tagged = spawned(none, B),
    TagRef = monitor(process, Tagged, [{tag, gone}]),
    Tagged ! {exit, tagged},
    show(
        "tag",
        receive
            {gone, TagRef, process, Tagged, Tag} -> Tag
        after 30000 -> timeout
        end
    ),
    Quiet = spawned(none, B),
    QuietRef = monitor(process, Quiet),
    show("demonitor", [demonitor(QuietRef, [info]), demonitor(QuietRef, [info])]),
    Quiet ! {monitored_by, self()},
    show("after demonitor", next()),
    Quiet ! {exit, quiet},
    Lone = spawned(none, B),
    {_, WatcherRef} = spawn_monitor(fun() -> monitor(process, Lone) end),
    receive
        {'DOWN', WatcherRef, _, _, _} -> ok
    end,
    Lone ! {monitored_by, self()},
    show("watcher ended", next()),
    Lone ! {exit, lone},
    {echo, B} ! {dead, self()},
    {dead, Dead} = next(),
    show("noproc", [down(monitor(process, Dead), Dead), down(monitor(process, {nobody, B}), {nobody, B})]),
    show("calls", [
        gen:call({echo, B}, '$gen_call', hello),
        gen:call({echo, B}, '$gen_call', hello, 5000),
        exited(fun() -> gen:call({nobody, B}, '$gen_call', hello) end),
        exited(fun() -> gen:call(Dead, '$gen_call', hello) end)
    ]),
    show("left", [process_info(self(), monitors), flush()]).

% Fun's result, or {exit, Reason}.
exited(Fun) ->
    try
        Fun()
    catch
        exit:Reason -> {exit, Reason}
    end.

% A new process of b, registered as Name unless none.
spawned(Name, B) ->
    {echo, B} ! {spawn, Name, self()},
    {spawned, Child} = next(),
    Child.

% The reason of the 'DOWN' of Ref for Item.
down(Ref, Item) ->
    receive
        {'DOWN', Ref, process, Item, Reason} -> Reason
    after 30000 -> timeout
    end.

%% Messages between the two nodes.
messages(B) ->
    {echo, B} ! {self(), hello},
    {reply, hello, Echo} = next(),
    show("reg_send", [is_pid(Echo), node(Echo) =:= B]),
    Echo ! {self(), to_pid},
    show("send_sender", element(2, next())),
    Alias = alias(),
    {echo, B} ! {alias, Alias, to_alias},
    show("alias_send", next()),
    unalias(Alias),
    {echo, B} ! {alias_late, Alias, self()},
    show("after unalias", next()),
    Big = {
        lists:seq(1, 1000),
        binary:copy(<<"x">>, 100000),
        #{a => 1.5, b => [self()], c => make_ref()},
        1 bsl 100,
        -7,
        fun lists:reverse/1,
        <<1:3>>,
        "text",
        'atom ü',
        {}
    },
    Echo ! {self(), Big},
    show("round trip", next() =:= {reply, Big, Echo}),
    [Echo ! {self(), N} || N <- lists:seq(1, 100)],
    show("in order", [N || _ <- lists:seq(1, 100), {reply, N, _} <- [next()]] =:= lists:seq(1, 100)),
    show("send options", [
        erlang:send(Echo, {self(), option}, [noconnect]),
        erlang:send_nosuspend({echo, B}, {self(), nosuspend}, [noconnect]),
        element(2, next()),
        element(2, next())
    ]),
    {nobody, B} ! lost,
    {echo, B} ! {stop, self()},
    show("stop", next()).

%% Sends to a connection's node, queued while it is pending and once it is up, and what the controller takes.
queued() ->
    controlled(fun(DH0) ->
        {x, ?FAKE} ! before_up,
        remote_pid() ! early,
        {ok, 0, 0, Pending} = erlang:dist_get_stat(DH0),
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        show("setnode", [DH =:= DH0, nodes(hidden)]),
        [Before, Early] = take(DH),
        show("queued while pending", [decoded(Before), decoded(Early), Pending - byte_size(Before) - byte_size(Early)]),
        {x, ?FAKE} ! hello,
        {ok, 0, 2, Queued} = erlang:dist_get_stat(DH),
        show("notification", [erlang:dist_ctrl_get_data_notification(DH), next()]),
        [Hello] = take(DH),
        show("queued", [decoded(Hello), Queued - byte_size(Hello), erlang:dist_get_stat(DH)]),
        remote_pid() ! to_pid,
        remote_alias() ! to_alias,
        show("send options", [
            erlang:send(remote_pid(), option, [noconnect]),
            erlang:send({x, nobody@nohost}, lost, [noconnect]),
            erlang:send_nosuspend(remote_pid(), nosuspend, [noconnect]),
            erlang:send_nosuspend({x, nobody@nohost}, lost, [noconnect])
        ]),
        show("packets", [decoded(Packet) || Packet <- take(DH)]),
        show("taken", erlang:dist_get_stat(DH))
    end).

%% Data the controller puts: the messages are delivered, ticks and data of another connection id do nothing.
puts() ->
    controlled(fun(DH0) ->
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        register(receiver, self()),
        Self = self(),
        Alias = alias(),
        Remote = remote_pid(),
        Puts = [
            [112, term_to_binary({22, Remote, Self}), term_to_binary(send_sender)],
            [112, term_to_binary({6, Remote, '', receiver}), term_to_binary(reg_send)],
            <<>>,
            [112, term_to_binary({2, '', Self}), term_to_binary(send)],
            [112, term_to_binary({12, '', Self, []}), term_to_binary(send_tt)],
            [112, term_to_binary({16, Remote, '', receiver, []}), term_to_binary(reg_send_tt)],
            [112, term_to_binary({23, Remote, Self, []}), term_to_binary(send_sender_tt)],
            [112, term_to_binary({33, Remote, Alias}), term_to_binary(alias_send)],
            [112, term_to_binary({34, Remote, Alias, []}), term_to_binary(alias_send_tt)],
            [112, term_to_binary({6, Remote, '', nobody}), term_to_binary(to_nobody)],
            [112, term_to_binary({22, Remote, Remote}), term_to_binary(to_remote)],
            [112, term_to_binary({33, Remote, make_ref()}), term_to_binary(to_no_alias)],
            [112, [term_to_binary({22, Self, Self})], [term_to_binary({from_self, [1, 2.5]}), <<1, 2>>]],
            iolist_to_binary([112, term_to_binary({22, Remote, Self}), term_to_binary(<<"binary">>)])
        ],
        show("puts", [erlang:dist_ctrl_put_data(DH, Put) || Put <- Puts]),
        show("other id", erlang:dist_ctrl_put_data({element(1, DH) + 1, element(2, DH)}, <<112>>)),
        show("old handle", DH0 =:= DH),
        show("delivered", flush()),
        show("stat", erlang:dist_get_stat(DH))
    end).

%% Process monitors on a connection the test controls. Monitors of the fake node's processes, made while the
%% connection is pending and once it is up, are MONITOR_P packets, demonitor/2 sends DEMONITOR_P, and its
%% MONITOR_P_EXIT gives their 'DOWN'. Its MONITOR_P of processes here, by pid and by name, are answered by
%% MONITOR_P_EXIT (the payload form, as the flags have EXIT_PAYLOAD) at once for no process, or once they end; its
%% DEMONITOR_P ends one. The connection's end gives the monitors left 'DOWN' with noconnection.
fake_monitors() ->
    Main = self(),
    controlled(fun(_) ->
        Pending = monitor(process, remote_pid()),
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        ByName = monitor(process, {x, ?FAKE}),
        Gone = monitor(process, remote_pid(), [{tag, gone}]),
        show("demonitor", demonitor(Gone, [info])),
        show("monitor packets", [decoded(Packet) || Packet <- take(DH)]),
        Self = self(),
        Downs = [
            [112, term_to_binary({28, remote_pid(), Self, Pending}), term_to_binary(pending_down)],
            [112, term_to_binary({21, x, Self, ByName, by_name_down})],
            [112, term_to_binary({28, remote_pid(), Self, Gone}), term_to_binary(after_demonitor)],
            [112, term_to_binary({28, remote_pid(), Self, make_ref()}), term_to_binary(unknown)]
        ],
        [ok = erlang:dist_ctrl_put_data(DH, Down) || Down <- Downs],
        show("downs", named(flush())),
        Helper = spawn(fun() ->
            receive
                {exit, R} -> exit(R)
            end
        end),
        Named = spawn(fun() ->
            receive
                {exit, R} -> exit(R)
            end
        end),
        Third = spawn(fun() ->
            receive
                {exit, R} -> exit(R)
            end
        end),
        register(watched, Named),
        Watches = [
            [112, term_to_binary({19, remote_pid(), Helper, remote_ref(1)})],
            [112, term_to_binary({19, remote_pid(), watched, remote_ref(2)})],
            [112, term_to_binary({19, remote_pid(), nobody, remote_ref(3)})],
            [112, term_to_binary({19, remote_pid(), Third, remote_ref(4)})]
        ],
        [ok = erlang:dist_ctrl_put_data(DH, Watch) || Watch <- Watches],
        show("watched", [named(process_info(P, monitored_by)) || P <- [Helper, Named, Third]]),
        ok = erlang:dist_ctrl_put_data(DH, [112, term_to_binary({20, remote_pid(), Third, remote_ref(4)})]),
        show("unwatched", named(process_info(Third, monitored_by))),
        NoProc = taken(DH, 1),
        Helper ! {exit, helper_done},
        HelperExit = taken(DH, 1),
        Named ! {exit, named_done},
        NamedExit = taken(DH, 1),
        show("monitor exits", [decoded(Packet) || Packet <- NoProc ++ HelperExit ++ NamedExit]),
        spawn(fun() ->
            Ref = monitor(process, {y, ?FAKE}),
            Self ! monitoring,
            receive
                {'DOWN', Ref, _, _, _} = Down -> Main ! {watcher, named(Down)}
            end
        end),
        receive
            monitoring -> ok
        end
    end),
    show(
        "noconnection",
        receive
            {watcher, Down} -> Down
        after 5000 -> timeout
        end
    ).

%% Data the controller cannot read kills it, after OTP's report (on standard error). OTP decodes a message's payload
%% when its receiver, here the controller, takes messages: it waits for one.
faults() ->
    Faults = [
        {"incompatible", fun(_) -> <<1, 2, 3>> end},
        {"short", fun(_) -> <<112, 131>> end},
        {"invalid", fun(_) -> [112, term_to_binary({99, "abc", <<"bin">>, 1.5, x})] end},
        {"wrong arity", fun(_) -> [112, term_to_binary({22, x}), term_to_binary(m)] end},
        {"bad receiver", fun(_) -> [112, term_to_binary({33, x, {}}), term_to_binary(m)] end},
        {"bad watcher", fun(Self) -> [112, term_to_binary({19, Self, x, make_ref()})] end},
        {"corrupted", fun(Self) -> [112, term_to_binary({2, '', Self}), <<131, 109, 0, 0, 0, 9, 1, 2>>] end},
        {"no payload", fun(Self) -> [112, term_to_binary({2, '', Self})] end}
    ],
    [
        controlled(Label, fun(_) ->
            DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
            ok = erlang:dist_ctrl_put_data(DH, Data(self())),
            receive
                Message -> show(Label, {received, Message})
            after 5000 -> show(Label, still_up)
            end
        end)
     || {Label, Data} <- Faults
    ].

% Run Fun in a new process as the controller of a new pending connection to the fake node; show how it ended and
% the node status messages of its connection.
controlled(Fun) -> controlled("ended", Fun).

controlled(Label, Fun) ->
    {_, Ref} = spawn_monitor(fun() -> Fun(erts_internal:new_connection(?FAKE)) end),
    Reason =
        receive
            {'DOWN', Ref, _, _, Down} -> Down
        end,
    show(Label, [Reason | statuses()]).

% The node status messages of the fake node: nodeup, then nodedown once its connection is down.
statuses() ->
    receive
        {nodeup, ?FAKE, _} = Up ->
            receive
                {nodedown, ?FAKE, _} = Down -> [Up, Down]
            after 5000 -> [Up]
            end
    after 0 -> []
    end.

take(DH) ->
    case erlang:dist_ctrl_get_data(DH) of
        none -> [];
        Data -> [iolist_to_binary(Data) | take(DH)]
    end.

% The next N packets, waiting for each.
taken(_, 0) ->
    [];
taken(DH, N) ->
    case erlang:dist_ctrl_get_data(DH) of
        none ->
            ok = erlang:dist_ctrl_get_data_notification(DH),
            receive
                dist_data -> taken(DH, N)
            after 5000 -> []
            end;
        Data ->
            [iolist_to_binary(Data) | taken(DH, N - 1)]
    end.

% A packet's control message and payload, if any, with this process's pid and the identities of other processes and
% nodes by name.
decoded(<<112, Rest/binary>>) ->
    {Control, Used} = binary_to_term(Rest, [used]),
    case Rest of
        <<_:Used/binary>> ->
            [named(Control)];
        <<_:Used/binary, Payload/binary>> ->
            [named(Control), binary_to_term(Payload)]
    end.

named(Term) when is_tuple(Term) -> list_to_tuple(named(tuple_to_list(Term)));
named(Term) when is_list(Term) -> [named(Element) || Element <- Term];
named(Term) when Term =:= self() -> self;
named(Term) when is_pid(Term); is_reference(Term) -> {identity, node(Term)};
named(Term) -> Term.

% A pid and an alias of the fake node, as its creation 77 makes them.
remote_pid() -> binary_to_term(<<131, 88, 119, 11, "fake@nohost", 5:32, 0:32, 77:32>>).
remote_alias() -> binary_to_term(<<131, 90, 3:16, 119, 11, "fake@nohost", 77:32, 1:32, 2:32, 3:32>>).
remote_ref(N) -> binary_to_term(<<131, 90, 3:16, 119, 11, "fake@nohost", 77:32, N:32, 0:32, 0:32>>).

flush() ->
    receive
        Message -> [Message | flush()]
    after 100 -> []
    end.

% The name of the other node once it registered with epmd: test nodes are named @localhost, as they listen on loopback
% only.
peer(Name) ->
    wait_registered(Name),
    list_to_atom(Name ++ "@localhost").

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

% The next message, timeout when none comes.
next() ->
    receive
        Message -> Message
    after 30000 -> timeout
    end.

done() -> io:format("dist_ping: ok~n").

show(Label, Term) -> io:format("~s: ~p~n", [Label, Term]).
