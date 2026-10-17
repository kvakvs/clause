-module(ordered_collections).
-export([main/1]).

%% ordsets, orddict and proplists (plan 21 step LIB1): results, representatives of equal elements (1 and 1.0), rests
%% OTP returns unchecked, and error reasons for shapes no clause accepts.
main(_) ->
    ordsets_cases(),
    orddict_cases(),
    proplists_cases(),
    io:format("ordered_collections: ok~n").

ordsets_cases() ->
    S = ordsets:from_list([c, a, b, a]),
    show(ordsets_basic, [
        S,
        ordsets:new(),
        ordsets:size(S),
        ordsets:is_empty(S),
        ordsets:is_empty([]),
        ordsets:to_list(S),
        ordsets:is_set(S),
        ordsets:is_set([b, a]),
        ordsets:is_set([a, a]),
        ordsets:is_set([1, 1.0]),
        ordsets:is_set(id(not_a_list)),
        ordsets:is_set([]),
        ordsets:is_element(b, S),
        ordsets:is_element(d, S),
        ordsets:is_element(1.0, [1, 2]),
        ordsets:is_element(0, id([1 | x])),
        ordsets:add_element(b, S),
        ordsets:add_element(d, S),
        ordsets:add_element(0, S),
        ordsets:add_element(1.0, [1, 2]),
        ordsets:del_element(b, S),
        ordsets:del_element(z, S),
        ordsets:del_element(1.0, [1, 2]),
        ordsets:is_equal([1, 2], [1.0, 2]),
        ordsets:is_equal([1], [2])
    ]),
    show(ordsets_algebra, [
        ordsets:union([1, 3, 5], [2, 3, 4]),
        ordsets:union([1, 2.0, 3], [1.0, 2, 4]),
        ordsets:union([1, 5], [2, 3, 5.0, 6]),
        ordsets:union([], id(not_a_set)),
        ordsets:union(id([1 | tail]), []),
        ordsets:union([[1, 3], [2], [], [3, 4]]),
        ordsets:intersection([1, 2, 3], [2, 3, 4]),
        ordsets:intersection([1, 2.0, 3], [1.0, 2, 3.0]),
        ordsets:intersection([5, 7.0], [5.0, 6, 7]),
        ordsets:intersection([], id(x)),
        ordsets:intersection([[1, 2, 3], [2, 3], [3, 4]]),
        ordsets:intersection([[a, b]]),
        ordsets:intersection([id(single)]),
        ordsets:is_disjoint([1, 2], [3, 4]),
        ordsets:is_disjoint([1, 2], [2.0, 4]),
        ordsets:is_disjoint([], id(x)),
        ordsets:subtract([1, 2, 3, 4], [2, 4]),
        ordsets:subtract([1, 2.0], [2]),
        ordsets:subtract(id([1, 2 | tail]), [0]),
        ordsets:subtract([1, 2], []),
        ordsets:is_subset([1, 3], [1, 2, 3]),
        ordsets:is_subset([1, 4], [1, 2, 3]),
        ordsets:is_subset([1.0], [1]),
        ordsets:is_subset([], id(x))
    ]),
    show(ordsets_funs, [
        ordsets:fold(fun(E, A) -> [E | A] end, [], S),
        ordsets:filter(fun(E) -> E =/= b end, S),
        ordsets:map(fun(E) -> E rem 3 end, [1, 2, 3, 4, 5]),
        ordsets:filtermap(fun(E) -> E > 2 andalso {true, -E} end, [1, 2, 3, 4])
    ]),
    show(ordsets_errors, [
        reason(fun() -> ordsets:is_set(id([1, 2 | x])) end),
        reason(fun() -> ordsets:is_element(9, id([1 | x])) end),
        reason(fun() -> ordsets:add_element(9, id([1 | x])) end),
        reason(fun() -> ordsets:del_element(9, id(x)) end),
        reason(fun() -> ordsets:union(id(x), [1]) end),
        reason(fun() -> ordsets:union([1], id(x)) end),
        reason(fun() -> ordsets:intersection(id(x), [1]) end),
        reason(fun() -> ordsets:intersection(id([])) end),
        reason(fun() -> ordsets:intersection(id([[1] | x])) end),
        reason(fun() -> ordsets:is_disjoint(id(x), [1]) end),
        reason(fun() -> ordsets:subtract(id(x), [1]) end),
        reason(fun() -> ordsets:is_subset([1], id(x)) end),
        reason(fun() -> ordsets:is_equal(id(x), []) end),
        reason(fun() -> ordsets:size(id([1 | x])) end)
    ]).

