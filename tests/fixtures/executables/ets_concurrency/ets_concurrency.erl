-module(ets_concurrency).
-export([main/1]).

%% ETS under concurrent use (plan 21 step LIB10E): processes update counters, insert, delete and walk public tables of
%% every type and concurrency option at once; large tables are filled, listed, cleared and deleted, and tables of many
%% owners die with them. Only totals are printed, which every interleaving agrees on.
main(_) ->
    Options = [
        [],
        [{write_concurrency, true}],
        [{write_concurrency, auto}],
        [{read_concurrency, true}, {write_concurrency, true}],
        [{decentralized_counters, true}, {write_concurrency, true}]
    ],
    [counters(Type, Opts) || Type <- [set, ordered_set], Opts <- Options],
    [inserts(Type) || Type <- [set, ordered_set, bag, duplicate_bag]],
    large(set),
    large(ordered_set),
    owners(),
    io:format("ets_concurrency: ok~n").

show(Label, Values) -> io:format("~p: ~w~n", [Label, Values]).

%% Runs Count processes of F(N) and waits for all of them.
parallel(Count, F) ->
    Self = self(),
    Pids = [spawn(fun() -> Self ! {done, self(), F(N)} end) || N <- lists:seq(1, Count)],
    [
        receive
            {done, Pid, Result} -> Result
        end
     || Pid <- Pids
    ].

counters(Type, Options) ->
    T = ets:new(counters, [Type, public | Options]),
    ets:insert(T, [{K, 0, 0} || K <- lists:seq(1, 8)]),
    parallel(8, fun(N) ->
        [ets:update_counter(T, (I rem 8) + 1, [{2, 1}, {3, N}]) || I <- lists:seq(1, 500)],
        ok
    end),
    Totals = lists:foldl(fun({_, A, B}, {SA, SB}) -> {SA + A, SB + B} end, {0, 0}, ets:tab2list(T)),
    show(counters, [Type, Options, Totals, ets:info(T, size)]),
    ets:delete(T).

inserts(Type) ->
    T = ets:new(inserts, [Type, public, {write_concurrency, true}]),
    Results = parallel(6, fun(N) ->
        [ets:insert(T, {{N, I}, I}) || I <- lists:seq(1, 300)],
        [ets:insert(T, {shared, N}) || _ <- [x]],
        [ets:delete(T, {N, I}) || I <- lists:seq(1, 300), I rem 3 =:= 0],
        ets:safe_fixtable(T, true),
        Walked = length(walk(T, ets:first(T))),
        ets:safe_fixtable(T, false),
        Walked >= 200
    end),
    Shared = length(ets:lookup(T, shared)),
    show(inserts, [Type, Results, ets:info(T, size) - Shared, Shared]),
    ets:delete(T).

walk(_T, '$end_of_table') -> [];
walk(T, Key) -> [Key | walk(T, ets:next(T, Key))].

large(Type) ->
    T = ets:new(large, [Type, public]),
    Count = 60000,
    ets:insert(T, [{K, <<K:64>>} || K <- lists:seq(1, Count)]),
    List = ets:tab2list(T),
    Sum = ets:foldl(fun({K, _}, Acc) -> K + Acc end, 0, T),
    Size = ets:info(T, size),
    true = ets:delete_all_objects(T),
    Cleared = ets:info(T, size),
    ets:insert(T, [{K, K} || K <- lists:seq(1, Count)]),
    true = ets:delete(T),
    show(large, [Type, length(List), Sum, Size, Cleared, ets:info(T)]).

owners() ->
    Self = self(),
    Pids = [
        spawn(fun() ->
            Tables = [ets:new(owned, [public]) || _ <- lists:seq(1, 10)],
            [ets:insert(T, [{K} || K <- lists:seq(1, 200)]) || T <- Tables],
            Self ! {tables, self(), Tables},
            receive
                stop -> ok
            end
        end)
     || _ <- lists:seq(1, 10)
    ],
    Tables = lists:append([
        receive
            {tables, Pid, Ts} -> Ts
        end
     || Pid <- Pids
    ]),
    Refs = [monitor(process, Pid) || Pid <- Pids],
    Sizes = lists:sum([ets:info(T, size) || T <- Tables]),
    [Pid ! stop || Pid <- Pids],
    [
        receive
            {'DOWN', Ref, process, _, _} -> ok
        end
     || Ref <- Refs
    ],
    show(owners, [length(Tables), Sizes, lists:usort([ets:info(T) || T <- Tables])]).
