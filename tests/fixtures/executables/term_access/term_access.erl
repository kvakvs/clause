-module(term_access).
-export([main/1]).

%% The term-access builtins: tuples, lists and maps in body context, ++ and --,
%% with boundary and invalid arguments, called directly, through apply/3 and as funs.
main([]) ->
    erlang:display([outcome(F, A) || {F, A} <- id(calls())]),
    direct(),
    operators(),
    funs(),
    large().

id(X) -> X.

%% Every term-access builtin with valid, boundary and invalid arguments.
calls() ->
    [
        {element, [1, {a, b}]},
        {element, [2, {a, b}]},
        {element, [0, {a}]},
        {element, [3, {a, b}]},
        {element, [1, [a]]},
        {element, [a, {a}]},
        {setelement, [1, {a, b}, x]},
        {setelement, [2, {a, b}, x]},
        {setelement, [0, {a}, x]},
        {setelement, [2, {a}, x]},
        {setelement, [1, {}, x]},
        {setelement, [1, [a], x]},
        {setelement, [1.0, {a}, x]},
        {setelement, [18446744073709551616, {a}, x]},
        {tuple_size, [{}]},
        {tuple_size, [{a, b, c}]},
        {tuple_size, [[a]]},
        {make_tuple, [0, x]},
        {make_tuple, [3, x]},
        {make_tuple, [-1, x]},
        {make_tuple, [a, x]},
        {make_tuple, [16777216, x]},
        {make_tuple, [3, d, []]},
        {make_tuple, [3, d, [{1, a}, {3, c}]]},
        {make_tuple, [2, d, [{1, a}, {1, b}]]},
        {make_tuple, [0, d, []]},
        {make_tuple, [2, d, [{3, a}]]},
        {make_tuple, [2, d, [{0, a}]]},
        {make_tuple, [2, d, [{-1, a}]]},
        {make_tuple, [2, d, [{1, a, b}]]},
        {make_tuple, [2, d, [a]]},
        {make_tuple, [2, d, [{1, a} | b]]},
        {make_tuple, [2, d, x]},
        {make_tuple, [-1, d, []]},
        {tuple_to_list, [{}]},
        {tuple_to_list, [{a, {b}, [c]}]},
        {tuple_to_list, [[a]]},
        {list_to_tuple, [[]]},
        {list_to_tuple, [[a, b]]},
        {list_to_tuple, [[a | b]]},
        {list_to_tuple, [x]},
        {hd, [[a, b]]},
        {hd, [[a | b]]},
        {hd, [[]]},
        {hd, [x]},
        {tl, [[a, b]]},
        {tl, [[a | b]]},
        {tl, [[]]},
        {length, [[]]},
        {length, [[a, b, c]]},
        {length, [[a | b]]},
        {length, [x]},
        {map_get, [1, #{1 => one}]},
        {map_get, [2, #{1 => one}]},
        {map_get, [1, [x]]},
        {map_size, [#{}]},
        {map_size, [#{1 => a, 2 => b}]},
        {map_size, [{}]},
        {is_map_key, [1, #{1 => a}]},
        {is_map_key, [2, #{1 => a}]},
        {is_map_key, [1, x]},
        {'++', [[1, 2], [3]]},
        {'++', [[], x]},
        {'++', [[1], x]},
        {'++', [[1], [2 | 3]]},
        {'++', [x, []]},
        {'++', [[1 | 2], [3]]},
        {'--', [[1, 2, 3, 2, 1], [2, 1]]},
        {'--', [[1, 1.0, 1], [1.0]]},
        {'--', [[a, b, a], [a, a, a]]},
        {'--', [[], [a]]},
        {'--', [[a, {b}], []]},
        {'--', [[a], x]},
        {'--', [x, []]},
        {'--', [[a | b], []]},
        {'--', [[a], [b | c]]}
    ].

%% The value of erlang:F(Args), or the class and reason it raised.
outcome(F, A) ->
    try apply(erlang, F, A) of
        Value -> {ok, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

%% The value of Fun(), or the class and reason it raised.
attempt(Fun) ->
    try Fun() of
        Value -> {ok, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

%% Direct calls, unqualified and erlang-qualified, of the builtins without an inline service.
direct() ->
    T = id({a, b, c}),
    erlang:display([
        setelement(2, T, x),
        erlang:setelement(3, T, y),
        erlang:make_tuple(2, z),
        erlang:make_tuple(id(0), z),
        tuple_size(erlang:make_tuple(id(64), id({q}))),
        element(64, erlang:make_tuple(id(64), id({q}))),
        tuple_size(erlang:make_tuple(id(65), q)),
        erlang:make_tuple(3, d, [{2, m}]),
        tuple_to_list(T),
        list_to_tuple(id([1, 2])),
        attempt(fun() -> setelement(id(4), T, x) end),
        attempt(fun() -> erlang:make_tuple(id(-2), x) end),
        attempt(fun() -> list_to_tuple(id([1 | 2])) end),
        attempt(fun() -> tuple_to_list(id(x)) end)
    ]).

%% The ++ and -- operators, evaluated left to right like other operators.
operators() ->
    A = id([1, 2]),
    B = id([2, 3]),
    erlang:display([A ++ B, A -- B, A ++ [] -- A, (A ++ B) -- (B ++ A), [] ++ id(tail)]),
    erlang:display([
        attempt(fun() -> id(x) ++ B end),
        attempt(fun() -> A -- id(x) end),
        attempt(fun() -> id([1 | 2]) -- [] end)
    ]),
    erlang:display([show(a) ++ show(b), show(c) -- show(d)]),
    % Literal prefixes are built as cons cells onto the right operand.
    erlang:display([
        "ab" ++ id([1]),
        "ab" ++ [],
        [] ++ id(z),
        [a, b | []] ++ id(c),
        "x" ++ "y" ++ id("z"),
        [id(1), 2] ++ id(tail)
    ]).

%% Print a tag on evaluation, then yield it as a one-element list.
show(Tag) ->
    erlang:display(Tag),
    [Tag].

%% Builtins as fun values.
funs() ->
    Set = fun setelement/3,
    erlang:display([
        Set(1, {a}, b),
        (fun erlang:'++'/2)([1], [2]),
        (fun erlang:'--'/2)([1, 2], [1]),
        fold(fun tuple_to_list/1, [{a}, {b, c}]),
        fun tuple_to_list/1 =:= fun erlang:tuple_to_list/1,
        fun list_to_tuple/1
    ]).

fold(F, [H | T]) -> [F(H) | fold(F, T)];
fold(_, []) -> [].

%% Large inputs: 100,000-element lists and tuples.
large() ->
    L = seq(1, 100000),
    Odd = [X || X <- L, X rem 2 =:= 1],
    Even = L -- Odd,
    Big = list_to_tuple(L),
    erlang:display([
        length(Even),
        hd(Even),
        lists_last(Even),
        tuple_size(Big),
        element(100000, Big),
        length(tuple_to_list(erlang:make_tuple(100000, z))),
        length(L ++ L),
        L -- L,
        element(50000, setelement(50000, Big, mid))
    ]).

seq(N, M) when N > M -> [];
seq(N, M) -> [N | seq(N + 1, M)].

lists_last([X]) -> X;
lists_last([_ | T]) -> lists_last(T).
