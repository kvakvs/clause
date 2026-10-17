-module(hashed_collections).
-export([main/1]).

%% OTP's term hashes and the hashed collections (plan 21 step LIB2): phash/2 and phash2/1,2 values, dict and sets
%% (versions 1 and 2) with their records through expansion and contraction, and error reasons.
main(_) ->
    hashes(),
    dict_cases(),
    sets_cases(),
    io:format("hashed_collections: ok~n").

hashes() ->
    Big = 1 bsl 64,
    <<_:3, Unaligned/bits>> = <<"unaligned bits", 5:6>>,
    Terms = [
        a,
        '',
        'hello world',
        'ä',
        'π',
        'Ω€',
        0,
        1,
        -1,
        255,
        256,
        (1 bsl 27) - 1,
        1 bsl 27,
        -(1 bsl 27),
        -(1 bsl 27) - 1,
        1 bsl 31,
        1 bsl 32,
        1 bsl 59,
        1 bsl 60,
        Big,
        -Big,
        Big * Big + 7,
        -(Big * Big),
        1000000000000000000000000000000,
        0.0,
        -0.0,
        1.5,
        -3.25e100,
        1.0e-300,
        <<>>,
        <<1>>,
        <<"abc">>,
        <<"0123456789ab">>,
        <<"0123456789abc">>,
        binary:copy(<<"xyz">>, 40),
        <<1:3>>,
        <<1, 2:5>>,
        Unaligned,
        [],
        [1, 2, 3],
        "abcd",
        "abcde",
        [256],
        [1, a],
        [a | b],
        [1, 2 | 3],
        [[1], [2, [3]]],
        "x" ++ [1.5],
        {},
        {a},
        {a, b, c},
        {[], {}, <<>>},
        #{},
        #{a => 1},
        #{1 => a, "k" => [v]},
        maps:from_list([{I, I * I} || I <- lists:seq(1, 40)]),
        #{m => #{n => {}}}
    ],
    show(phash2, [erlang:phash2(T) || T <- Terms]),
    show(phash2_range, [
        erlang:phash2(T, R)
     || T <- [a, 1, <<"b">>, {x}], R <- [1, 7, 1 bsl 16, 1 bsl 32]
    ]),
    show(phash, [phash(T, 1 bsl 32) || T <- Terms]),
    show(phash_range, [phash(T, R) || T <- [a, 1, <<"b">>, {x}], R <- [1, 7, 16, 1 bsl 32]]),
    show(hash_errors, [
        reason(fun() -> erlang:phash2(a, id(0)) end),
        reason(fun() -> erlang:phash2(a, id((1 bsl 32) + 1)) end),
        reason(fun() -> phash(a, id(-1)) end),
        reason(fun() -> phash(a, id(x)) end)
    ]).

