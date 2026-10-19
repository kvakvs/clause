-module(ets_portions).
-export([main/1, id/1]).

%% Long ETS operations in portions (plan 21 step LIB10F): tab2list/1, the selects of every object, select_count/2,
%% delete_all_objects/1 and delete/1 of large tables let other processes run meanwhile (a ping-pong of two processes
%% goes on while one process runs the operation, also with one scheduler), and give what they give in one piece.
%% update_counter/3 changes small counters in place: concurrent increments, the step to and from bignums, and objects
%% that are module literals keep their values.
main(_) ->
    show(results, results()),
    show(interleaved, interleaved()),
    show(counters, counters()),
    show(owner_end, owner_end()),
    io:format("ets_portions: ok~n").

id(X) -> X.

show(Label, Value) -> io:format("~p: ~w~n", [Label, Value]).

-define(COUNT, 100000).

filled(Type) ->
    T = ets:new(t, [Type, public]),
    ets:insert(T, [{K, K rem 7} || K <- lists:seq(1, ?COUNT)]),
    T.

results() ->
    [
        begin
            T = filled(Type),
            All = ets:tab2list(T),
            Selected = ets:select(T, [{{'$1', 3}, [], ['$1']}]),
            Reversed = ets:select_reverse(T, [{{'$1', '_'}, [{'<', '$1', 4}], ['$1']}]),
            R = [
                length(All),
                lists:sum([K || {K, _} <- All]),
                length(Selected),
                lists:sum(Selected),
                ets:select_count(T, [{{'_', 0}, [], [true]}]),
                length(ets:match_object(T, {'_', 6})),
                sorted_if(Type, Reversed),
                ets:delete_all_objects(T),
                ets:info(T, size),
                ets:tab2list(T)
            ],
            ets:delete(T),
            R
        end
     || Type <- [set, ordered_set, bag]
    ].

%% An ordered_set's select_reverse comes in reverse key order; a hash table's results are sorted to compare.
sorted_if(ordered_set, List) -> List;
sorted_if(_, List) -> lists:sort(List).

%% Whether the two other processes exchanged messages while each operation ran.
interleaved() ->
    [
        exchanged(fun(T) -> length(ets:tab2list(T)) end),
        exchanged(fun(T) -> length(ets:select(T, [{'$1', [], ['$1']}])) end),
        exchanged(fun(T) -> ets:select_count(T, [{'_', [], [true]}]) end),
        exchanged(fun(T) -> ets:delete_all_objects(T) end),
        exchanged(fun(T) -> ets:delete(T) end)
    ].

%% The operation runs in a process that reads, right before and right after it, how many exchanges the pair made:
%% they made some only if they ran while the operation did.
exchanged(Operation) ->
    Main = self(),
    T = filled(set),
    Counter = ets:new(counter, [public]),
    ets:insert(Counter, {n, 0}),
    Pong = spawn(fun() -> pong() end),
    {Ping, Watch} = spawn_monitor(fun() -> ping(Counter, Pong) end),
    started(Counter),
    spawn(fun() ->
        Before = ets:lookup_element(Counter, n, 2),
        Result = Operation(T),
        After = ets:lookup_element(Counter, n, 2),
        Main ! {done, Result, After > Before}
    end),
    receive
        {done, Result, Moved} -> ok
    end,
    Ping ! stop,
    receive
        {'DOWN', Watch, process, Ping, _} -> ok
    end,
    ets:delete(Counter),
    _ = [ets:delete(T) || lists:member(T, ets:all())],
    {Result, Moved}.

%% Wait for the first exchange.
started(Counter) ->
    case ets:lookup_element(Counter, n, 2) of
        0 ->
            receive
            after 1 -> started(Counter)
            end;
        _ ->
            ok
    end.

ping(Counter, Pong) ->
    ets:update_counter(Counter, n, 1),
    Pong ! {ping, self()},
    receive
        pong -> ok
    end,
    receive
        stop -> Pong ! stop
    after 0 -> ping(Counter, Pong)
    end.

pong() ->
    receive
        {ping, From} ->
            From ! pong,
            pong();
        stop ->
            ok
    end.

-define(SMALL_MAX, 576460752303423487).

counters() ->
    T = ets:new(c, [set, public, {write_concurrency, true}]),
    ets:insert(T, [{shared, 0}, {big, ?SMALL_MAX - 1, 0}, literal()]),
    Self = self(),
    Workers = [
        spawn(fun() ->
            [ets:update_counter(T, shared, 1) || _ <- lists:seq(1, 5000)],
            Self ! done
        end)
     || _ <- lists:seq(1, 4)
    ],
    [
        receive
            done -> ok
        end
     || _ <- Workers
    ],
    Steps = [
        ets:update_counter(T, big, 1),
        ets:update_counter(T, big, 1),
        ets:update_counter(T, big, 1),
        ets:update_counter(T, big, -3),
        ets:update_counter(T, big, [{2, 1}, {3, 5}, {3, -1}]),
        ets:update_counter(T, big, {2, 10, ?SMALL_MAX, 0}),
        ets:update_counter(T, big, {3, -10, 0, -1}),
        ets:update_counter(T, literal, 2),
        ets:update_counter(T, literal, 2),
        literal(),
        ets:update_counter(T, missing, 1, {missing, 40}),
        ets:update_counter(T, missing, 1)
    ],
    R = [ets:lookup(T, shared), Steps, lists:sort(ets:tab2list(T))],
    ets:delete(T),
    R.

%% A module literal object.
literal() -> {literal, 7}.

%% A large table deleted at its owner's end.
owner_end() ->
    Self = self(),
    {Owner, Ref} = spawn_monitor(fun() ->
        T = filled(set),
        Self ! {table, T},
        receive
            stop -> ok
        end
    end),
    T =
        receive
            {table, Table} -> Table
        end,
    Size = ets:info(T, size),
    Owner ! stop,
    receive
        {'DOWN', Ref, process, Owner, _} -> ok
    end,
    [Size, ets:info(T, size), lists:member(T, ets:all())].
