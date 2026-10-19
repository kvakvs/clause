-module(ets_owner).
-export([main/1, id/1]).

%% ETS table ownership (plan 21 step LIB10B): deletion when the owner ends, inheritance by a heir with its
%% 'ETS-TRANSFER' message, heirs that ended first, give_away/3 and setopts/2. Pids and table identifiers differ
%% between runs, so they are printed as checks.
main(_) ->
    owner_exit(normal),
    owner_exit(kill),
    owner_exit(crash),
    inherit(normal),
    inherit(kill),
    dead_heir(),
    self_heir(),
    give_away(),
    setopts(),
    chain(),
    io:format("ets_owner: ok~n").

id(X) -> X.

show(Label, Values) -> io:format("~p: ~w~n", [Label, Values]).

attempt(F) ->
    try F() of
        Value -> {ok, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

%% A process that creates tables with Make, reports them and then ends as told.
owner(Make) ->
    Self = self(),
    {Pid, Ref} = spawn_monitor(fun() ->
        Self ! {self(), Make()},
        receive
            {stop, normal} -> ok;
            {stop, crash} -> error(crashed)
        end
    end),
    receive
        {Pid, Tables} -> {Pid, Ref, Tables}
    end.

stop(Pid, Ref, How) ->
    case How of
        kill -> exit(Pid, kill);
        _ -> Pid ! {stop, How}
    end,
    receive
        {'DOWN', Ref, process, Pid, Reason} -> reason_class(Reason)
    end.

%% An exit reason without the stack trace of an error, whose file names differ between runs.
reason_class({Reason, Stack}) when is_list(Stack) -> Reason;
reason_class(Reason) -> Reason.

owner_exit(How) ->
    {Pid, Ref, [T, N]} = owner(fun() ->
        T = ets:new(unnamed, [public]),
        N = ets:new(owned, [named_table, public]),
        ets:insert(T, {k, v}),
        [T, N]
    end),
    Before = [ets:info(T, size), ets:lookup(owned, k), ets:info(T, owner) =:= Pid],
    Reason = stop(Pid, Ref, How),
    show(owner_exit, [
        How,
        Before,
        Reason,
        ets:info(T),
        ets:info(N),
        ets:whereis(owned),
        attempt(fun() -> ets:lookup(T, k) end),
        is_reference(ets:new(owned, [named_table])) orelse ets:delete(owned)
    ]).

receive_transfer() ->
    receive
        {'ETS-TRANSFER', T, From, Data} -> {T, From, Data}
    after 5000 -> timeout
    end.

inherit(How) ->
    Self = self(),
    {Pid, Ref, T} = owner(fun() ->
        T = ets:new(inherited, [{heir, Self, {my, data}}]),
        ets:insert(T, [{a, 1}, {b, 2}]),
        T
    end),
    Down = stop(Pid, Ref, How),
    {Tab, From, Data} = receive_transfer(),
    show(inherit, [
        How,
        Down,
        Tab =:= T,
        From =:= Pid,
        Data,
        ets:info(T, owner) =:= Self,
        ets:info(T, heir) =:= Self,
        lists:sort(ets:tab2list(T)),
        ets:insert(T, {c, 3}),
        ets:info(T, size)
    ]),
    ets:delete(T).

dead_heir() ->
    Heir = spawn(fun() -> ok end),
    HeirRef = monitor(process, Heir),
    receive
        {'DOWN', HeirRef, process, Heir, _} -> ok
    end,
    {Pid, Ref, T} = owner(fun() ->
        T = ets:new(orphan, [{heir, Heir, data}]),
        Info = ets:info(T, heir),
        {T, Info}
    end),
    stop(Pid, Ref, normal),
    {Table, HeirInfo} = T,
    show(dead_heir, [
        HeirInfo,
        ets:info(Table),
        receive
            {'ETS-TRANSFER', _, _, _} -> transferred
        after 100 -> none
        end
    ]).

self_heir() ->
    {Pid, Ref, T} = owner(fun() ->
        T = ets:new(self_heir, [{heir, self(), data}]),
        {T, ets:info(T, heir) =:= self()}
    end),
    stop(Pid, Ref, normal),
    {Table, IsSelf} = T,
    show(self_heir, [IsSelf, ets:info(Table)]).

%% A process that waits for a table, reports what it sees and then gives the table back.
receiver(Parent) ->
    receive
        {'ETS-TRANSFER', T, From, Data} ->
            Seen = [
                From =:= Parent,
                Data,
                ets:info(T, owner) =:= self(),
                ets:lookup(T, k),
                ets:insert(T, {k, receiver})
            ],
            Parent ! {seen, Seen},
            receive
                back -> ets:give_away(T, Parent, returned)
            end,
            receive
                stop -> ok
            end
    end.

give_away() ->
    Self = self(),
    T = ets:new(gift, [{heir, Self, heir_data}]),
    ets:insert(T, {k, v}),
    Receiver = spawn(fun() -> receiver(Self) end),
    Dead = spawn(fun() -> ok end),
    DeadRef = monitor(process, Dead),
    receive
        {'DOWN', DeadRef, process, Dead, _} -> ok
    end,
    Errors = [
        attempt(fun() -> ets:give_away(T, Self, data) end),
        attempt(fun() -> ets:give_away(T, Dead, data) end),
        attempt(fun() -> ets:give_away(T, id(not_a_pid), data) end),
        attempt(fun() -> ets:give_away(id(no_such_table), Receiver, data) end)
    ],
    Given = ets:give_away(T, Receiver, present),
    Seen =
        receive
            {seen, S} -> S
        end,
    After = [
        ets:info(T, owner) =:= Receiver,
        ets:info(T, heir) =:= Self,
        attempt(fun() -> ets:insert(T, {mine, again}) end),
        attempt(fun() -> ets:give_away(T, Receiver, data) end),
        ets:lookup(T, k)
    ],
    Receiver ! back,
    {Back, From, Data} = receive_transfer(),
    Receiver ! stop,
    show(give_away, [
        Errors,
        Given,
        Seen,
        After,
        Back =:= T,
        From =:= Receiver,
        Data,
        ets:info(T, owner) =:= Self,
        ets:insert(T, {mine, again})
    ]),
    ets:delete(T).

elsewhere(F) ->
    Self = self(),
    Pid = spawn(fun() -> Self ! {self(), attempt(F)} end),
    receive
        {Pid, Result} -> Result
    end.

setopts() ->
    Self = self(),
    Other = spawn(fun() ->
        receive
            stop -> ok
        end
    end),
    T = ets:new(options, []),
    show(setopts, [
        ets:setopts(T, {heir, Other, data}),
        ets:info(T, heir) =:= Other,
        ets:setopts(T, [{heir, none}]),
        ets:info(T, heir),
        ets:setopts(T, {protection, public}),
        ets:info(T, protection),
        elsewhere(fun() -> ets:insert(T, {from, other}) end),
        ets:setopts(T, [{protection, private}, {heir, Self, me}]),
        ets:info(T, protection),
        ets:info(T, heir) =:= Self,
        ets:setopts(T, []),
        elsewhere(fun() -> ets:setopts(T, {protection, public}) end),
        attempt(fun() -> ets:setopts(T, id({protection, open})) end),
        attempt(fun() -> ets:setopts(T, id({keypos, 2})) end),
        attempt(fun() -> ets:setopts(T, id({heir, nobody, data})) end),
        attempt(fun() -> ets:setopts(T, id(heir)) end),
        attempt(fun() -> ets:setopts(id(no_such_table), []) end),
        ets:info(T, protection)
    ]),
    Other ! stop,
    ets:delete(T).

%% A table passed on twice: the second heir is not set, so it dies with its second owner.
chain() ->
    Self = self(),
    {First, FirstRef, T} = owner(fun() ->
        ets:new(chain, [public, {heir, Self, first}])
    end),
    Second = spawn(fun() ->
        receive
            {'ETS-TRANSFER', Tab, _, _} ->
                Self ! {second, ets:info(Tab, owner) =:= self()},
                receive
                    stop -> ok
                end
        end
    end),
    SecondRef = monitor(process, Second),
    stop(First, FirstRef, normal),
    {T, First, first} = receive_transfer(),
    ets:give_away(T, Second, second),
    Owned =
        receive
            {second, O} -> O
        end,
    Heir = ets:info(T, heir),
    Second ! stop,
    receive
        {'DOWN', SecondRef, process, Second, _} -> ok
    end,
    show(chain, [
        Owned, Heir =:= Self, ets:info(T) =/= undefined, receive_transfer() =:= {T, Second, first}
    ]),
    ets:delete(T).
