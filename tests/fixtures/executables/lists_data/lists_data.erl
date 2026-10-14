-module(lists_data).
-export([main/1]).

%% The lists data functions (plan step 41T) with valid, boundary and invalid
%% arguments, through apply/3, direct calls and funs; then the fun-taking ones.
main([]) ->
    [io:format("~w~n", [outcome(F, A)]) || {F, A} <- id(calls())],
    direct(),
    higher_order(),
    filters(),
    io:format("lists_data: ok~n").

id(X) -> X.

%% The result of lists:F(A...), or the error it raises.
outcome(F, A) ->
    try
        {F, ok, apply(lists, F, A)}
    catch
        error:R -> {F, error, R}
    end.

calls() ->
    [
        {sublist, [[a, b, c], 2]},
        {sublist, [[a, b, c], 0]},
        {sublist, [[a, b, c], 5]},
        {sublist, [[], 1]},
        {sublist, [[a | b], 1]},
        {sublist, [[a | b], 2]},
        {sublist, [[a, b], -1]},
        {sublist, [x, 1]},
        {sublist, [[a], 1.0]},
        {sublist, [[a, b, c, d], 2, 2]},
        {sublist, [[a, b, c], 1, 5]},
        {sublist, [[a, b, c], 3, 1]},
        {sublist, [[a, b, c], 4, 1]},
        {sublist, [[a, b, c], 6, 1]},
        {sublist, [[a, b, c], 0, 1]},
        {sublist, [[a, b, c], 2, -1]},
        {droplast, [[a, b, c]]},
        {droplast, [[a]]},
        {droplast, [[]]},
        {droplast, [[a | b]]},
        {droplast, [x]},
        {nthtail, [0, [a, b]]},
        {nthtail, [1, [a, b]]},
        {nthtail, [2, [a, b]]},
        {nthtail, [3, [a, b]]},
        {nthtail, [-1, [a, b]]},
        {nthtail, [1, [a | b]]},
        {nthtail, [0, x]},
        {nthtail, [a, [a]]},
        {flatten, [[a, [b, [c, []], d], [[[e]]]]]},
        {flatten, [[]]},
        {flatten, [[a | b]]},
        {flatten, [[[a | b], c]]},
        {flatten, [x]},
        {flatten, [[a, [b]], [c]]},
        {flatten, [[a], x]},
        {flatten, [[a], [b | c]]},
        {duplicate, [3, x]},
        {duplicate, [0, x]},
        {duplicate, [-1, x]},
        {duplicate, [a, x]},
        {max, [[1, 3, 2]]},
        {max, [[1, 1.0]]},
        {max, [[1.0, 1]]},
        {max, [[a, 1, {}, "s"]]},
        {max, [[]]},
        {max, [[1 | 2]]},
        {max, [x]},
        {min, [[3, 1, 2]]},
        {min, [[1, 1.0]]},
        {min, [[1.0, 1]]},
        {min, [[a, 1, {}]]},
        {min, [[]]},
        {last, [[a, b, c]]},
        {last, [[a]]},
        {last, [[]]},
        {last, [[a | b]]},
        {umerge, [[[1, 3, 5], [2, 3, 4], [0, 6]]]},
        {umerge, [[[1, 2], [1.0, 2.0]]]},
        {umerge, [[[1.0, 2.0], [1, 2]]]},
        {umerge, [[]]},
        {umerge, [[[]]]},
        {umerge, [[[a], [b], [a, c]]]},
        {umerge, [[1, 3], [2, 3]]},
        {umerge, [[1], [1.0]]},
        {umerge, [[1.0], [1]]},
        {umerge, [[], [a]]},
        {usort, [[c, a, b, a, c]]},
        {usort, [[1, 1.0, 2.0, 2]]},
        {usort, [[1.0, 1]]},
        {usort, [[]]},
        {usort, [[b, {a}, 1, "x", [], c]]},
        {join, [x, [a, b, c]]},
        {join, [x, [a]]},
        {join, [x, []]},
        {join, [x, [a | b]]},
        {join, [x, y]},
        {keymember, [b, 1, [{a, 1}, {b, 2}]]},
        {keymember, [1, 2, [{a, 1.0}]]},
        {keymember, [c, 1, [{a, 1}, x, {b}]]},
        {keymember, [a, 0, [{a}]]},
        {keymember, [a, 1, x]},
        {keymember, [a, 1, [{b} | x]]},
        {keysearch, [b, 1, [{a, 1}, {b, 2}, {b, 3}]]},
        {keysearch, [z, 1, [{a, 1}]]},
        {keysearch, [a, 3, [{a, 1}]]},
        {keysearch, [a, a, []]},
        {keydelete, [b, 1, [{a, 1}, {b, 2}, {b, 3}]]},
        {keydelete, [1, 2, [{a, 1.0}, x, {b, 1}]]},
        {keydelete, [z, 1, []]},
        {keydelete, [z, 0, []]},
        {keydelete, [z, 1, [{a} | b]]}
    ].

