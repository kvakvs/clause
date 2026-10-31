%% Distribution started from the node start options before the entry runs, as OTP's kernel starts it at boot
%% (plan 30 step DIST4b1b): the node's name, state and registration, stop/0 refused, start/1 already started.
-module(dist_node).
-export([main/1]).

main(_) ->
    show("node", {node(), is_alive(), node(self())}),
    show("state", net_kernel:get_state()),
    show("requests", [net_kernel:get_net_ticktime(), net_kernel:longnames()]),
    show("processes", [is_pid(whereis(Name)) || Name <- [net_sup, net_kernel, erl_epmd]]),
    show("registered", name_registered()),
    show("stop", net_kernel:stop()),
    show("start", already(net_kernel:start([other, shortnames]))),
    show("still", node()),
    io:format("dist_node: ok~n").

already({error, {already_started, Pid}}) -> {already_started, Pid =:= whereis(net_sup)};
already(Other) -> Other.

% Whether epmd holds this node's name; the nodes of the run share one epmd.
name_registered() ->
    {ok, Names} = erl_epmd:names(),
    [Alive, _] = string:split(atom_to_list(node()), "@"),
    lists:keymember(Alive, 1, Names).

% The host name is HOST in the output; maps print in key order (~kp), as OTP prints atom keys in an order that varies.
show(Label, Term) ->
    {ok, Host} = inet:gethostname(),
    io:format("~s: ~s~n", [Label, string:replace(lists:flatten(io_lib:format("~kp", [Term])), Host, "HOST", all)]).