dict_cases() ->
    D0 = dict:new(),
    D1 = dict:from_list([{a, 1}, {b, 2}, {c, 3}, {a, 10}]),
    show(dict_small, [
        D0,
        D1,
        dict:to_list(D1),
        dict:fetch_keys(D1),
        dict:size(D1),
        dict:is_empty(D0),
        dict:is_key(b, D1),
        dict:is_key(z, D1),
        dict:fetch(c, D1),
        dict:find(a, D1),
        dict:find(z, D1),
        dict:find(1.0, dict:store(1, int, D0)),
        dict:erase(b, D1),
        dict:erase(z, D1),
        dict:take(a, D1),
        dict:take(z, D1),
        dict:store(b, new, D1),
        dict:append(k, x, dict:append(k, w, D0)),
        dict:append_list(k, [y, z], dict:append(k, x, D0)),
        dict:update_counter(a, 5, D1),
        dict:update_counter(n, 0.5, D1),
        dict:update(a, fun(V) -> V * 2 end, D1),
        dict:update(z, fun(V) -> V end, init, D1),
        dict:fold(fun(K, V, A) -> [{K, V} | A] end, [], D1),
        dict:map(fun(K, V) -> {K, V} end, D1),
        dict:filter(fun(_, V) -> V > 2 end, D1),
        dict:to_list(
            dict:merge(fun(_, V1, V2) -> V1 + V2 end, D1, dict:from_list([{a, 100}, {d, 4}]))
        )
    ]),
    Keys =
        [{key, I} || I <- lists:seq(1, 200)] ++ [I * 1.5 || I <- lists:seq(1, 50)] ++
            ["s" ++ [I] || I <- "abcdefghij"],
    Large = lists:foldl(fun(K, D) -> dict:store(K, K, D) end, D0, Keys),
    Shrunk = lists:foldl(fun(K, D) -> dict:erase(K, D) end, Large, lists:sublist(Keys, 230)),
    show(dict_large, [
        header(Large),
        dict:size(Large),
        erlang:phash2(dict:to_list(Large)),
        Large =:= dict:from_list([{K, K} || K <- Keys]),
        header(Shrunk),
        dict:to_list(Shrunk),
        Shrunk,
        header(dict:filter(fun(K, _) -> is_float(K) end, Large)),
        dict:to_list(dict:filter(fun(K, _) -> is_list(K) end, Large)),
        header(dict:map(fun(_, _) -> x end, Large)),
        erlang:phash2(dict:fetch_keys(Large))
    ]),
    show(dict_errors, [
        reason(fun() -> dict:fetch(z, D1) end),
        reason(fun() -> dict:fetch(a, id(x)) end),
        reason(fun() -> dict:store(a, 1, id({x})) end),
        reason(fun() -> dict:size(id(x)) end),
        reason(fun() -> dict:is_empty(id(x)) end),
        reason(fun() -> dict:to_list(id(x)) end),
        reason(fun() -> dict:update(z, fun(V) -> V end, D1) end),
        reason(fun() -> dict:update(z, id(x), 0, D1) end),
        reason(fun() -> dict:update_counter(a, id(x), D1) end),
        reason(fun() -> dict:update_counter(a, 1, dict:store(a, x, D0)) end),
        reason(fun() -> dict:append(a, 1, dict:store(a, [x | y], D0)) end),
        reason(fun() -> dict:from_list(id([x])) end),
        reason(fun() -> dict:from_list(id([{a, 1} | x])) end),
        reason(fun() -> dict:fold(id(x), 0, D0) end),
        reason(fun() -> dict:fold(fun(_, _, A) -> A end, 0, id(x)) end),
        reason(fun() -> dict:map(id(x), D0) end),
        reason(fun() -> dict:map(fun(_, V) -> V end, id(x)) end),
        reason(fun() -> dict:filter(fun(_, _) -> 1 end, D1) end),
        reason(fun() -> dict:merge(fun(_, A, _) -> A end, id(x), D1) end),
        reason(fun() -> dict:is_key(a, id(sets:new([{version, 1}]))) end)
    ]).

