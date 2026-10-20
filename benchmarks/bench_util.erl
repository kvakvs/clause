%% Shared timing helpers of the manual benchmarks (plan step 69A); built by both OTP and Clause.
-module(bench_util).
-export([run/3, random_list/2, bytes/1, done/1]).

-define(SETTLE_MS, 10000).

%% Time Iterations calls of Fun after one warm-up call and print one "BENCH Name Nanoseconds" line.
%% The kernel runs in a fresh process, so no kernel inherits heaps an earlier one grew; its exit frees its memory
%% before the next kernel starts. Added for per-kernel processes.
run(Name, Iterations, Fun) ->
    Parent = self(),
    {Pid, Ref} = spawn_monitor(fun() -> Parent ! {self(), measure(Name, Iterations, Fun)} end),
    receive
        {Pid, Elapsed} ->
            receive
                {'DOWN', Ref, process, Pid, normal} -> ok
            end,
            io:format("BENCH ~s ~w~n", [Name, Elapsed]);
        {'DOWN', Ref, process, Pid, Reason} ->
            erlang:error({kernel_failed, Name, Reason})
    end.

%% Nanoseconds of Iterations calls of Fun. Untimed, the warm-up and the timed calls each wait until the processes
%% they spawned have exited.
measure(Name, Iterations, Fun) ->
    Processes = erlang:system_info(process_count),
    _ = Fun(),
    settle(Name, Processes),
    Start = erlang:monotonic_time(nanosecond),
    repeat(Iterations, Fun),
    Elapsed = erlang:monotonic_time(nanosecond) - Start,
    settle(Name, Processes),
    Elapsed.

%% Wait until no more than Processes processes are alive, so one kernel's exiting processes never run into the
%% next; fails the program after SETTLE_MS milliseconds. Added for benchmark process settling.
settle(Name, Processes) ->
    settle(Name, Processes, erlang:monotonic_time(millisecond) + ?SETTLE_MS).

settle(Name, Processes, Deadline) ->
    Count = erlang:system_info(process_count),
    Late = erlang:monotonic_time(millisecond) > Deadline,
    if
        Count =< Processes ->
            ok;
        Late ->
            erlang:error({processes_left, Name, Count - Processes});
        true ->
            receive
            after 1 -> settle(Name, Processes, Deadline)
            end
    end.

repeat(0, _Fun) ->
    ok;
repeat(N, Fun) ->
    _ = Fun(),
    repeat(N - 1, Fun).

%% Deterministic pseudo-random integers (a linear congruential generator), the same on every system.
random_list(Count, Seed) ->
    random_list(Count, Seed, []).

random_list(0, _State, Acc) ->
    Acc;
random_list(Count, State, Acc) ->
    Next = (State * 1103515245 + 12345) band 16#7FFFFFFF,
    random_list(Count - 1, Next, [Next | Acc]).

%% A binary of Size bytes cycling through the hexadecimal digits.
bytes(Size) ->
    list_to_binary([lists:nth(N rem 16 + 1, "0123456789abcdef") || N <- lists:seq(0, Size - 1)]).

%% Confirm that every kernel of the program finished.
done(Program) ->
    io:format("~s: ok~n", [Program]).
