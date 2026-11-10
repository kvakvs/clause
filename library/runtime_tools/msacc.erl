%% OTP's microstate accounting module (plan 21 step MIM5, docs/libraries/runtime_tools.md): switches
%% erlang:statistics(microstate_accounting) on and off, normalizes its counters to microseconds and analyses and
%% prints them as OTP's msacc does. Clause's runtime accounts its workers as schedulers.
-module(msacc).
-export([
    available/0,
    from_file/1,
    print/0,
    print/1,
    print/2,
    print/3,
    reset/0,
    start/0,
    start/1,
    stats/0,
    stats/2,
    stop/0,
    to_file/1
]).

%% OTP's column order of the states it prints; other states follow in term order.
-define(STATES, [emulator, port, aux, check_io, gc, other, sleep]).

available() ->
    try erlang:statistics(microstate_accounting) of
        _ -> true
    catch
        error:badarg -> false
    end.

start() ->
    erlang:system_flag(microstate_accounting, true).

%% Counts from zero for Time milliseconds; the accounting stays on when the sleep fails, as OTP's.
start(Time) ->
    stop(),
    reset(),
    start(),
    timer:sleep(Time),
    stop().

stop() ->
    erlang:system_flag(microstate_accounting, false).

reset() ->
    erlang:system_flag(microstate_accounting, reset).

%% The runtime's counters in microseconds, by thread type and id.
stats() ->
    Threads = [
        #{
            '$type' => msacc_data,
            type => Type,
            id => Id,
            counters => maps:map(
                fun(_, Value) -> erlang:convert_time_unit(Value, perf_counter, microsecond) end, Counters
            )
        }
     || #{type := Type, id := Id, counters := Counters} <- erlang:statistics(microstate_accounting)
    ],
    by_thread(Threads).