sets_cases() ->
    V1 = sets:new([{version, 1}]),
    S1 = sets:from_list([c, a, b, a], [{version, 1}]),
    M = sets:from_list([c, a, b]),
    show(sets_small, [
        V1,
        S1,
        sets:new(),
        sets:new([{version, 2}]),
        sets:new([]),
        lists:sort(sets:to_list(M)),
        sets:to_list(S1),
        sets:size(S1),
        sets:size(M),
        sets:is_empty(V1),
        sets:is_empty(M),
        sets:is_set(S1),
        sets:is_set(M),
        sets:is_set(#{a => 1}),
        sets:is_set({set}),
        sets:is_element(b, S1),
        sets:is_element(z, S1),
        sets:is_element(1.0, sets:from_list([1])),
        sets:add_element(d, S1),
        sets:add_element(a, S1),
        sets:del_element(b, S1),
        sets:del_element(z, S1),
        sets:union(S1, sets:from_list([x, a], [{version, 1}])),
        sets:union([S1, sets:from_list([y], [{version, 1}]), sets:from_list([z], [{version, 1}])]),
        sets:union([]),
        sets:union([S1]),
        lists:sort(maps:to_list(sets:union(#{a => 1}, #{a => 2, b => 3}))),
        sets:intersection(S1, sets:from_list([b, c, d], [{version, 1}])),
        sets:intersection([S1, sets:from_list([a, b], [{version, 1}])]),
        sets:subtract(S1, sets:from_list([a], [{version, 1}])),
        sets:is_subset(sets:from_list([a], [{version, 1}]), S1),
        sets:is_subset(S1, sets:from_list([a], [{version, 1}])),
        sets:is_disjoint(S1, sets:from_list([x], [{version, 1}])),
        sets:is_disjoint(S1, sets:from_list([a], [{version, 1}])),
        sets:is_equal(S1, M),
        sets:is_equal(S1, sets:from_list([a, b], [{version, 1}])),
        sets:fold(fun(E, A) -> [E | A] end, [], S1),
        sets:filter(fun(E) -> E =/= b end, S1),
        sets:map(fun(E) -> {E} end, S1),
        sets:filtermap(
            fun
                (a) -> false;
                (E) -> {true, [E]}
            end,
            S1
        ),
        lists:sort(sets:to_list(sets:map(fun(E) -> {E} end, M))),
        lists:sort(sets:to_list(sets:filter(fun(E) -> E =/= b end, M)))
    ]),
    Big = lists:seq(1, 120),
    L1 = sets:from_list(Big, [{version, 1}]),
    L2 = sets:from_list(Big),
    Small = lists:foldl(fun(E, S) -> sets:del_element(E, S) end, L1, lists:seq(1, 110)),
    show(sets_large, [
        header(L1),
        sets:to_list(Small),
        Small,
        header(sets:filter(fun(E) -> E rem 3 =:= 0 end, L1)),
        sets:to_list(sets:filter(fun(E) -> E rem 17 =:= 0 end, L1)),
        header(sets:intersection(L1, sets:from_list(lists:seq(100, 300), [{version, 1}]))),
        header(sets:union(L1, sets:from_list(lists:seq(100, 300), [{version, 1}]))),
        lists:sort(
            maps:to_list(sets:intersection(#{1 => a, 2 => b, 3 => c}, #{2 => x, 3 => y, 4 => z}))
        ),
        lists:sort(
            maps:to_list(
                sets:intersection(#{1 => a, 2 => b, 3 => c, 4 => d}, #{1 => x, 5 => y, 6 => z})
            )
        ),
        lists:sort(
            maps:to_list(sets:subtract(#{1 => a, 2 => b, 3 => c, 4 => d, 5 => e}, #{1 => x}))
        ),
        lists:sort(
            maps:to_list(
                sets:subtract(#{1 => a, 2 => b, 3 => c, 4 => d}, #{1 => x, 2 => y, 3 => z})
            )
        ),
        sets:size(sets:intersection(L2, sets:from_list(lists:seq(50, 200)))),
        sets:is_subset(sets:from_list([1, 2]), L2),
        sets:is_equal(L1, L2)
    ]),
    show(sets_errors, [
        reason(fun() -> sets:is_element(a, id(x)) end),
        reason(fun() -> sets:add_element(a, id(x)) end),
        reason(fun() -> sets:size(id(x)) end),
        reason(fun() -> sets:to_list(id(x)) end),
        reason(fun() -> sets:union(id(x), V1) end),
        reason(fun() -> sets:union(#{}, id(x)) end),
        reason(fun() -> sets:intersection(id([])) end),
        reason(fun() -> sets:from_list(id(x)) end),
        reason(fun() -> sets:from_list(id([a | b])) end),
        reason(fun() -> sets:new(id([{version, 3}])) end),
        reason(fun() -> sets:is_subset(#{}, id(x)) end),
        reason(fun() -> sets:is_subset(M, id(x)) end),
        reason(fun() -> sets:fold(id(x), 0, V1) end),
        reason(fun() -> sets:filter(fun(_) -> 1 end, S1) end),
        reason(fun() -> sets:filter(fun(_) -> 1 end, M) end),
        reason(fun() -> sets:is_equal(id(x), V1) end),
        reason(fun() -> sets:subtract(id(x), #{}) end),
        reason(fun() -> sets:subtract(M, id(x)) end),
        reason(fun() -> sets:is_disjoint(id(x), #{}) end)
    ]).

%% The record fields of a dict or set without its segments.
header(Table) -> list_to_tuple(lists:sublist(tuple_to_list(Table), 7)).

id(Value) -> Value.

%% erlang:phash/2 without its deprecation warning.
phash(Term, Range) -> apply(erlang, phash, [Term, Range]).

show(Name, Values) -> io:format("~w: ~w~n", [Name, Values]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.
