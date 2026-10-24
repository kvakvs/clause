-module(timers).
-export([main/1, notify/2, slow/2]).

%% timer (plan 21 step LIB22): conversions, measurements, one-shot timers (sends, applies, exits) and their
%% cancellation, interval timers that end with their target, bad arguments, and TRef shapes. Timings are checked as
%% orders of events, never as durations.
main(_) ->
    conversions(),
    measurements(),
    one_shot(),
    cancelling(),
    exits(),
    intervals(),
    bad_arguments(),
    io:format("timers: ok~n").

conversions() ->
    show(conversions, [
        timer:seconds(1),
        timer:minutes(2),
        timer:hours(3),
        timer:hms(1, 2, 3),
        timer:seconds(1.5),
        timer:minutes(-2),
        timer:hours(1 bsl 70),
        timer:hms(0, 0.5, 1),
        timer:now_diff({1, 2, 3}, {0, 0, 0}),
        timer:now_diff({0, 0, 0}, {1, 2, 3}),
        timer:now_diff({1, 0.5, 0}, {0, 0, 0})
    ]),
    show(conversion_errors, [
        failure(fun() -> timer:seconds(id(a)) end),
        failure(fun() -> timer:minutes(id("1")) end),
        failure(fun() -> timer:hours(id([])) end),
        failure(fun() -> timer:hms(1, id(b), 3) end),
        failure(fun() -> timer:now_diff(id({1, 2}), {0, 0, 0}) end),
        failure(fun() -> timer:now_diff({0, 0, 0}, id(now)) end),
        failure(fun() -> timer:now_diff({a, 0, 0}, {0, 0, 0}) end)
    ]).

measurements() ->
    Units = [
        timer:tc(fun() -> forty_two end),
        timer:tc(fun() -> forty_two end, millisecond),
        timer:tc(fun(X, Y) -> X + Y end, [40, 2]),
        timer:tc(fun(X) -> X end, [forty_two], nanosecond),
        timer:tc(lists, sum, [[40, 2]]),
        timer:tc(lists, max, [[4, 42]], second)
    ],
    show(tc, [{is_integer(T) andalso T >= 0, V} || {T, V} <- Units]),
    show(sleep, [timer:sleep(5), timer:sleep(0)]),
    show(start, timer:start()).

one_shot() ->
    Self = self(),
    true = register(timers_main, Self),
    Refs = [
        timer:send_after(30, s3),
        timer:send_after(10, Self, s1),
        timer:send_after(20, timers_main, s2),
        timer:send_after(40, {timers_main, node()}, s4),
        timer:send_after(0, s0),
        timer:send_after(0, timers_main, s0_name),
        timer:send_after(0, {timers_main, node()}, s0_dest),
        timer:apply_after(0, ?MODULE, notify, [Self, a0]),
        timer:apply_after(50, fun() -> Self ! {a1, self() =/= Self} end),
        timer:apply_after(60, fun(X) -> Self ! X end, [a2]),
        timer:apply_after(70, ?MODULE, notify, [Self, a3])
    ],
    show(trefs, [tref(R) || R <- Refs]),
    show(received, lists:sort([receive_one() || _ <- lists:seq(1, 11)])),
    unregister(timers_main).

cancelling() ->
    Self = self(),
    {ok, Send} = timer:send_after(100, cancelled_send),
    {ok, Named} = timer:send_after(100, timers_unknown, cancelled_named),
    {ok, Apply} = timer:apply_after(100, ?MODULE, notify, [Self, cancelled_apply]),
    {ok, Kept} = timer:send_after(150, kept),
    {ok, Instant} = timer:send_after(0, instant),
    show(cancel, [timer:cancel(R) || R <- [Send, Named, Apply, Instant]]),
    show(after_cancel, [receive_one(), receive_one()]),
    show(cancel_again, [timer:cancel(Send), timer:cancel(Apply), timer:cancel(Kept)]),
    show(cancel_bad, [
        timer:cancel(id(nope)),
        timer:cancel(id({once, nope})),
        timer:cancel(id({other, make_ref()})),
        timer:cancel(id({once, make_ref()})),
        timer:cancel(id({interval, make_ref()}))
    ]),
    show(quiet, quiet(120)).

