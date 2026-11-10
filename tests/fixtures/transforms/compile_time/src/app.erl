-module(app).
-export([main/1]).

main(_) ->
    io:format("~p~n", [table:size()]),
    io:format("app: ok~n").
