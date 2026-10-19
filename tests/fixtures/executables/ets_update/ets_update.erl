-module(ets_update).
-export([main/1, id/1]).

%% ETS bags and updates (plan 21 step LIB10C): bag and duplicate_bag inserts, delete_object/2, take/2,
%% delete_all_objects/1, lookup_element/3,4, update_element/3,4 and update_counter/3,4 with their errors, over every
%% table type.
main(_) ->
    [bags(Type) || Type <- [bag, duplicate_bag]],
    [objects(Type) || Type <- [set, ordered_set, bag, duplicate_bag]],
    [elements(Type) || Type <- [set, ordered_set, bag, duplicate_bag]],
    [update_elements(Type) || Type <- [set, ordered_set, bag]],
    [counters(Type) || Type <- [set, ordered_set, bag]],
    defaults(),
    io:format("ets_update: ok~n").

id(X) -> X.

show(Label, Values) -> io:format("~p: ~w~n", [Label, Values]).

attempt(F) ->
    try F() of
        Value -> {ok, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

bags(Type) ->
    T = ets:new(bags, [Type]),
    show(Type, [
        ets:insert(T, {k, 1}),
        ets:insert(T, {k, 2}),
        ets:insert(T, {k, 1}),
        ets:lookup(T, k),
        ets:insert(T, [{k, 3}, {k, 1}, {j, 1}, {k, 3}]),
        ets:lookup(T, k),
        ets:insert(T, {k, 1.0}),
        ets:lookup(T, k),
        ets:info(T, size),
        ets:insert_new(T, {k, 9}),
        ets:insert_new(T, [{m, 1}, {m, 2}, {m, 1}]),
        ets:lookup(T, m),
        ets:member(T, m),
        ets:delete(T, m),
        ets:lookup(T, m),
        ets:info(T, size),
        lists:sort(ets:tab2list(T))
    ]),
    ets:delete(T).

objects(Type) ->
    T = ets:new(objects, [Type]),
    ets:insert(T, [{a, 1}, {a, 2}, {b, 1}, {c, 1}]),
    case Type of
        duplicate_bag -> ets:insert(T, [{a, 1}, {a, 1}]);
        _ -> ok
    end,
    show(objects, [
        Type,
        ets:delete_object(T, {a, 1}),
        ets:lookup(T, a),
        ets:delete_object(T, {b, 2}),
        ets:lookup(T, b),
        ets:delete_object(T, {z, 1}),
        ets:delete_object(T, {a, 1.0}),
        ets:lookup(T, a),
        attempt(fun() -> ets:delete_object(T, id(notuple)) end),
        attempt(fun() -> ets:delete_object(T, id({})) end),
        ets:take(T, b),
        ets:take(T, b),
        ets:take(T, zz),
        ets:info(T, size),
        ets:delete_all_objects(T),
        ets:info(T, size),
        ets:tab2list(T),
        ets:insert(T, {after_clear, 1}),
        ets:tab2list(T)
    ]),
    ets:delete(T).

elements(Type) ->
    T = ets:new(elements, [Type]),
    ets:insert(T, [{a, 1, x}, {b, 2}]),
    case Type of
        bag -> ets:insert(T, [{a, 2, y}, {c, 1}, {c, 2, z}]);
        duplicate_bag -> ets:insert(T, [{a, 1, x}, {c, 1}, {c, 2, z}]);
        _ -> ets:insert(T, [{c, 1}])
    end,
    show(elements, [
        Type,
        attempt(fun() -> ets:lookup_element(T, a, 1) end),
        attempt(fun() -> ets:lookup_element(T, a, 2) end),
        attempt(fun() -> ets:lookup_element(T, a, 3) end),
        attempt(fun() -> ets:lookup_element(T, a, 4) end),
        attempt(fun() -> ets:lookup_element(T, a, id(0)) end),
        attempt(fun() -> ets:lookup_element(T, a, id(x)) end),
        attempt(fun() -> ets:lookup_element(T, zz, 2) end),
        attempt(fun() -> ets:lookup_element(T, c, 2) end),
        attempt(fun() -> ets:lookup_element(T, c, 3) end),
        attempt(fun() -> ets:lookup_element(T, zz, 2, default) end),
        attempt(fun() -> ets:lookup_element(T, a, 2, default) end),
        attempt(fun() -> ets:lookup_element(T, a, 9, default) end),
        attempt(fun() -> ets:lookup_element(T, zz, id(0), default) end)
    ]),
    ets:delete(T).

update_elements(Type) ->
    T = ets:new(update_elements, [Type, {keypos, 2}]),
    ets:insert(T, [{a, k, 1, 2}, {b, j, 1}]),
    show(update_element, [
        Type,
        attempt(fun() -> ets:update_element(T, k, {3, three}) end),
        ets:lookup(T, k),
        attempt(fun() -> ets:update_element(T, k, [{1, first}, {4, four}, {1, again}]) end),
        ets:lookup(T, k),
        attempt(fun() -> ets:update_element(T, zz, {3, x}) end),
        attempt(fun() -> ets:update_element(T, k, {2, key}) end),
        attempt(fun() -> ets:update_element(T, k, {5, x}) end),
        attempt(fun() -> ets:update_element(T, k, {id(0), x}) end),
        attempt(fun() -> ets:update_element(T, k, [{3, ok}, {7, x}]) end),
        ets:lookup(T, k),
        attempt(fun() -> ets:update_element(T, k, id(bad)) end),
        attempt(fun() -> ets:update_element(T, k, []) end),
        attempt(fun() -> ets:update_element(T, zz, {3, x}, {d, zz, 0}) end),
        ets:lookup(T, zz),
        attempt(fun() -> ets:update_element(T, yy, {3, x}, {d, other, 0, 0}) end),
        ets:lookup(T, yy),
        attempt(fun() -> ets:update_element(T, xx, {3, x}, {d}) end),
        attempt(fun() -> ets:update_element(T, k, {3, x}, {d, k, 0}) end),
        ets:lookup(T, k)
    ]),
    ets:delete(T).

counters(Type) ->
    T = ets:new(counters, [Type]),
    ets:insert(T, [{c, 0, 10, x}, {big, 1 bsl 62}]),
    show(update_counter, [
        Type,
        attempt(fun() -> ets:update_counter(T, c, 1) end),
        attempt(fun() -> ets:update_counter(T, c, -5) end),
        attempt(fun() -> ets:update_counter(T, c, {3, 5}) end),
        attempt(fun() -> ets:update_counter(T, c, [{2, 1}, {3, -1}, {2, 10}]) end),
        attempt(fun() -> ets:update_counter(T, c, []) end),
        ets:lookup(T, c),
        attempt(fun() -> ets:update_counter(T, c, {2, 100, 50, 7}) end),
        attempt(fun() -> ets:update_counter(T, c, {2, -100, -50, -7}) end),
        attempt(fun() -> ets:update_counter(T, c, {3, 1, 14, 0}) end),
        attempt(fun() -> ets:update_counter(T, c, {3, 1, 14, 0}) end),
        ets:lookup(T, c),
        attempt(fun() -> ets:update_counter(T, big, 1 bsl 62) end),
        attempt(fun() -> ets:update_counter(T, big, -(1 bsl 64)) end),
        attempt(fun() -> ets:update_counter(T, c, 1.5) end),
        attempt(fun() -> ets:update_counter(T, c, {4, 1}) end),
        attempt(fun() -> ets:update_counter(T, c, {1, 1}) end),
        attempt(fun() -> ets:update_counter(T, c, {5, 1}) end),
        attempt(fun() -> ets:update_counter(T, c, {2, 1, x, 0}) end),
        attempt(fun() -> ets:update_counter(T, c, [{2, 1}, {4, 1}]) end),
        ets:lookup(T, c),
        attempt(fun() -> ets:update_counter(T, zz, 1) end),
        attempt(fun() -> ets:update_counter(T, c, id(bad)) end),
        attempt(fun() -> ets:update_counter(T, c, {id(two), 1}) end)
    ]),
    ets:delete(T).

defaults() ->
    T = ets:new(defaults, [{keypos, 2}]),
    O = ets:new(defaults, [ordered_set]),
    show(defaults, [
        attempt(fun() -> ets:update_counter(T, k, 1, {x, k, 10}) end),
        ets:lookup(T, k),
        attempt(fun() -> ets:update_counter(T, k, 1, {x, k, 10}) end),
        attempt(fun() -> ets:update_counter(T, j, {3, 2}, {x, other, 10}) end),
        ets:lookup(T, j),
        attempt(fun() -> ets:update_counter(T, i, {3, 2}, {x}) end),
        attempt(fun() -> ets:update_counter(T, i, {3, 2}, {x, i}) end),
        attempt(fun() -> ets:update_counter(T, i, {3, 2}, {x, i, y}) end),
        attempt(fun() -> ets:update_counter(T, i, {3, 2}, id(notuple)) end),
        ets:lookup(T, i),
        attempt(fun() -> ets:update_counter(O, 1.0, 1, {1, 0}) end),
        attempt(fun() -> ets:update_counter(O, 1, 1, {1, 0}) end),
        ets:tab2list(O)
    ]),
    ets:delete(T),
    ets:delete(O).
