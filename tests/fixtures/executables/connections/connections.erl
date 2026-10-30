%% Connection entries of other nodes (plan 30 step DIST2b), as OTP allows them without distribution: a stand-in
%% net_kernel and controller processes; erts_internal:new_connection/1, erlang:setnode/3 and
%% abort_pending_connection/2, nodes/1,2, node status messages (process_flag(monitor_nodes, ...),
%% net_kernel:monitor_nodes/1,2) and the ends of controllers and of net_kernel. OTP's connection ids differ between
%% runs, so each prints as a label; messages of different nodes are taken node by node, as OTP sends them from
%% different threads.
-module(connections).
-export([main/1]).

main(["mandatory"]) ->
    mandatory();
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

%% A stand-in net_kernel that passes its messages on to the caller.
kernel() ->
    Self = self(),
    Kernel = spawn(fun Loop() ->
        receive
            {stop, Reason} ->
                exit(Reason);
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
