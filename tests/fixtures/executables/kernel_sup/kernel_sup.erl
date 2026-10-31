%% kernel_sup and the kernel services Clause runs as processes (plan 30 step DIST4b1d): its children after a first use,
%% with distribution started by net_kernel:start/1 or by --sname, and the program's halt when net_kernel ends, as an
%% OTP node halts when the kernel application ends. Only the children Clause has are shown.
-module(kernel_sup).
-export([main/1]).

main(["first"]) ->
    yes = global:register_name(first_name, self()),
    show("tree", tree()),
    show("kernel_safe_sup", supervisor:which_children(kernel_safe_sup)),
    show("registered", [
        is_pid(whereis(Name))
     || Name <- [kernel_sup, kernel_safe_sup, rex, global_name_server, global_group]
    ]),
    {ok, NetSup} = net_kernel:start([kfirst, shortnames]),
    show("started", tree()),
    show("net_sup", NetSup =:= whereis(net_sup)),
    ok = net_kernel:stop(),
    show("stopped", tree()),
    done();
main(["static"]) ->
    show("tree", tree()),
    show("net_sup", whereis(net_sup) =:= child_pid(net_sup)),
    done();
main(["kill_dynamic"]) ->
    {ok, _} = net_kernel:start([kdynamic, shortnames]),
    killed();
main(["kill_static"]) ->
    killed().

% Killing net_kernel ends net_sup, then kernel_sup, and the program halts before the wait ends.
killed() ->
    show("killing", is_pid(whereis(net_kernel))),
    exit(whereis(net_kernel), kill),
    receive
    after 10000 -> show("still running", true)
    end.

done() -> io:format("kernel_sup: ok~n").

% kernel_sup's children Clause has, in its order (the latest started first): id, type, running.
tree() ->
    Ids = [kernel_safe_sup, global_group, net_sup_dynamic, net_sup, global_name_server, rex],
    [{Id, Type, is_pid(Pid)} || {Id, Pid, Type, _} <- supervisor:which_children(kernel_sup), lists:member(Id, Ids)].

child_pid(Id) ->
    {Id, Pid, _, _} = lists:keyfind(Id, 1, supervisor:which_children(kernel_sup)),
    Pid.

show(Label, Term) -> io:format("~s: ~p~n", [Label, Term]).