orddict_cases() ->
    D = orddict:from_list([{b, 2}, {a, 1}, {c, 3}, {a, 10}]),
    show(orddict_basic, [
        D,
        orddict:from_list([]),
        orddict:from_list([{x, 1}]),
        orddict:from_list([{1, a}, {1.0, b}, {0, c}]),
        orddict:new(),
        orddict:size(D),
        orddict:is_empty(D),
        orddict:is_empty([]),
        orddict:to_list(D),
        orddict:is_key(b, D),
        orddict:is_key(z, D),
        orddict:is_key(1.0, [{1, a}]),
        orddict:fetch(b, D),
        orddict:fetch(1.0, [{1, one}]),
        orddict:find(c, D),
        orddict:find(z, D),
        orddict:find(0, D),
        orddict:fetch_keys(D),
        orddict:erase(b, D),
        orddict:erase(z, D),
        orddict:erase(1.0, [{1, a}, {2, b}]),
        orddict:take(b, D),
        orddict:take(z, D),
        orddict:take(0, D)
    ]),
    show(orddict_updates, [
        orddict:store(b, new, D),
        orddict:store(0, new, D),
        orddict:store(z, new, D),
        orddict:store(1.0, new, [{1, old}]),
        orddict:append(a, x, [{a, [1]}, {b, []}]),
        orddict:append(z, x, D),
        orddict:append_list(a, [x, y], [{a, [1]}]),
        orddict:append_list(a, tail, [{a, [1]}]),
        orddict:append_list(z, [x], D),
        orddict:update_counter(a, 5, D),
        orddict:update_counter(z, 5, D),
        orddict:update_counter(a, 0.5, [{a, 1}]),
        orddict:update(b, fun(V) -> V * 100 end, D),
        orddict:update(z, fun(V) -> V end, init, D),
        orddict:update(a, fun(V) -> V + 1 end, init, D)
    ]),
    show(orddict_funs, [
        orddict:fold(fun(K, V, A) -> [{V, K} | A] end, [], D),
        orddict:map(fun(K, V) -> {K, V} end, D),
        orddict:filter(fun(_, V) -> V > 2 end, D),
        orddict:merge(fun(_, V1, V2) -> V1 + V2 end, [{a, 1}, {c, 3}], [{a, 10}, {b, 20}]),
        orddict:merge(fun(K, _, _) -> K end, [], [{x, 1}]),
        orddict:merge(fun(K, _, _) -> K end, [{x, 1}], [])
    ]),
    show(orddict_errors, [
        reason(fun() -> orddict:fetch(z, D) end),
        reason(fun() -> orddict:fetch(0, D) end),
        reason(fun() -> orddict:is_key(a, id([x])) end),
        reason(fun() -> orddict:find(a, id([{a, 1, 2}])) end),
        reason(fun() -> orddict:find(z, id([{a, 1} | x])) end),
        reason(fun() -> orddict:from_list(id([{a, 1}, b])) end),
        reason(fun() -> orddict:from_list(id([b])) end),
        reason(fun() -> orddict:from_list(id(not_a_list)) end),
        reason(fun() -> orddict:fetch_keys(id([{a, 1} | x])) end),
        reason(fun() -> orddict:store(z, 1, id([{a, 1} | x])) end),
        reason(fun() -> orddict:append(a, x, id([{a, [1 | t]}])) end),
        reason(fun() -> orddict:update_counter(a, 1, id([{a, x}])) end),
        reason(fun() -> orddict:update(z, fun(V) -> V end, D) end),
        reason(fun() -> orddict:update(a, id(not_fun), D) end),
        reason(fun() -> orddict:is_empty(id(x)) end),
        reason(fun() -> orddict:fold(id(not_fun), 0, []) end),
        reason(fun() -> orddict:map(fun(K, V) -> {K, V} end, id([x])) end),
        reason(fun() -> orddict:filter(fun(_, _) -> other end, [{a, 1}]) end),
        reason(fun() -> orddict:merge(id(not_fun), [], []) end)
    ]).

