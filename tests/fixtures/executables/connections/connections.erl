%% Connection entries of other nodes (plan 30 step DIST2b), as OTP allows them without distribution: a stand-in
%% net_kernel and controller processes; erts_internal:new_connection/1, erlang:setnode/3 and
%% abort_pending_connection/2, nodes/1,2, node status messages (process_flag(monitor_nodes, ...),
%% net_kernel:monitor_nodes/1,2) and the ends of controllers and of net_kernel. OTP's connection ids differ between
%% runs, so each prints as a label; messages of different nodes are taken node by node, as OTP sends them from
%% different threads. The monitors run covers node monitors and auto-connect (plan 30 step DIST2c):
%% monitor_node/2,3, erlang:dmonitor_node/3 and list_to_pid/1, list_to_port/1, list_to_ref/1 of a connected node.
-module(connections).
-export([main/1]).

main(["mandatory"]) ->
    mandatory();
main(["monitors"]) ->
    monitors();
main(_) ->
    Mandatory = flags(),
    subscriptions(),
    Kernel = kernel(),
    unnamed(),
    io:format("setnode/2: ~p~n", [erlang:setnode('a@host', 5)]),
    show("named", flush()),
    io:format("known: ~p~n", [known()]),
    Visible = visible(Mandatory),
    pending(Mandatory, Visible),
    hidden(Mandatory),
    io:format(
        "order: ~p~n",
        [{known(), nodes([this, visible, hidden]), nodes([visible, this]), nodes(connected)}]
    ),
    ends(Mandatory, Visible),
    kernel_end(Kernel),
    io:format("connections: ok~n").

%% A call's result, or its exception class and reason.
safe(Fun) ->
    try
        {ok, Fun()}
    catch
        Class:Reason -> {Class, Reason}
    end.

%% The messages waiting for the caller.
flush() ->
    receive
        Message -> [Message | flush()]
    after 0 -> []
    end.

%% Messages, one per line, with connection ids as labels.
show(Title, Messages) ->
    io:format("~s:~n", [Title]),
    [io:format("  ~w~n", [masked(Message)]) || Message <- Messages],
    ok.

%% The next Count messages {What, Node, ...}.
collect(_, _, 0) ->
    [];
collect(What, Node, Count) ->
    receive
        Message when element(1, Message) =:= What, element(2, Message) =:= Node ->
            [Message | collect(What, Node, Count - 1)]
    after 2000 -> [timeout]
    end.

%% Name the connection id of a node's dist handle.
label(Node, Handle, Label) ->
    put({connection_id, Node, element(1, Handle)}, Label).

%% A node status message or a nodes/2 list with each connection id in its info as its label.
masked({What, Node, Info}) when is_atom(Node) ->
    {What, Node, masked_info(Node, Info)};
masked(Listed) when is_list(Listed) ->
    [{Node, masked_info(Node, Info)} || {Node, Info} <- Listed];
masked(Term) ->
    Term.

