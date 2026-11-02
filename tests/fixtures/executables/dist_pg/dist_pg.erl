%% pg across nodes (plan 30 step DIST9, docs/distribution.md#pg): b's scopes hold members before a connects; a's scopes
%% discover b's at nodeup and at their start, a's scope and group monitors hear b's members join, leave and end, a's
%% joins reach b, and a disconnecting b drops b's members until a reconnects. Members print as their registered names.
%% Only exported functions travel, so any node may be an OTP node.
-module(dist_pg).
-export([main/1]).
-export([member/0, join/2, leave/2, stop/1, names/0, view/1, finish/0]).

%% kernel's start_pg (DIST13): the default scope runs under kernel_safe_sup and serves the scope-less functions.
main(["start_pg"]) ->
    io:format("join: ~p~n", [pg:join(g, self())]),
    io:format("members: ~p~n", [pg:get_members(g) =:= [self()]]),
    io:format("groups: ~p~n", [pg:which_groups()]),
    Ids = [Id || {Id, _, _, _} <- supervisor:which_children(kernel_safe_sup)],
    io:format("child: ~p~n", [lists:member(pg, Ids)]),
    io:format("leave: ~p~n", [pg:leave(g, self())]),
    finish();
%% Without it there is no default scope.
main(["no_pg"]) ->
    io:format("members: ~p~n", [pg:get_members(g)]),
    io:format("join: ~p~n", [catch_exit(fun() -> pg:join(g, self()) end)]),
    io:format("scope: ~p~n", [whereis(pg)]),
    finish();
main(["a"]) ->
    B = 'dpgb@localhost',
    {ok, _} = pg:start_link(),
    put(names, [{a1, start_member(a1)}, {a2, start_member(a2)}]),
    ok = pg:join(g1, whereis(a1)),
    {ScopeRef, Alone} = pg:monitor_scope(),
    {GroupRef, []} = pg:monitor(g2),
    show("alone", label(maps:to_list(Alone))),
    wait_registered("dpgb_ready"),
    show("ping", net_adm:ping(B)),
    put(names, get(names) ++ rpc:call(B, ?MODULE, names, [])),
    show("discovered", collect(ScopeRef, 2)),
    show("discovered_g2", collect(GroupRef, 1)),
    show("members", view(pg)),
    show("local", [{Group, label(pg:get_local_members(Group))} || Group <- [g1, g2]]),
    show("nolocal", attempt(fun() -> pg:join(g1, name(b1)) end)),
    relayed(B, ScopeRef, GroupRef),
    other_scope(),
    show("demonitor", pg:demonitor(GroupRef)),
    show("ended", rpc:call(B, ?MODULE, stop, [b1])),
    show("ended_updates", collect(ScopeRef, 2)),
    disconnected(B, ScopeRef),
    show("halted", rpc:call(B, ?MODULE, finish, [])),
    show("halted_dropped", collect(ScopeRef, 1)),
    show("unexpected", receive_all()),
    done();
main(["b"]) ->
    {ok, _} = pg:start_link(),
    {ok, _} = pg:start(other),
    [start_member(Name) || Name <- [b1, b2, b3]],
    ok = pg:join(g1, whereis(b1)),
    ok = pg:join(g1, whereis(b2)),
    ok = pg:join(g2, whereis(b2)),
    ok = pg:join(other, g1, whereis(b3)),
    _Ready = mark("dpgb_ready"),
    receive
    after infinity -> ok
    end.

%% b's joins and leaves reach a's monitors, a's join reaches b.
relayed(B, ScopeRef, GroupRef) ->
    show("join_b", rpc:call(B, ?MODULE, join, [g2, b1])),
    show("joined", collect(ScopeRef, 1)),
    show("joined_g2", collect(GroupRef, 1)),
    show("leave_b", rpc:call(B, ?MODULE, leave, [g1, b2])),
    show("left", collect(ScopeRef, 1)),
    show("not_joined_b", rpc:call(B, ?MODULE, leave, [g1, b2])),
    ok = pg:join(g2, whereis(a2)),
    show("joined_a", collect(ScopeRef, 1)),
    show("joined_a_g2", collect(GroupRef, 1)),
    wait_until(fun() -> length(rpc:call(B, pg, get_members, [g2])) =:= 3 end),
    show("members_b", label(rpc:call(B, ?MODULE, view, [pg]))),
    show("groups", lists:sort(pg:which_groups())),
    show("local_groups", lists:sort(pg:which_local_groups())).

%% A scope started after the connection asks the connected nodes' scopes of its name.
other_scope() ->
    {ok, _} = pg:start(other),
    wait_until(fun() -> pg:get_members(other, g1) =/= [] end),
    show("other", view(other)),
    show("other_local", pg:which_local_groups(other)),
    {Ref, Members} = pg:monitor(other, g1),
    show("other_monitor", label(Members)),
    show("other_demonitor", pg:demonitor(other, Ref)).

%% A lost connection drops b's members; connecting again brings them back.
disconnected(B, ScopeRef) ->
    ok = net_kernel:monitor_nodes(true),
    show("disconnect", erlang:disconnect_node(B)),
    receive
        {nodedown, B} -> ok
    end,
    show("dropped", collect(ScopeRef, 1)),
    show("members_alone", view(pg)),
    show("other_alone", view(other)),
    show("ping_again", net_adm:ping(B)),
    show("rediscovered", collect(ScopeRef, 1)),
    wait_until(fun() -> length(rpc:call(B, pg, get_members, [g2])) =:= 2 end),
    show("members_again", view(pg)),
    show("members_b_again", label(rpc:call(B, ?MODULE, view, [pg]))).

%% Group members: registered processes that wait to be stopped.
start_member(Name) ->
    Pid = spawn(?MODULE, member, []),
    true = register(Name, Pid),
    Pid.

member() ->
    receive
        {stop, From} -> From ! {stopped, self()}
    end.

%% b's actions for a, by member name.
join(Group, Name) -> pg:join(Group, whereis(Name)).

leave(Group, Name) -> pg:leave(Group, whereis(Name)).

stop(Name) ->
    Pid = whereis(Name),
    Ref = erlang:monitor(process, Pid),
    Pid ! {stop, self()},
    receive
        {'DOWN', Ref, process, Pid, Reason} -> Reason
    end.

names() -> [{Name, whereis(Name)} || Name <- [b1, b2, b3]].

%% A scope's groups and members, sorted by group.
view(Scope) -> lists:sort([{Group, pg:get_members(Scope, Group)} || Group <- pg:which_groups(Scope)]).

%% Notifications of a monitor: Count of them, sorted, as messages of different groups may come in any order.
collect(Ref, Count) -> lists:sort([receive_update(Ref) || _ <- lists:seq(1, Count)]).

receive_update(Ref) ->
    receive
        {Ref, Verb, Group, Pids} -> {Verb, Group, label(Pids)}
    after 5000 -> timeout
    end.

%% Pids replaced by the names of a's and b's members, also inside lists and tuples.
label(Pid) when is_pid(Pid) ->
    case lists:keyfind(Pid, 2, get(names)) of
        {Name, _} -> Name;
        false -> {unknown, node(Pid)}
    end;
label(List) when is_list(List) ->
    [label(Element) || Element <- List];
label(Tuple) when is_tuple(Tuple) ->
    list_to_tuple(label(tuple_to_list(Tuple)));
label(Other) ->
    Other.

name(Name) -> element(2, lists:keyfind(Name, 1, get(names))).

%% b's end: its standard output is its own, whatever the caller's group leader is.
% The exit reason of a call that exits, without its call details.
catch_exit(Fun) ->
    try Fun() of
        Result -> Result
    catch
        exit:{Reason, _} -> {exit, Reason}
    end.

finish() ->
    io:format(user, "dist_pg: ok~n", []),
    erlang:halt().

show(Label, Value) -> io:format("~s: ~p~n", [Label, label(Value)]).

attempt(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

receive_all() ->
    receive
        Message -> [Message | receive_all()]
    after 100 -> []
    end.

wait_until(Condition) -> wait_until(Condition, 500).

wait_until(_, 0) ->
    timeout;
wait_until(Condition, Tries) ->
    case Condition() of
        true ->
            ok;
        false ->
            timer:sleep(20),
            wait_until(Condition, Tries - 1)
    end.

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

done() -> io:format("dist_pg: ok~n").
