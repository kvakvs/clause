-module(loop_fairness).
-export([main/1]).

% A long comprehension that calls no function spends reductions at its loop head and yields (OPT0.7): on one
% scheduler a process spawned before it runs while it is still iterating.

main(_) ->
    Numbers = lists:seq(1, 300000),
    Parent = self(),
    spawn(fun() -> Parent ! started end),
    Squares = [X * X || X <- Numbers, X rem 7 =/= 0],
    Order =
        receive
            started -> during
        after 0 -> after_loop
        end,
    io:format("~p ~p~n", [length(Squares), Order]),
    io:format("loop_fairness: ok~n").
