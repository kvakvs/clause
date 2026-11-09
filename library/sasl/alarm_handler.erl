%% OTP's alarm handler (plan 21 step MIM6, docs/libraries/library.md#otp-applications): the alarm_handler
%% event manager sasl starts and its default handler, which keeps the alarms set, newest first, and reports
%% each change.
-module(alarm_handler).
-behaviour(gen_event).
-export([start_link/0, set_alarm/1, clear_alarm/1, get_alarms/0, add_alarm_handler/1, add_alarm_handler/2]).
-export([delete_alarm_handler/1]).
-export([init/1, handle_event/2, handle_call/2, handle_info/2, terminate/2]).

% The event manager, with this module as its handler.
start_link() ->
    case gen_event:start_link({local, alarm_handler}) of
        {ok, Pid} ->
            ok = gen_event:add_handler(alarm_handler, alarm_handler, []),
            {ok, Pid};
        Error ->
            Error
    end.

set_alarm(Alarm) ->
    gen_event:notify(alarm_handler, {set_alarm, Alarm}).

clear_alarm(AlarmId) ->
    gen_event:notify(alarm_handler, {clear_alarm, AlarmId}).

get_alarms() ->
    gen_event:call(alarm_handler, alarm_handler, get_alarms).

add_alarm_handler(Module) ->
    gen_event:add_handler(alarm_handler, Module, []).

add_alarm_handler(Module, Args) ->
    gen_event:add_handler(alarm_handler, Module, Args).

delete_alarm_handler(Module) ->
    gen_event:delete_handler(alarm_handler, Module, []).

% It starts without alarms, even when another handler swapped for it hands some over (as OTP's).
init(_) ->
    {ok, []}.

handle_event({set_alarm, Alarm}, Alarms) ->
    error_logger:info_report([{alarm_handler, {set, Alarm}}]),
    {ok, [Alarm | Alarms]};
handle_event({clear_alarm, AlarmId}, Alarms) ->
    error_logger:info_report([{alarm_handler, {clear, AlarmId}}]),
    {ok, lists:keydelete(AlarmId, 1, Alarms)};
handle_event(_, Alarms) ->
    {ok, Alarms}.

handle_call(get_alarms, Alarms) ->
    {ok, Alarms, Alarms};
handle_call(_, Alarms) ->
    {ok, {error, bad_query}, Alarms}.

handle_info(_, Alarms) ->
    {ok, Alarms}.

% Swapped out, the alarms go to the new handler.
terminate(swap, Alarms) ->
    {alarm_handler, Alarms};
terminate(_, _) ->
    ok.
