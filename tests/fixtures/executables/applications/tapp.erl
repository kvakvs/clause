%% The start module of the applications test: start/2 starts tapp_sup or fails as its arguments ask, and each
%% callback reports to the test process registered as applications.
-module(tapp).
-behaviour(application).
-export([start/2, start_phase/3, prep_stop/1, stop/1]).

report(What) ->
    case whereis(applications) of
        undefined -> ok;
        Pid -> Pid ! {tapp, What}
    end.

start(Type, normal_start) ->
    report({start, Type}),
    case tapp_sup:start_link() of
        {ok, Pid} -> {ok, Pid, started_state};
        Error -> Error
    end;
start(_, fail) ->
    {error, refused};
start(_, odd) ->
    odd_return;
start(_, crash) ->
    error(start_crashed);
start(_, {transient_exit, Reason}) ->
    {ok, tapp_worker:start_exiting(Reason)}.

start_phase(Phase, Type, Args) ->
    report({phase, Phase, Type, Args}),
    ok.

prep_stop(State) ->
    report({prep_stop, State}),
    prepped_state.

stop(State) ->
    report({stop, State}),
    ok.
