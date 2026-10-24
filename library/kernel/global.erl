%% Clause's global (plan 21 step LIB25, docs/library.md): global names and locks as on an OTP node that is not
%% distributed. The server global_name_server starts at the first use; the ETS tables global_names
%% ({Name, Pid, Method, Ref}), global_pid_names ({Pid, Name}) and global_locks ({ResourceId, RequesterId,
%% [{Pid, Ref}]}) are OTP's. Names and locks of a process go when it ends. Other nodes are never reachable, so
%% lock requests for them succeed at once and name conflicts never arise.
-module(global).
-behaviour(gen_server).
-export([
    start/0,
    start_link/0,
    stop/0,
    sync/0,
    sync/1,
    whereis_name/1,
    register_name/2,
    register_name/3,
    register_name_external/2,
    register_name_external/3,
    unregister_name_external/1,
    re_register_name/2,
    re_register_name/3,
    unregister_name/1,
    registered_names/0,
    send/2,
    set_lock/1,
    set_lock/2,
    set_lock/3,
    del_lock/1,
    del_lock/2,
    trans/2,
    trans/3,
    trans/4,
    random_exit_name/3,
    random_notify_name/3,
    notify_all_name/3,
    disconnect/0
]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2, code_change/3, resolve_it/4, get_locker/0]).
-export([info/0]).

-define(SERVER, global_name_server).

%% The server.

start() -> gen_server:start({local, ?SERVER}, ?MODULE, [], []).

start_link() -> gen_server:start_link({local, ?SERVER}, ?MODULE, [], []).

stop() -> gen_server:call(?SERVER, stop, infinity).

%% Starts the server at the first use, unlinked from the caller.
ensure_started() ->
    case whereis(?SERVER) of
        undefined ->
            case start() of
                {ok, _} -> ok;
                {error, {already_started, _}} -> ok
            end;
        _ ->
            ok
    end.

call(Request) ->
    ensure_started(),
    gen_server:call(?SERVER, Request, infinity).