stats(system_realtime, Data) ->
    lists:foldl(fun(#{counters := Counters}, Sum) -> Sum + total(Counters) end, 0, Data);
stats(system_runtime, Data) ->
    stats(system_realtime, run_time(Data));
stats(realtime, Data) ->
    System = stats(system_realtime, Data),
    by_thread([percentages(Thread, System) || Thread <- Data]);
stats(runtime, Data) ->
    stats(realtime, run_time(Data));
stats(type, [#{'$type' := msacc_data} | _] = Data) ->
    [merged_data(Type, Threads) || {Type, Threads} <- by_type(Data)];
stats(type, [#{'$type' := msacc_stats} | _] = Stats) ->
    [merged_stats(Type, Threads) || {Type, Threads} <- by_type(Stats)];
stats(type, []) ->
    [].

%% The threads without their sleep time.
run_time(Data) ->
    [Thread#{counters := maps:remove(sleep, Counters)} || #{counters := Counters} = Thread <- Data].

total(Counters) ->
    lists:sum(maps:values(Counters)).

percentages(#{counters := Counters} = Thread, System) ->
    Total = total(Counters),
    Shares = maps:map(
        fun(_, Value) -> #{thread => percent(Value, Total), system => percent(Value, System)} end, Counters
    ),
    Thread#{'$type' := msacc_stats, counters := Shares, system => percent(Total, System)}.

percent(_, 0) -> 0.0;
percent(Value, Total) -> Value / Total * 100.

by_thread(Threads) ->
    lists:sort(fun(A, B) -> thread_key(A) =< thread_key(B) end, Threads).

thread_key(Thread) ->
    {maps:get(type, Thread), maps:get(id, Thread, undefined)}.

%% The threads of each type, types in order.
by_type(Threads) ->
    Types = lists:usort([Type || #{type := Type} <- Threads]),
    [{Type, [Thread || #{type := T} = Thread <- by_thread(Threads), T =:= Type]} || Type <- Types].

%% A type's counters summed.
merged_data(Type, Threads) ->
    Sum = fun(#{counters := Counters}, Acc) -> maps:merge_with(fun(_, A, B) -> A + B end, Acc, Counters) end,
    #{'$type' => msacc_data, type => Type, counters => lists:foldl(Sum, #{}, Threads)}.

%% A type's shares: thread shares averaged, system shares summed.
merged_stats(Type, Threads) ->
    Count = length(Threads),
    [#{counters := First} | _] = Threads,
    Counters = maps:map(
        fun(State, _) ->
            Shares = [maps:get(State, C) || #{counters := C} <- Threads],
            #{
                thread => lists:sum([S || #{thread := S} <- Shares]) / Count,
                system => lists:sum([S || #{system := S} <- Shares])
            }
        end,
        First
    ),
    #{
        '$type' => msacc_stats,
        type => Type,
        counters => Counters,
        system => lists:sum([S || #{system := S} <- Threads])
    }.

%% --- Printing ---

print() ->
    print(stats()).

print(DataOrStats) ->
    print(DataOrStats, #{}).

print(DataOrStats, Options) ->
    print(standard_io, DataOrStats, Options).

print(Device, DataOrStats, Options) ->
    print_int(Device, DataOrStats, maps:merge(#{system => false}, Options)).

print_int(Device, [#{'$type' := msacc_data, id := _} | _] = Data, Options) ->
    print_stats_overview(Device, Data),
    print_stats(Device, stats(realtime, Data), stats(realtime, stats(type, Data)), Options);
print_int(Device, [#{'$type' := msacc_data} | _] = Data, Options) ->
    print_stats(Device, [], stats(realtime, Data), Options);
print_int(Device, [#{'$type' := msacc_stats, id := _} | _] = Stats, Options) ->
    print_stats(Device, Stats, stats(type, Stats), Options);
print_int(Device, [#{'$type' := msacc_stats} | _] = Stats, Options) ->
    print_stats(Device, [], Stats, Options).

%% The real and run time totals; the scheduler average divides by the scheduler threads (badarith without one).
print_stats_overview(Device, Data) ->
    Threads = length(Data),
    RealTime = stats(system_realtime, Data) div Threads,
    RunTime = stats(system_runtime, Data),
    Schedulers = [Thread || #{type := scheduler} = Thread <- Data],
    SchedulerRunTime = stats(system_runtime, Schedulers) div length(Schedulers),
    Width = lists:max([length(integer_to_list(N)) || N <- [RealTime, RunTime, SchedulerRunTime]]),
    io:format(Device, "Average thread real-time    : ~*w us~n", [Width, RealTime]),
    io:format(Device, "Accumulated system run-time : ~*w us~n", [Width, RunTime]),
    io:format(Device, "Average scheduler run-time  : ~*w us~n", [Width, SchedulerRunTime]),
    io:format(Device, "~n", []).

print_stats(Device, Threads, Types, Options) ->
    [#{counters := Counters} | _] = Types,
    States = states(Counters),
    io:format(Device, "~14s", ["Thread"]),
    [io:format(Device, heading(Options), [State]) || State <- States],
    io:format(Device, "~n", []),
    print_threads(Device, Threads, States, Options),
    io:format(Device, "~nStats per type:~n", []),
    [
        print_row(Device, io_lib:format("~14s", [Type]), Counters1, States, Options)
     || #{type := Type, counters := Counters1} <- Types
    ],
    ok.

print_threads(_, [], _, _) ->
    ok;
print_threads(Device, Threads, States, Options) ->
    io:format(Device, "~nStats per thread:~n", []),
    [
        print_row(Device, io_lib:format("~10s(~2w)", [Type, Id]), Counters, States, Options)
     || #{type := Type, id := Id, counters := Counters} <- Threads
    ],
    ok.

heading(#{system := true}) -> "~9s     ";
heading(_) -> "~9s".

print_row(Device, Name, Counters, States, Options) ->
    io:format(Device, "~ts", [Name]),
    [io:format(Device, share_format(Options), shares(maps:get(State, Counters), Options)) || State <- States],
    io:format(Device, "~n", []).

share_format(#{system := true}) -> "~6.2f%(~4.1f%)";
share_format(_) -> "~8.2f%".

shares(#{thread := Thread, system := System}, #{system := true}) -> [Thread, System];
shares(#{thread := Thread}, _) -> [Thread].

%% The states of a counters map in OTP's column order.
states(Counters) ->
    Known = [State || State <- ?STATES, is_map_key(State, Counters)],
    Known ++ lists:sort(maps:keys(Counters) -- ?STATES).

%% --- Files ---

to_file(Filename) ->
    file:write_file(Filename, io_lib:format("~p.~n", [stats()])).

from_file(Filename) ->
    {ok, [Stats]} = file:consult(Filename),
    Stats.
