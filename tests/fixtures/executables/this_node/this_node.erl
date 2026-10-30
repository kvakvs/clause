%% This node's name (plan 30 step DIST2a): erlang:setnode/2 under a process registered as net_kernel, what reads the
%% name (node/0,1, is_alive/0, erts_internal:get_creation/0, nodes/1,2, encodings, monitors by name, ets:info/2,
%% spawn/2, monitor_node/2,3) and the name's end with net_kernel.
-module(this_node).
-export([main/1]).

main(_) ->
    unnamed(),
    io:format("without net_kernel: ~p~n", [safe(fun() -> erlang:setnode('a@host', 7) end)]),
    Kernel = kernel(),
    refused(),
    named(),
    readers(),
    monitors(),
    again(Kernel),
    io:format("this_node: ok~n").

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

%% What a node without a name answers.
unnamed() ->
    io:format("unnamed: ~p ~p ~p ~p~n", [node(), is_alive(), erts_internal:get_creation(), nodes(this)]),
    erts_internal:dynamic_node_name(true),
    io:format("dynamic name: ~p~n", [is_alive()]),
    erts_internal:dynamic_node_name(false),
    Calls = [
        [nonode@nohost, true],
        [nonode@nohost, false],
        [nonode@nohost, true, [allow_passive_connect]],
        [nonode@nohost, true, [other]],
        [nonode@nohost, other],
        [other@host, true],
        [other@host, false],
        [other@host, other],
        ["other@host", true]
    ],
    [io:format("monitor_node ~w: ~p~n", [Call, safe(fun() -> apply(erlang, monitor_node, Call) end)]) || Call <- Calls],
    io:format("messages: ~p~n", [flush()]).

%% A process registered as net_kernel, which setnode/2 requires.
kernel() ->
    Self = self(),
    Kernel = spawn(fun() ->
        register(net_kernel, self()),
        Self ! registered,
        receive
            stop -> ok
        end
    end),
    receive
        registered -> Kernel
    end.

%% Names and creations setnode/2 refuses.
refused() ->
    Calls = [
        {name, 7},
        {'@host', 7},
        {'name@', 7},
        {'a@b@c', 7},
        {'a.b@host', 7},
        {nonode@nohost, 7},
        {"a@host", 7},
        {'a@host', -1},
        {'a@host', 1 bsl 59},
        {'a@host', 1.0},
        {'a@host', creation}
    ],
    [
        io:format("refused ~w: ~p~n", [Call, safe(fun() -> erlang:setnode(Node, Creation) end)])
     || {Node, Creation} = Call <- Calls
    ],
    io:format("still unnamed: ~p ~p~n", [node(), is_alive()]).

%% Naming the node: the creation is cut to 32 bits; a named node cannot be named again.
named() ->
    io:format("setnode: ~p~n", [erlang:setnode('a_1-B@host.example', (1 bsl 32) + 7)]),
    io:format("named: ~p ~p ~p ~p~n", [node(), node(self()), is_alive(), erts_internal:get_creation()]),
    io:format("again: ~p~n", [safe(fun() -> erlang:setnode('a_1-B@host.example', 7) end)]),
    io:format("other name: ~p~n", [safe(fun() -> erlang:setnode('b@host', 7) end)]).

%% The node name and creation of an encoded pid or reference.
encoded(Pid) when is_pid(Pid) ->
    <<131, 88, 119, Length, Node:Length/binary, _:64, Creation:32>> = term_to_binary(Pid),
    {binary_to_atom(Node), Creation};
encoded(Ref) when is_reference(Ref) ->
    <<131, 90, _:16, 119, Length, Node:Length/binary, Creation:32, _/binary>> = term_to_binary(Ref),
    {binary_to_atom(Node), Creation}.

%% Self's encoding with another creation, decoded.
with_creation(Creation) ->
    <<Head:3/binary, Length, Rest/binary>> = term_to_binary(self()),
    <<Node:Length/binary, Numbers:8/binary, _:32>> = Rest,
    binary_to_term(<<Head/binary, Length, Node/binary, Numbers/binary, Creation:32>>).

%% What reads the name.
readers() ->
    io:format("encoded: ~p ~p~n", [encoded(self()), encoded(make_ref())]),
    Same = [with_creation(Creation) =:= self() || Creation <- [0, 7, 8]],
    Other = with_creation(8),
    io:format("decoded: ~p ~p ~p~n", [Same, node(Other), is_pid(Other)]),
    io:format("nodes: ~p ~p ~p~n", [
        nodes(), nodes(this), nodes([this, visible], #{node_type => true, connection_id => true})
    ]),
    Table = ets:new(table, []),
    io:format("ets node: ~p~n", [ets:info(Table, node)]),
    Self = self(),
    spawn(node(), fun() -> Self ! spawned end),
    receive
        spawned -> io:format("spawned on this node~n")
    end,
    register(main, self()),
    {main, node()} ! by_name,
    receive
        by_name -> io:format("sent by name~n")
    end,
    unregister(main),
    Calls = [[node(), true], [node(), false], [other@host, false], [node(), true, [allow_passive_connect]]],
    [io:format("monitor_node ~w: ~p~n", [Call, safe(fun() -> apply(erlang, monitor_node, Call) end)]) || Call <- Calls],
    io:format("messages: ~p~n", [flush()]).

%% Monitors by name name this node.
monitors() ->
    Self = self(),
    Pid = spawn(fun() ->
        register(watched, self()),
        Self ! registered,
        receive
            stop -> ok
        end
    end),
    receive
        registered -> ok
    end,
    Ref = monitor(process, {watched, node()}),
    io:format("monitors: ~p~n", [process_info(self(), monitors)]),
    Pid ! stop,
    receive
        {'DOWN', Ref, process, Item, Reason} -> io:format("down: ~p ~p~n", [Item, Reason])
    end.

%% net_kernel's end takes the name; a new net_kernel names the node again, with a creation of at least 4.
again(Kernel) ->
    Ref = monitor(process, Kernel),
    Kernel ! stop,
    receive
        {'DOWN', Ref, process, Kernel, _} -> ok
    end,
    wait_unnamed(1000),
    io:format("unnamed again: ~p ~p ~p ~p~n", [node(), is_alive(), erts_internal:get_creation(), encoded(self())]),
    io:format("without net_kernel: ~p~n", [safe(fun() -> erlang:setnode('a@host', 7) end)]),
    register(net_kernel, self()),
    io:format("setnode: ~p ~p ~p~n", [erlang:setnode('a@host', 1), node(), erts_internal:get_creation()]),
    io:format("again: ~p~n", [safe(fun() -> erlang:setnode('a@host', 1) end)]).

%% OTP takes the name after net_kernel ended, not at once.
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
