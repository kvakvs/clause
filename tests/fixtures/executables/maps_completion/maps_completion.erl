-module(maps_completion).
-export([main/1]).

%% The maps functions completed after the lists completion: intersect/2 and is_iterator_valid/1 are runtime builtins,
%% the functions taking funs Erlang, and the functions over maps also take iterators. Maps print as sorted lists.
main(_) ->
    Map = #{a => 1, b => 2, c => 3},
    Kept = fun(K, V) ->
        case V of
            1 -> false;
            2 -> true;
            _ -> {true, {K, V}}
        end
    end,
    show(filtermap, [
        maps:filtermap(Kept, Map),
        maps:filtermap(Kept, maps:iterator(Map, ordered)),
        maps:filtermap(Kept, maps:next(maps:iterator(Map, reversed))),
        maps:filtermap(Kept, #{}),
        maps:filtermap(Kept, id(none))
    ]),
    Parity = fun(X) -> X rem 2 end,
    show(groups, [
        maps:groups_from_list(Parity, [1, 2, 3]),
        maps:groups_from_list(Parity, fun(X) -> X * X end, [1, 2, 3, 4, 5]),
        maps:groups_from_list(fun length/1, fun lists:reverse/1, ["ant", "buffalo", "cat", "dingo"]),
        maps:groups_from_list(fun(T) -> element(1, T) end, [{1, a}, {1.0, b}, {1, c}]),
        maps:groups_from_list(Parity, []),
        maps:groups_from_list(fun(X) -> X rem 40 end, lists:seq(1, 200)),
        calls(fun(Log) -> maps:groups_from_list(Log(key), Log(value), [x, y, z]) end)
    ]),
    Small = #{b => x},
    Large = maps:from_list([{I, I} || I <- lists:seq(1, 100)]),
    Shifted = maps:from_list([{I, -I} || I <- lists:seq(50, 150)]),
    show(intersect, [
        maps:intersect(#{a => 1, b => 2}, #{a => x, c => y}),
        maps:intersect(Map, Small),
        maps:intersect(Small, Map),
        maps:intersect(#{1 => a}, #{1.0 => b}),
        maps:intersect(#{}, Map),
        summary(maps:intersect(Large, Shifted)),
        summary(maps:intersect(Shifted, Large))
    ]),
    Pair = fun(_, V1, V2) -> {V1, V2} end,
    show(intersect_with, [
        maps:intersect_with(Pair, #{a => "one", b => "two"}, #{a => 1, c => 3}),
        maps:intersect_with(Pair, Map, Small#{b := y}),
        maps:intersect_with(Pair, Small, Map),
        maps:intersect_with(fun(K, _, _) -> K end, Map, #{}),
        summary(maps:intersect_with(fun(_, V1, V2) -> V1 + V2 end, Large, Shifted))
    ]),
    Sum = fun(_, V1, V2) -> V1 + V2 end,
    show(merge_with, [
        maps:merge_with(Sum, #{a => 1, b => 2}, #{a => 10, c => 3}),
        maps:merge_with(Pair, Map, Small),
        maps:merge_with(Pair, Small, Map),
        maps:merge_with(Pair, #{1 => a}, #{1.0 => b}),
        summary(maps:merge_with(Sum, Large, Shifted))
    ]),
    {_, _, Rest} = maps:next(maps:iterator(Map)),
    show(is_iterator_valid, [
        maps:is_iterator_valid(maps:iterator(Map)),
        maps:is_iterator_valid(maps:iterator(#{})),
        maps:is_iterator_valid(maps:iterator(Map, ordered)),
        maps:is_iterator_valid(maps:iterator(Map, fun erlang:'>='/2)),
        maps:is_iterator_valid(Rest),
        maps:is_iterator_valid(maps:next(Rest)),
        maps:is_iterator_valid(none),
        maps:is_iterator_valid({a, b, none}),
        maps:is_iterator_valid({a, b, c}),
        maps:is_iterator_valid(Map),
        maps:is_iterator_valid([]),
        maps:is_iterator_valid(a)
    ]),
    show(iterators, [
        maps:fold(fun(K, V, Acc) -> [{K, V} | Acc] end, [], maps:iterator(Map, ordered)),
        maps:fold(fun(K, _, Acc) -> [K | Acc] end, [], maps:iterator(Map, reversed)),
        maps:fold(fun(_, V, Acc) -> V + Acc end, 0, maps:next(maps:iterator(Map, ordered))),
        maps:map(fun(K, V) -> {K, V} end, maps:iterator(Map, reversed)),
        maps:filter(fun(_, V) -> V > 1 end, maps:iterator(Map, ordered)),
        calls(fun(Log) ->
            maps:foreach(fun(K, _) -> (Log(K))(K) end, maps:iterator(Map, reversed))
        end)
    ]),
    show(errors, [
        reason(fun() -> maps:filtermap(id(not_fun), #{}) end),
        reason(fun() -> maps:filtermap(Kept, id(not_map)) end),
        reason(fun() -> maps:filtermap(fun(_, _) -> other end, #{a => 1}) end),
        reason(fun() -> maps:groups_from_list(Parity, id([1 | 2])) end),
        reason(fun() -> maps:groups_from_list(id(not_fun), []) end),
        reason(fun() -> maps:groups_from_list(Parity, id(not_fun), []) end),
        reason(fun() -> maps:groups_from_list(Parity, Parity, id(atom)) end),
        reason(fun() -> maps:intersect(id(a), #{}) end),
        reason(fun() -> maps:intersect(#{}, id(b)) end),
        reason(fun() -> maps:intersect(id(a), id(b)) end),
        reason(fun() -> maps:intersect_with(id(fun(_, _) -> x end), #{}, #{}) end),
        reason(fun() -> maps:intersect_with(id(bad), id(a), #{}) end),
        reason(fun() -> maps:intersect_with(Pair, #{}, id(b)) end),
        reason(fun() -> maps:merge_with(Pair, id(a), id(b)) end),
        reason(fun() -> maps:merge_with(id(bad), #{}, #{}) end),
        reason(fun() -> maps:fold(Sum, 0, id(not_iterator)) end),
        reason(fun() -> maps:map(fun(_, V) -> V end, id([x])) end),
        reason(fun() -> maps:map(Pair, #{}) end),
        reason(fun() -> maps:foreach(fun(_, _) -> ok end, id({a, b, c})) end)
    ]),
    io:format("maps_completion: ok~n").

id(Value) -> Value.

%% The size and the sums of the keys and the values of a large map.
summary(Map) ->
    {map_size(Map), lists:sum(maps:keys(Map)), lists:sum(maps:values(Map))}.

%% The result of Run and the order of the calls of the funs Log(Tag) gives it.
calls(Run) ->
    put(calls, []),
    Log = fun(Tag) ->
        fun(X) ->
            put(calls, [{Tag, X} | get(calls)]),
            X
        end
    end,
    Result = Run(Log),
    {Result, lists:reverse(get(calls))}.

show(Name, Values) -> io:format("~w: ~w~n", [Name, [sorted(Value) || Value <- Values]]).

%% Maps as sorted lists, inside tuples too: OTP prints a map in its internal order.
sorted(Map) when is_map(Map) -> lists:sort([{K, sorted(V)} || K := V <- Map]);
sorted(Tuple) when is_tuple(Tuple) -> list_to_tuple([sorted(E) || E <- tuple_to_list(Tuple)]);
sorted(Value) -> Value.

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.
