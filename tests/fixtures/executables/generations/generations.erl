-module(generations).
-export([main/1]).

%% Long-lived data survives many minor collections next to short-lived garbage (generational collection).
main(_) ->
    Kept = lists:seq(1, 50000),
    io:format("~p~n", [churn(200, Kept, 0)]),
    io:format("~p~n", [lists:sum(Kept)]),
    io:format("~p~n", [maps_kept(maps:from_list([{K, K} || K <- lists:seq(1, 100)]), 300)]),
    io:format("~p~n", [binaries(400, [])]),
    io:format("~p~n", [mapped(lists:seq(1, 30000))]),
    io:format("~p~n", [nested(100, [])]),
    io:format("generations: ok~n").

%% Garbage around a long-lived list; the list is read after every round.
churn(0, Kept, Acc) ->
    Acc + length(Kept);
churn(N, Kept, Acc) ->
    Garbage = [{N, X} || X <- lists:seq(1, 500)],
    churn(N - 1, Kept, Acc + length(Garbage) + lists:nth(N, Kept)).

%% A map above 32 keys (an off-heap tree) updated in place across collections after it is promoted.
maps_kept(Map, 0) ->
    lists:sum(maps:values(Map));
maps_kept(Map, N) ->
    _ = [X * N || X <- lists:seq(1, 200)],
    maps_kept(maps:fold(fun(K, V, Acc) -> Acc#{K := V + 1} end, Map, Map), N - 1).

%% Off-heap binaries kept across collections, then half dropped and collected.
binaries(0, Kept) ->
    {Half, _} = lists:split(length(Kept) div 2, Kept),
    true = erlang:garbage_collect(),
    {length(Half), lists:sum([byte_size(B) || B <- Half])};
binaries(N, Kept) ->
    Bin = binary:copy(<<N:32>>, 40),
    _ = [X || X <- lists:seq(1, 100)],
    binaries(N - 1, [Bin | Kept]).

%% A body recursion whose returns allocate the result without a safepoint between them.
mapped(List) ->
    Doubled = lists:map(fun(X) -> {X, X * 2} end, List),
    lists:foldl(fun({_, Y}, Acc) -> Acc + Y end, 0, Doubled).

%% Young tuples that point at promoted ones.
nested(0, Acc) ->
    length(Acc) + lists:sum([element(1, T) || T <- Acc]);
nested(N, Acc) ->
    _ = lists:seq(1, 1000),
    nested(N - 1, [{N, Acc} | Acc]).
