%% Clause's pg (plan 21 step LIB25, docs/libraries/library.md): process groups in scopes, each scope a data_publisher with
%% this module as its callback module. A scope's ETS table, named after it, holds {Group, Members, LocalMembers};
%% members are monitored and leave when they end, and monitors of a scope or group get {Ref, join | leave, Group,
%% Pids} messages. A Clause program is one node, so all members are local.
-module(pg).
-behaviour(data_publisher).
-export([
    start_link/0,
    join/2,
    leave/2,
    monitor_scope/0,
    monitor_scope/1,
    monitor/1,
    monitor/2,
    demonitor/1,
    demonitor/2,
    get_members/1,
    get_local_members/1,
    which_groups/0,
    which_local_groups/0
]).
-export([
    start/1, start_link/1, join/3, leave/3, get_members/2, get_local_members/2, which_groups/1, which_local_groups/1
]).
-export([
    version/0,
    init_global_view/2,
    init_local_data/1,
    stop_global_view/1,
    update_local_data/2,
    update_global_view_and_notify/4,
    data_diff/2,
    new_subscription/2,
    translate_update/3,
    translate_local_data/3,
    translate_message/2
]).

-define(DEFAULT_SCOPE, ?MODULE).

start_link() -> data_publisher:start_link(?DEFAULT_SCOPE, ?MODULE).

start(Scope) when is_atom(Scope) -> data_publisher:start(Scope, ?MODULE).

start_link(Scope) when is_atom(Scope) -> data_publisher:start_link(Scope, ?MODULE).

join(Group, PidOrPids) -> join(?DEFAULT_SCOPE, Group, PidOrPids).

join(Scope, Group, PidOrPids) when is_pid(PidOrPids); is_list(PidOrPids) ->
    ok = ensure_local(PidOrPids),
    data_publisher:update(Scope, [{Group, to_list(PidOrPids), []}]).

leave(Group, PidOrPids) -> leave(?DEFAULT_SCOPE, Group, PidOrPids).

%% not_joined when none of the processes is a member.
leave(Scope, Group, PidOrPids) when is_pid(PidOrPids); is_list(PidOrPids) ->
    ok = ensure_local(PidOrPids),
    Pids = to_list(PidOrPids),
    case Pids -- ets:lookup_element(Scope, Group, 2, []) of
        Pids -> not_joined;
        _ -> data_publisher:update(Scope, [{Group, [], Pids}])
    end.

monitor_scope() -> monitor_scope(?DEFAULT_SCOPE).

monitor_scope(Scope) -> data_publisher:subscribe(Scope, scope).

monitor(Group) -> ?MODULE:monitor(?DEFAULT_SCOPE, Group).

monitor(Scope, Group) -> data_publisher:subscribe(Scope, {group, Group}).

demonitor(Ref) -> ?MODULE:demonitor(?DEFAULT_SCOPE, Ref).

demonitor(Scope, Ref) ->
    data_publisher:unsubscribe(Scope, Ref),
    flush(Ref).

get_members(Group) -> get_members(?DEFAULT_SCOPE, Group).

get_members(Scope, Group) ->
    try
        ets:lookup_element(Scope, Group, 2, [])
    catch
        error:badarg -> []
    end.

get_local_members(Group) -> get_local_members(?DEFAULT_SCOPE, Group).

get_local_members(Scope, Group) ->
    try
        ets:lookup_element(Scope, Group, 3, [])
    catch
        error:badarg -> []
    end.

which_groups() -> which_groups(?DEFAULT_SCOPE).

which_groups(Scope) when is_atom(Scope) -> [Group || [Group] <- ets:match(Scope, {'$1', '_', '_'})].

which_local_groups() -> which_local_groups(?DEFAULT_SCOPE).

which_local_groups(Scope) when is_atom(Scope) -> ets:select(Scope, [{{'$1', '_', '$2'}, [{'=/=', '$2', []}], ['$1']}]).

