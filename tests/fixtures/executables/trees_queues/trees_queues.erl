-module(trees_queues).
-export([main/1]).

%% gb_trees, gb_sets and queue (plan 21 step LIB3): results and exact internal shapes (printed, or checked through
%% erlang:phash2/1 for large ones), representatives of equal elements, and error reasons.
main(_) ->
    trees(),
    sets(),
    queues(),
    io:format("trees_queues: ok~n").

%% A deterministic permutation of 1..N.
mixed(N) -> [(I * 37) rem (N + 1) || I <- lists:seq(1, N)].

trees() ->
    T0 = gb_trees:empty(),
    T1 = lists:foldl(fun(K, T) -> gb_trees:insert(K, K * 10, T) end, T0, [5, 3, 8, 1, 4, 7, 9, 2, 6]),
    Up = lists:foldl(fun(K, T) -> gb_trees:insert(K, K, T) end, T0, lists:seq(1, 200)),
    Mixed = lists:foldl(fun(K, T) -> gb_trees:enter(K, -K, T) end, T0, mixed(150)),
    Shrunk = lists:foldl(fun(K, T) -> gb_trees:delete(K, T) end, Up, lists:seq(1, 200, 3)),
    show(trees_shapes, [
        T1,
        lists:foldl(fun(K, T) -> gb_trees:insert(K, K, T) end, T0, lists:seq(1, 12)),
        erlang:phash2(Up),
        erlang:phash2(Mixed),
        erlang:phash2(Shrunk),
        gb_trees:balance(T1),
        erlang:phash2(gb_trees:balance(Shrunk)),
        gb_trees:from_list([{b, 2}, {a, 1}, {c, 3}, {a, 10}]),
        gb_trees:from_orddict([{1, a}, {2, b}, {3, c}, {4, d}, {5, e}]),
        erlang:phash2(gb_trees:from_list([{K, K} || K <- mixed(99)])),
        gb_trees:update(4, new, T1),
        gb_trees:enter(4, entered, T1),
        gb_trees:delete(5, T1),
        gb_trees:delete_any(42, T1),
        gb_trees:take(3, T1),
        gb_trees:take_any(42, T1),
        gb_trees:take_smallest(T1),
        gb_trees:take_largest(T1),
        gb_trees:map(fun(K, V) -> {K, V} end, T1)
    ]),
    show(trees_queries, [
        gb_trees:size(T1),
        gb_trees:is_empty(T0),
        gb_trees:is_empty(T1),
        gb_trees:lookup(4, T1),
        gb_trees:lookup(4.0, T1),
        gb_trees:lookup(42, T1),
        gb_trees:get(7, T1),
        gb_trees:is_defined(2, T1),
        gb_trees:is_defined(0, T1),
        gb_trees:keys(T1),
        gb_trees:values(T1),
        gb_trees:to_list(T1),
        gb_trees:smallest(T1),
        gb_trees:largest(T1),
        gb_trees:smaller(5, T1),
        gb_trees:smaller(1, T1),
        gb_trees:smaller(4.5, T1),
        gb_trees:larger(5, T1),
        gb_trees:larger(9, T1),
        walk(gb_trees:iterator(T1), []),
        walk(gb_trees:iterator(T1, reversed), []),
        walk(gb_trees:iterator_from(4, T1), []),
        walk(gb_trees:iterator_from(4.5, T1, reversed), []),
        gb_trees:iterator(T1),
        gb_trees:next(gb_trees:iterator(T0)),
        lists:sum(gb_trees:values(Mixed))
    ]),
    show(trees_errors, [
        reason(fun() -> gb_trees:insert(5, x, T1) end),
        reason(fun() -> gb_trees:get(42, T1) end),
        reason(fun() -> gb_trees:update(42, x, T1) end),
        reason(fun() -> gb_trees:delete(42, T1) end),
        reason(fun() -> gb_trees:take(42, T1) end),
        reason(fun() -> gb_trees:smallest(T0) end),
        reason(fun() -> gb_trees:take_largest(T0) end),
        reason(fun() -> gb_trees:size(id(x)) end),
        reason(fun() -> gb_trees:lookup(1, id(x)) end),
        reason(fun() -> gb_trees:from_orddict(id([{2, b}, {1, a}])) end),
        reason(fun() -> gb_trees:from_orddict(id([{1, a} | x])) end),
        reason(fun() -> gb_trees:from_list(id([x])) end),
        reason(fun() -> gb_trees:iterator(T1, id(sideways)) end),
        reason(fun() -> gb_trees:map(id(x), T1) end)
    ]).

