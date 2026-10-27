-module(pt_count).
-export([parse_transform/2]).

%% Counts the error forms it gets: a module's parse errors reach transforms as forms, as epp's do (plan 21 LIB77D).
parse_transform(Forms, _Options) ->
    io:format("pt_count: ~p error forms~n", [length([Form || {error, _} = Form <- Forms])]),
    Forms.
