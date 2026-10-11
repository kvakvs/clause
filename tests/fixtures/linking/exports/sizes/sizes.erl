-module(sizes).
-export([main/1]).

% A program using a few library functions: its executable size with and without unreachable exports.

main(_) ->
    Squares = lists:map(fun(X) -> X * X end, lists:seq(1, 10)),
    Sum = lists:foldl(fun(X, Acc) -> X + Acc end, 0, Squares),
    io:format("~p ~p~n", [lists:reverse(Squares), Sum]),
    io:format("sizes: ok~n").
