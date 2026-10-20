-module(map_fold_loops).
-export([main/1, id/1]).

%% At O2, maps:fold/3 called with a known fun becomes a loop over the map's entries with the fun inlined, and a fun
%% returning its accumulator updated changes that map in place from the second step on (map fold loops, map trees).
%% Results, evaluation order and error reasons must stay those of the maps module; the folded and initial maps keep
%% their values.
id(X) -> X.

show(Name, F) ->
    R =
        try F() of
            V -> {ok, V}
        catch
            C:E -> {C, E}
        end,
    io:format("~s: ~p~n", [Name, R]).

main(_) ->
    Tree = id(maps:from_list([{K, K} || K <- lists:seq(1, 100)])),
    Small = id(#{1 => 10, 2 => 20, 3 => 30}),
    updates(Tree, Small),
    inserts(Tree),
    failures(Tree),
    others(Tree, Small),
    lists_folds(Tree),
    io:format("map_fold_loops: ok~n").

%% The sum of a map's values, for checking that a map kept its values.
sum(Map) ->
    maps:fold(fun(_, V, Acc) -> V + Acc end, 0, Map).

updates(Tree, Small) ->
    show("update tree", fun() ->
        Next = maps:fold(fun(K, V, Acc) -> Acc#{K := V + 1} end, Tree, Tree),
        {sum(Next), sum(Tree), map_size(Next)}
    end),
    show("update twice", fun() ->
        Once = maps:fold(fun(K, V, Acc) -> Acc#{K := V * 2} end, Tree, Tree),
        Twice = maps:fold(fun(K, V, Acc) -> Acc#{K := V * 2} end, Once, Once),
        {sum(Once), sum(Twice), sum(Tree)}
    end),
    show("update small", fun() ->
        Next = maps:fold(fun(K, V, Acc) -> Acc#{K := V + 10} end, Small, Small),
        {Next, Small}
    end),
    show("update other map", fun() ->
        Init = maps:from_list([{K, 0} || K <- lists:seq(1, 100)]),
        Next = maps:fold(fun(K, V, Acc) -> Acc#{K := V} end, Init, Tree),
        {sum(Next), sum(Init)}
    end),
    show("update with collections", fun() ->
        Next = maps:fold(
            fun(K, V, Acc) ->
                erlang:garbage_collect(),
                Acc#{K := V + 1}
            end,
            Tree,
            Tree
        ),
        {sum(Next), sum(Tree)}
    end),
    show("update atom keys", fun() ->
        Atoms = maps:from_list([
            {list_to_atom("k" ++ integer_to_list(I)), I}
         || I <- lists:seq(1, 50)
        ]),
        Next = maps:fold(fun(K, V, Acc) -> Acc#{K := V + 1} end, Atoms, Atoms),
        {sum(Next), sum(Atoms), maps:get(k7, Next)}
    end),
    show("update read twice", fun() ->
        Next = maps:fold(
            fun(K, V, Acc) ->
                Size = map_size(Acc),
                Acc#{K := V + Size}
            end,
            Tree,
            Tree
        ),
        {sum(Next), sum(Tree)}
    end),
    show("bound fun", fun() ->
        F = fun(K, V, Acc) -> Acc#{K := V - 1} end,
        Direct = F(1, 50, Tree),
        Next = maps:fold(F, Tree, Tree),
        {maps:get(1, Direct), sum(Next), sum(Tree)}
    end).

inserts(Tree) ->
    show("insert", fun() ->
        Next = maps:fold(fun(K, V, Acc) -> Acc#{K + 1000 => V} end, Tree, Tree),
        {
            map_size(Next),
            sum(Next),
            map_size(Tree),
            lists:sublist(lists:sort(maps:keys(Next)), 98, 5)
        }
    end),
    show("insert into empty", fun() ->
        Next = maps:fold(fun(K, V, Acc) -> Acc#{-K => V} end, #{}, Tree),
        {map_size(Next), sum(Next), lists:sublist(lists:sort(maps:keys(Next)), 3)}
    end).

failures(Tree) ->
    show("badkey after steps", fun() ->
        maps:fold(
            fun
                (K, V, Acc) when K =/= 40 -> Acc#{K := V};
                (_, V, Acc) -> Acc#{missing := V}
            end,
            Tree,
            Tree
        )
    end),
    show("after badkey", fun() -> sum(Tree) end),
    show("no clause", fun() -> maps:fold(fun(K, _, Acc) when K < 3 -> Acc end, 0, Tree) end),
    show("not a map", fun() -> maps:fold(fun(_, _, Acc) -> Acc end, 0, id(foo)) end),
    show("non-map accumulator", fun() ->
        maps:fold(fun(K, V, Acc) -> Acc#{K := V} end, id(acc), Tree)
    end).

others(Tree, Small) ->
    show("sum", fun() -> sum(Tree) end),
    show("empty", fun() -> maps:fold(fun(_, _, _) -> never end, empty, id(#{})) end),
    show("order", fun() ->
        maps:fold(fun(K, _, Acc) -> [K | Acc] end, [], Small)
    end),
    show("iterator", fun() ->
        maps:fold(fun(K, V, Acc) -> K + V + Acc end, 0, maps:iterator(Tree))
    end),
    show("ordered iterator", fun() ->
        maps:fold(fun(K, _, Acc) -> [K | Acc] end, [], maps:iterator(Small, ordered))
    end),
    show("tuple accumulator", fun() ->
        maps:fold(fun(K, V, {N, S}) -> {N + 1, S + K * V} end, {0, 0}, Tree)
    end),
    show("nested", fun() ->
        maps:fold(
            fun(_, V, Acc) -> maps:fold(fun(_, W, In) -> In + V * W end, Acc, Small) end, 0, Tree
        )
    end).

%% lists:foldl/3 and foldr/3 loops own a map accumulator their fun returns updated the same way.
lists_folds(Tree) ->
    show("foldl build", fun() ->
        Built = lists:foldl(fun(X, Acc) -> Acc#{X => X * X} end, #{}, lists:seq(1, 100)),
        {map_size(Built), sum(Built)}
    end),
    show("foldl shared", fun() ->
        Next = lists:foldl(fun(X, Acc) -> Acc#{X := 0} end, Tree, lists:seq(1, 50)),
        {sum(Next), sum(Tree)}
    end),
    show("foldr shared", fun() ->
        Next = lists:foldr(fun(X, Acc) -> Acc#{X := -X} end, Tree, lists:seq(51, 100)),
        {sum(Next), sum(Tree)}
    end),
    show("foldl badkey", fun() ->
        lists:foldl(fun(X, Acc) -> Acc#{X := 1} end, Tree, [1, 2, 3, 1000])
    end),
    show("foldl after badkey", fun() -> sum(Tree) end).