walk(Iterator, Acc) ->
    case gb_trees:next(Iterator) of
        {K, V, Next} -> walk(Next, [{K, V} | Acc]);
        none -> lists:reverse(Acc)
    end.

walk_set(Iterator, Acc) ->
    case gb_sets:next(Iterator) of
        {E, Next} -> walk_set(Next, [E | Acc]);
        none -> lists:reverse(Acc)
    end.

sets() ->
    S0 = gb_sets:new(),
    S1 = gb_sets:from_list([5, 3, 8, 1, 4, 7, 9, 2, 6, 3]),
    Big = gb_sets:from_list(lists:seq(1, 100)),
    Odd = gb_sets:from_list(lists:seq(1, 300, 2)),
    Grown = lists:foldl(fun gb_sets:add/2, S0, mixed(120)),
    show(sets_shapes, [
        S1,
        gb_sets:singleton(x),
        erlang:phash2(Grown),
        erlang:phash2(lists:foldl(fun gb_sets:insert/2, S0, lists:seq(1, 64))),
        gb_sets:union(S1, gb_sets:from_list([0, 4.0, 10])),
        gb_sets:union(gb_sets:from_list([0, 4.0, 10]), S1),
        erlang:phash2(gb_sets:union(Big, Odd)),
        erlang:phash2(gb_sets:union(gb_sets:from_list([1.0, 2.0]), Big)),
        gb_sets:union([S1, gb_sets:singleton(20), gb_sets:singleton(30)]),
        gb_sets:union([]),
        gb_sets:intersection(S1, gb_sets:from_list([2.0, 4, 11])),
        erlang:phash2(gb_sets:intersection(Big, Odd)),
        erlang:phash2(gb_sets:intersection(gb_sets:from_list([3.0, 5.0, 77.0]), Big)),
        gb_sets:intersection([S1, gb_sets:from_list([1, 2, 3]), gb_sets:from_list([2, 3, 4])]),
        gb_sets:difference(S1, gb_sets:from_list([2, 4.0, 6])),
        erlang:phash2(gb_sets:difference(Big, Odd)),
        erlang:phash2(gb_sets:subtract(gb_sets:from_list(lists:seq(1, 12)), Big)),
        gb_sets:delete(5, S1),
        gb_sets:delete_any(42, S1),
        gb_sets:del_element(3, S1),
        gb_sets:add(3.0, S1),
        gb_sets:balance(S1),
        gb_sets:take_smallest(S1),
        gb_sets:take_largest(S1),
        gb_sets:from_ordset([a, b, c, d]),
        gb_sets:filter(fun(E) -> E rem 2 =:= 0 end, S1),
        gb_sets:map(fun(E) -> E div 3 end, S1),
        gb_sets:filtermap(
            fun
                (E) when E > 5 -> {true, -E};
                (_) -> false
            end,
            S1
        )
    ]),
    show(sets_queries, [
        gb_sets:size(S1),
        gb_sets:is_empty(S0),
        gb_sets:is_set(S1),
        gb_sets:is_set(x),
        gb_sets:is_member(4.0, S1),
        gb_sets:is_element(42, S1),
        gb_sets:is_equal(S1, gb_sets:from_list([1.0, 2, 3, 4, 5, 6, 7, 8, 9])),
        gb_sets:is_equal(S1, Big),
        gb_sets:is_subset(S1, Big),
        gb_sets:is_subset(Big, S1),
        gb_sets:is_disjoint(S1, gb_sets:from_list([20, 30])),
        gb_sets:is_disjoint(S1, Odd),
        gb_sets:to_list(S1),
        gb_sets:smallest(S1),
        gb_sets:largest(S1),
        gb_sets:smaller(5, S1),
        gb_sets:larger(5.5, S1),
        gb_sets:larger(9, S1),
        gb_sets:fold(fun(E, A) -> [E | A] end, [], S1),
        walk_set(gb_sets:iterator(S1), []),
        walk_set(gb_sets:iterator(S1, reversed), []),
        walk_set(gb_sets:iterator_from(6, S1), []),
        walk_set(gb_sets:iterator_from(6, S1, reversed), [])
    ]),
    show(sets_errors, [
        reason(fun() -> gb_sets:insert(5, S1) end),
        reason(fun() -> gb_sets:delete(42, S1) end),
        reason(fun() -> gb_sets:smallest(S0) end),
        reason(fun() -> gb_sets:from_ordset(id([2, 1])) end),
        reason(fun() -> gb_sets:from_ordset(id([1 | x])) end),
        reason(fun() -> gb_sets:union(id(x), S1) end),
        reason(fun() -> gb_sets:intersection(id([])) end),
        reason(fun() -> gb_sets:filter(fun(_) -> 1 end, S1) end),
        reason(fun() -> gb_sets:is_member(1, id(x)) end)
    ]).

