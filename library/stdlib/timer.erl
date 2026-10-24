%% Clause's timer (plan 21 step LIB22, docs/library.md): OTP's timer without its timer server. Timed sends to local
%% pids are erlang:send_after/3; every other timer is a process of its own, holding an alias that its TRef carries,
%% so cancel/1 reaches it without a registry. One-shot timers are not linked to anything; interval timers end with
%% the process they work for. The conversions and now_diff/2 are runtime builtins.
-module(timer).
-export([
    apply_after/2,
    apply_after/3,
    apply_after/4,
    apply_interval/2,
    apply_interval/3,
    apply_interval/4,
    apply_repeatedly/2,
    apply_repeatedly/3,
    apply_repeatedly/4,
    cancel/1,
    code_change/3,
    exit_after/2,
    exit_after/3,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    hms/3,
    hours/1,
    init/1,
    kill_after/1,
    kill_after/2,
    minutes/1,
    now_diff/2,
    seconds/1,
    send_after/2,
    send_after/3,
    send_interval/2,
    send_interval/3,
    sleep/1,
    start/0,
    start_link/0,
    tc/1,
    tc/2,
    tc/3,
    tc/4,
    terminate/2
]).
-include("timer.types.hrl").

-define(MAX_RECEIVE_AFTER, 16#ffffffff).

%% Created for LIB22: an interval timer process's state. Its alias receives cancel/1's message, `target` monitors the
%% process the timer works for, `action` runs every `time` milliseconds, waiting for it to finish when `wait` is set.
-record(interval, {alias, target, time, action, wait}).
-define(valid_time(T), is_integer(T), T >= 0).
-define(valid_apply(F), is_function(F, 0)).
-define(valid_apply(F, A), is_list(A), is_function(F, length(A))).
-define(valid_apply(M, F, A), is_atom(M), is_atom(F), is_list(A)).

apply_after(Time, F) when ?valid_apply(F) -> apply_after(Time, erlang, apply, [F, []]);
apply_after(_, _) -> {error, badarg}.

apply_after(Time, F, A) when ?valid_apply(F, A) -> apply_after(Time, erlang, apply, [F, A]);
apply_after(_, _, _) -> {error, badarg}.

apply_after(0, M, F, A) when ?valid_apply(M, F, A) ->
    _ = do_apply({M, F, A}, false),
    {ok, {instant, make_ref()}};
apply_after(Time, M, F, A) when ?valid_time(Time), ?valid_apply(M, F, A) ->
    start_timer(once, {deadline(Time), {M, F, A}});
apply_after(_, _, _, _) ->
    {error, badarg}.

send_after(0, Dest, Message) when is_pid(Dest); is_atom(Dest) ->
    Dest ! Message,
    {ok, {instant, make_ref()}};
send_after(0, {Name, Node} = Dest, Message) when is_atom(Name), is_atom(Node) ->
    Dest ! Message,
    {ok, {instant, make_ref()}};
send_after(Time, Pid, Message) when ?valid_time(Time), is_pid(Pid), node(Pid) =:= node() ->
    {ok, {send_local, erlang:send_after(Time, Pid, Message)}};
send_after(Time, Pid, Message) when is_pid(Pid) ->
    apply_after(Time, ?MODULE, send, [Pid, Message]);
send_after(Time, Name, Message) when is_atom(Name) ->
    apply_after(Time, ?MODULE, send, [Name, Message]);
send_after(Time, {Name, Node} = Dest, Message) when is_atom(Name), is_atom(Node) ->
    apply_after(Time, ?MODULE, send, [Dest, Message]);
send_after(_, _, _) ->
    {error, badarg}.

send_after(Time, Message) -> send_after(Time, self(), Message).

exit_after(Time, Pid, Reason) -> apply_after(Time, erlang, exit, [Pid, Reason]).

exit_after(Time, Reason) -> exit_after(Time, self(), Reason).

kill_after(Time, Pid) -> exit_after(Time, Pid, kill).

kill_after(Time) -> exit_after(Time, self(), kill).

apply_interval(Time, F) when ?valid_apply(F) -> apply_interval(Time, erlang, apply, [F, []]);
apply_interval(_, _) -> {error, badarg}.

apply_interval(Time, F, A) when ?valid_apply(F, A) -> apply_interval(Time, erlang, apply, [F, A]);
apply_interval(_, _, _) -> {error, badarg}.

apply_interval(Time, M, F, A) when ?valid_time(Time), ?valid_apply(M, F, A) ->
    start_timer(interval, {deadline(0), Time, self(), {M, F, A}, false});
apply_interval(_, _, _, _) ->
    {error, badarg}.

apply_repeatedly(Time, F) when ?valid_apply(F) -> apply_repeatedly(Time, erlang, apply, [F, []]);
apply_repeatedly(_, _) -> {error, badarg}.

apply_repeatedly(Time, F, A) when ?valid_apply(F, A) ->
    apply_repeatedly(Time, erlang, apply, [F, A]);
apply_repeatedly(_, _, _) ->
    {error, badarg}.

apply_repeatedly(Time, M, F, A) when ?valid_time(Time), ?valid_apply(M, F, A) ->
    start_timer(interval, {deadline(0), Time, self(), {M, F, A}, true});
apply_repeatedly(_, _, _, _) ->
    {error, badarg}.

send_interval(Time, Pid, Message) when ?valid_time(Time), is_pid(Pid) ->
    start_timer(interval, {deadline(0), Time, Pid, {?MODULE, send, [Pid, Message]}, false});
send_interval(Time, Name, Message) when ?valid_time(Time), is_atom(Name) ->
    start_timer(interval, {deadline(0), Time, Name, {?MODULE, send, [Name, Message]}, false});
send_interval(Time, {Name, Node} = Dest, Message) when
    ?valid_time(Time), is_atom(Name), is_atom(Node)
->
    start_timer(interval, {deadline(0), Time, Dest, {?MODULE, send, [Dest, Message]}, false});
send_interval(_, _, _) ->
    {error, badarg}.

send_interval(Time, Message) -> send_interval(Time, self(), Message).

%% A timer process acts on {cancel, Alias} sent to its alias; a TRef whose timer has ended names a dead alias, and the
%% message is dropped.
cancel({instant, Ref}) when is_reference(Ref) ->
    {ok, cancel};
cancel({send_local, Ref}) when is_reference(Ref) ->
    _ = erlang:cancel_timer(Ref),
    {ok, cancel};
cancel({Type, Alias}) when (Type =:= once orelse Type =:= interval), is_reference(Alias) ->
    Alias ! {cancel, Alias},
    {ok, cancel};
cancel(_) ->
    {error, badarg}.

sleep(T) when is_integer(T), T > ?MAX_RECEIVE_AFTER ->
    receive
    after ?MAX_RECEIVE_AFTER -> sleep(T - ?MAX_RECEIVE_AFTER)
    end;
sleep(T) ->
    receive
    after T -> ok
    end.

tc(F) -> tc(F, microsecond).

tc(F, A) when is_list(A) -> tc(F, A, microsecond);
tc(F, Unit) -> measured(fun() -> F() end, Unit).

tc(M, F, A) when is_list(A) -> tc(M, F, A, microsecond);
tc(F, A, Unit) -> measured(fun() -> apply(F, A) end, Unit).

tc(M, F, A, Unit) -> measured(fun() -> apply(M, F, A) end, Unit).

hms(_, _, _) -> clause_builtin_placeholder.
hours(_) -> clause_builtin_placeholder.
minutes(_) -> clause_builtin_placeholder.
now_diff(_, _) -> clause_builtin_placeholder.
seconds(_) -> clause_builtin_placeholder.

%% Timers need no server to be started.
start() -> ok.

%% OTP's timer server callbacks: Clause runs no timer server.
code_change(_, _, _) -> erlang:error({not_implemented, code_change, 3}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
init(_) -> erlang:error({not_implemented, init, 1}).
start_link() -> erlang:error({not_implemented, start_link, 0}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).

%% {Microseconds or Unit, Value} of one evaluation.
measured(Evaluate, Unit) ->
    T1 = erlang:monotonic_time(),
    Value = Evaluate(),
    T2 = erlang:monotonic_time(),
    {erlang:convert_time_unit(T2 - T1, native, Unit), Value}.

%% The millisecond on the monotonic clock `Time` milliseconds from now, rounded up as OTP's timer server rounds.
deadline(Time) -> (erlang:monotonic_time(microsecond) + 999) div 1000 + Time.

%% Starts a timer process and returns its TRef once the process has made its alias.
start_timer(Type, Spec) ->
    Owner = self(),
    Tag = make_ref(),
    _ = spawn(fun() -> timer_process(Type, Owner, Tag, Spec) end),
    receive
        {Tag, Alias} -> {ok, {Type, Alias}}
    end.

%% Created for LIB22: the body of a once or interval timer process.
timer_process(once, Owner, Tag, {Deadline, Action}) ->
    Alias = alias(),
    Owner ! {Tag, Alias},
    Timer = erlang:start_timer(Deadline, self(), Alias, [{abs, true}]),
    receive
        {cancel, Alias} -> ok;
        {timeout, Timer, Alias} -> do_apply(Action, false)
    end;
timer_process(interval, Owner, Tag, {Started, Time, Target, Action, Wait}) ->
    Alias = alias(),
    Owner ! {Tag, Alias},
    Interval = #interval{
        alias = Alias, target = monitor(process, Target), time = Time, action = Action, wait = Wait
    },
    interval_loop(Interval, schedule(Started + Time, Alias)).

%% Applies an interval timer's action at each tick until it is cancelled or its target ends; a repeated action
%% finishes before the next tick is scheduled.
interval_loop(#interval{alias = Alias, target = Target} = Interval, {Timer, Due}) ->
    receive
        {cancel, Alias} ->
            cancel_timer(Timer);
        {'DOWN', Target, process, _, _} ->
            cancel_timer(Timer);
        {timeout, Timer, Alias} ->
            case do_apply(Interval#interval.action, Interval#interval.wait) of
                {ok, {spawn, Action}} -> await_action(Interval, Due, Action);
                _ -> next_tick(Interval, Due)
            end
    end.

%% Waits for a repeated action to finish before the next tick.
await_action(#interval{alias = Alias, target = Target} = Interval, Due, Action) ->
    receive
        {cancel, Alias} -> ok;
        {'DOWN', Target, process, _, _} -> ok;
        {'DOWN', Action, process, _, _} -> next_tick(Interval, Due)
    end.

next_tick(#interval{alias = Alias, time = Time} = Interval, Due) ->
    interval_loop(Interval, schedule(Due + Time, Alias)).

%% A tick at the monotonic millisecond Due, as {Timer, Due}; a tick due already is taken at once.
schedule(Due, Alias) ->
    case Due =< deadline(0) of
        true ->
            Timer = make_ref(),
            self() ! {timeout, Timer, Alias},
            {Timer, Due};
        false ->
            {erlang:start_timer(Due, self(), Alias, [{abs, true}]), Due}
    end.

cancel_timer(Timer) ->
    _ = erlang:cancel_timer(Timer, [{async, true}, {info, false}]),
    ok.

%% Runs a timer's action as OTP's timer server does: sends and exits in the timer's process, other actions in a
%% fresh process (monitored when the timer waits for it).
do_apply({?MODULE, send, [Dest, Message]}, _) ->
    try Dest ! Message of
        _ -> {ok, send}
    catch
        _:_ -> error
    end;
do_apply({erlang, exit, [Name, Reason]}, _) ->
    try exit(pid_of(Name), Reason) of
        _ -> {ok, exit}
    catch
        _:_ -> error
    end;
do_apply({M, F, A}, false) ->
    try spawn(M, F, A) of
        _ -> {ok, spawn}
    catch
        error:badarg -> error
    end;
do_apply({M, F, A}, true) ->
    try spawn_monitor(M, F, A) of
        {_, Ref} -> {ok, {spawn, Ref}}
    catch
        error:badarg -> error
    end.

pid_of(Name) when is_pid(Name) -> Name;
pid_of(Name) when is_atom(Name), Name =/= undefined -> pid_of(whereis(Name));
pid_of(_) -> undefined.
