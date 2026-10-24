%% A report handler of error_logger for the loggers test: it sends each event, without its pids, to the test process.
-module(loggers_events).
-behaviour(gen_event).
-export([init/1, handle_event/2, handle_call/2, handle_info/2, terminate/2]).

init(Owner) -> {ok, Owner}.

handle_event({Tag, _GroupLeader, {_Pid, FormatOrType, ArgsOrReport}}, Owner) ->
    Owner ! {event, {Tag, FormatOrType, ArgsOrReport}},
    {ok, Owner};
handle_event(Other, Owner) ->
    Owner ! {event, {other, Other}},
    {ok, Owner}.

handle_call(_, Owner) -> {ok, ok, Owner}.

handle_info(_, Owner) -> {ok, Owner}.

terminate(Reason, _) ->
    io:format("loggers_events terminated: ~p~n", [Reason]).
