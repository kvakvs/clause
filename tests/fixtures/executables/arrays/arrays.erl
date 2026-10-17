-module(arrays).
-export([main/1]).

%% array (plan 21 step LIB3B): results and exact records (printed, or checked through erlang:phash2/1 for large
%% ones) through set, grow, shift, slice, resize, the folds and mapfolds, and error reasons.
main(_) ->
    A0 = array:new(),
    A1 = lists:foldl(fun(I, A) -> array:set(I, I * I, A) end, A0, lists:seq(0, 20)),
    Big = lists:foldl(fun(I, A) -> array:set(I, I, A) end, A0, lists:seq(0, 600)),
    Sparse = array:set(5000, x, array:set(70, y, array:new([{default, d}]))),
    show(records, [
        A0,
        array:new(10),
        array:new([{size, 5}, {default, z}]),
        array:new(3, [fixed, {default, 0}]),
        array:new({default, q}),
        array:new([{fixed, false}, 40]),
        array:set(0, a, A0),
        array:set(17, b, A0),
        A1,
        erlang:phash2(Big),
        erlang:phash2(Sparse),
        array:from_list([a, b, c]),
        array:from_list([], x),
        erlang:phash2(array:from_list(lists:seq(1, 300))),
        array:from_orddict([{0, a}, {3, b}]),
        erlang:phash2(array:from_orddict([{2, a}, {40, b}, {41, c}, {700, d}, {5000, e}])),
        array:from(
            fun
                (N) when N < 3 -> {N * 10, N + 1};
                (_) -> done
            end,
            0
        ),
        array:from(fun(_) -> done end, 0, dflt),
        array:append(new, A1),
        erlang:phash2(lists:foldl(fun array:append/2, A0, lists:seq(1, 100))),
        array:prepend(first, array:from_list([a, b])),
        erlang:phash2(lists:foldl(fun array:prepend/2, A0, lists:seq(1, 100))),
        array:shift(2, array:from_list([a, b, c, d])),
        array:shift(-3, array:from_list([a, b])),
        array:slice(1, 2, array:from_list([a, b, c, d])),
        erlang:phash2(array:slice(100, 300, Big)),
        array:reset(3, A1),
        array:reset(30, A1),
        array:resize(5, A1),
        array:resize(40, A1),
        erlang:phash2(array:resize(1000, array:shift(50, Big))),
        erlang:phash2(array:resize(20, array:shift(250, Big))),
        array:resize(array:set(9, v, array:new(20))),
        array:fix(A1) =:= array:set(0, 0, array:fix(A1)),
        array:relax(array:new(3)),
        array:concat(array:from_list([a, b]), array:from_list([c, d, e])),
        array:concat([array:from_list([a]), array:from_list([b]), array:from_list([c])]),
        erlang:phash2(array:concat(Big, Big))
    ]),
    show(queries, [
        array:size(A1),
        array:get(4, A1),
        array:get(400, A1),
        array:get(400, Big),
        array:get(70, Sparse),
        array:get(71, Sparse),
        array:sparse_size(Sparse),
        array:sparse_size(A0),
        array:default(Sparse),
        array:is_array(A1),
        array:is_array({array}),
        array:is_fix(array:new(3)),
        array:to_list(array:from_list([a, b, c])),
        array:sparse_to_list(array:set(4, x, array:new())),
        array:to_orddict(array:set(2, x, array:new(4))),
        array:sparse_to_orddict(Sparse),
        array:foldl(fun(I, V, Acc) -> [{I, V} | Acc] end, [], array:from_list([a, b, c])),
        array:foldr(fun(I, V, Acc) -> [{I, V} | Acc] end, [], array:from_list([a, b, c])),
        array:foldl(1, 2, fun(I, _, Acc) -> [I | Acc] end, [], array:from_list([a, b, c, d])),
        array:sparse_foldl(fun(I, V, Acc) -> [{I, V} | Acc] end, [], Sparse),
        array:sparse_foldr(fun(I, V, Acc) -> [{I, V} | Acc] end, [], Sparse),
        array:foldl(fun(_, V, Acc) -> V + Acc end, 0, Big),
        array:mapfoldl(fun(I, V, Acc) -> {{I, V}, Acc + 1} end, 0, array:from_list([a, b])),
        array:mapfoldr(fun(I, V, Acc) -> {{I, V}, [I | Acc]} end, [], array:from_list([a, b])),
        array:map(fun(I, V) -> {I, V} end, array:set(20, x, array:new())),
        array:sparse_map(fun(I, V) -> {I, V} end, array:set(20, x, array:new())),
        erlang:phash2(array:map(fun(_, V) -> V end, Sparse)),
        erlang:phash2(array:sparse_map(fun(_, V) -> {V} end, Sparse)),
        array:sparse_mapfoldl(fun(I, V, Acc) -> {V, [I | Acc]} end, [], Sparse),
        array:mapfoldl(
            2, 3, fun(I, V, Acc) -> {{V}, [I | Acc]} end, [], array:from_list([a, b, c, d, e])
        ),
        array:sparse_mapfoldr(1, 70, fun(I, V, Acc) -> {V, [I | Acc]} end, [], Sparse)
    ]),
    show(errors, [
        reason(fun() -> array:get(-1, A1) end),
        reason(fun() -> array:get(5, array:new(3)) end),
        reason(fun() -> array:set(5, x, array:new(3)) end),
        reason(fun() -> array:set(a, x, A1) end),
        reason(fun() -> array:get(0, id(x)) end),
        reason(fun() -> array:new(id(-1), []) end),
        reason(fun() -> array:new(id([bad])) end),
        reason(fun() -> array:shift(5, array:new(3)) end),
        reason(fun() -> array:slice(2, 5, array:new(3)) end),
        reason(fun() -> array:from_list(id(x)) end),
        reason(fun() -> array:from_list(id([a | b])) end),
        reason(fun() -> array:from_orddict(id([{1, a}, {0, b}])) end),
        reason(fun() -> array:from_orddict(id([x])) end),
        reason(fun() -> array:from(fun(_) -> bad end, 0) end),
        reason(fun() -> array:foldl(id(x), 0, A1) end),
        reason(fun() -> array:foldl(0, 50, fun(_, _, A) -> A end, 0, A1) end),
        reason(fun() -> array:mapfoldl(fun(_, _, _) -> bad end, 0, A1) end),
        reason(fun() -> array:size(id(x)) end),
        reason(fun() -> array:fix(id(x)) end),
        reason(fun() -> array:prepend(a, id(x)) end),
        reason(fun() -> array:concat([]) end)
    ]),
    io:format("arrays: ok~n").

id(Value) -> Value.

show(Name, Values) -> io:format("~w: ~w~n", [Name, Values]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.