init([]) ->
    process_flag(trap_exit, true),
    _ = ets:new(global_names, [set, named_table, protected]),
    _ = ets:new(global_pid_names, [bag, named_table, protected]),
    _ = ets:new(global_locks, [set, named_table, protected]),
    {ok, #{}}.

%% The state maps a monitor reference to the process it watches.
handle_call({register, Name, Pid, Method}, _, Monitors) ->
    {reply, yes, register(Name, Pid, Method, Monitors)};
handle_call({register_new, Name, Pid, Method}, _, Monitors) ->
    case where(Name) =:= undefined andalso check_dupname(Name, Pid) of
        true -> {reply, yes, register(Name, Pid, Method, Monitors)};
        false -> {reply, no, Monitors}
    end;
handle_call({unregister, Name}, _, Monitors) ->
    {reply, ok, unregister(Name, Monitors)};
handle_call({set_lock, Id, Pid}, _, Monitors) ->
    {Reply, Next} = set_local_lock(Id, Pid, Monitors),
    {reply, Reply, Next};
handle_call({del_lock, Id, Pid}, _, Monitors) ->
    {reply, true, del_local_lock(Id, Pid, Monitors)};
handle_call(sync, _, Monitors) ->
    {reply, ok, Monitors};
handle_call(disconnect, _, Monitors) ->
    {reply, [], Monitors};
handle_call(stop, _, Monitors) ->
    {stop, normal, stopped, Monitors}.

handle_cast(_, Monitors) -> {noreply, Monitors}.

%% A process with names or locks ended.
handle_info({'DOWN', Ref, process, Pid, _}, Monitors) ->
    _ = [ets:delete(global_names, Name) || {_, Name} <- ets:lookup(global_pid_names, Pid)],
    ets:delete(global_pid_names, Pid),
    _ = [del_lock_entry(Entry, Pid) || Entry <- ets:tab2list(global_locks)],
    {noreply, maps:remove(Ref, Monitors)};
handle_info(_, Monitors) ->
    {noreply, Monitors}.

terminate(_, _) -> ok.

code_change(_, Monitors, _) -> {ok, Monitors}.

%% Watches `Pid` once, whatever its names and locks.
watch(Pid, Monitors) ->
    case lists:member(Pid, maps:values(Monitors)) of
        true -> Monitors;
        false -> Monitors#{monitor(process, Pid) => Pid}
    end.

register(Name, Pid, Method, Monitors) ->
    Kept = unregister(Name, Monitors),
    ets:insert(global_names, {Name, Pid, Method, undefined}),
    ets:insert(global_pid_names, {Pid, Name}),
    watch(Pid, Kept).

unregister(Name, Monitors) ->
    case ets:lookup(global_names, Name) of
        [{_, Pid, _, _}] ->
            ets:delete(global_names, Name),
            ets:delete_object(global_pid_names, {Pid, Name}),
            Monitors;
        [] ->
            Monitors
    end.

%% A process may have one name unless kernel's global_multi_name_action is allow.
check_dupname(Name, Pid) ->
    case ets:lookup(global_pid_names, Pid) of
        [] ->
            true;
        PidNames ->
            case clause_logger:app_env(kernel, global_multi_name_action) of
                {ok, allow} ->
                    true;
                _ ->
                    Names = [Name | [Other || {_, Other} <- PidNames]],
                    logger:log(error, "global: ~w registered under several names: ~tw\n", [Pid, Names]),
                    false
            end
    end.

%% Names.

where(Name) ->
    ensure_started(),
    case ets:lookup(global_names, Name) of
        [{_, Pid, _, _}] when node(Pid) =:= node() ->
            case is_process_alive(Pid) of
                true -> Pid;
                false -> undefined
            end;
        [{_, Pid, _, _}] ->
            Pid;
        [] ->
            undefined
    end.

whereis_name(Name) -> where(Name).

send(Name, Message) ->
    case where(Name) of
        Pid when is_pid(Pid) ->
            Pid ! Message,
            Pid;
        undefined ->
            exit({badarg, {Name, Message}})
    end.

register_name(Name, Pid) when is_pid(Pid) -> register_name(Name, Pid, fun random_exit_name/3).

register_name(Name, Pid, Method) when is_pid(Pid) -> call({register_new, Name, Pid, allow_tuple_fun(Method)}).

register_name_external(Name, Pid) when is_pid(Pid) -> register_name_external(Name, Pid, fun random_exit_name/3).

register_name_external(Name, Pid, Method) when is_pid(Pid) ->
    case where(Name) of
        undefined -> call({register, Name, Pid, Method});
        _ -> no
    end.

unregister_name_external(Name) -> unregister_name(Name).

re_register_name(Name, Pid) when is_pid(Pid) -> re_register_name(Name, Pid, fun random_exit_name/3).

re_register_name(Name, Pid, Method) when is_pid(Pid) -> call({register, Name, Pid, allow_tuple_fun(Method)}).

unregister_name(Name) ->
    case where(Name) of
        undefined -> ok;
        _ -> call({unregister, Name})
    end.

registered_names() ->
    ensure_started(),
    [Name || [Name] <- ets:match(global_names, {'$1', '_', '_', '_'})].

allow_tuple_fun({M, F}) when is_atom(M), is_atom(F) -> fun M:F/3;
allow_tuple_fun(Fun) when is_function(Fun, 3) -> Fun.

%% Name conflict resolvers: they only run when nodes with the same name meet.

random_exit_name(Name, Pid, Pid2) ->
    {Min, Max} = minmax(Pid, Pid2),
    logger:log(info, "global: Name conflict terminating ~tw\n", [{Name, Max}]),
    exit(Max, kill),
    Min.

random_notify_name(Name, Pid, Pid2) ->
    {Min, Max} = minmax(Pid, Pid2),
    Max ! {global_name_conflict, Name},
    Min.

notify_all_name(Name, Pid, Pid2) ->
    Pid ! {global_name_conflict, Name, Pid2},
    Pid2 ! {global_name_conflict, Name, Pid},
    none.

%% Ordered by node only: on one node the second pid wins, as in OTP.
minmax(P1, P2) when node(P1) < node(P2) -> {P1, P2};
minmax(P1, P2) -> {P2, P1}.

%% Locks: held by a requester id, which several processes may share; other nodes cannot be reached.

set_lock(Id) -> set_lock(Id, [node()], infinity, 1).

set_lock(Id, Nodes) -> set_lock(Id, Nodes, infinity, 1).

set_lock(Id, Nodes, Retries) when is_integer(Retries), Retries >= 0 -> set_lock(Id, Nodes, Retries, 1);
set_lock(Id, Nodes, infinity) -> set_lock(Id, Nodes, infinity, 1).

set_lock({_, _} = Id, Nodes, Retries, Times) ->
    case lists:member(node(), Nodes) andalso not call({set_lock, Id, self()}) of
        false ->
            true;
        true when Retries =:= 0 ->
            false;
        true ->
            random_sleep(Times),
            set_lock(Id, Nodes, dec(Retries), Times + 1)
    end.

dec(infinity) -> infinity;
dec(N) -> N - 1.

%% Waits up to an eighth of 2^Times seconds, at most 8 seconds, as OTP does between lock attempts.
random_sleep(Times) ->
    Max =
        case Times > 5 of
            true -> 8000;
            false -> ((1 bsl Times) * 1000) div 8
        end,
    receive
    after rand:uniform(Max) -> ok
    end.

del_lock(Id) -> del_lock(Id, [node()]).

del_lock({_, _} = Id, Nodes) ->
    _ = lists:member(node(), Nodes) andalso call({del_lock, Id, self()}),
    true.

trans(Id, Fun) -> trans(Id, Fun, [node()], infinity).

trans(Id, Fun, Nodes) -> trans(Id, Fun, Nodes, infinity).

trans(Id, Fun, Nodes, Retries) ->
    case set_lock(Id, Nodes, Retries) of
        true ->
            try
                Fun()
            after
                del_lock(Id, Nodes)
            end;
        false ->
            aborted
    end.

set_local_lock({Resource, Requester}, Pid, Monitors) ->
    case ets:lookup(global_locks, Resource) of
        [{_, Requester, Pids}] ->
            {true, lock(Resource, Requester, Pid, Pids, Monitors)};
        [{_, _, _}] ->
            {false, Monitors};
        [] ->
            {true, lock(Resource, Requester, Pid, [], Monitors)}
    end.

lock(Resource, Requester, Pid, Pids, Monitors) ->
    case lists:member(Pid, Pids) of
        true ->
            Monitors;
        false ->
            ets:insert(global_locks, {Resource, Requester, [Pid | Pids]}),
            watch(Pid, Monitors)
    end.

del_local_lock({Resource, Requester}, Pid, Monitors) ->
    case ets:lookup(global_locks, Resource) of
        [{_, Requester, _} = Entry] -> del_lock_entry(Entry, Pid);
        _ -> ok
    end,
    Monitors.

del_lock_entry({Resource, Requester, Pids}, Pid) ->
    case lists:delete(Pid, Pids) of
        [] -> ets:delete(global_locks, Resource);
        Pids -> ok;
        Rest -> ets:insert(global_locks, {Resource, Requester, Rest})
    end.

%% Synchronisation and disconnection: no other nodes.

sync() ->
    _ = call(sync),
    ok.

sync(Nodes) when is_list(Nodes) ->
    _ = call(sync),
    ok.

disconnect() -> call(disconnect).

%% The server's internal state, the locker process and conflict resolution belong to distribution.
info() -> erlang:error({not_implemented, info, 0}).
get_locker() -> erlang:error({not_implemented, get_locker, 0}).
resolve_it(_, _, _, _) -> erlang:error({not_implemented, resolve_it, 4}).
