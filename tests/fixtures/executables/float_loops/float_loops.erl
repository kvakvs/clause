-module(float_loops).
-export([main/1, id/1]).

%% Functions whose own tail calls pass new floats to a parameter loop on raw doubles, without a float per step.
%% Each case checks that values the caller holds, literals and earlier results stay intact, that leaving and
%% re-entering such a loop works, and that errors and collections inside one behave as on OTP.
main(_) ->
    io:format("~p~n", [decay(1000, 1.0)]),
    io:format("~p~n", [decay(10, 1)]),
    %% A float the caller built itself and still holds afterwards.
    Start = id(1.5) * 2.0,
    io:format("~p~n", [{decay(5, Start), Start}]),
    %% Two results of the same loop are distinct values.
    First = halve(3, 1.0),
    Second = halve(4, 1.0),
    io:format("~p~n", [{First, Second, First + Second}]),
    io:format("~p~n", [sum([float(X) || X <- lists:seq(1, 1000)], 0.0)]),
    io:format("~p~n", [mean([1, 2.5, 4, 8.25], 0, 0.0)]),
    io:format("~p~n", [swap(7, 1.0, 2.0)]),
    io:format("~p~n", [pair(7, 1.0, 2.0)]),
    io:format("~p~n", [reset(10, 1.0)]),
    io:format("~p~n", [literal(6, 1.0)]),
    io:format("~p~n", [{0.5, 3.0}]),
    io:format("~p~n", [until(1.0)]),
    io:format("~p~n", [roots(20, 2.0)]),
    io:format("~p~n", [handoff(4, 1.0)]),
    io:format("~p~n", [escaping(3, 1.0, [])]),
    io:format("~p~n", [captured(3, 1.0)]),
    io:format("~p~n", [garbage(5000, 0.0, [])]),
    io:format("~p~n", [calling(2000, 0.0)]),
    io:format("~p~n", [caught(fun() -> failing(5, 1.0) end)]),
    io:format("~p~n", [caught(fun() -> failing(5, an_atom) end)]),
    io:format("~p~n", [caught(fun() -> overflow(2000, 2.0) end)]),
    io:format("~p~n", [signs(4, 0.0, [])]),
    io:format("~p~n", [wide(6, 1.0, a, b, c, d, e, 0.5)]),
    io:format("float_loops: ok~n").

id(X) -> X.

%% The basic loop: an integer counter and a float accumulator, whose first value may be an integer.
decay(0, X) -> X;
decay(N, X) -> decay(N - 1, X * 0.999 + 0.002).

halve(0, X) -> X;
halve(N, X) -> halve(N - 1, X / 2).

%% A float accumulator over the floats of a list.
sum([], Acc) -> Acc;
sum([H | T], Acc) -> sum(T, Acc + H * 0.5).

%% Integer and float elements; the count stays an integer.
mean([], Count, Total) -> Total / Count;
mean([H | T], Count, Total) -> mean(T, Count + 1, Total + H * 1.0).

%% Two float parameters trading places: the second one's value moves to the first.
swap(0, X, Y) -> {X, Y};
swap(N, X, Y) -> swap(N - 1, Y, X + 1.0).

%% Two float parameters, each passed its own new value.
pair(0, X, Y) -> {X, Y};
pair(N, X, Y) -> pair(N - 1, Y * 0.5, X + 3.0).

%% Every third step passes an integer, which leaves the loop until the next float.
reset(0, X) -> X;
reset(N, _) when N rem 3 =:= 0 -> reset(N - 1, 0);
reset(N, X) -> reset(N - 1, X * 2.0 + 1.0).

%% A float literal passed as the new value must stay a literal.
literal(0, X) -> X;
literal(N, _) when N rem 2 =:= 0 -> literal(N - 1, 0.5);
literal(N, X) -> literal(N - 1, X * 3.0).

%% A guard compares the parameter.
until(X) when X > 100.0 -> X;
until(X) -> until(X * 1.5).

%% Inline math.
roots(0, X) -> X;
roots(N, X) -> roots(N - 1, math:sqrt(X) + 1.0).

%% The loop ends in a tail call of another function.
handoff(0, X) -> finish(X);
handoff(N, X) -> handoff(N - 1, X + 0.25).

finish(X) -> {finished, X}.

%% The parameter is kept in a list, so it keeps a new float each step.
escaping(0, X, Seen) -> {X, Seen};
escaping(N, X, Seen) -> escaping(N - 1, X + 1.0, [X | Seen]).

%% A fun captures the parameter.
captured(0, X) ->
    X;
captured(N, X) ->
    F = fun() -> X end,
    captured(N - 1, F() + 1.0).

%% Collections run while the loop's cell is live.
garbage(0, X, Acc) -> {X, length(Acc)};
garbage(N, X, Acc) -> garbage(N - 1, X + 0.5, [{N, N * 2} | Acc]).

%% A call in each step: the cell and its value live across it.
calling(0, X) ->
    X;
calling(N, X) ->
    Step = length(lists:seq(1, N rem 7)),
    calling(N - 1, X + Step * 0.5).

%% An error raised inside the loop names the function.
failing(0, X) -> X;
failing(N, X) -> failing(N - 1, X / (N - 3)).

%% A result past the largest float.
overflow(0, X) -> X;
overflow(N, X) -> overflow(N - 1, X * X).

%% Comparisons of two floats in registers: -0.0 equals 0.0 without being exactly equal to it.
signs(0, _X, Acc) ->
    lists:reverse(Acc);
signs(N, X, Acc) ->
    Y = X * -1.0,
    signs(N - 1, X + 0.5 * (N rem 2), [{X == Y, X =:= Y, X < Y, X >= Y, X < 1} | Acc]).

%% Eight parameters: the float passed in a register loops, the one past the registers keeps a new float per step.
wide(0, X, _, _, _, _, _, Y) -> {X, Y};
wide(N, X, A, B, C, D, E, Y) -> wide(N - 1, X * 0.5, A, B, C, D, E, Y + 1.5).

%% The reason of an error and the innermost frame of this module in its stack.
caught(Fun) ->
    try
        Fun()
    catch
        error:Reason:Stack ->
            [{Module, Function, Arity, _} | _] = [Frame || {float_loops, _, _, _} = Frame <- Stack],
            {Reason, Module, Function, Arity}
    end.
