-module(math_calls).
-export([main/1]).

%% The math module (plan step 41V): every function with integers, floats,
%% domain errors, overflows and non-numbers, through apply/3, direct calls and
%% funs.
main([]) ->
    [io:format("~w~n", [outcome(F, A)]) || {F, A} <- calls()],
    io:format("~w~n", [
        [
            math:pow(2, 10),
            math:pow(10, 100),
            math:sqrt(2),
            math:floor(-1.5),
            math:ceil(1.2),
            math:pi(),
            math:tau(),
            (fun math:exp/1)(1),
            (fun math:atan2/2)(1, 1)
        ]
    ]),
    io:format("math_calls: ok~n").

%% The result of math:F(A...), or the error it raises.
outcome(F, A) ->
    try
        {F, A, apply(math, F, A)}
    catch
        error:R -> {F, A, error, R}
    end.

calls() ->
    [
        {sin, [0]},
        {sin, [1.0]},
        {cos, [0.5]},
        {tan, [1]},
        {asin, [0.5]},
        {asin, [2]},
        {acos, [-1]},
        {acos, [1.5]},
        {atan, [1]},
        {sinh, [1]},
        {sinh, [1000]},
        {cosh, [-1]},
        {tanh, [0.5]},
        {asinh, [1]},
        {acosh, [2]},
        {acosh, [0.5]},
        {atanh, [0.5]},
        {atanh, [1]},
        {exp, [1]},
        {exp, [1000]},
        {log, [10]},
        {log, [0]},
        {log, [-1]},
        {log2, [8]},
        {log10, [1000]},
        {sqrt, [16]},
        {sqrt, [-1]},
        {sqrt, [1 bsl 1000]},
        {sqrt, [1 bsl 2000]},
        {sqrt, [a]},
        {sqrt, ["4"]},
        {erf, [1]},
        {erfc, [1]},
        {floor, [1]},
        {floor, [-1.5]},
        {floor, [1 bsl 70]},
        {ceil, [1.2]},
        {ceil, [-0.5]},
        {pow, [2, 0.5]},
        {pow, [10, 400]},
        {pow, [-8, 1 / 3]},
        {pow, [0, -1]},
        {pow, [1.0e100, 1]},
        {pow, [2, a]},
        {atan2, [0, 0]},
        {atan2, [1, -1]},
        {fmod, [5.5, 2]},
        {fmod, [-5, 3]},
        {fmod, [1, 0]},
        {pi, []},
        {tau, []}
    ].
