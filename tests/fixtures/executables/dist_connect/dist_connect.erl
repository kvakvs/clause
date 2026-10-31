%% Connections between two nodes (plan 30 step DIST4b2a): node a connects to node b with a wrong cookie first, then
%% visibly, keeps the idle connection up over several tick times, disconnects and connects hidden. Node b reports the
%% node status messages it gets. Both nodes run with net_ticktime 3.
%% The handshake statuses (DIST4b2b): nodes sa and sb connect at once; sb then runs raw handshakes against itself for
%% the statuses alive, nok, named: and not_allowed, and sa connects to a node allow/1 does not allow; epmd names
%% Node_ready tell each other they are subscribed. A node without a name (dynamic) takes one from node db, loses it
%% with that connection and gets it back.
-module(dist_connect).
-export([main/1]).

-define(DFLAG_PUBLISHED, 16#01).
-define(DFLAG_NAME_ME, (16#02 bsl 32)).

main(["a"]) ->
    B = peer("dcb"),
    ok = net_kernel:monitor_nodes(true, [nodedown_reason, {node_type, all}]),
    true = erlang:set_cookie(B, wrong),
    show("wrong cookie", [net_kernel:connect_node(B), nodes(connected)]),
    true = erlang:set_cookie(B, erlang:get_cookie()),
    show("connect", [net_kernel:connect_node(B), nodes(), nodes(hidden), nodes(connected)]),
    show("status", next()),
    show("again", net_kernel:connect_node(B)),
    true = monitor_node(B, true),
    % Ticks keep the idle connection up past its tick time.
    timer:sleep(7000),
    show("after ticks", nodes()),
    show("disconnect", [erlang:disconnect_node(B), nodes(connected)]),
    show("messages", [next(), next()]),
    show("disconnect again", [erlang:disconnect_node(B), erlang:disconnect_node(node())]),
    show("hidden", [net_kernel:hidden_connect_node(B), nodes(), nodes(hidden)]),
    show("status", next()),
    done();
main(["b"]) ->
    ok = net_kernel:monitor_nodes(true, [nodedown_reason, {node_type, all}]),
    lists:foreach(fun(_) -> show("status", next()) end, lists:seq(1, 4)),
    done();
main(["sa"]) ->
    ok = net_kernel:monitor_nodes(true, [nodedown_reason]),
    B = peer("dsb"),
    _Ready = ready("dsa", "dsb"),
    show("simultaneous", [net_kernel:connect_node(B), nodes()]),
    show("status", next()),
    % sb's handshake in sa's name replaces this connection.
    show("replaced", next()),
    show("allow", [net_kernel:allow([dsz@nohost, dsz@nohost]), net_kernel:allow([1]), net_kernel:allow(x)]),
    show("allowed", net_kernel:allowed()),
    show("disallowed", net_kernel:connect_node(B)),
    done();
main(["sb"]) ->
    ok = net_kernel:monitor_nodes(true, [nodedown_reason]),
    _Ready = ready("dsb", "dsa"),
    A = peer("dsa"),
    show("simultaneous", [net_kernel:connect_node(A), nodes()]),
    show("status", next()),
    show("alive false", raw(A, ?DFLAG_PUBLISHED, <<"false">>)),
    show("still", nodes()),
    {Status, Socket} = raw(A, ?DFLAG_PUBLISHED, <<"true">>),
    show("alive true", Status),
    show("messages", [next(), next()]),
    gen_tcp:close(Socket),
    show("closed", next()),
    show("own name", raw(node(), 0, none)),
    dynamic_names(),
    ok = net_kernel:allow([A]),
    show("not allowed", raw(dsz, 0, none)),
    done();
main(["db"]) ->
    ok = net_kernel:monitor_nodes(true, [nodedown_reason, {node_type, hidden}]),
    lists:foreach(fun(_) -> show("status", next()) end, lists:seq(1, 4)),
    done();
main(["dynamic"]) ->
    ok = net_kernel:monitor_nodes(true, [nodedown_reason, {node_type, all}]),
    B = peer("ddb"),
    show("unnamed", [node(), is_alive(), maps:with([name, name_type], net_kernel:get_state())]),
    show("connect", [net_kernel:connect_node(B), node(), nodes(), nodes(hidden)]),
    Name = node(),
    show("named", [maps:get(name, net_kernel:get_state()) =:= Name, next(), next()]),
    show("disconnect", [erlang:disconnect_node(B), next(), next()]),
    wait_restarted(),
    show("unnamed again", [node(), is_alive(), maps:with([name, name_type], net_kernel:get_state())]),
    show("connect again", [net_kernel:connect_node(B), node() =:= Name]),
    done().

% Dynamic names sb gives raw handshakes from another host: a name freed is given again with the next creation, and a
% name in use is not.
dynamic_names() ->
    {{named, Name, Creation}, First} = raw(name_me, ?DFLAG_NAME_ME, none),
    gen_tcp:close(First),
    wait_gone(Name),
    {{named, Again, Next}, Second} = raw(name_me, ?DFLAG_NAME_ME, none),
    {{named, Other, _}, Third} = raw(name_me, ?DFLAG_NAME_ME, none),
    show("named", [Name, Again =:= Name, Next =:= Creation + 1, Other =/= Name, Other]),
    gen_tcp:close(Second),
    gen_tcp:close(Third).

% A raw handshake with this node, sent as node Claimed or as a request for a dynamic name of otherhost, with the
% default flags and Flags. An alive status is answered with Answer. Returns the status and, for a connection that
% came up, its socket.
raw(Claimed, Flags, Answer) ->
    {ok, Names} = net_adm:names(),
    {_, Port} = lists:keyfind(hd(string:split(atom_to_list(node()), "@")), 1, Names),
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, Port, [binary, {active, false}, {packet, 2}]),
    {erts_dflags, Default, _, _, _, _} = erts_internal:get_dflags(),
    Name = claimed(Claimed),
    ok = gen_tcp:send(Socket, <<$N, (Default bor Flags):64, 77:32, (byte_size(Name)):16, Name/binary>>),
    raw_status(Socket, gen_tcp:recv(Socket, 0, 5000), Answer).

claimed(name_me) -> <<"otherhost">>;
claimed(dsz) -> atom_to_binary(node_of("dsz"));
claimed(Node) -> atom_to_binary(Node).

raw_status(Socket, {ok, <<"salive">>}, Answer) ->
    ok = gen_tcp:send(Socket, <<"s", Answer/binary>>),
    case Answer of
        <<"true">> -> {alive, challenge(Socket)};
        _ -> {alive, gen_tcp:recv(Socket, 0, 5000)}
    end;
raw_status(Socket, {ok, <<"snamed:", Length:16, Name:Length/binary, Creation:32>>}, _) ->
    {{named, binary_to_atom(Name), Creation}, challenge(Socket)};
raw_status(Socket, {ok, <<"s", Status/binary>>}, _) ->
    {binary_to_atom(Status), gen_tcp:recv(Socket, 0, 5000)}.

% The rest of the handshake: the reply to this node's challenge, then its ack; the socket of the connection.
challenge(Socket) ->
    {ok, <<$N, _:64, Challenge:32, _/binary>>} = gen_tcp:recv(Socket, 0, 5000),
    Cookie = atom_to_list(erlang:get_cookie()),
    ok = gen_tcp:send(Socket, <<$r, 5:32, (erlang:md5([Cookie, integer_to_list(Challenge)]))/binary>>),
    Ack = erlang:md5([Cookie, "5"]),
    {ok, <<$a, Ack/binary>>} = gen_tcp:recv(Socket, 0, 5000),
    Socket.

wait_gone(Name) ->
    case lists:member(Name, nodes(hidden)) of
        true ->
            timer:sleep(50),
            wait_gone(Name);
        false ->
            ok
    end.

% The dynamic node's distribution starts again once its name is lost.
wait_restarted() ->
    case whereis(net_kernel) of
        undefined ->
            timer:sleep(50),
            wait_restarted();
        _ ->
            ok
    end.

% Tell the other node this one subscribed to node status messages, by registering the epmd name Own_ready while the
% returned socket stays open, and wait for its own; both then connect at about the same time.
ready(Own, Other) ->
    Name = list_to_binary(Own ++ "_ready"),
    Port = list_to_integer(os:getenv("ERL_EPMD_PORT", "4369")),
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, Port, [binary, {active, false}]),
    Request = <<120, 1:16, 77, 0, 6:16, 5:16, (byte_size(Name)):16, Name/binary, 0:16>>,
    ok = gen_tcp:send(Socket, [<<(byte_size(Request)):16>>, Request]),
    {ok, <<_, 0, _/binary>>} = gen_tcp:recv(Socket, 0, 5000),
    wait_registered(Other ++ "_ready"),
    Socket.

% The name of the other node once it registered with epmd.
peer(Name) ->
    wait_registered(Name),
    node_of(Name).

node_of(Name) ->
    {ok, Host} = inet:gethostname(),
    list_to_atom(Name ++ "@" ++ Host).

wait_registered(Name) ->
    {ok, Names} = net_adm:names(),
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

done() -> io:format("dist_connect: ok~n").

% The host name is HOST and a dynamic name DYNAMIC in the output.
show(Label, Term) ->
    io:format("~s: ~s~n", [Label, mask(lists:flatten(io_lib:format("~p", [Term])))]).

mask(Text) ->
    {ok, Host} = inet:gethostname(),
    Hosted = string:replace(Text, Host, "HOST", all),
    re:replace(Hosted, "'?\\b[0-9A-Z]{6,13}@(HOST|otherhost)'?", "DYNAMIC@\\1", [global, {return, list}]).
