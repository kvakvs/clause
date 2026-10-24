%% pg, data_publisher, global and global_group (plan 21 step LIB25) on a node that is not distributed: scopes,
%% joins and leaves, members that end, scope and group monitors, global names with gen_server {global, Name},
%% locks and transactions, and global_group without groups.
-module(process_groups).
-export([main/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

main(_) ->
    scopes(),
    groups(),
    monitors(),
    names(),
    servers(),
    locks(),
    groups_of_nodes(),
    io:format("process_groups: ok~n").

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

attempt(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

%% Pids printed as their positions in a list of known processes.
names_of(Pids, Known) -> [index(Pid, Known, 1) || Pid <- Pids].

index(Pid, [Pid | _], N) -> N;
index(Pid, [_ | Rest], N) -> index(Pid, Rest, N + 1);
index(_, [], _) -> unknown.

sleeper() ->
    spawn(fun() ->
        receive
            stop -> ok
        end
    end).

stop(Pid) ->
    Ref = monitor(process, Pid),
    Pid ! stop,
    receive
        {'DOWN', Ref, _, _, _} -> ok
    end.

%% A call to the scope process, so that it has handled earlier messages.
settle(Scope) -> sys:get_state(Scope).

scopes() ->
    show("no_scope", [
        attempt(fun() -> pg:join(group, []) end), pg:get_members(group), attempt(fun() -> pg:which_groups() end)
    ]),
    {ok, Default} = pg:start_link(),
    show("default", [is_pid(Default), whereis(pg) =:= Default, pg:which_groups()]),
    {ok, Scope} = pg:start(other_scope),
    show("start_again", [element(1, pg:start(other_scope)), element(1, pg:start_link(other_scope))]),
    show("scope", [whereis(other_scope) =:= Scope, ets:info(other_scope, name)]).

groups() ->
    A = sleeper(),
    B = sleeper(),
    C = sleeper(),
    Known = [self(), A, B, C],
    Members = fun(Group) -> lists:sort(names_of(pg:get_members(Group), Known)) end,
    show("join", [pg:join(g1, A), pg:join(g1, [B, C]), pg:join(g2, A), pg:join(g1, A)]),
    show("members", [Members(g1), Members(g2), Members(none)]),
    show("local", [lists:sort(names_of(pg:get_local_members(g1), Known)), lists:sort(pg:which_groups())]),
    show("local_groups", lists:sort(pg:which_local_groups())),
    show("leave", [pg:leave(g1, A), Members(g1), pg:leave(g1, self()), pg:leave(g1, [A, B])]),
    show("after_leave", [Members(g1), Members(g2)]),
    show("join_errors", [
        attempt(fun() -> pg:join(g1, not_a_pid) end),
        attempt(fun() -> pg:join(g1, [self(), x]) end),
        attempt(fun() -> pg:leave(g1, x) end)
    ]),
    show("other_scope", [pg:join(other_scope, g1, self()), names_of(pg:get_members(other_scope, g1), Known)]),
    show("scopes_apart", [
        Members(g1), pg:which_groups(other_scope), names_of(pg:get_local_members(other_scope, g1), Known)
    ]),
    stop(C),
    settle(pg),
    show("ended_member", [Members(g1), lists:sort(pg:which_groups())]),
    stop(A),
    settle(pg),
    show("group_gone", [Members(g2), lists:sort(pg:which_groups())]),
    stop(B),
    settle(pg),
    show("all_gone", lists:sort(pg:which_groups())),
    ok = pg:leave(other_scope, g1, self()),
    show("other_empty", pg:which_groups(other_scope)).

monitors() ->
    A = sleeper(),
    B = sleeper(),
    Known = [self(), A, B],
    ok = pg:join(watched, A),
    {ScopeRef, Initial} = pg:monitor_scope(),
    show("scope_view", maps:map(fun(_, Pids) -> names_of(Pids, Known) end, Initial)),
    {GroupRef, GroupView} = pg:monitor(watched),
    show("group_view", names_of(GroupView, Known)),
    ok = pg:join(watched, B),
    ok = pg:join(elsewhere, B),
    ok = pg:leave(watched, A),
    stop(B),
    settle(pg),
    Events = collect([ScopeRef, GroupRef], Known),
    show("scope_events", [E || {scope, E} <- Events]),
    show("group_events", [E || {group, E} <- Events]),
    show("demonitor", [pg:demonitor(ScopeRef), pg:demonitor(GroupRef)]),
    ok = pg:join(watched, A),
    settle(pg),
    show("after_demonitor", collect([ScopeRef, GroupRef], Known)),
    {OtherRef, []} = pg:monitor(other_scope, nothing),
    show("other_monitor", [pg:demonitor(other_scope, OtherRef), is_reference(OtherRef)]),
    stop(A).

collect([ScopeRef, GroupRef] = Refs, Known) ->
    receive
        {ScopeRef, Verb, Group, Pids} -> [{scope, {Verb, Group, names_of(Pids, Known)}} | collect(Refs, Known)];
        {GroupRef, Verb, Group, Pids} -> [{group, {Verb, Group, names_of(Pids, Known)}} | collect(Refs, Known)]
    after 50 -> []
    end.

names() ->
    A = sleeper(),
    show("empty", [global:registered_names(), global:whereis_name(nobody), global:unregister_name(nobody)]),
    show("register", [
        global:register_name(alpha, A), global:register_name(alpha, self()), global:whereis_name(alpha) =:= A
    ]),
    show("send", [global:send(alpha, hello) =:= A, attempt(fun() -> global:send(nobody, hello) end)]),
    show("re_register", [global:re_register_name(alpha, self()), global:whereis_name(alpha) =:= self()]),
    show("second_name", [
        global:register_name(beta, A), global:register_name(gamma, A), lists:sort(global:registered_names())
    ]),
    show("unregister", [global:unregister_name(alpha), lists:sort(global:registered_names())]),
    stop(A),
    show("ended", [global:whereis_name(beta), lists:sort(global:registered_names())]),
    show("sync", [global:sync(), global:disconnect()]),
    B = sleeper(),
    C = sleeper(),
    global:register_name(conflict, B, fun global:notify_all_name/3),
    show("resolvers", [
        global:notify_all_name(conflict, B, C),
        global:random_notify_name(conflict, B, C) =:= min(B, C)
    ]),
    show("external", [
        global:register_name_external(ext, C), global:whereis_name(ext) =:= C, global:unregister_name_external(ext)
    ]),
    stop(B),
    stop(C),
    flush().

flush() ->
    receive
        _ -> flush()
    after 0 -> ok
    end.

%% gen_server callbacks for the {global, Name} servers.
init(Value) -> {ok, Value}.

handle_call(get, _, Value) -> {reply, Value, Value}.

handle_cast({set, Value}, _) -> {noreply, Value}.

handle_info(_, Value) -> {noreply, Value}.

servers() ->
    {ok, Pid} = gen_server:start({global, counter}, ?MODULE, 1, []),
    show("global_server", [global:whereis_name(counter) =:= Pid, gen_server:call({global, counter}, get)]),
    ok = gen_server:cast({global, counter}, {set, 2}),
    show("cast", gen_server:call({global, counter}, get)),
    show("again", element(1, gen_server:start({global, counter}, ?MODULE, 1, []))),
    show("missing", [
        attempt(fun() -> gen_server:call({global, nobody}, get) end),
        gen_server:cast({global, nobody}, x)
    ]),
    ok = gen_server:stop({global, counter}),
    show("stopped", global:whereis_name(counter)).

locks() ->
    Id = {resource, self()},
    show("set_lock", [global:set_lock(Id), global:set_lock(Id), global:set_lock(Id, [node()], 0)]),
    Parent = self(),
    Other = spawn(fun() ->
        Parent ! {other, global:set_lock({resource, other}, [node()], 0)},
        receive
            stop -> ok
        end
    end),
    receive
        {other, Locked} -> show("held_elsewhere", Locked)
    end,
    show("del_lock", [global:del_lock(Id), global:del_lock(Id)]),
    stop(Other),
    show("free_again", [global:set_lock({resource, mine}, [node()], 0), global:del_lock({resource, mine})]),
    show("no_nodes", [global:set_lock({resource, x}, [], 0), global:set_lock({resource, x}, [other@host], 0)]),
    show("trans", [
        global:trans({t, self()}, fun() -> inside end),
        global:trans({t, self()}, fun() -> inner end, [node()]),
        global:trans({t, self()}, fun() -> retried end, [node()], 0),
        attempt(fun() -> global:trans({t, self()}, fun() -> error(failed) end) end),
        global:set_lock({t, x}, [node()], 0)
    ]),
    Holder = spawn(fun() ->
        true = global:set_lock({busy, holder}),
        Parent ! locked,
        receive
            stop -> ok
        end
    end),
    receive
        locked -> ok
    end,
    show("aborted", global:trans({busy, me}, fun() -> never end, [node()], 0)),
    stop(Holder),
    show("released_by_exit", global:trans({busy, me}, fun() -> now end, [node()], 0)).

groups_of_nodes() ->
    {ok, Pid} = gen_server:start({global, grouped}, ?MODULE, 0, []),
    show("global_group", [
        global_group:global_groups(),
        global_group:own_nodes() =:= [node()],
        global_group:sync(),
        global_group:monitor_nodes(true),
        global_group:monitor_nodes(false)
    ]),
    show("info", global_group:info()),
    show("group_names", [
        global_group:registered_names({node, node()}),
        global_group:registered_names({node, other@host}),
        global_group:registered_names({group, g})
    ]),
    show("group_where", [
        global_group:whereis_name(grouped) =:= Pid,
        global_group:whereis_name({node, node()}, grouped) =:= Pid,
        global_group:whereis_name({group, g}, grouped),
        global_group:whereis_name(nobody)
    ]),
    show("group_send", [
        global_group:send(grouped, hi) =:= Pid,
        global_group:send(nobody, hi),
        global_group:send({node, other@host}, grouped, hi)
    ]),
    ok = gen_server:stop(Pid).