%% Direct remote calls and funs of the same builtins.
direct() ->
    L = lists:seq(1, 10),
    io:format("~w~n", [
        [
            lists:sublist(L, 3),
            lists:sublist(L, 4, 2),
            lists:droplast(L),
            lists:nthtail(8, L),
            lists:flatten([L, [L]]) =:= L ++ L,
            lists:duplicate(2, L),
            lists:max(L),
            lists:min(L),
            lists:last(L),
            lists:umerge([L, L]) =:= L,
            lists:usort(lists:reverse(L) ++ L) =:= L,
            lists:join(0, [1, 2]),
            lists:keymember(3, 2, [{x, 3}]),
            lists:keysearch(x, 1, [{x, 3}]),
            lists:keydelete(x, 1, [{x, 3}])
        ]
    ]),
    Sublist = fun lists:sublist/2,
    Nthtail = fun lists:nthtail/2,
    io:format("~w~n", [
        [
            Sublist([a, b, c], 2),
            Nthtail(2, [a, b, c]),
            length(lists:flatten(lists:duplicate(1000, [x])))
        ]
    ]).

%% The fun-taking functions: foreach, mapfoldl, mapfoldr, splitwith, partition.
higher_order() ->
    lists:foreach(fun(X) -> io:format("~w ", [X]) end, [a, b, c]),
    io:format("~n"),
    io:format("~w~n", [
        [
            lists:mapfoldl(fun(X, Acc) -> {X * 2, Acc + X} end, 0, [1, 2, 3]),
            lists:mapfoldr(fun(X, Acc) -> {X * 2, [X | Acc]} end, [], [1, 2, 3]),
            lists:splitwith(fun(X) -> X < 3 end, [1, 2, 3, 1]),
            lists:splitwith(fun(X) -> X < 3 end, []),
            lists:partition(fun(X) -> X rem 2 =:= 0 end, [1, 2, 3, 4, 5]),
            lists:mapfoldl(fun(X, A) -> {X, A} end, acc, [])
        ]
    ]),
    io:format("~w~n", [
        [
            catch_error(fun() -> lists:foreach(fun(X) -> X end, x) end),
            catch_error(fun() -> lists:foreach(x, [a]) end),
            catch_error(fun() -> lists:splitwith(fun(_) -> x end, [a]) end),
            catch_error(fun() -> lists:partition(fun(_) -> x end, [a]) end),
            catch_error(fun() -> lists:mapfoldl(fun(_, _) -> x end, 0, [a]) end)
        ]
    ]).

%% prefix/2, suffix/2, filtermap/2 and uniq/1,2 (step 41L).
filters() ->
    Keep = fun
        (X) when X > 2 -> {true, X * 10};
        (X) -> X =:= 1
    end,
    io:format("~w~n", [
        [
            lists:prefix([1, 2], [1, 2, 3]),
            lists:prefix([], []),
            lists:prefix([2], [1, 2]),
            lists:suffix([2, 3], [1, 2, 3]),
            lists:suffix([1, 2, 3, 4], [2, 3]),
            lists:filtermap(Keep, [1, 2, 3, 4]),
            lists:uniq([b, a, b, c, a]),
            lists:uniq([1, 1.0, 1]),
            lists:uniq(fun(X) -> X rem 3 end, [1, 2, 4, 5, 6]),
            catch_error(fun() -> lists:prefix(x, [1]) end),
            catch_error(fun() -> lists:suffix(x, [1]) end),
            catch_error(fun() -> lists:filtermap(fun(_) -> x end, [a]) end),
            catch_error(fun() -> lists:filtermap(x, [a]) end),
            catch_error(fun() -> lists:uniq(x) end),
            catch_error(fun() -> lists:uniq(x, [a]) end)
        ]
    ]).

%% The error class and reason of Fun(), or its value.
catch_error(Fun) ->
    try
        {ok, Fun()}
    catch
        Class:Reason -> {Class, Reason}
    end.
