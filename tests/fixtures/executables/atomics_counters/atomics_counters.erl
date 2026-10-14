-module(atomics_counters).
-export([main/1]).

%% atomics, counters and the erts_internal helpers stdlib calls (plan step
%% 41N). Arrays are shared by every copy of their reference: across
%% processes, in persistent terms and after garbage collections. Map
%% iteration is checked through ordered iterators and sorted results, since
%% OTP's order for small maps follows atom indexes.
main([]) ->
    atomics(),
    sharing(),
    counters(),
    helpers(),
    iterators(),
    purger(),
    io:format("atomics_counters: ok~n").

atomics() ->
    A = atomics:new(3, []),
    U = atomics:new(2, [{signed, false}]),
    io:format("~p~n", [
        [
            atomics:info(A),
            atomics:info(U),
            is_reference(A),
            atomics:get(A, 1),
            atomics:add_get(A, 1, 5),
            atomics:sub_get(A, 1, 7),
            atomics:put(A, 2, 9223372036854775807),
            atomics:add_get(A, 2, 1),
            atomics:exchange(A, 3, 4),
            atomics:compare_exchange(A, 3, 4, 8),
            atomics:compare_exchange(A, 3, 4, 9),
            atomics:get(A, 3),
            atomics:add(A, 1, 1),
            atomics:sub(A, 1, 1),
            atomics:add_get(A, 1, (1 bsl 64) - 1),
            atomics:put(U, 1, 18446744073709551615),
            atomics:add_get(U, 1, 2),
            atomics:sub_get(U, 2, 1),
            atomics:add_get(U, 1, -(1 bsl 63)),
            outcome(fun() -> atomics:put(U, 1, -1) end),
            outcome(fun() -> atomics:put(A, 1, 1 bsl 63) end),
            outcome(fun() -> atomics:get(A, 4) end),
            outcome(fun() -> atomics:get(A, 0) end),
            outcome(fun() -> atomics:get(A, x) end),
            outcome(fun() -> atomics:new(0, []) end),
            outcome(fun() -> atomics:new(x, []) end),
            outcome(fun() -> atomics:new(2, [x]) end),
            outcome(fun() -> atomics:new(2, {signed, true}) end),
            outcome(fun() -> atomics:add(A, 1, 1 bsl 64) end),
            outcome(fun() -> atomics:add(A, 1, -(1 bsl 63) - 1) end),
            outcome(fun() -> atomics:exchange(U, 1, -1) end),
            outcome(fun() -> atomics:compare_exchange(A, 1, 1 bsl 63, 0) end),
            outcome(fun() -> atomics:get(make_ref(), 1) end),
            outcome(fun() -> atomics:info(x) end),
            is_reference(atomics:new(2, [{signed, true}, {signed, false}]))
        ]
    ]).

%% Copies of a reference in other processes, a persistent term and after
%% collections all name the same array.
sharing() ->
    A = atomics:new(1, []),
    Parent = self(),
    Workers = [
        spawn(fun() ->
            [atomics:add(A, 1, 1) || _ <- lists:seq(1, 1000)],
            Parent ! done
        end)
     || _ <- lists:seq(1, 4)
    ],
    [
        receive
            done -> ok
        end
     || _ <- Workers
    ],
    persistent_term:put({?MODULE, array}, A),
    garbage_collect(),
    atomics:add(persistent_term:get({?MODULE, array}), 1, 1),
    [atomics:new(8, []) || _ <- lists:seq(1, 200)],
    garbage_collect(),
    Copy = binary_to_term(term_to_binary(A)),
    io:format("~p~n", [
        [
            atomics:get(A, 1),
            A =:= persistent_term:get({?MODULE, array}),
            Copy =:= A,
            outcome(fun() -> atomics:get(Copy, 1) end)
        ]
    ]).

counters() ->
    C = counters:new(2, []),
    W = counters:new(2, [write_concurrency]),
    io:format("~p~n", [
        [
            counters:info(C),
            maps:get(size, counters:info(W)),
            element(1, C),
            element(1, W),
            counters:new(1, [atomics]) =/= C,
            counters:add(C, 1, 5),
            counters:sub(W, 2, 3),
            counters:get(C, 1),
            counters:get(W, 2),
            counters:put(C, 2, -1),
            counters:put(W, 1, 3),
            counters:get(C, 2),
            counters:get(W, 1),
            counters:add(C, 1, 1 bsl 63),
            counters:sub(C, 1, -(1 bsl 63)),
            outcome(fun() -> counters:new(2, [atomics, write_concurrency]) end),
            outcome(fun() -> counters:new(0, []) end),
            outcome(fun() -> counters:new(1, [x]) end),
            outcome(fun() -> counters:get(C, 3) end),
            outcome(fun() -> counters:add(C, 1, x) end),
            outcome(fun() -> counters:add(C, 1, 1 bsl 64) end),
            outcome(fun() -> counters:get({atomics, make_ref()}, 1) end),
            outcome(fun() -> counters:info(x) end)
        ]
    ]).

