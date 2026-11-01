%% net_kernel complete (plan 30 step DIST6). Node a connects to node b and reads the connection's information
%% (node_info/1,2, nodes_info/0, i/0,1) and socket options (getopts/2, setopts/2, also of connections to come), spawns
%% through b's net_kernel, changes its tick time, and runs under dist_auto_connect once: after a disconnect, an
%% auto-connect to b is barred until a explicit connect, and b connects back with a passive node monitor. Node a then
%% connects to the hidden node h, allows only b and h, and sees b and h stop. Node status messages show on every node.
%% The unalive run calls the same functions without distribution.
-module(dist_net_kernel).
-export([main/1, child/1]).

main(["a"]) ->
    ok = net_kernel:monitor_nodes(true, [nodedown_reason, {node_type, all}]),
    ok = net_kernel:monitor_nodes(true),
    B = peer("nkb"),
    wait_registered("nkb_ready"),
    show("connect", [net_kernel:connect_node(B), nodes()]),
    show("status", statuses(2)),
    info(B),
    options(B),
    show("spawn", spawn_through(B)),
    show("verbose", [net_kernel:verbose(1), net_kernel:verbose(0)]),
    show("kernel_apply", net_kernel:kernel_apply(erlang, node, [])),
    ticktime(),
    auto_connect_once(B),
    H = peer("nkh"),
    wait_registered("nkh_ready"),
    show("hidden", [net_kernel:connect_node(H), nodes(), nodes(hidden)]),
    show("status", next()),
    {ok, Infos} = net_kernel:nodes_info(),
    show("nodes_info", lists:sort([{Node, Type} || {Node, Info} <- Infos, {type, Type} <- Info])),
    show("allow", [net_kernel:allow([B, H]), net_kernel:allowed(), net_kernel:connect_node(node_of("nkz"))]),
    {nkb, B} ! stop,
    show("stopped", statuses(2)),
    {nkh, H} ! stop,
    show("stopped", next()),
    done();
main(["b"]) ->
    ok = net_kernel:monitor_nodes(true, [nodedown_reason]),
    register(nkb, self()),
    _Ready = mark("nkb_ready"),
    A = node_of("nka"),
    % Up, disconnected, connected again, disconnected again.
    lists:foreach(fun(_) -> show("status", next()) end, lists:seq(1, 4)),
    show("passive", [erlang:monitor_node(A, true, [allow_passive_connect]), nodes()]),
    show("status", next()),
    receive
        stop -> done()
    end;
main(["h"]) ->
    ok = net_kernel:monitor_nodes(true, [{node_type, all}]),
    register(nkh, self()),
    _Ready = mark("nkh_ready"),
    show("status", next()),
    show("nodes", [nodes(), nodes(hidden)]),
    receive
        stop -> done()
    end;
main(["unalive"]) ->
    show("ticktime", [net_kernel:get_net_ticktime(), net_kernel:set_net_ticktime(5), net_kernel:set_net_ticktime(5, 0)]),
    show("ticktime errors", [
        error_of(fun() -> net_kernel:set_net_ticktime(x) end),
        error_of(fun() -> net_kernel:set_net_ticktime(0) end),
        error_of(fun() -> net_kernel:set_net_ticktime(5, -1) end)
    ]),
    show("options", [
        net_kernel:getopts(nkb@localhost, [nodelay]),
        net_kernel:setopts(new, [{nodelay, true}]),
        error_of(fun() -> net_kernel:setopts(new, nodelay) end),
        error_of(fun() -> net_kernel:getopts("nkb", []) end)
    ]),
    show("verbose", [net_kernel:verbose(1), error_of(fun() -> net_kernel:verbose(x) end)]),
    show("calls", [net_kernel:kernel_apply(erlang, node, []), net_kernel:async_disconnect(nkb@localhost)]),
    show("info", [
        error_of(fun() -> net_kernel:node_info(nkb@localhost) end),
        error_of(fun() -> net_kernel:node_info(nkb@localhost, type) end),
        error_of(fun() -> net_kernel:nodes_info() end),
        error_of(fun() -> net_kernel:i() end),
        error_of(fun() -> net_kernel:i(nkb@localhost) end)
    ]),
    show("connecttime", [connecttime(Time) || Time <- [unset, 2, 0.5, 500, 0, x]]),
    done().

% The connection to B as node_info/1,2 and i/0,1 give it: address and counts vary, so they show as shapes.
info(B) ->
    {ok, Info} = net_kernel:node_info(B),
    {address, {net_address, {IP, Port}, Host, Protocol, Family}} = lists:keyfind(address, 1, Info),
    show("node_info", [
        [Key || {Key, _} <- Info],
        is_pid(element(2, lists:keyfind(owner, 1, Info))),
        [IP, is_integer(Port), Host, Protocol, Family],
        [is_integer(Count) andalso Count > 0 || {Key, Count} <- Info, Key =:= in orelse Key =:= out]
    ]),
    show("node_info keys", [net_kernel:node_info(B, Key) || Key <- [state, type, foo]]),
    show("node_info nodes", [
        net_kernel:node_info(node_of("nope")), net_kernel:node_info(node()), net_kernel:node_info("x")
    ]),
    {ok, Infos} = net_kernel:nodes_info(),
    show("nodes_info", [Node || {Node, _} <- Infos]),
    % i/0,1 print the counts and the address, which the golden's stdout masks.
    show("i", [net_kernel:i(), net_kernel:i(B)]),
    show("i none", net_kernel:i(node_of("nope"))).

