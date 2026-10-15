-module(float_arithmetic).
-export([main/1]).

% Float arithmetic (OPT0.15): + - * / of floats, of floats with small integers and bignums, and / of integers,
% written inline into new float cells, with the immediate service raising badarith for non-finite results, zero
% divisors and non-numbers, and promoting the other cases; a float loop allocates past many collections. Math
% functions of numbers compute inline too (inline math), the builtin raising for domain errors and other operands.

id(X) -> X.

ops(X, Y) ->
    [t(fun() -> X + Y end), t(fun() -> X - Y end), t(fun() -> X * Y end), t(fun() -> X / Y end)].

t(F) ->
    try
        F()
    catch
        error:E -> E
    end.

maths(X, Values) ->
    [
        t(fun() -> math:sqrt(X) end),
        t(fun() -> math:exp(X) end),
        t(fun() -> math:log(X) end),
        t(fun() -> math:log2(X) end),
        t(fun() -> math:log10(X) end),
        t(fun() -> math:sin(X) end),
        t(fun() -> math:cos(X) end),
        t(fun() -> math:floor(X) end),
        t(fun() -> math:ceil(X) end)
        | [t(fun() -> math:pow(X, Y) end) || Y <- Values]
    ].

loop(0, Acc) -> Acc;
loop(N, Acc) -> loop(N - 1, Acc * 1.0000001 + 0.5 - N / 3).

main(_) ->
    Values = [
        id(1.5),
        id(2),
        id(-0.0),
        id(0.0),
        id(1.0e308),
        id(-3),
        id(7.25),
        id(1 bsl 70),
        id(0),
        id(1),
        id(a),
        id(5.0e-324)
    ],
    lists:foreach(fun(X) -> io:format("~w~n", [[ops(X, Y) || Y <- Values]]) end, Values),
    % OTP's Windows build computes log2 as log(X) / log(2), one bit off the C library's log2 near 1.0e308.
    Maths = [V || V <- Values, V =/= 1.0e308],
    lists:foreach(fun(X) -> io:format("~w~n", [maths(X, Maths)]) end, Maths),
    io:format("~p~n", [{loop(200000, 0.0), 10 / 4, -7 / 2, 1.0e308 * 0.5, 3 * 0.1, 0.1 + 0.2}]),
    io:format("float_arithmetic: ok~n").