exits() ->
    {Victim, VictimRef} = spawn_monitor(fun() -> idle() end),
    {ok, Kill} = timer:kill_after(10, Victim),
    show(kill_after, [tref(Kill), down(VictimRef)]),
    {Exited, ExitedRef} = spawn_monitor(fun() -> idle() end),
    {ok, _} = timer:exit_after(10, Exited, shutdown),
    show(exit_after, down(ExitedRef)),
    Self = self(),
    {Trapper, TrapperRef} = spawn_monitor(fun() -> trapper(Self) end),
    receive
        {trapper, Trapper, Got} -> show(exit_after_self, Got)
    end,
    show(trapper_exit, down(TrapperRef)),
    {Own, OwnRef} = spawn_monitor(fun() ->
        {ok, _} = timer:kill_after(10),
        idle()
    end),
    show(kill_after_self, [is_pid(Own), down(OwnRef)]),
    {_, NamedRef} = spawn_monitor(fun() ->
        register(timers_victim, self()),
        Self ! registered,
        idle()
    end),
    receive
        registered -> ok
    end,
    {ok, _} = timer:exit_after(10, timers_victim, by_name),
    show(exit_after_name, down(NamedRef)),
    show(exit_after_missing, tref(timer:exit_after(5, timers_missing, gone))),
    show(quiet, quiet(30)).

trapper(Main) ->
    process_flag(trap_exit, true),
    {ok, _} = timer:exit_after(10, trapped),
    receive
        {'EXIT', _, Reason} -> Main ! {trapper, self(), {trapped, Reason}}
    end,
    {ok, _} = timer:exit_after(10, self(), kill),
    idle().

intervals() ->
    Self = self(),
    {ok, Send} = timer:send_interval(10, tick),
    show(send_interval, [tref(Send) | [receive_one() || _ <- lists:seq(1, 3)]]),
    show(cancel_interval, timer:cancel(Send)),
    settle(),
    show(quiet, quiet(50)),
    {ok, Apply} = timer:apply_interval(10, ?MODULE, notify, [Self, applied]),
    show(apply_interval, [receive_one() || _ <- lists:seq(1, 3)]),
    {ok, cancel} = timer:cancel(Apply),
    settle(),
    {ok, Fun} = timer:apply_interval(10, fun() -> Self ! fun_tick end),
    show(apply_interval_fun, [receive_one() || _ <- lists:seq(1, 2)]),
    {ok, cancel} = timer:cancel(Fun),
    settle(),
    {ok, Args} = timer:apply_interval(10, fun(M) -> Self ! M end, [arg_tick]),
    show(apply_interval_args, [receive_one() || _ <- lists:seq(1, 2)]),
    {ok, cancel} = timer:cancel(Args),
    settle(),
    {ok, Slow} = timer:apply_repeatedly(5, ?MODULE, slow, [Self, 20]),
    show(apply_repeatedly, [tref(Slow) | sequence(3)]),
    {ok, cancel} = timer:cancel(Slow),
    settle(),
    {ok, Repeated} = timer:apply_repeatedly(10, fun() -> Self ! repeated end),
    show(apply_repeatedly_fun, [receive_one() || _ <- lists:seq(1, 2)]),
    {ok, cancel} = timer:cancel(Repeated),
    settle(),
    {ok, RepeatedArgs} = timer:apply_repeatedly(10, fun(M) -> Self ! M end, [repeated_arg]),
    show(apply_repeatedly_args, [receive_one() || _ <- lists:seq(1, 2)]),
    {ok, cancel} = timer:cancel(RepeatedArgs),
    settle(),
    show(quiet, quiet(50)),
    ended_target(),
    ended_owner().