masked_info(Node, #{connection_id := Id} = Info) when is_integer(Id) ->
    Info#{connection_id := id_label(Node, Id)};
masked_info(Node, Info) when is_list(Info) ->
    [
        case Item of
            {connection_id, Id} when is_integer(Id) -> {connection_id, id_label(Node, Id)};
            _ -> Item
        end
     || Item <- Info
    ];
masked_info(_, Info) ->
    Info.

id_label(Node, Id) ->
    case get({connection_id, Node, Id}) of
        undefined -> unknown_id;
        Label -> Label
    end.

%% Known nodes without OTP's entries of this node's former names, which Clause does not keep.
known() ->
    [
        Node
     || Node <- nodes(known),
        Node =/= nonode@nohost orelse node() =:= nonode@nohost,
        Node =/= 'a@host' orelse
            node() =:= 'a@host'
    ].

%% erts_internal:get_dflags/0: the mandatory, rejectable and strict order flags OTP has; Clause's default set leaves
%% out what it does not offer.
flags() ->
    {erts_dflags, Default, Mandatory, Addable, Rejectable, StrictOrder} = erts_internal:get_dflags(),
    io:format("dflags: ~p~n", [
        {Mandatory, Rejectable, StrictOrder, Default band Mandatory =:= Mandatory, Addable =:= Default, Default band 1}
    ]),
    Mandatory.

%% Subscriptions to node status messages, in the order the messages come.
subscriptions() ->
    Flags = [
        {monitor_nodes, true},
        {monitor_nodes, true},
        {{monitor_nodes, #{node_type => all, connection_id => true, nodedown_reason => true}}, true},
        {{monitor_nodes, #{list => true, node_type => all, nodedown_reason => true, connection_id => true}}, true},
        {{monitor_nodes, []}, true},
        {{monitor_nodes, #{}}, true},
        {{monitor_nodes, #{node_type => hidden}}, true},
        {{monitor_nodes, #{node_type => hidden}}, true},
        {{monitor_nodes, #{node_type => hidden}}, false},
        {{monitor_nodes, #{node_type => hidden}}, false},
        {{monitor_nodes, #{bad => 1}}, true},
        {{monitor_nodes, #{list => false}}, true},
        {monitor_nodes, perhaps},
        {{monitor_nodes, [a]}, true},
        {{monitor_nodes, #{}, extra}, true}
    ],
    [
        io:format("process_flag ~w: ~p~n", [Call, safe(fun() -> process_flag(Flag, Value) end)])
     || {Flag, Value} = Call <- Flags
    ],
    Calls = [
        [true, [{node_type, hidden}]],
        [true, #{node_type => visible}],
        [true, #{bad => 1}],
        [true, #{node_type => bad}],
        [true, #{connection_id => other}],
        [true, #{node_type => bad, unknown_key => 1}],
        [true, #{list => true}],
        [true, [{node_type, all}, {node_type, visible}]],
        [true, [{node_type, bad}]],
        [true, [bad, {node_type, bad}]],
        [true, [bad, other]],
        [true, [{connection_id, true}]],
        [true, atom],
        [perhaps, []],
        [false, #{}],
        [false, [nodedown_reason]]
    ],
    [io:format("monitor_nodes ~w: ~p~n", [Call, apply(net_kernel, monitor_nodes, Call)]) || Call <- Calls],
    io:format("monitor_nodes/1: ~p~n", [[net_kernel:monitor_nodes(perhaps), net_kernel:monitor_nodes(true)]]),
    io:format("messages: ~p~n", [flush()]).

%% A stand-in net_kernel that passes its messages on to the caller and declines each call.
kernel() ->
    Self = self(),
    Kernel = spawn(fun Loop() ->
        receive
            {stop, Reason} ->
                exit(Reason);
            {'$gen_call', From, Request} ->
                Self ! {kernel, {call, Request}},
                gen_server:reply(From, false),
                Loop();
            Message ->
                Self ! {kernel, Message},
                Loop()
        end
    end),
    register(net_kernel, Kernel),
    Kernel.

%% A controller process that ends when told to.
controller() ->
    spawn(fun() ->
        receive
            {stop, Reason} -> exit(Reason)
        end
    end).

%% The next message the stand-in net_kernel passed on.
kernel_message() ->
    receive
        {kernel, Message} -> Message
    after 2000 -> timeout
    end.

%% Entries before the node has a name: new_connection/1 needs no name, setnode/3 does.
unnamed() ->
    Handle = erts_internal:new_connection('z@host'),
    {Id, Reference} = Handle,
    io:format("handle: ~p ~p ~p~n", [
        is_integer(Id), is_reference(Reference), erts_internal:new_connection('z@host') =:= Handle
    ]),
    Calls = [nonode@nohost, name, "z@host", 'a@b@c'],
    [
        io:format("new_connection ~w: ~p~n", [Node, safe(fun() -> erts_internal:new_connection(Node) end)])
     || Node <- Calls
    ],
    io:format("known: ~p~n", [nodes(known)]),
    io:format("setnode/3 unnamed: ~p~n", [safe(fun() -> erlang:setnode('z@host', controller(), {1 bsl 40, 1}) end)]).

%% A visible connection: setnode/3 returns the pending entry's handle, tells net_kernel and the subscribers.
visible(Mandatory) ->
    Handle = erts_internal:new_connection('b@host'),
    label('b@host', Handle, b1),
    Controller = controller(),
    io:format("setnode/3: ~p~n", [erlang:setnode('b@host', Controller, {Mandatory bor 1, 6}) =:= Handle]),
    io:format("net_kernel: ~p~n", [kernel_message() =:= {dist_ctrlr, Controller, 'b@host', self()}]),
    show("nodeup", flush()),
    io:format("nodes: ~p ~p ~w~n", [
        nodes(), nodes(hidden), masked(nodes(connected, #{connection_id => true, node_type => true}))
    ]),
    io:format("known info: ~w~n", [
        masked(
            nodes(known, #{connection_id => true, node_type => true}) --
                [
                    {nonode@nohost, #{connection_id => undefined, node_type => known}}
                ]
        )
    ]),
    io:format("again: ~p~n", [safe(fun() -> erlang:setnode('b@host', Controller, {Mandatory bor 1, 6}) end)]),
    io:format("new_connection up: ~p~n", [erts_internal:new_connection('b@host') =:= Handle]),
    Aborts = [
        {up, ['b@host', Handle]},
        {other_node, ['x@host', Handle]},
        {string, ["b@host", Handle]},
        {other_reference, ['b@host', {element(1, Handle), make_ref()}]},
        {reference, ['b@host', element(2, Handle)]},
        {this_node, ['a@host', Handle]}
    ],
    [
        io:format("abort ~w: ~p~n", [Label, safe(fun() -> apply(erts_internal, abort_pending_connection, Abort) end)])
     || {Label, Abort} <- Aborts
    ],
    {Controller, Handle}.

%% Pending entries: aborting one, a stale handle, and what setnode/3 refuses.
pending(Mandatory, {Controller, _}) ->
    First = erts_internal:new_connection('e@host'),
    io:format("abort pending: ~p ~p~n", [
        erts_internal:abort_pending_connection('e@host', First), erts_internal:abort_pending_connection('e@host', First)
    ]),
    Second = erts_internal:new_connection('e@host'),
    label('e@host', Second, e1),
    io:format("next id: ~p~n", [(element(1, Second) - element(1, First)) band 16#ffffff]),
    io:format("abort stale: ~p~n", [erts_internal:abort_pending_connection('e@host', First)]),
    % A pid of a node the table has: OTP keeps an entry for every node its terms name.
    Other = <<131, 88, 119, 6, "z@host", 1:32, 0:32, 1:32>>,
    Calls = [
        {another_connection, ['e@host', Controller, {Mandatory bor 1, 6}]},
        {net_kernel, ['e@host', whereis(net_kernel), {Mandatory bor 1, 6}]},
        {idle, ['f@host', controller(), {Mandatory bor 1, 6}]},
        {options, ['e@host', controller(), bad]},
        {negative_creation, ['e@host', controller(), {Mandatory bor 1, -1}]},
        {big_creation, ['e@host', controller(), {Mandatory bor 1, 1 bsl 59}]},
        {negative_flags, ['e@host', controller(), {-1, 1}]},
        {big_flags, ['e@host', controller(), {1 bsl 64, 1}]},
        {external_pid, ['e@host', binary_to_term(Other), {Mandatory bor 1, 6}]},
        {atom, ['e@host', self, {Mandatory bor 1, 6}]},
        {string, ["e@host", controller(), {Mandatory bor 1, 6}]},
        {this_node, ['a@host', controller(), {Mandatory bor 1, 6}]}
    ],
    [io:format("setnode ~w: ~p~n", [Label, safe(fun() -> apply(erlang, setnode, Call) end)]) || {Label, Call} <- Calls],
    io:format("create_dist_channel: ~p~n", [erts_internal:create_dist_channel('e@host', controller(), bad)]),
    Up = controller(),
    Handle = erlang:setnode('e@host', Up, {Mandatory bor 1, (1 bsl 32) + 9}),
    io:format("setnode/3 e: ~p ~p~n", [Handle =:= Second, kernel_message() =:= {dist_ctrlr, Up, 'e@host', self()}]),
    show("nodeup", flush()).

%% A hidden connection: only subscriptions to hidden nodes hear of it.
hidden(Mandatory) ->
    label('h@host', erts_internal:new_connection('h@host'), h1),
    Controller = controller(),
    erlang:setnode('h@host', Controller, {Mandatory, 7}),
    io:format("hidden: ~p ~p ~p~n", [
        kernel_message() =:= {dist_ctrlr, Controller, 'h@host', self()}, nodes(), nodes(hidden)
    ]),
    show("nodeup", flush()).

%% A controller's end takes its connection down: {shutdown, Reason} gives Reason, normal connection_closed.
ends(Mandatory, {Controller, Handle}) ->
    Controller ! {stop, {shutdown, gone}},
    show("nodedown", collect(nodedown, 'b@host', 7)),
    io:format("connected: ~p~n", [nodes(connected)]),
    Again = erts_internal:new_connection('b@host'),
    label('b@host', Again, b2),
    io:format("next id: ~p~n", [(element(1, Again) - element(1, Handle)) band 16#ffffff]),
    Second = controller(),
    erlang:setnode('b@host', Second, {Mandatory bor 1, 6}),
    io:format("net_kernel: ~p~n", [kernel_message() =:= {dist_ctrlr, Second, 'b@host', self()}]),
    show("nodeup", flush()),
    Second ! {stop, normal},
    show("nodedown", collect(nodedown, 'b@host', 7)).

%% net_kernel's end kills the controllers (nodedown reason killed), aborts pending entries, then takes the name.
kernel_end(Kernel) ->
    io:format("before: ~p ~p~n", [nodes(connected), known()]),
    Kernel ! {stop, no_network},
    show("hidden down", collect(nodedown, 'h@host', 3)),
    show("visible down", collect(nodedown, 'e@host', 7)),
    show("this node down", collect(nodedown, 'a@host', 7)),
    wait_unnamed(1000),
    % OTP's controllers end on their own schedulers, in either order.
    io:format("after: ~p ~p ~p ~p~n", [node(), is_alive(), nodes(connected), lists:sort(known())]),
    io:format("messages: ~p~n", [flush()]).

%% OTP takes the name after the last controller ended, not at once.
wait_unnamed(0) ->
    timeout;
wait_unnamed(Tries) ->
    case node() of
        nonode@nohost ->
            ok;
        _ ->
            timer:sleep(1),
            wait_unnamed(Tries - 1)
    end.

%% setnode/3 with flags lacking a mandatory one: badarg, and an error report on standard output.
mandatory() ->
    kernel(),
    erlang:setnode('a@host', 5),
    erts_internal:new_connection('g@host'),
    register(me, self()),
    Result = safe(fun() -> erlang:setnode('g@host', controller(), {1, 1}) end),
    %% OTP's logger prints the report after the call returns.
    timer:sleep(200),
    io:format("mandatory: ~p~n", [Result]),
    io:format("connections: ok~n").

%% Node monitors over the connection entries: monitor_node/2,3 auto-connects an idle entry through net_kernel, holds
%% one monitor per call, and the entry's abort or end sends {nodedown, Node} once per monitor.
monitors() ->
    {erts_dflags, _, Mandatory, _, _, _} = erts_internal:get_dflags(),
    Kernel = kernel(),
    io:format("unnamed: ~p ~p~n", [
        safe(fun() -> monitor_node(nonode@nohost, true) end), safe(fun() -> monitor_node('x@host', true) end)
    ]),
    erlang:setnode('a@host', 5),
    io:format("this node: ~p ~p~n", [monitor_node('a@host', true), flush()]),
    auto_connect(),
    Controller = up_monitors(Mandatory),
    identity_text(),
    Controller ! {stop, bye},
    show("controller end", collect(nodedown, 'c@host', 2)),
    io:format("list_to_pid down: ~p~n", [safe(fun() -> list_to_pid(get(pid_text)) end)]),
    monitor_off(),
    monitor_errors(),
    passive(Mandatory),
    dmonitor(),
    no_kernel(Kernel),
    monitors_kernel_end(Kernel, Mandatory),
    io:format("connections: ok~n").

%% monitor_node(Node, true) of an idle entry makes it pending and asks net_kernel; aborting it takes both monitors.
auto_connect() ->
    io:format("monitor b: ~p ~p~n", [monitor_node('b@host', true), monitor_node('b@host', true)]),
    {auto_connect, 'b@host', Handle} = kernel_message(),
    io:format("auto_connect: ~p ~p ~p~n", [
        Handle =:= erts_internal:new_connection('b@host'), lists:member('b@host', nodes(known)), kernel_messages()
    ]),
    io:format("abort: ~p~n", [erts_internal:abort_pending_connection('b@host', Handle)]),
    show("aborted", collect(nodedown, 'b@host', 2)).

%% A pending entry's monitor needs no auto-connect and stays when the connection comes up; false ends one monitor.
up_monitors(Mandatory) ->
    Handle = erts_internal:new_connection('c@host'),
    io:format("monitor pending: ~p ~p~n", [monitor_node('c@host', true), kernel_messages()]),
    Controller = controller(),
    Handle = erlang:setnode('c@host', Controller, {Mandatory bor 1, 6}),
    {dist_ctrlr, Controller, 'c@host', _} = kernel_message(),
    io:format("monitor up: ~p~n", [
        [monitor_node('c@host', true), monitor_node('c@host', true), monitor_node('c@host', false)]
    ]),
    io:format("messages: ~p ~p~n", [flush(), kernel_messages()]),
    Controller.

%% The node part of the text of a pid of Node.
node_part(Node) ->
    Pid = binary_to_term(<<131, 88, 119, (byte_size(Node)), Node/binary, 1:32, 0:32, 6:32>>),
    hd(string:split(tl(pid_to_list(Pid)), ".")).

%% The text of a connected node's pid, port and reference reads back under the connection's creation.
identity_text() ->
    Pid = binary_to_term(<<131, 88, 119, 6, "c@host", 1:32, 0:32, 6:32>>),
    Other = binary_to_term(<<131, 88, 119, 6, "c@host", 1:32, 0:32, 7:32>>),
    Port = binary_to_term(<<131, 120, 119, 6, "c@host", 7:64, 6:32>>),
    Ref = binary_to_term(<<131, 90, 0, 3, 119, 6, "c@host", 6:32, 1:32, 2:32, 3:32>>),
    Text = pid_to_list(Pid),
    put(pid_text, Text),
    Node = node_part(<<"c@host">>),
    io:format("read back: ~p~n", [
        [
            list_to_pid(Text) =:= Pid,
            list_to_pid(pid_to_list(Other)) =:= Other,
            list_to_port(port_to_list(Port)) =:= Port,
            list_to_ref(ref_to_list(Ref)) =:= Ref,
            node(list_to_pid(Text))
        ]
    ]),
    Cut = [
        {pid, "<" ++ Node ++ ".4294967296.4294967297>", "<" ++ Node ++ ".0.1>"},
        {port, "#Port<" ++ Node ++ ".18446744073709551617>", "#Port<" ++ Node ++ ".1>"},
        {ref, "#Ref<" ++ Node ++ ".1.2.3.4.5>", "#Ref<" ++ Node ++ ".1.2.3.4.5>"},
        {ref, "#Ref<" ++ Node ++ ".7>", "#Ref<" ++ Node ++ ".7>"},
        {pid, "<0" ++ Node ++ ".1.0>", Text}
    ],
    [io:format("~w text: ~p~n", [Kind, identity_back(Kind, In) =:= Out]) || {Kind, In, Out} <- Cut],
    io:format("local: ~p~n", [list_to_pid("<0" ++ tl(pid_to_list(self()))) =:= self()]),
    Bad = [
        {pid, "<" ++ Node ++ ".1>"},
        {pid, "<" ++ Node ++ ". 1.0>"},
        {pid, "<" ++ Node ++ ".1.0> "},
        {ref, "#Ref<" ++ Node ++ ".1.2.3.4.5.6>"},
        {ref, "#Ref<" ++ Node ++ ".4294967296.2.3>"},
        {pid, "<" ++ node_part(<<"b@host">>) ++ ".1.0>"},
        {port, "#Port<" ++ node_part(<<"b@host">>) ++ ".1>"}
    ],
    [io:format("~w: ~p~n", [Kind, safe(fun() -> identity_back(Kind, In) end)]) || {Kind, In} <- Bad],
    ok.

%% An identity read from its text and printed again.
identity_back(pid, Text) -> pid_to_list(list_to_pid(Text));
identity_back(port, Text) -> port_to_list(list_to_port(Text));
identity_back(ref, Text) -> ref_to_list(list_to_ref(Text)).

%% Monitors ended before the entry's abort send nothing; false without a monitor makes no entry.
monitor_off() ->
    [monitor_node('d@host', Flag) || Flag <- [true, true, false]],
    {auto_connect, 'd@host', Handle} = kernel_message(),
    io:format("abort d: ~p~n", [erts_internal:abort_pending_connection('d@host', Handle)]),
    show("aborted d", collect(nodedown, 'd@host', 1)),
    io:format("off: ~p ~p~n", [monitor_node('q@host', false), lists:member('q@host', nodes(known))]).

%% monitor_node/2,3's argument errors, in OTP's order.
monitor_errors() ->
    Calls = [
        ['q@host', perhaps],
        ['q@host', true, [bad]],
        ['q@host', true, [allow_passive_connect | tail]],
        ['q@host', perhaps, [allow_passive_connect]],
        ["q@host", true],
        [nonode@nohost, true, [bad]]
    ],
    [io:format("monitor_node ~w: ~p~n", [Call, safe(fun() -> apply(erlang, monitor_node, Call) end)]) || Call <- Calls],
    io:format("messages: ~p ~p~n", [flush(), kernel_messages()]).

%% allow_passive_connect: a connection that is not up asks net_kernel (passive_cnct), which declines, so the node is
%% down at once and no monitor stays; an up connection takes the monitor.
passive(Mandatory) ->
    Passive = [allow_passive_connect],
    io:format("passive idle: ~p ~p~n", [erlang:monitor_node('p@host', true, Passive), kernel_message()]),
    show("passive idle", collect(nodedown, 'p@host', 1)),
    io:format("passive twice: ~p ~p~n", [erlang:monitor_node('p@host', true, Passive ++ Passive), kernel_message()]),
    show("passive twice", collect(nodedown, 'p@host', 1)),
    io:format("passive off: ~p~n", [erlang:monitor_node('p@host', false, Passive)]),
    Handle = erts_internal:new_connection('w@host'),
    io:format("passive pending: ~p ~p~n", [erlang:monitor_node('w@host', true, Passive), kernel_message()]),
    show("passive pending", collect(nodedown, 'w@host', 1)),
    Controller = controller(),
    Handle = erlang:setnode('w@host', Controller, {Mandatory bor 1, 6}),
    {dist_ctrlr, Controller, 'w@host', _} = kernel_message(),
    io:format("passive up: ~p ~p ~p~n", [erlang:monitor_node('w@host', true, Passive), flush(), kernel_messages()]),
    Controller ! {stop, normal},
    show("passive up end", collect(nodedown, 'w@host', 1)),
    io:format("known: ~p~n", [[Node || Node <- ['p@host', 'w@host'], lists:member(Node, nodes(known))]]).

%% erlang:dmonitor_node/3: {nodedown, Node} at once whatever the arguments, after asking net_kernel when the options
%% hold allow_passive_connect.
dmonitor() ->
    Calls = [
        ['r@host', true, []],
        ['r@host', false, []],
        ['s@host', true, [bad, allow_passive_connect]],
        ['s@host', other, [bad]],
        ['a@host', true, []],
        ["r@host", true, []]
    ],
    [
        io:format("dmonitor_node ~w: ~p ~p ~p~n", [
            Call, apply(erlang, dmonitor_node, Call), collect(nodedown, hd(Call), 1), kernel_messages()
        ])
     || Call <- Calls
    ],
    ok.

%% Without a process registered as net_kernel, auto-connect aborts at once.
no_kernel(Kernel) ->
    unregister(net_kernel),
    io:format("no kernel: ~p~n", [monitor_node('t@host', true)]),
    show("no kernel", collect(nodedown, 't@host', 1)),
    io:format("known: ~p~n", [lists:member('t@host', nodes(known))]),
    register(net_kernel, Kernel).

%% net_kernel's end aborts the pending entries and kills the controllers; each monitor gets its nodedown.
monitors_kernel_end(Kernel, Mandatory) ->
    [monitor_node(Node, true) || Node <- ['u@host', 'u@host', 'v@host']],
    {auto_connect, 'u@host', _} = kernel_message(),
    {auto_connect, 'v@host', _} = kernel_message(),
    Handle = erts_internal:new_connection('y@host'),
    monitor_node('y@host', true),
    Controller = controller(),
    Handle = erlang:setnode('y@host', Controller, {Mandatory bor 1, 6}),
    monitor_node('y@host', true),
    {dist_ctrlr, Controller, 'y@host', _} = kernel_message(),
    Kernel ! {stop, gone},
    show("u down", collect(nodedown, 'u@host', 2)),
    show("v down", collect(nodedown, 'v@host', 1)),
    show("y down", collect(nodedown, 'y@host', 2)),
    wait_unnamed(1000),
    io:format("after: ~p ~p ~p~n", [node(), flush(), kernel_messages()]).

%% The messages the stand-in net_kernel passed on so far.
kernel_messages() ->
    receive
        {kernel, Message} -> [Message | kernel_messages()]
    after 0 -> []
    end.
