%% Messages across nodes (plan 30 step DIST4b3a). Node a sends node b's echo server messages to {Name, Node}, to its
%% pid and to an alias, a large term and a long run of messages; b answers each. Node a also controls a connection to
%% a node that does not exist (setnode/3 with its own process): the packets its sends queue, the connection's
%% statistics, the data it puts and the unreadable data that kills it.
-module(dist_ping).
-export([main/1]).

% The flags Clause offers: OTP's own add the atom cache, whose packets this test does not read.
-define(FLAGS, 16#D075F5FBC).
-define(FAKE, 'fake@nohost').

main(["a"]) ->
    ok = net_kernel:monitor_nodes(true, #{node_type => hidden, nodedown_reason => true}),
    queued(),
    puts(),
    faults(),
    B = peer("dpb"),
    wait_registered("dpb_ready"),
    messages(B),
    done();
main(["b"]) ->
    register(echo, self()),
    _Ready = mark("dpb_ready"),
    echo(0).

%% The echo server: a reply to a pid, a message through an alias, and a stop. A program's end does not wait for its
%% messages to other nodes, so b answers the stop and ends once a's node is gone.
echo(Count) ->
    receive
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

%% Messages between the two nodes; the first send connects them.
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

%% Data the controller cannot read kills it, after OTP's report (on standard error). OTP decodes a message's payload
%% when its receiver, here the controller, takes messages: it waits for one.
faults() ->
    Faults = [
        {"incompatible", fun(_) -> <<1, 2, 3>> end},
        {"short", fun(_) -> <<112, 131>> end},
        {"invalid", fun(_) -> [112, term_to_binary({99, "abc", <<"bin">>, 1.5, x})] end},
        {"wrong arity", fun(_) -> [112, term_to_binary({22, x}), term_to_binary(m)] end},
        {"bad receiver", fun(_) -> [112, term_to_binary({33, x, {}}), term_to_binary(m)] end},
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

% A packet's control message and payload, with this process's pid and the fake node's identities by name.
decoded(<<112, Rest/binary>>) ->
    {Control, Used} = binary_to_term(Rest, [used]),
    <<_:Used/binary, Payload/binary>> = Rest,
    [named(Control), binary_to_term(Payload)].

named(Term) when is_tuple(Term) -> list_to_tuple(named(tuple_to_list(Term)));
named(Term) when is_list(Term) -> [named(Element) || Element <- Term];
named(Term) when Term =:= self() -> self;
named(Term) when is_pid(Term); is_reference(Term) -> {identity, node(Term)};
named(Term) -> Term.

% A pid and an alias of the fake node, as its creation 77 makes them.
remote_pid() -> binary_to_term(<<131, 88, 119, 11, "fake@nohost", 5:32, 0:32, 77:32>>).
remote_alias() -> binary_to_term(<<131, 90, 3:16, 119, 11, "fake@nohost", 77:32, 1:32, 2:32, 3:32>>).

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