%% data_publisher callbacks: the global view is {Scope, #{Pid => {MonitorRef, Groups}}}, the local data
%% #{Group => Pids}.

version() -> 1.

init_global_view(Scope, _) ->
    _ = ets:new(Scope, [set, protected, named_table, {read_concurrency, true}]),
    {Scope, #{}}.

init_local_data(_) -> #{}.

stop_global_view({Scope, _}) -> ets:delete(Scope).

update_local_data([], Local) ->
    Local;
update_local_data([{Group, Add, Remove} | Rest], Local) ->
    case Add ++ (maps:get(Group, Local, []) -- Remove) of
        [] -> update_local_data(Rest, maps:remove(Group, Local));
        Pids -> update_local_data(Rest, Local#{Group => Pids})
    end.

update_global_view_and_notify(_, [], _, View) ->
    View;
update_global_view_and_notify(Node, [{Group, Add, Remove} | Rest], Subscriptions, {Scope, Monitors}) ->
    Local = Node =:= node(),
    case ets:lookup(Scope, Group) of
        [{_, All, Mine}] ->
            case Add ++ (All -- Remove) of
                [] -> ets:delete(Scope, Group);
                NewAll -> ets:insert(Scope, {Group, NewAll, local_members(Local, Add, Mine, Remove)})
            end;
        [] ->
            ets:insert(Scope, {Group, Add, local_members(Local, Add, [], [])})
    end,
    _ = notify(Group, Add, Remove, Subscriptions),
    Monitored =
        case Local of
            true -> unwatch(Group, Remove, watch(Group, Add, Monitors));
            false -> Monitors
        end,
    update_global_view_and_notify(Node, Rest, Subscriptions, {Scope, Monitored}).

local_members(true, Add, Mine, Remove) -> Add ++ (Mine -- Remove);
local_members(false, _, Mine, _) -> Mine.

%% Members are monitored once, whatever the number of groups they joined.
watch(Group, Pids, Monitors) ->
    lists:foldl(
        fun(Pid, Acc) ->
            case Acc of
                #{Pid := {Ref, Groups}} ->
                    Acc#{Pid => {Ref, [Group | Groups]}};
                _ ->
                    Acc#{Pid => {erlang:monitor(process, Pid, [{tag, {'DOWN', ?MODULE}}]), [Group]}}
            end
        end,
        Monitors,
        Pids
    ).

unwatch(Group, Pids, Monitors) ->
    lists:foldl(
        fun(Pid, Acc) ->
            case Acc of
                #{Pid := {Ref, [Group]}} ->
                    erlang:demonitor(Ref),
                    maps:remove(Pid, Acc);
                #{Pid := {Ref, Groups}} ->
                    Acc#{Pid => {Ref, Groups -- [Group]}};
                _ ->
                    Acc
            end
        end,
        Monitors,
        Pids
    ).

data_diff(Old, New) ->
    [
        {Group, Add, Remove}
     || Group <- (maps:keys(Old) -- maps:keys(New)) ++ maps:keys(New),
        {Add, Remove} <- [diff(maps:get(Group, Old, []), maps:get(Group, New, []))],
        Add =/= [] orelse Remove =/= []
    ].

diff(Old, New) -> {New -- Old, Old -- New}.

new_subscription(scope, {Scope, _}) ->
    maps:from_list([{Group, Pids} || [Group, Pids] <- ets:match(Scope, {'$1', '$2', '_'})]);
new_subscription({group, Group}, {Scope, _}) ->
    get_members(Scope, Group).

%% Version 0 peers (OTP before 29) get join and leave messages.
translate_update(1, 0, Update) ->
    Leaves = maps:groups_from_list(fun({_, _, Remove}) -> Remove end, fun({Group, _, _}) -> Group end, Update),
    LeaveMessages = [{leave, self(), Remove, Groups} || Remove := Groups <- Leaves, Remove =/= []],
    JoinMessages = [{join, self(), Group, Add} || {Group, Add, _} <- Update, Add =/= []],
    {'$plain_messages', LeaveMessages ++ JoinMessages};
translate_update(1, _, Update) ->
    Update.

translate_local_data(1, 0, Local) -> {'$plain_message', {sync, self(), maps:to_list(Local)}};
translate_local_data(1, _, Local) -> Local.

translate_message({join, Peer, Group, PidOrPids}, View) ->
    {update, Peer, [{Group, to_list(PidOrPids), []}], View};
translate_message({leave, Peer, PidOrPids, Groups}, View) ->
    {update, Peer, [{Group, [], to_list(PidOrPids)} || Group <- Groups], View};
translate_message({{'DOWN', ?MODULE}, Ref, process, Pid, _}, {Scope, Monitors} = View) ->
    case Monitors of
        #{Pid := {Ref, Groups}} ->
            {update, [{Group, [], [Pid]} || Group <- Groups], {Scope, maps:remove(Pid, Monitors)}};
        _ ->
            {drop, View}
    end;
translate_message({sync, Peer, Sync}, View) ->
    {local_data, Peer, 0, maps:from_list(Sync), View};
translate_message({discover, Peer}, View) ->
    {discover, Peer, 0, View};
translate_message(_, _) ->
    error(badarg).

notify(Group, Add, Remove, Subscriptions) ->
    [notify(Group, Add, Remove, Ref, Pid) || Ref := Pid <- maps:get(scope, Subscriptions, #{})],
    [notify(Group, Add, Remove, Ref, Pid) || Ref := Pid <- maps:get({group, Group}, Subscriptions, #{})].

notify(Group, Add, Remove, Ref, Pid) ->
    Add =/= [] andalso erlang:send(Pid, {Ref, join, Group, Add}, [noconnect]),
    Remove =/= [] andalso erlang:send(Pid, {Ref, leave, Group, Remove}, [noconnect]).

to_list(Pids) when is_list(Pids) -> Pids;
to_list(Pid) -> [Pid].

ensure_local(Pid) when is_pid(Pid), node(Pid) =:= node() ->
    ok;
ensure_local(Pids) when is_list(Pids) ->
    lists:foreach(
        fun
            (Pid) when is_pid(Pid), node(Pid) =:= node() -> ok;
            (Bad) -> erlang:error({nolocal, Bad})
        end,
        Pids
    );
ensure_local(Bad) ->
    erlang:error({nolocal, Bad}).

flush(Ref) ->
    receive
        {Ref, Verb, _, _} when Verb =:= join; Verb =:= leave -> flush(Ref)
    after 0 -> ok
    end.
