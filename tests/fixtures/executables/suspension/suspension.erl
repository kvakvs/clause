-module(suspension).
-export([main/1]).

%% suspend_process/1,2, resume_process/1 and process_display/2 (plan 21 step LIB9B): a suspended process runs again
%% only once every suspend is resumed, its messages wait, its suspender's exit resumes it, and the options and
%% errors follow OTP.
main(_) ->
    basic(),
    counts(),
    options(),
    suspender_exit(),
    errors(),
    display(),
    io:format("suspension: ok~n").

%% A process answering pings until told to stop.
ponger() ->
    receive
        {ping, From} ->
            From ! pong,
            ponger();
        stop ->
            ok
    end.

ping(Pid) ->
    Pid ! {ping, self()},
    receive
        pong -> pong
    after 200 -> silent
    end.

wait_pong() ->
    receive
        pong -> pong
    after 5000 -> timeout
    end.

status(Pid) ->
    {status, Status} = process_info(Pid, status),
    Status.

basic() ->
    P = spawn(fun ponger/0),
    R1 = ping(P),
    T1 = erlang:suspend_process(P),
    S1 = status(P),
    R2 = ping(P),
    T2 = erlang:resume_process(P),
    R3 = wait_pong(),
    show(basic, [R1, T1, S1, R2, T2, R3, ping(P)]),
    P ! stop.

counts() ->
    P = spawn(fun ponger/0),
    true = erlang:suspend_process(P),
    true = erlang:suspend_process(P, []),
    R1 = ping(P),
    true = erlang:resume_process(P),
    R2 = ping(P),
    S1 = status(P),
    true = erlang:resume_process(P),
    R3 = wait_pong(),
    show(counts, [R1, R2, S1, R3, catching(fun() -> erlang:resume_process(P) end)]),
    P ! stop.

options() ->
    P = spawn(fun ponger/0),
    U1 = erlang:suspend_process(P, [unless_suspending]),
    U2 = erlang:suspend_process(P, [unless_suspending]),
    true = erlang:resume_process(P),
    A1 = erlang:suspend_process(P, [asynchronous]),
    true = erlang:resume_process(P),
    A2 = erlang:suspend_process(P, [{asynchronous, tag}]),
    Reply =
        receive
            {tag, State} -> State
        after 5000 -> timeout
        end,
    true = erlang:resume_process(P),
    A3 = erlang:suspend_process(P, [asynchronous, unless_suspending]),
    A4 = erlang:suspend_process(P, [asynchronous, unless_suspending]),
    true = erlang:resume_process(P),
    show(options, [U1, U2, A1, A2, Reply, A3, A4, ping(P)]),
    P ! stop.

%% A suspender's exit removes its suspends.
suspender_exit() ->
    P = spawn(fun ponger/0),
    Self = self(),
    {S, Ref} = spawn_monitor(fun() ->
        true = erlang:suspend_process(P),
        Self ! suspended
    end),
    receive
        suspended -> ok
    end,
    receive
        {'DOWN', Ref, process, S, _} -> ok
    end,
    show(suspender_exit, [wait_until_running(P, 50), ping(P)]),
    P ! stop.

wait_until_running(_, 0) ->
    still_suspended;
wait_until_running(P, N) ->
    case status(P) of
        suspended ->
            receive
            after 10 -> wait_until_running(P, N - 1)
            end;
        _ ->
            resumed
    end.

errors() ->
    P = spawn(fun ponger/0),
    Dead = spawn(fun() -> ok end),
    Ref = monitor(process, Dead),
    receive
        {'DOWN', Ref, _, _, _} -> ok
    end,
    show(errors, [
        catching(fun() -> erlang:suspend_process(self()) end),
        catching(fun() -> erlang:suspend_process(id(x)) end),
        catching(fun() -> erlang:suspend_process(Dead) end),
        catching(fun() -> erlang:suspend_process(P, [id(bad)]) end),
        catching(fun() -> erlang:suspend_process(P, id(x)) end),
        catching(fun() -> erlang:suspend_process(P, [{asynchronous, a} | id(tail)]) end),
        catching(fun() -> erlang:resume_process(P) end),
        catching(fun() -> erlang:resume_process(self()) end),
        catching(fun() -> erlang:resume_process(id(x)) end),
        catching(fun() -> erlang:resume_process(Dead) end),
        ping(P)
    ]),
    P ! stop.

display() ->
    P = spawn(fun ponger/0),
    show(display, [
        erlang:process_display(self(), backtrace),
        erlang:process_display(P, backtrace),
        catching(fun() -> erlang:process_display(P, id(other)) end),
        catching(fun() -> erlang:process_display(id(x), backtrace) end)
    ]),
    P ! stop.

catching(Fun) ->
    try Fun() of
        Value -> Value
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Values) -> io:format("~s: ~w~n", [Label, Values]).

id(X) -> X.