queues() ->
    Q0 = queue:new(),
    Q1 = queue:from_list([1, 2, 3, 4, 5]),
    Q2 = lists:foldl(fun queue:in/2, Q0, [a, b, c]),
    show(queue_ops, [
        Q1,
        Q2,
        queue:from_list([]),
        queue:from_list([x]),
        queue:from_list([x, y]),
        queue:in(z, queue:from_list([x])),
        queue:in(z, Q0),
        queue:in_r(z, queue:from_list([x])),
        queue:in_r(z, Q2),
        queue:out(Q0),
        queue:out(Q1),
        queue:out(Q2),
        queue:out({[c, b, a], []}),
        queue:out({[c, b, a], [z]}),
        queue:out_r(Q1),
        queue:out_r({[], [a, b, c]}),
        queue:out_r({[x], [a, b, c, d]}),
        queue:get(Q2),
        queue:get_r(Q2),
        queue:get({[c, b, a], []}),
        queue:get_r({[], [a, b, c]}),
        queue:peek(Q0),
        queue:peek(Q1),
        queue:peek_r(Q1),
        queue:drop(Q1),
        queue:drop({[c, b, a], []}),
        queue:drop_r(Q1),
        queue:drop_r({[], [a, b, c]}),
        queue:to_list(Q1),
        queue:len(Q1),
        queue:is_empty(Q0),
        queue:is_queue(Q1),
        queue:is_queue({a, []}),
        queue:member(3, Q1),
        queue:member(3.0, Q1),
        queue:reverse(Q1),
        queue:join(Q1, Q2),
        queue:join(Q0, Q2),
        queue:join(Q1, Q0),
        [queue:split(N, Q1) || N <- lists:seq(0, 5)],
        queue:split(2, lists:foldl(fun queue:in/2, Q0, lists:seq(1, 7))),
        queue:split(5, lists:foldl(fun queue:in/2, Q0, lists:seq(1, 7))),
        queue:delete(3, Q1),
        queue:delete(1, Q1),
        queue:delete(5, Q1),
        queue:delete(9, Q1),
        queue:delete_r(3, Q1),
        queue:delete_r(5, queue:from_list([5, 1, 5])),
        queue:delete_with(fun(E) -> E > 2 end, Q1),
        queue:delete_with_r(fun(E) -> E > 2 end, Q1),
        queue:filter(fun(E) -> E rem 2 =:= 1 end, Q1),
        queue:filter(fun(E) -> [E, E] end, Q1),
        queue:filter(fun(E) -> E > 3 end, Q1),
        queue:filtermap(
            fun
                (E) when E > 2 -> {true, E * 10};
                (_) -> false
            end,
            Q1
        ),
        queue:fold(fun(E, A) -> [E | A] end, [], Q1),
        queue:any(fun(E) -> E > 4 end, Q1),
        queue:all(fun(E) -> E > 4 end, Q1),
        queue:cons(0, Q1),
        queue:head(Q1),
        queue:tail(Q1),
        queue:snoc(Q1, 6),
        queue:daeh(Q1),
        queue:last(Q1),
        queue:liat(Q1),
        queue:init(Q1),
        erlang:phash2(
            lists:foldl(
                fun(I, Q) -> queue:drop(queue:in(I, queue:in(I, Q))) end, Q0, lists:seq(1, 50)
            )
        )
    ]),
    show(queue_errors, [
        reason(fun() -> queue:get(Q0) end),
        reason(fun() -> queue:drop(Q0) end),
        reason(fun() -> queue:drop_r(Q0) end),
        reason(fun() -> queue:head(Q0) end),
        reason(fun() -> queue:in(x, id(x)) end),
        reason(fun() -> queue:len(id({a, b})) end),
        reason(fun() -> queue:from_list(id(x)) end),
        reason(fun() -> queue:split(9, Q1) end),
        reason(fun() -> queue:split(id(-1), Q1) end),
        reason(fun() -> queue:filter(fun(_) -> 1 end, Q1) end),
        reason(fun() -> queue:join(Q1, id(x)) end),
        reason(fun() -> queue:delete_with(fun(_) -> 1 end, Q1) end)
    ]).

id(Value) -> Value.

show(Name, Values) -> io:format("~w: ~w~n", [Name, Values]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.
