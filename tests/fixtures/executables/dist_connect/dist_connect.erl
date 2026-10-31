%% Connections between two nodes (plan 30 step DIST4b2a): node a connects to node b with a wrong cookie first, then
%% visibly, keeps the idle connection up over several tick times, disconnects and connects hidden. Node b reports the
%% node status messages it gets. Both nodes run with net_ticktime 3.
-module(dist_connect).
-export([main/1]).

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
    done().

% The name of the other node once it registered with epmd.
peer(Name) ->
    {ok, Host} = inet:gethostname(),
    wait_registered(Name),
    list_to_atom(Name ++ "@" ++ Host).

wait_registered(Name) ->
    {ok, Names} = net_adm:names(),
    case lists:keymember(Name, 1, Names) of
        true ->
            ok;
        false ->
            timer:sleep(100),
            wait_registered(Name)
    end.

% The next message, timeout when none comes.
next() ->
    receive
        Message -> Message
    after 30000 -> timeout
    end.

done() -> io:format("dist_connect: ok~n").

% The host name is HOST in the output.
show(Label, Term) ->
    {ok, Host} = inet:gethostname(),
    io:format("~s: ~s~n", [Label, string:replace(lists:flatten(io_lib:format("~p", [Term])), Host, "HOST", all)]).
