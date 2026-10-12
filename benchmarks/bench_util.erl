%% Shared timing helpers of the manual benchmarks (plan step 69A); built by both OTP and Clause.
-module(bench_util).
-export([run/3, random_list/2, bytes/1, done/1]).

%% Time Iterations calls of Fun after one warm-up call and print one "BENCH Name Microseconds" line.
run(Name, Iterations, Fun) ->
    _ = Fun(),
    Start = erlang:monotonic_time(microsecond),
    repeat(Iterations, Fun),
    Elapsed = erlang:monotonic_time(microsecond) - Start,
    io:format("BENCH ~s ~w~n", [Name, Elapsed]).

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
