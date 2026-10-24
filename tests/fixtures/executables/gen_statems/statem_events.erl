-module(statem_events).
-behaviour(gen_statem).
-export([init/1, callback_mode/0, handle_event/4, terminate/3]).

%% The handle_event_function machine of the gen_statems case. Requests ask for actions and results; time-outs,
%% inserted events, casts and infos are reported to sm_main. The callback mode comes from the process dictionary,
%% so starts can try other modes.
init({events, errors}) ->
    put(mode, [handle_event_function, state_enter]),
    {ok, start, #{}};
init({events, _}) ->
    {ok, start, #{}};
init({mode, Mode}) ->
    put(mode, Mode),
    {ok, start, #{}};
init({stop, Reason}) ->
    {stop, Reason};
init({error, Reason}) ->
    {error, Reason};
init(ignore) ->
    ignore;
init({bad, Value}) ->
    Value;
init({crash, Reason}) ->
    erlang:error(Reason).

callback_mode() ->
    case get(mode) of
        undefined -> handle_event_function;
        Mode -> Mode
    end.

handle_event(enter, _, postpone_on_enter, _) ->
    {keep_state_and_data, [postpone]};
handle_event(enter, _, change_on_enter, Data) ->
    {next_state, elsewhere, Data};
handle_event(enter, _, _, _) ->
    keep_state_and_data;
handle_event({call, From}, {actions, Actions}, _, _) ->
    {keep_state_and_data, [{reply, From, ok} | listify(Actions)]};
handle_event({call, From}, {echo, X}, _, _) ->
    {keep_state_and_data, {reply, From, {echo, X}}};
handle_event({call, From}, {goto, State}, _, Data) ->
    {next_state, State, Data, [{reply, From, {went, State}}]};
handle_event({call, From}, repeat, _, Data) ->
    {repeat_state, Data, [{reply, From, repeated}]};
handle_event({call, From}, stop_and_reply, _, _) ->
    {stop_and_reply, normal, [{reply, From, bye}]};
handle_event({call, _}, {bad_return, Value}, _, _) ->
    Value;
handle_event({call, _}, {bad_action, Actions}, _, _) ->
    {keep_state_and_data, Actions};
handle_event({call, _}, {crash, Reason}, _, _) ->
    erlang:error(Reason);
handle_event({call, _}, {throw, Value}, _, _) ->
    throw(Value);
handle_event(cast, {postpone_in, X}, there, _) ->
    report({postponed_handled, X}),
    keep_state_and_data;
handle_event(cast, {postpone_in, _}, _, _) ->
    {keep_state_and_data, postpone};
handle_event(cast, {stop, Reason}, _, _) ->
    {stop, Reason};
handle_event(Type, Content, State, _) ->
    report({Type, Content, State}),
    keep_state_and_data.

terminate(Reason, State, _) ->
    report({terminate, gen_statems_strip(Reason), State}).

gen_statems_strip({Reason, [{_, _, _, _} | _]}) -> {Reason, stack};
gen_statems_strip(Reason) -> Reason.

listify(Actions) when is_list(Actions) -> Actions;
listify(Action) -> [Action].

report(Event) -> sm_main ! {report, Event}.