helpers() ->
    Big = erts_internal:list_to_integer("99999999999999999999z", 10),
    io:format("~p~n", [
        [
            erts_internal:list_to_integer("-12ab", 10),
            erts_internal:list_to_integer("+", 10),
            erts_internal:list_to_integer("-", 10),
            erts_internal:list_to_integer("0x", 10),
            erts_internal:list_to_integer("007", 10),
            erts_internal:list_to_integer("-0", 10),
            erts_internal:list_to_integer([$1 | x], 10),
            erts_internal:list_to_integer([$1, a], 10),
            erts_internal:list_to_integer("12", 1),
            erts_internal:list_to_integer(x, 10),
            erts_internal:list_to_integer([], 37),
            erts_internal:list_to_integer("ff", 16),
            erts_internal:list_to_integer("FFg", 16),
            Big,
            erts_internal:binary_to_integer(<<"-ff">>, 16),
            erts_internal:binary_to_integer(<<"1x">>, 10),
            erts_internal:binary_to_integer(<<>>, 10),
            erts_internal:binary_to_integer(x, 10),
            erts_internal:binary_to_integer(<<"99999999999999999999">>, 10),
            erts_internal:cmp_term(1, 1.0),
            erts_internal:cmp_term(1.0, 1),
            erts_internal:cmp_term(a, a),
            erts_internal:cmp_term({1, [2.0]}, {1, [2]}),
            erts_internal:cmp_term(b, a),
            erts_internal:map_to_tuple_keys(#{2 => x, 1 => y}),
            erts_internal:term_type(1),
            erts_internal:term_type(1 bsl 60),
            erts_internal:term_type(1.0),
            erts_internal:term_type([]),
            erts_internal:term_type([a]),
            erts_internal:term_type({}),
            erts_internal:term_type(#{}),
            erts_internal:term_type(fun lists:map/2),
            erts_internal:term_type(fun() -> ok end),
            erts_internal:term_type(make_ref()),
            erts_internal:term_type(self()),
            erts_internal:term_type(atom),
            erts_internal:get_creation(),
            erts_internal:dynamic_node_name(),
            erts_internal:dynamic_node_name(true),
            erts_internal:dynamic_node_name(),
            erts_internal:dynamic_node_name(false),
            erts_internal:dynamic_node_name()
        ]
    ]).

iterators() ->
    M = #{1 => a, 2 => b, 3 => c},
    io:format("~p~n", [
        [
            walk(maps:iterator(M, ordered)),
            walk(maps:iterator(M, reversed)),
            walk(maps:iterator(M, fun(X, Y) -> X rem 2 =< Y rem 2 end)),
            lists:sort(walk(maps:iterator(M))),
            lists:sort(walk(maps:iterator(M, undefined))),
            maps:next(maps:iterator(#{})),
            maps:next(none),
            lists:sort(erts_internal:map_next(0, M, [x])),
            lists:sort(walk(maps:iterator(maps:from_list([{K, K} || K <- lists:seq(1, 40)])))) =:=
                [{K, K} || K <- lists:seq(1, 40)],
            outcome(fun() -> maps:iterator(x) end),
            outcome(fun() -> maps:iterator(M, sideways) end),
            outcome(fun() -> maps:next(x) end),
            outcome(fun() -> maps:next([[9] | M]) end),
            lists:sort(fun(X, Y) -> X >= Y end, [3, 1, 2, 1.0]),
            lists:sort(fun(X, Y) -> element(1, X) =< element(1, Y) end, [{1, b}, {0, c}, {1, a}]),
            lists:split(2, [a, b, c]),
            lists:split(0, []),
            outcome(fun() -> lists:split(4, [a, b, c]) end),
            outcome(fun() -> lists:split(-1, [a]) end)
        ]
    ]).

%% The associations an iterator yields, in order.
walk(Iterator) ->
    case maps:next(Iterator) of
        {Key, Value, Next} -> [{Key, Value} | walk(Next)];
        none -> []
    end.

purger() ->
    io:format("~p~n", [[erts_code_purger:purge(lists), erts_code_purger:soft_purge(lists)]]).

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
