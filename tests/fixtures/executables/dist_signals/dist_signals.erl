%% Links and exit signals across nodes (plan 30 step DIST5a). Node a controls connections to a node that does not
%% exist (setnode/3 with its own process): the LINK, UNLINK_ID and EXIT2 packets of its links and exits, the fake
%% node's links, unlinks, link exits and exit signals put back, the links a lost connection ends, and the data OTP
%% calls an invalid distribution message. Then links and exits between a's processes and b's agents, and b's node
%% going down.
-module(dist_signals).
-export([main/1]).

% The flags Clause offers: OTP's own add the atom cache, whose packets this test does not read.
-define(FLAGS, 16#D075F5FBC).
-define(FAKE, 'fake@nohost').

main(["a"]) ->
    fake_links(),
    fake_relinks(),
    fake_linked(),
    fake_exits(),
    fake_down(),
    faults(),
    B = peer("dsb"),
    wait_registered("dsb_ready"),
    show("ping", net_adm:ping(B)),
    links(B),
    linked_here(B),
    exits(B),
    node_down(B),
    done();
main(["b"]) ->
    register(server, self()),
    _Ready = mark("dsb_ready"),
    serve().

%% b's server: agents for a's processes, a pid of a process that ended, and the stop that ends b's node.
serve() ->
    receive
        {agent, Owner} ->
            Owner ! {agent, spawn(fun() -> agent(Owner) end)},
            serve();
        {dead, Owner} ->
            {Dead, Ref} = spawn_monitor(fun() -> ok end),
            receive
                {'DOWN', Ref, _, _, _} -> Owner ! {dead, Dead}
            end,
            serve();
        stop ->
            done()
    end.

%% An agent: it links, unlinks, traps exits, tells its links, sends exit signals and ends as its owner asks, and
%% tells its owner the exit messages it gets.
agent(Owner) ->
    receive
        {link, Pid} ->
            Owner ! {agent, self(), link(Pid)};
        {unlink, Pid} ->
            Owner ! {agent, self(), unlink(Pid)};
        {trap, Flag} ->
            Owner ! {agent, self(), process_flag(trap_exit, Flag)};
        links ->
            {links, Links} = process_info(self(), links),
            Owner ! {agent, self(), Links};
        {exit_signal, Pid, Reason} ->
            Owner ! {agent, self(), exit(Pid, Reason)};
        {exit, Reason} ->
            exit(Reason);
        {'EXIT', From, Reason} ->
            Owner ! {agent, self(), {'EXIT', From, Reason}}
    end,
    agent(Owner).

% A new agent of b, or of this node for this, owned by the caller.
agent_of(this) ->
    Owner = self(),
    spawn(fun() -> agent(Owner) end);
agent_of(B) ->
    {server, B} ! {agent, self()},
    receive
        {agent, Agent} -> Agent
    after 30000 -> timeout
    end.

% Ask an agent and wait for its answer.
ask(Agent, Request) ->
    Agent ! Request,
    answer(Agent).

answer(Agent) ->
    receive
        {agent, Agent, Answer} -> Answer
    after 30000 -> timeout
    end.

%% Links of the controller to the fake node's processes, made while the connection is pending and once it is up, and
%% what they send: LINK once per link, UNLINK_ID, and EXIT2 (the payload form, as the flags have EXIT_PAYLOAD) for
%% exit/2,3; an exit to another node's alias is dropped, as the node lacks ALTACT_SIG. An exit from a link being
%% unlinked is dropped, UNLINK_ID_ACK ends it; link exits of the other forms arrive as messages.
fake_links() ->
    controlled(fun(_) ->
        process_flag(trap_exit, true),
        Self = self(),
        true = link(remote_pid(1)),
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        show("link", [link(remote_pid(2)), link(remote_pid(2)), link(remote_pid(9)), links()]),
        show("unlink", [unlink(remote_pid(2)), unlink(remote_pid(2)), unlink(remote_pid(4)), links()]),
        show("exit", [exit(remote_pid(3), bye), exit(remote_alias(), bye), erlang:exit(remote_pid(3), bye, [priority])]),
        Packets = [decoded(Packet) || Packet <- take(DH)],
        show("packets", [unlink_id(Packet) || Packet <- Packets]),
        [Id] = [Id || [{35, Id, _, _}] <- Packets],
        show("unlink id", is_integer(Id) andalso Id > 0),
        Puts = [
            {{24, remote_pid(2), Self}, unlinking},
            {36, Id, remote_pid(2), Self},
            {3, remote_pid(2), Self, after_ack},
            {13, remote_pid(1), Self, [], tt_form},
            {{24, remote_pid(1), Self}, again},
            {3, remote_pid(9), Self, plain},
            {{25, remote_pid(4), Self, []}, never_linked}
        ],
        [ok = deliver(DH, Put) || Put <- Puts],
        show("link exits", [named(Message) || Message <- flush()]),
        show("left", [links(), take(DH)])
    end).

%% A link made again while its unlink waits for UNLINK_ID_ACK: LINK again, and the old acknowledgement leaves the
%% link. The fake node's UNLINK_ID ends a link here and is acknowledged, also for no link; its LINK links.
fake_relinks() ->
    controlled(fun(_) ->
        process_flag(trap_exit, true),
        Self = self(),
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        show("relink", [link(remote_pid(5)), unlink(remote_pid(5)), link(remote_pid(5)), links()]),
        Packets = [decoded(Packet) || Packet <- take(DH)],
        show("relink packets", [unlink_id(Packet) || Packet <- Packets]),
        [Id] = [Id || [{35, Id, _, _}] <- Packets],
        ok = deliver(DH, {36, Id, remote_pid(5), Self}),
        show("after old ack", links()),
        ok = deliver(DH, {{24, remote_pid(5), Self}, relinked}),
        true = link(remote_pid(6)),
        [ok = deliver(DH, Put) || Put <- [{35, 99, remote_pid(6), Self}, {35, 98, remote_pid(7), Self}]],
        ok = deliver(DH, {1, remote_pid(8), Self}),
        ok = deliver(DH, {1, remote_pid(8), Self}),
        show("fake unlinks", [[named(Message) || Message <- flush()], links(), [decoded(P) || P <- take(DH)]])
    end).

%% The fake node's links to processes here: a process's end sends EXIT (payload form) to it, also for normal; a link
%% to no process here, or to a pid of an earlier incarnation of this node, is answered with noproc. Its link exit
%% ends a process that does not trap exits.
fake_linked() ->
    controlled(fun(_) ->
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        Helper = agent_of(this),
        Normal = agent_of(this),
        Victim = agent_of(this),
        VictimRef = monitor(process, Victim),
        Dead = dead(),
        Puts = [
            {1, remote_pid(1), Helper},
            {1, remote_pid(2), Normal},
            {1, remote_pid(3), Victim},
            {1, remote_pid(4), Dead},
            {1, remote_pid(4), old_self()}
        ],
        [ok = deliver(DH, Put) || Put <- Puts],
        show("linked here", [named(ask(Agent, links)) || Agent <- [Helper, Normal, Victim]]),
        show("noproc", [decoded(Packet) || Packet <- taken(DH, 2)]),
        ok = deliver(DH, {35, 5, remote_pid(1), Helper}),
        show("unlinked here", [named(ask(Helper, links)), [decoded(P) || P <- take(DH)]]),
        ok = deliver(DH, {1, remote_pid(1), Helper}),
        Helper ! {exit, {shutdown, helper}},
        HelperEnd = taken(DH, 1),
        Normal ! {exit, normal},
        show("ends", [decoded(Packet) || Packet <- HelperEnd ++ taken(DH, 1)]),
        ok = deliver(DH, {{24, remote_pid(3), Victim}, boom}),
        show("victim", down(VictimRef))
    end).

%% The fake node's exit signals (EXIT2 and its forms): kill ends even a process that traps exits, a trapping process
%% gets the others as messages, normal leaves another process; an exit to a pid of an earlier incarnation or to no
%% process does nothing, and a link exit without a link is dropped.
fake_exits() ->
    controlled(fun(_) ->
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        Killed = agent_of(this),
        Trapper = agent_of(this),
        Plain = agent_of(this),
        false = ask(Killed, {trap, true}),
        false = ask(Trapper, {trap, true}),
        KilledRef = monitor(process, Killed),
        Puts = [
            {{26, remote_pid(1), Trapper}, {shutdown, payload}},
            {8, remote_pid(1), Trapper, normal},
            {18, remote_pid(1), Trapper, [], tt},
            {{27, remote_pid(1), Trapper, []}, payload_tt},
            {{26, remote_pid(1), Plain}, normal},
            {3, remote_pid(1), Plain, no_link},
            {{26, remote_pid(1), old_self()}, kill},
            {8, remote_pid(1), dead(), kill},
            {{26, remote_pid(1), Killed}, kill}
        ],
        [ok = deliver(DH, Put) || Put <- Puts],
        show("trapped", [named(answer(Trapper)) || _ <- [1, 2, 3, 4]]),
        show("plain", ask(Plain, links)),
        show("killed", down(KilledRef))
    end).

%% A lost connection: links of processes here to the fake node's processes, made by either side, exit noconnection,
%% before the monitors' 'DOWN'; a process that does not trap exits ends.
fake_down() ->
    Trapper = agent_of(this),
    Linked = agent_of(this),
    Doomed = agent_of(this),
    [false = ask(Agent, {trap, true}) || Agent <- [Trapper, Linked]],
    DoomedRef = monitor(process, Doomed),
    Main = self(),
    Watcher = spawn(fun() -> watch(Main) end),
    controlled(fun(_) ->
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        Trapper ! {link, remote_pid(1)},
        Doomed ! {link, remote_pid(2)},
        Watcher ! {watch, remote_pid(4)},
        ok = deliver(DH, {1, remote_pid(3), Linked}),
        % Their LINK packets and the watcher's MONITOR_P.
        4 = length(taken(DH, 4))
    end),
    [true = answer(Agent) || Agent <- [Trapper, Doomed]],
    Watched =
        receive
            {watched, Signals} -> Signals
        after 30000 -> timeout
        end,
    show("connection lost", [named(answer(Trapper)), named(answer(Linked)), down(DoomedRef), Watched]).

% Link to and monitor the pid the caller names while trapping exits, and tell the caller the signals that come.
watch(Main) ->
    process_flag(trap_exit, true),
    receive
        {watch, Pid} -> ok
    end,
    true = link(Pid),
    _ = monitor(process, Pid),
    First = next(),
    Main ! {watched, [named(First), named(next())]}.

%% Data the controller cannot read kills it, after OTP's report (on standard error): the old UNLINK, and link
%% signals naming what OTP's dist.c refuses.
faults() ->
    Faults = [
        {"unlink", fun(Self) -> {4, remote_pid(1), Self} end},
        {"link from here", fun(Self) -> {1, Self, Self} end},
        {"unlink id 0", fun(Self) -> {35, 0, remote_pid(1), Self} end},
        {"exit to there", fun(_) -> {3, remote_pid(1), remote_pid(2), x} end}
    ],
    [
        controlled(Label, fun(_) ->
            DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
            ok = deliver(DH, Data(self())),
            receive
                Message -> show(Label, {received, Message})
            after 5000 -> show(Label, still_up)
            end
        end)
     || {Label, Data} <- Faults
    ].

%% Links between a and b: an exit of a linked process of b arrives as a message while trapping exits, a link ended by
%% unlink/1 brings none, and b's process links to a's processes. A link to a process of b that is not there exits
%% noproc.
links(B) ->
    process_flag(trap_exit, true),
    One = agent_of(B),
    true = link(One),
    show("b links", [ask(One, links) =:= [self()], links_to(One)]),
    One ! {exit, {shutdown, one}},
    show("b exits", exit_of(One)),
    Two = agent_of(B),
    Ref = monitor(process, Two),
    show("unlinked", [link(Two), unlink(Two), ask(Two, links), links_to(Two)]),
    Two ! {exit, two},
    show("no exit", [down(Ref), flush()]),
    Three = agent_of(B),
    show("b links here", [ask(Three, {link, self()}), links_to(Three)]),
    show("b unlinks", [ask(Three, {unlink, self()}), links_to(Three)]),
    {server, B} ! {dead, self()},
    Dead =
        receive
            {dead, D} -> D
        end,
    show("noproc", [link(Dead), exit_of(Dead), links_to(Dead)]),
    {_, NoProcRef} = spawn_monitor(fun() ->
        link(Dead),
        receive
        after 30000 -> alive
        end
    end),
    show("noproc ends", down(NoProcRef)).

%% b's processes linked to processes here: an exit here arrives there; b's exit ends a process here that does not
%% trap exits, normal does not.
linked_here(B) ->
    Here = agent_of(this),
    Trapping = agent_of(B),
    false = ask(Trapping, {trap, true}),
    true = ask(Trapping, {link, Here}),
    Here ! {exit, {here, three}},
    {'EXIT', From, Reason} = answer(Trapping),
    show("exit there", [From =:= Here, Reason]),
    Victim = agent_of(this),
    Boom = agent_of(B),
    true = ask(Victim, {link, Boom}),
    VictimRef = monitor(process, Victim),
    Boom ! {exit, boom},
    show("exit here", down(VictimRef)),
    Survivor = agent_of(this),
    Normal = agent_of(B),
    true = ask(Survivor, {link, Normal}),
    NormalRef = monitor(process, Normal),
    Normal ! {exit, normal},
    show("normal", [down(NormalRef), ask(Survivor, links)]).

%% exit/2 between a and b: kill and other reasons end b's processes, a trapping process gets them as messages, normal
%% leaves another; b's exit/2 reaches a's processes.
exits(B) ->
    Killed = agent_of(B),
    false = ask(Killed, {trap, true}),
    KilledRef = monitor(process, Killed),
    Ended = agent_of(B),
    EndedRef = monitor(process, Ended),
    Trapper = agent_of(B),
    false = ask(Trapper, {trap, true}),
    Plain = agent_of(B),
    show("exit", [exit(Killed, kill), exit(Ended, {complex, [1, 2], <<"bin">>}), exit(Plain, normal)]),
    show("exits there", [down(KilledRef), down(EndedRef), ask(Plain, links)]),
    true = exit(Trapper, normal),
    {'EXIT', From, normal} = answer(Trapper),
    show("trapped there", From =:= self()),
    Here = agent_of(this),
    HereRef = monitor(process, Here),
    show("exit from there", [ask(Trapper, {exit_signal, self(), normal}), exit_of(Trapper)]),
    true = ask(Trapper, {exit_signal, Here, {from, b}}),
    show("ended here", down(HereRef)).

%% b's node goes down: a process here linked to b's process and monitoring it gets the link's noconnection, then the
%% 'DOWN', then nodedown; a process that does not trap exits ends with noconnection.
node_down(B) ->
    Main = self(),
    Agent = agent_of(B),
    Watcher = spawn(fun() ->
        process_flag(trap_exit, true),
        true = link(Agent),
        _ = monitor(process, Agent),
        true = monitor_node(B, true),
        Main ! watching,
        Main ! {signals, [named_node(next(), Agent, B) || _ <- [1, 2, 3]]}
    end),
    Doomed = spawn(fun() ->
        true = link(Agent),
        Main ! watching,
        receive
        after infinity -> ok
        end
    end),
    DoomedRef = monitor(process, Doomed),
    [
        receive
            watching -> ok
        end
     || _ <- [Watcher, Doomed]
    ],
    true = ask(Agent, links) =/= [],
    {server, B} ! stop,
    show("node down", [
        receive
            {signals, Signals} -> Signals
        after 30000 -> timeout
        end,
        down(DoomedRef)
    ]).

named_node({'EXIT', Agent, Reason}, Agent, _) -> {'EXIT', agent, Reason};
named_node({'DOWN', _, process, Agent, Reason}, Agent, _) -> {'DOWN', agent, Reason};
named_node({nodedown, B}, _, B) -> {nodedown, b};
named_node(Other, _, _) -> Other.

% The exit message of Pid.
exit_of(Pid) ->
    receive
        {'EXIT', Pid, Reason} -> Reason
    after 30000 -> timeout
    end.

% Whether the caller's links hold Pid.
links_to(Pid) -> lists:member(Pid, element(2, process_info(self(), links))).

% The caller's links, sorted and named.
links() -> lists:sort(named(element(2, process_info(self(), links)))).

% A pid of a process of this node that ended.
dead() ->
    {Pid, Ref} = spawn_monitor(fun() -> ok end),
    down(Ref),
    Pid.

% Put a control message, or {Control, Payload}, as a packet.
deliver(DH, {Control, Payload}) when is_tuple(Control) ->
    erlang:dist_ctrl_put_data(DH, [112, term_to_binary(Control), term_to_binary(Payload)]);
deliver(DH, Control) ->
    erlang:dist_ctrl_put_data(DH, [112, term_to_binary(Control)]).

% A packet with its unlink id hidden: OTP's ids vary.
unlink_id([{35, _, From, To}]) -> [{35, id, From, To}];
unlink_id(Packet) -> Packet.

% Run Fun in a new process as the controller of a new pending connection to the fake node; show how it ended.
controlled(Fun) -> controlled("ended", Fun).

controlled(Label, Fun) ->
    {_, Ref} = spawn_monitor(fun() -> Fun(erts_internal:new_connection(?FAKE)) end),
    show(Label, down(Ref)).

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

% A packet's control message and payload, if any, with identities named.
decoded(<<112, Rest/binary>>) ->
    {Control, Used} = binary_to_term(Rest, [used]),
    case Rest of
        <<_:Used/binary>> ->
            [named(Control)];
        <<_:Used/binary, Payload/binary>> ->
            [named(Control), named(binary_to_term(Payload))]
    end.

% The fake node's pids as {fake, N}, this process as self, other identities by their node.
named(Term) when is_tuple(Term) ->
    list_to_tuple(named(tuple_to_list(Term)));
named(Term) when is_list(Term) ->
    [named(Element) || Element <- Term];
named(Term) when Term =:= self() -> self;
named(Term) when is_pid(Term) ->
    case term_to_binary(Term) of
        <<131, 88, 119, 11, "fake@nohost", N:32, 0:32, 77:32>> -> {fake, N};
        _ -> {identity, node(Term)}
    end;
named(Term) when is_reference(Term) -> {identity, node(Term)};
named(Term) ->
    Term.

% A pid and an alias of the fake node, as its creation 77 makes them.
remote_pid(N) -> binary_to_term(<<131, 88, 119, 11, "fake@nohost", N:32, 0:32, 77:32>>).
remote_alias() -> binary_to_term(<<131, 90, 3:16, 119, 11, "fake@nohost", 77:32, 1:32, 2:32, 3:32>>).

% A pid of an earlier incarnation of this node.
old_self() ->
    Name = atom_to_binary(node()),
    Creation = erts_internal:get_creation() + 1,
    binary_to_term(<<131, 88, 119, (byte_size(Name)), Name/binary, 5:32, 0:32, Creation:32>>).

% The reason of the 'DOWN' of Ref.
down(Ref) ->
    receive
        {'DOWN', Ref, process, _, Reason} -> Reason
    after 30000 -> timeout
    end.

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

done() -> io:format("dist_signals: ok~n").

show(Label, Term) -> io:format("~s: ~p~n", [Label, Term]).