% Socket options of the connection, of connections to come, and the kernel parameters these change.
options(B) ->
    show("getopts", net_kernel:getopts(B, [active, packet, mode, nodelay])),
    show("setopts", [
        net_kernel:setopts(B, [{nodelay, false}]),
        net_kernel:getopts(B, [nodelay]),
        net_kernel:setopts(B, [{packet, 2}]),
        net_kernel:getopts(node_of("nope"), [nodelay]),
        net_kernel:getopts(new, [nodelay]),
        error_of(fun() -> net_kernel:setopts(B, nodelay) end)
    ]),
    show("setopts new", [
        net_kernel:setopts(new, [{nodelay, false}, {sndbuf, 9000}]),
        net_kernel:setopts(new, [{active, true}]),
        net_kernel:setopts(new, [{sndbuf, 10000}])
    ]),
    show("kernel", [
        application:get_env(kernel, Key)
     || Key <- [inet_dist_connect_options, inet_dist_listen_options, dist_nodelay]
    ]).

% A process of B's spawned by its net_kernel, the old spawn/4 protocol: it answers with its pid and runs child/1.
spawn_through(B) ->
    Pid = gen_server:call({net_kernel, B}, {spawn, ?MODULE, child, [self()], group_leader()}),
    [
        node(Pid) =:= B,
        receive
            {child, Pid, Node} -> Node
        after 30000 -> timeout
        end
    ].

child(Parent) -> Parent ! {child, self(), node()}.

% A longer tick time takes effect when asked, while the transition period runs; a shorter one with no period at once.
ticktime() ->
    show("ticktime", [net_kernel:get_net_ticktime(), net_kernel:set_net_ticktime(3)]),
    show("longer", [net_kernel:set_net_ticktime(4, 1), net_kernel:get_net_ticktime(), net_kernel:set_net_ticktime(9)]),
    timer:sleep(2000),
    show("longer after", net_kernel:get_net_ticktime()),
    show("shorter", net_kernel:set_net_ticktime(3, 0)),
    timer:sleep(200),
    show("shorter after", net_kernel:get_net_ticktime()).

% dist_auto_connect once: a disconnected node is no longer auto-connected; connect_node/1 connects it, and it may
% connect to this node.
auto_connect_once(B) ->
    show("disconnect", [erlang:disconnect_node(B), statuses(2)]),
    show("barred", [monitor_node(B, true), next(), nodes()]),
    show("explicit", [net_kernel:connect_node(B), statuses(2)]),
    show("async", [net_kernel:async_disconnect(B), statuses(2)]),
    % b connects with its passive node monitor.
    show("passive", [statuses(2), nodes()]).

% Count node status messages; their order between the two subscriptions is not given.
statuses(Count) -> lists:sort([next() || _ <- lists:seq(1, Count)]).

connecttime(unset) ->
    ok = application:unset_env(kernel, net_setuptime),
    net_kernel:connecttime();
connecttime(Time) ->
    ok = application:set_env(kernel, net_setuptime, Time),
    net_kernel:connecttime().

error_of(Fun) ->
    try Fun() of
        Result -> Result
    catch
        error:Reason -> {error, Reason}
    end.

% Register the epmd name Marker, which the other node waits for, while the returned socket stays open.
mark(Marker) ->
    Name = list_to_binary(Marker),
    Port = list_to_integer(os:getenv("ERL_EPMD_PORT", "4369")),
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, Port, [binary, {active, false}]),
    Request = <<120, 1:16, 77, 0, 6:16, 5:16, (byte_size(Name)):16, Name/binary, 0:16>>,
    ok = gen_tcp:send(Socket, [<<(byte_size(Request)):16>>, Request]),
    {ok, <<_, 0, _/binary>>} = gen_tcp:recv(Socket, 0, 5000),
    Socket.

% The name of the other node once it registered with epmd.
peer(Name) ->
    wait_registered(Name),
    node_of(Name).

% Test nodes are named @localhost: they listen on loopback only.
node_of(Name) -> list_to_atom(Name ++ "@localhost").

wait_registered(Name) ->
    {ok, Names} = net_adm:names(localhost),
    case lists:keymember(Name, 1, Names) of
        true ->
            ok;
        false ->
            timer:sleep(10),
            wait_registered(Name)
    end.

% The next message, timeout when none comes.
next() ->
    receive
        Message -> Message
    after 30000 -> timeout
    end.

done() -> io:format("dist_net_kernel: ok~n").

show(Label, Term) -> io:format("~s: ~p~n", [Label, Term]).
