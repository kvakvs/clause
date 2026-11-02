%% Links and exit signals across nodes (plan 30 step DIST5a), group leaders (DIST5b1), spawns (DIST5b2), the order
%% of signals, aliases and send options (DIST5c), and other nodes' references and large unlink ids (DIST5e). Node a
%% controls connections to a node that does not exist (setnode/3 with its own process): the LINK, UNLINK_ID and EXIT2
%% packets of its links and exits, the fake node's links, unlinks, link exits and exit signals put back, the links a
%% lost connection ends, and the data OTP calls an invalid distribution message. Then links and exits between a's
%% processes and b's agents, and b's node going down.
-module(dist_signals).
-export([main/1, served/1, report/1, ready/0]).

% The flags Clause offers: OTP's own add the atom cache, whose packets this test does not read.
-define(FLAGS, 16#D075F5FBC).
-define(FAKE, 'fake@nohost').

main(["a"]) ->
    fake_links(),
    fake_relinks(),
    fake_linked(),
    fake_exits(),
    fake_down(),
    fake_leaders(),
    fake_requests(),
    fake_served(),
    fake_order(),
    faults(),
    references(),
    B = peer("dsb"),
    wait_registered("dsb_ready"),
    show("ping", net_adm:ping(B)),
    links(B),
    linked_here(B),
    exits(B),
    leaders(B),
    spawns(B),
    order(B),
    aliases(B),
    send_options(B),
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
        leader ->
            Owner ! {agent, self(), group_leader()};
        spawned_leader ->
            Self = self(),
            spawn(fun() -> Self ! {leader, group_leader()} end),
            receive
                {leader, Leader} -> Owner ! {agent, self(), Leader}
            end;
        {group_leader, Leader, Pid} ->
            Owner ! {agent, self(), group_leader(Leader, Pid)};
        {send, To, Messages} ->
            [To ! Message || Message <- Messages],
            Owner ! {agent, self(), sent};
        {collect, Count} ->
            Owner ! {agent, self(), [next() || _ <- lists:seq(1, Count)]};
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
        Unlinks = [
            {35, 99, remote_pid(6), Self}, {35, 98, remote_pid(7), Self}, {35, 1 bsl 64 - 1, remote_pid(7), Self}
        ],
        [ok = deliver(DH, Put) || Put <- Unlinks],
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

%% Group leaders (plan 30 step DIST5b1): group_leader/2 of the fake node's pid sends GROUP_LEADER, with a leader of
%% either node; the fake node's GROUP_LEADER makes its pid the group leader of a process here, which that process's
%% spawns inherit, and one for another node's pid does nothing. Another node's pid can lead this process too; a pid
%% of an earlier incarnation of this node cannot be led.
fake_leaders() ->
    controlled(fun(_) ->
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        show("group_leader/2", [group_leader(self(), remote_pid(1)), group_leader(remote_pid(2), remote_pid(1))]),
        show("leader packets", [decoded(Packet) || Packet <- take(DH)]),
        Member = agent_of(this),
        [ok = deliver(DH, Put) || Put <- [{7, remote_pid(3), Member}, {7, remote_pid(3), remote_pid(4)}]],
        {group_leader, Leader} = process_info(Member, group_leader),
        show("led", [named(Leader), named(ask(Member, leader)), named(ask(Member, spawned_leader))]),
        Old = group_leader(),
        true = group_leader(remote_pid(5), self()),
        Led = group_leader(),
        true = group_leader(Old, self()),
        show("own leader", [named(Led), group_leader() =:= Old]),
        show(
            "earlier incarnation",
            try
                group_leader(self(), old_self())
            catch
                error:Reason -> Reason
            end
        )
    end).

%% Spawn requests to the fake node (plan 30 step DIST5b2): SPAWN_REQUEST with the caller's group leader and the
%% options it does not keep (monitor and link first for their tuple forms); the fake node's SPAWN_REPLY activates the
%% link and monitor and brings the reply the options ask for. An abandoned request, or one whose requester ended,
%% demonitors and unlinks the child once it is spawned.
fake_requests() ->
    controlled(fun(_) ->
        process_flag(trap_exit, true),
        Self = self(),
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        Options = [link, {monitor, [{tag, t}]}, {reply_tag, tag}, {priority, high}],
        First = spawn_request(?FAKE, m, f, [a, 1], Options),
        Second = spawn_request(?FAKE, m, f, [], [{reply, error_only}]),
        Third = spawn_request(?FAKE, m, f, [], [{reply, success_only}]),
        show("requests", [decoded(Packet) || Packet <- take(DH)]),
        show("pending", [demonitor(First), links(), process_info(self(), monitors)]),
        Replies = [{31, First, Self, 3, remote_pid(7)}, {31, Second, Self, 0, badarg}, {31, Third, Self, 0, badarg}],
        [ok = deliver(DH, Reply) || Reply <- Replies],
        show("replies", [named(Message) || Message <- flush()]),
        show("spawned", [links(), named(element(2, process_info(self(), monitors)))]),
        ok = deliver(DH, {{28, remote_pid(7), Self, First}, gone}),
        ok = deliver(DH, {{24, remote_pid(7), Self}, gone}),
        show("child ended", [named(Message) || Message <- flush()]),
        Abandoned = spawn_request(?FAKE, m, f, [], [link, monitor]),
        _ = take(DH),
        show("abandon", [spawn_request_abandon(Abandoned), spawn_request_abandon(Abandoned)]),
        ok = deliver(DH, {31, Abandoned, Self, 3, remote_pid(8)}),
        % OTP's requester takes the reply as a signal of its own, when it next receives.
        Flushed = flush(),
        show("abandoned", [[decoded(Packet) || Packet <- take(DH)], Flushed]),
        {Requester, Ref} = spawn_monitor(fun() ->
            Self ! {request, spawn_request(?FAKE, m, f, [], [link, monitor])},
            exit({shutdown, gone})
        end),
        Request =
            receive
                {request, R} -> R
            end,
        {shutdown, gone} = down(Ref),
        _ = take(DH),
        ok = deliver(DH, {31, Request, Requester, 3, remote_pid(9)}),
        show("requester ended", [decoded(Packet) || Packet <- taken(DH, 2)]),
        show("stale", [deliver(DH, {31, make_ref(), Self, 0, remote_pid(9)}), take(DH), flush()])
    end).

%% The fake node's spawn requests (plan 30 step DIST5b2): the child runs here linked to and monitored by the
%% requester, with the request's group leader, after SPAWN_REPLY; its end sends both their exits. Bad options are
%% answered with badopt.
fake_served() ->
    controlled(fun(_) ->
        Self = self(),
        DH = erlang:setnode(?FAKE, self(), {?FLAGS, 77}),
        Request = {29, remote_ref(1), remote_pid(10), remote_pid(11), {dist_signals, served, 1}, [link, monitor]},
        ok = deliver(DH, {Request, [Self]}),
        Child =
            receive
                {served, C, Leader, Links} ->
                    show("served", [named(Leader), named(Links), named(process_info(C, monitored_by))]),
                    C
            after 30000 -> timeout
            end,
        show("served reply", [decoded(Packet) || Packet <- take(DH)]),
        Child ! stop,
        show("served ends", [decoded(Packet) || Packet <- taken(DH, 2)]),
        Bad = {29, remote_ref(2), remote_pid(10), remote_pid(11), {m, f, 0}, [bogus]},
        ok = deliver(DH, {Bad, []}),
        show("bad options", [decoded(Packet) || Packet <- taken(DH, 1)])
    end).

%% Signals of one process to the fake node while its connection is pending and once it is up keep their order
%% (plan 30 step DIST5c): messages, a link, an unlink, a monitor, an exit signal.
fake_order() ->
    controlled(fun(DH0) ->
        Self = self(),
        Steps = fun(Tag) ->
            remote_pid(1) ! {Tag, 1},
            true = link(remote_pid(1)),
            remote_pid(1) ! {Tag, 2},
            true = unlink(remote_pid(1)),
            _ = monitor(process, remote_pid(1)),
            true = exit(remote_pid(1), {Tag, 3})
        end,
        Steps(pending),
        {ok, 0, 0, _} = erlang:dist_get_stat(DH0),
        DH = erlang:setnode(?FAKE, Self, {?FLAGS, 77}),
        Steps(up),
        show("order", [unlink_id(Packet) || Packet <- [decoded(P) || P <- take(DH)]])
    end).

% A process the fake node spawns: it tells the test its group leader and links, and ends when told.
served(Test) ->
    {links, Links} = process_info(self(), links),
    Test ! {served, self(), group_leader(), Links},
    receive
        stop -> ok
    end.

%% Data the controller cannot read kills it, after OTP's report (on standard error): the old UNLINK, and link,
%% group leader and spawn signals naming what OTP's dist.c refuses.
faults() ->
    Faults = [
        {"unlink", fun(Self) -> {4, remote_pid(1), Self} end},
        {"link from here", fun(Self) -> {1, Self, Self} end},
        {"unlink id 0", fun(Self) -> {35, 0, remote_pid(1), Self} end},
        {"unlink id 2^64", fun(Self) -> {35, 1 bsl 64, remote_pid(1), Self} end},
        {"exit to there", fun(_) -> {3, remote_pid(1), remote_pid(2), x} end},
        {"group leader", fun(Self) -> {7, x, Self} end},
        {"spawn request", fun(Self) -> {29, make_ref(), Self, Self, {m, f, 0}, []} end},
        {"spawn reply", fun(Self) -> {31, make_ref(), Self, -1, remote_pid(1)} end}
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

%% References of other nodes (DIST5e): no spawn request of this process to abandon; an earlier incarnation's monitors
%% nothing (demonitor/2 flushes its 'DOWN' still), another node's is badarg to demonitor/1,2.
references() ->
    Old = old_ref(),
    self() ! {'DOWN', Old, process, gone, normal},
    show("references", [
        spawn_request_abandon(remote_ref(1)),
        spawn_request_abandon(Old),
        demonitor(Old),
        demonitor(Old, [info]),
        demonitor(Old, [flush, info]),
        flush(),
        failure(fun() -> demonitor(remote_ref(1)) end),
        failure(fun() -> demonitor(remote_ref(1), [info]) end)
    ]).

% The error reason of Fun(), or its value.
failure(Fun) ->
    try
        Fun()
    catch
        error:Reason -> Reason
    end.

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

%% Group leaders between a and b: a's process leads b's agent and the processes it spawns, and b's agent makes
%% itself the group leader of a's process (plan 30 step DIST5b1).
leaders(B) ->
    Agent = agent_of(B),
    true = group_leader(self(), Agent),
    show("leader there", [ask(Agent, leader) =:= self(), ask(Agent, spawned_leader) =:= self()]),
    Here = agent_of(this),
    true = ask(Agent, {group_leader, Agent, Here}),
    show("leader here", ask(Here, leader) =:= Agent).

%% Spawns on b (plan 30 step DIST5b2): spawn/2,4, spawn_link/4, spawn_monitor/4, spawn_opt/5 and spawn_request/5
%% run their processes there with this process's group leader; bad options, and a node that cannot be reached, as
%% OTP's.
spawns(B) ->
    Main = self(),
    First = spawn(B, dist_signals, report, [Main]),
    show("spawn", [node(First), reported(First)]),
    Linked = spawn_link(B, dist_signals, report, [Main]),
    show("spawn_link", [reported(Linked), exit_of(Linked)]),
    {Monitored, Ref} = spawn_monitor(B, erlang, exit, [bye]),
    show("spawn_monitor", [node(Monitored), down(Ref)]),
    {Opted, OptRef} = spawn_opt(B, erlang, exit, [opt], [monitor, {priority, high}]),
    show("spawn_opt", [node(Opted), down(OptRef)]),
    FunPid = spawn(B, fun dist_signals:ready/0),
    show("spawn fun", node(FunPid)),
    Request = spawn_request(B, dist_signals, report, [Main], [link, monitor, {reply_tag, rt}]),
    Requested =
        receive
            {rt, Request, ok, P} -> P
        after 30000 -> timeout
        end,
    show("spawn_request", [node(Requested), reported(Requested), exit_of(Requested), down(Request)]),
    Bad = spawn_request(B, m, f, [], [bogus]),
    show("badopt", [
        receive
            {spawn_reply, Bad, error, Reason} -> Reason
        after 30000 -> timeout
        end,
        try
            spawn_opt(B, m, f, [], [bogus])
        catch
            error:Error -> Error
        end
    ]),
    Nobody = 'nobody@localhost',
    {_, NobodyRef} = spawn_monitor(Nobody, m, f, []),
    Unreached = spawn_request(Nobody, m, f, []),
    show("unreachable", [
        down(NobodyRef),
        receive
            {spawn_reply, Unreached, error, Why} -> Why
        after 30000 -> timeout
        end
    ]).

% What a process spawned on b tells: its group leader is the caller's.
report(Main) -> Main ! {report, self(), group_leader()}.

ready() -> ok.

% Whether the spawned process reported the caller's group leader.
reported(Pid) ->
    receive
        {report, Pid, Leader} -> Leader =:= group_leader()
    after 30000 -> timeout
    end.

%% Signals of one process to b's agent arrive in the order sent (plan 30 step DIST5c): messages around a link, an
%% unlink and an exit signal the trapping agent turns into a message.
order(B) ->
    Agent = agent_of(B),
    false = ask(Agent, {trap, true}),
    Agent ! {collect, 3},
    Agent ! one,
    true = link(Agent),
    Agent ! two,
    true = unlink(Agent),
    true = exit(Agent, three),
    show("order there", [
        case Message of
            {'EXIT', From, Reason} -> {'EXIT', From =:= self(), Reason};
            _ -> Message
        end
     || Message <- answer(Agent)
    ]),
    Collector = agent_of(this),
    Collector ! {collect, 50},
    sent = ask(Agent, {send, Collector, lists:seq(1, 50)}),
    show("order here", answer(Collector) =:= lists:seq(1, 50)).

%% Aliases reachable from b (plan 30 step DIST5c): an alias with reply takes one message, an explicit one until
%% unalias/1, and a monitor's alias with reply_demonitor ends the monitor with its reply.
aliases(B) ->
    Agent = agent_of(B),
    Once = alias([reply]),
    sent = ask(Agent, {send, Once, [first, second]}),
    Explicit = alias(),
    sent = ask(Agent, {send, Explicit, [third, fourth]}),
    true = unalias(Explicit),
    sent = ask(Agent, {send, Explicit, [fifth]}),
    Watched = agent_of(B),
    Ref = monitor(process, Watched, [{alias, reply_demonitor}]),
    sent = ask(Agent, {send, Ref, [reply]}),
    sent = ask(Agent, {send, Ref, [late]}),
    Watched ! {exit, after_reply},
    timer:sleep(200),
    show("aliases", [flush(), process_info(self(), monitors)]).

%% send/3 options to b: noconnect sends on an up connection; a node not connected takes nothing with noconnect, and
%% send_nosuspend/3 then returns false (plan 30 step DIST5c).
send_options(B) ->
    Agent = agent_of(B),
    Agent ! {collect, 2},
    show("send options", [
        erlang:send(Agent, one, [noconnect]),
        erlang:send(Agent, two, [nosuspend]),
        answer(Agent),
        erlang:send({x, 'nobody@localhost'}, lost, [noconnect]),
        erlang:send_nosuspend({x, 'nobody@localhost'}, lost, [noconnect]),
        erlang:send_nosuspend(Agent, three)
    ]).

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
remote_ref(N) -> binary_to_term(<<131, 90, 3:16, 119, 11, "fake@nohost", 77:32, N:32, 0:32, 0:32>>).

% A reference of an earlier incarnation of this node.
old_ref() ->
    Name = atom_to_binary(node()),
    Creation = erts_internal:get_creation() + 1,
    binary_to_term(<<131, 90, 3:16, 119, (byte_size(Name)), Name/binary, Creation:32, 1:32, 0:32, 0:32>>).

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
