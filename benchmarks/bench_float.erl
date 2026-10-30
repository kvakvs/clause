%% Float benchmarks (plan 20 OPT1.11): tail loops whose floats can stay in registers, and the shapes around them that
%% box: comparisons with a parameter, unary minus and float/1, is_float/1 guards, floats crossing calls.
-module(bench_float).
-export([main/1]).

main(_Args) ->
    Coefficients = [1.0 / K || K <- lists:seq(1, 12)],
    bench_util:run("float.sum", 200, fun() -> sum(1000000, 0.0) end),
    bench_util:run("float.mandelbrot", 120, fun() -> mandelbrot(120, 0) end),
    bench_util:run("float.limit", 80, fun() -> grow(1.0, 1.0e100, 0) end),
    bench_util:run("float.negate", 70, fun() -> alternate(1000000, 1.0, 0.0) end),
    bench_util:run("float.guards", 120, fun() -> clamp(1000000, 0.5, 0.0) end),
    bench_util:run("float.calls", 160, fun() -> integrate(Coefficients, 100000, 0.0, 0.0) end),
    bench_util:done("bench_float").

%% A float accumulator over an integer counter.
sum(0, Acc) -> Acc;
sum(N, Acc) -> sum(N - 1, Acc + N * 0.5).

%% Escape counts over a Size x Size grid of [-2, 1] x [-1.5, 1.5]: float/1 of the grid indexes, then an inner loop
%% comparing the magnitude with a literal.
mandelbrot(Size, Total) -> rows(Size, Size - 1, Total).

rows(_Size, -1, Total) ->
    Total;
rows(Size, Row, Total) ->
    Y = float(Row) * 3.0 / Size - 1.5,
    rows(Size, Row - 1, columns(Size, Y, Size - 1, Total)).

columns(_Size, _Y, -1, Total) ->
    Total;
columns(Size, Y, Column, Total) ->
    X = float(Column) * 3.0 / Size - 2.0,
    columns(Size, Y, Column - 1, Total + escape(0.0, 0.0, X, Y, 0)).

escape(_X, _Y, _Cx, _Cy, 200) ->
    200;
escape(X, Y, Cx, Cy, Count) ->
    X2 = X * X,
    Y2 = Y * Y,
    if
        X2 + Y2 > 4.0 -> Count;
        true -> escape(X2 - Y2 + Cx, 2.0 * X * Y + Cy, Cx, Cy, Count + 1)
    end.

%% A float growing until it passes a limit the caller passes: each step compares with a word that is not a float the
%% loop made.
grow(X, Limit, Steps) when X > Limit -> Steps;
grow(X, Limit, Steps) -> grow(X * 1.0001 + 0.5, Limit, Steps + 1).

%% Unary minus and float/1 inside a float loop.
alternate(0, _X, Acc) -> Acc;
alternate(N, X, Acc) -> alternate(N - 1, -X * 0.999, Acc + X + float(N band 7)).

%% An is_float/1 guard on a float loop parameter.
clamp(0, _X, Acc) -> Acc;
clamp(N, X, Acc) when is_float(X), X > 1.0 -> clamp(N - 1, X - 1.0, Acc + X);
clamp(N, X, Acc) -> clamp(N - 1, X * 1.7 + 0.1, Acc + X).

%% The trapezoid sum of a polynomial over [0, 1]: each step passes a float to a function of its own and adds the float
%% it returns.
integrate(_Coefficients, 0, _X, Sum) ->
    Sum;
integrate(Coefficients, N, X, Sum) ->
    integrate(Coefficients, N - 1, X + 1.0e-5, Sum + horner(Coefficients, X, 0.0) * 1.0e-5).

horner([], _X, Acc) -> Acc;
horner([C | Rest], X, Acc) -> horner(Rest, X, Acc * X + C).
