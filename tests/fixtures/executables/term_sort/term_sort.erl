-module(term_sort).
-export([main/1]).

%% Term order of lists:sort/1, lists:usort/1 and the comparison operators (plan 20 step OPT3.17): numbers in every
%% representation, atoms, tuples, lists, binaries and the kinds left to the full structural order keep OTP's order,
%% and elements comparing equal keep their input order.
main(_) ->
    Big = id(1 bsl 64),
    Numbers = [
        3,
        -7,
        0,
        1.0,
        1,
        -0.0,
        0.0,
        Big,
        -Big,
        float(Big),
        Big + 1,
        -Big - 1,
        Big bsl 136,
        -(Big bsl 136),
        1.0e300,
        -1.0e300,
        9007199254740993,
        9007199254740992.0,
        Big bsr 5,
        (Big bsr 5) - 1,
        -(Big bsr 5),
        Big bsr 4,
        2.5,
        -2.5,
        Big bsr 37,
        (Big bsr 37) - 1,
        -(Big bsr 37) - 1,
        Big bsr 33,
        Big bsr 32
    ],
    show(Numbers),
    show([b, a, 'B', '', ab, 'a b', true, false, undefined, b, zz]),
    show([
        {b, 1},
        {a, 2},
        {a, 1.0},
        {a, 1},
        {},
        {a},
        {a, 1, x},
        {a, [1, 2]},
        {a, [1]},
        {a, <<"x">>},
        {a, {b, {c, 2}}},
        {a, {b, {c, 1}}},
        {1, a},
        {1.0, a},
        {Big, a},
        {-Big, a}
    ]),
    show([
        [],
        [a],
        [a | b],
        [a, b],
        "abc",
        "abd",
        "ab",
        [1, 2 | 3],
        [1.0],
        [1],
        [[1]],
        [[0, 2]],
        [a | {}]
    ]),
    Large = binary:copy(<<"ab">>, 40),
    show([
        <<"b">>,
        <<"a">>,
        <<>>,
        <<"ab">>,
        Large,
        binary_part(Large, 2, 70),
        <<1:3>>,
        <<"a", 1:1>>,
        <<"ab", 0>>
    ]),
    show([nest(12, 2), nest(12, 1), nest(3, 5), nest(11, 1)]),
    show([1, 1.0, {1}, {1.0}, [1.0], [1], 2.0, 2, {1.0}, {1}]),
    show(lists:usort([1.0, 1, 2, 2.0, a, b, a, {1}, {1.0}])),
    show(lists:usort([3, 1, 2, 3, 1])),
    show(lists:usort([c, a, b, a])),
    io:format("~w~n", [[kind(X) || X <- lists:sort(mixed())]]),
    io:format("~w~n", [[kind(X) || X <- lists:usort(mixed() ++ mixed())]]),
    io:format("~w~n", [[compare(A, B) || {A, B} <- pairs(Big)]]),
    io:format("~w~n", [
        [failure(fun lists:sort/1, X) || X <- [[a | b], [b, a | c], x, [1, 2, 3 | 4]]]
    ]),
    io:format("~w~n", [[failure(fun lists:usort/1, X) || X <- [[a | b], [b, a | c], x]]]),
    io:format("term_sort: ok~n").

%% Print a list sorted, then the same list as the comparison operators order it.
show(List) ->
    Sorted = lists:sort(List),
    io:format("~w~n", [Sorted]),
    io:format("~w~n", [insertion_sort(List)]),
    io:format("~w~n", [Sorted =:= insertion_sort(List)]).

%% A stable insertion sort by the generated comparison operators.
insertion_sort(List) ->
    lists:foldl(fun insert/2, [], List).

insert(X, [Y | Rest]) when Y =< X -> [Y | insert(X, Rest)];
insert(X, Sorted) -> [X | Sorted].

%% Each comparison operator of A against B.
compare(A, B) ->
    {A < B, A == B, A > B, A =< B, A >= B, A /= B}.

pairs(Big) ->
    [
        {1, 1.0},
        {Big, float(Big)},
        {Big + 1, float(Big)},
        {-Big, -float(Big)},
        {9007199254740993, 9007199254740992.0},
        {Big bsl 136, 1.0e300},
        {-(Big bsl 136), -1.0e300},
        {Big bsl 940, 1.0e300},
        {{a, 1}, {a, 1.0}},
        {{a, Big}, {a, 2.5}},
        {[1, 2], [1, 2.0]},
        {"abc", "abd"},
        {[a | b], [a | c]},
        {<<"ab">>, <<"a">>},
        {{}, {a}},
        {a, {}},
        {[], {}},
        {nest(12, 1), nest(12, 2)},
        {Big bsr 5, float(Big bsr 5)},
        {Big, -Big},
        {Big bsl 1, Big}
    ].

%% Terms of every kind, some of them left to the full structural order.
mixed() ->
    [
        self(),
        make_ref(),
        fun id/1,
        #{a => 1},
        #{b => 1},
        #{},
        [],
        {},
        a,
        1,
        2.0,
        <<>>,
        "x",
        {a},
        id(1 bsl 70)
    ].

kind(X) when is_integer(X) -> integer;
kind(X) when is_float(X) -> float;
kind(X) when is_atom(X) -> atom;
kind(X) when is_reference(X) -> reference;
kind(X) when is_function(X) -> function;
kind(X) when is_pid(X) -> pid;
kind(X) when is_tuple(X) -> {tuple, tuple_size(X)};
kind(X) when is_map(X) -> {map, maps:keys(X)};
kind([]) -> nil;
kind(X) when is_list(X) -> list;
kind(X) when is_binary(X) -> binary.

%% A value wrapped in Depth one-element tuples.
nest(0, Value) -> Value;
nest(Depth, Value) -> {nest(Depth - 1, Value)}.

%% The error class and reason of a call that fails.
failure(Fun, Argument) ->
    try Fun(Argument) of
        Result -> {returned, Result}
    catch
        Class:Reason -> {Class, Reason}
    end.

id(Value) -> Value.