proplists_cases() ->
    L = [a, {b, 1}, {c, 2, 3}, {a, false}, "str", {b, 2}, {d, true}, d, {}],
    show(proplists_lookups, [
        proplists:property({a, true}),
        proplists:property({"a", true}),
        proplists:property({a, x}),
        proplists:property(a, true),
        proplists:property(a, false),
        proplists:property("a", true),
        proplists:lookup(a, L),
        proplists:lookup(b, L),
        proplists:lookup(c, L),
        proplists:lookup(z, L),
        proplists:lookup_all(a, L),
        proplists:lookup_all(b, L),
        proplists:is_defined(c, L),
        proplists:is_defined(z, L),
        proplists:get_value(a, L),
        proplists:get_value(b, L),
        proplists:get_value(c, L),
        proplists:get_value(c, L, dflt),
        proplists:get_value(z, L),
        proplists:get_value(z, L, dflt),
        proplists:get_value(1, [{1.0, float}, {1, int}]),
        proplists:get_all_values(a, L),
        proplists:get_all_values(b, L),
        proplists:get_all_values(c, L),
        proplists:append_values(a, L),
        proplists:append_values(x, [{x, [1, 2]}, x, {x, 3}, {x, []}, {x, [4]}]),
        proplists:get_bool(a, L),
        proplists:get_bool(b, L),
        proplists:get_bool(d, L),
        proplists:get_bool(z, L),
        lists:sort(proplists:get_keys(L)),
        proplists:get_keys([]),
        proplists:delete(a, L),
        proplists:delete(b, L)
    ]),
    show(proplists_forms, [
        proplists:unfold([a, {b, 1}, "s", c]),
        proplists:compact([{a, true}, {b, true, x}, {"s", true}, {c, false}, d]),
        proplists:substitute_aliases([{a, alpha}, {b, beta}], [a, {a, 1}, {b, true}, c, {b, 2, 3}]),
        proplists:substitute_aliases([{a, "alpha"}], [a]),
        proplists:substitute_negations([{no_a, a}], [
            no_a, {no_a, true}, {no_a, false}, {no_a, 7}, b
        ]),
        proplists:expand([{foo, [bar, baz]}], [fie, foo, fum]),
        proplists:expand([{{foo, true}, [bar, baz]}], [fie, {foo, true}, fum]),
        proplists:expand([{{foo, false}, [bar, baz]}], [{foo, false}, fie, foo, fum]),
        proplists:expand([{{foo, false}, [bar, baz]}], [{foo, true}, fie, foo, fum]),
        proplists:expand([{a, [x]}, {a, [y]}, skipped], [a, b, a]),
        proplists:expand([], [[1, 2], 3, []]),
        proplists:normalize([a, {b, true}, no_c, {d, 1}], [
            {aliases, [{b, bee}]}, {negations, [{no_c, c}]}, {expand, [{{d, 1}, [{e, 1}]}]}
        ]),
        proplists:normalize([{a, true}], []),
        proplists:split([{c, 2}, {e, 1}, a, {c, 3, 4}, d, {b, 5}, b], [a, b, c]),
        proplists:split([a, b], [a, a]),
        proplists:split([], []),
        lists:sort(maps:to_list(proplists:to_map([a, {b, 1}, {c, 2}, {c, 3}, {b}, {b, 4}, "x"]))),
        lists:sort(maps:to_list(proplists:to_map([{b, 1}, {b}]))),
        lists:sort(maps:to_list(proplists:to_map([no_a, {b, 1}], [{negations, [{no_a, a}]}]))),
        lists:sort(proplists:from_map(#{a => 1, b => 2}))
    ]),
    show(proplists_errors, [
        reason(fun() -> proplists:lookup(a, id([b | x])) end),
        reason(fun() -> proplists:get_value(a, id(x)) end),
        reason(fun() -> proplists:delete(a, id([a | x])) end),
        reason(fun() -> proplists:compact(id(x)) end),
        reason(fun() -> proplists:compact(id([a | x])) end),
        reason(fun() -> proplists:unfold(id([a | x])) end),
        reason(fun() -> proplists:append_values(a, id([{a, [1 | t]}])) end),
        reason(fun() -> proplists:substitute_aliases(id([x]), [a]) end),
        reason(fun() -> proplists:substitute_aliases(id([x]), []) end),
        reason(fun() -> proplists:expand([], id(x)) end),
        reason(fun() -> proplists:expand(id(x), []) end),
        reason(fun() -> proplists:split(id([a | x]), [a]) end),
        reason(fun() -> proplists:split([], id(x)) end),
        reason(fun() -> proplists:to_map(id([a | x])) end),
        reason(fun() -> proplists:normalize([], id([bad])) end),
        reason(fun() -> proplists:get_keys(id(x)) end)
    ]).

id(Value) -> Value.

show(Name, Values) -> io:format("~w: ~w~n", [Name, Values]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.