%% A send interval to a registered name follows the process registered when it started: once that process ends,
%% the timer ends too, and the name's next holder gets nothing.
ended_target() ->
    Self = self(),
    {_, Ref} = spawn_monitor(fun() ->
        register(timers_target, self()),
        Self ! registered,
        forward(Self, 2)
    end),
    receive
        registered -> ok
    end,
    {ok, _} = timer:send_interval(10, timers_target, ping),
    show(target_pings, [receive_one(), receive_one()]),
    show(target_down, down(Ref)),
    Second = spawn(fun() ->
        register(timers_target, self()),
        Self ! registered,
        forward(Self, 1)
    end),
    receive
        registered -> ok
    end,
    show(next_holder, quiet(60)),
    exit(Second, kill),
    {ok, _} = timer:send_interval(10, {timers_unregistered, node()}, ping),
    show(unregistered, quiet(40)).

%% An apply interval ends with the process that started it.
ended_owner() ->
    Self = self(),
    {Owner, Ref} = spawn_monitor(fun() ->
        {ok, _} = timer:apply_interval(10, ?MODULE, notify, [Self, owned]),
        receive
            stop -> ok
        end
    end),
    show(owned, [receive_one(), receive_one()]),
    Owner ! stop,
    show(owner_down, down(Ref)),
    settle(),
    show(quiet, quiet(50)).

bad_arguments() ->
    F0 = fun() -> ok end,
    F1 = fun(_) -> ok end,
    show(bad_arguments, [
        timer:send_after(id(-1), self(), m),
        timer:send_after(id(a), self(), m),
        timer:send_after(10, id("name"), m),
        timer:send_after(10, id({1, 2}), m),
        timer:send_after(id(-1), m),
        timer:send_after(id(-5), timers_x, m),
        timer:send_after(0, id(1), m),
        timer:apply_after(10, id(F1)),
        timer:apply_after(10, id(x)),
        timer:apply_after(10, F1, id([a, b])),
        timer:apply_after(10, F0, id(not_a_list)),
        timer:apply_after(id(-1), lists, sum, [[]]),
        timer:apply_after(10, id("m"), f, []),
        timer:apply_after(10, m, id(1), []),
        timer:apply_after(10, m, f, id(x)),
        timer:apply_after(id(1.5), m, f, []),
        timer:exit_after(id(-1), self(), r),
        timer:kill_after(id(x)),
        timer:apply_interval(10, id(F1)),
        timer:apply_interval(10, F0, id([a])),
        timer:apply_interval(id(-1), m, f, []),
        timer:apply_repeatedly(10, id(F1)),
        timer:apply_repeatedly(10, F1, id([])),
        timer:apply_repeatedly(id(a), m, f, []),
        timer:send_interval(id(-1), m),
        timer:send_interval(10, id("p"), m),
        timer:send_interval(10, id({a, "n"}), m)
    ]),
    show(quiet, quiet(20)).

%% Helpers run by timers.
notify(Pid, Message) -> Pid ! Message.

slow(Pid, Time) ->
    Pid ! {slow, start},
    timer:sleep(Time),
    Pid ! {slow, done}.

%% The next three starts and finishes of the slow action: each finishes before the next starts.
sequence(Count) -> [receive_one() || _ <- lists:seq(1, 2 * Count)].

forward(_, 0) ->
    ok;
forward(Main, N) ->
    receive
        Message -> Main ! {forwarded, Message}
    end,
    forward(Main, N - 1).

tref({ok, {Type, Ref}}) -> {ok, Type, is_reference(Ref)};
tref({Type, Ref}) -> {Type, is_reference(Ref)};
tref(Other) -> Other.

receive_one() ->
    receive
        Message -> Message
    after 2000 -> timeout
    end.

down(Ref) ->
    receive
        {'DOWN', Ref, process, _, Reason} -> Reason
    after 2000 -> timeout
    end.

%% Whether no message arrives within Time milliseconds.
quiet(Time) ->
    receive
        Message -> {unexpected, Message}
    after Time -> true
    end.

%% Lets actions already under way finish, then drops their messages.
settle() ->
    timer:sleep(50),
    flush().

flush() ->
    receive
        _ -> flush()
    after 0 -> ok
    end.

idle() ->
    receive
    after infinity -> ok
    end.

failure(F) ->
    try F() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Value) -> io:format("~p: ~p~n", [Label, Value]).

id(X) -> X.
