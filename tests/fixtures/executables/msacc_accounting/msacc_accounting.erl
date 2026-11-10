%% msacc (plan 21 step MIM5): switching microstate accounting, the counters it gathers while processes run and
%% wait, and OTP's analyses and printing of fixed counters. Golden generated once from OTP; the threads besides the
%% schedulers differ between runtimes, so the live counters are checked by their properties only.
-module(msacc_accounting).
-export([main/1]).

main(_) ->
    switching(),
    gathering(),
    analysing(),
    printing(),
    io:format("msacc_accounting: ok~n").

show(Name, Value) ->
    io:format("~p ~p~n", [Name, sorted(Value)]).

%% Maps as sorted key lists, so that the output does not depend on map printing order.
sorted(Map) when is_map(Map) -> lists:sort([{K, sorted(V)} || {K, V} <- maps:to_list(Map)]);
sorted(List) when is_list(List) -> [sorted(E) || E <- List];
sorted(Other) -> Other.

%% --- Live accounting ---

switching() ->
    show(available, msacc:available()),
    show(start, [msacc:start(), msacc:start()]),
    show(stop, [msacc:stop(), msacc:stop()]),
    show(reset, msacc:reset()),
    show(zero_after_reset, lists:all(fun zero/1, msacc:stats())),
    show(flag_badarg, flag_error(fun() -> erlang:system_flag(microstate_accounting, x) end)).

flag_error(Fun) ->
    try Fun() of
        Value -> Value
    catch
        error:Reason -> {error, Reason}
    end.

zero(#{counters := Counters}) ->
    lists:all(fun(V) -> V =:= 0 end, maps:values(Counters)).

gathering() ->
    Raw = erlang:statistics(microstate_accounting),
    show(raw_keys, lists:usort([lists:sort(maps:keys(T)) || T <- Raw])),
    show(raw_states, lists:usort([lists:sort(maps:keys(C)) || #{counters := C} <- Raw])),
    show(start_timed, msacc:start(200)),
    Busy = spawn(fun() -> spin(200000) end),
    show(start_while_busy, msacc:start()),
    Ref = monitor(process, Busy),
    receive
        {'DOWN', Ref, process, Busy, _} -> ok
    end,
    timer:sleep(50),
    show(stop_after_work, msacc:stop()),
    Stats = msacc:stats(),
    show(stats_shape, lists:usort([{maps:get('$type', T), lists:sort(maps:keys(T))} || T <- Stats])),
    Schedulers = [T || #{type := scheduler} = T <- Stats],
    show(schedulers, Schedulers =/= []),
    show(scheduler_ids, lists:sort([Id || #{id := Id} <- Schedulers]) =:= lists:seq(1, length(Schedulers))),
    show(counted, [sum(emulator, Schedulers) > 0, sum(sleep, Schedulers) > 0, all_counters_integers(Stats)]),
    show(frozen_after_stop, Stats =:= msacc:stats()),
    Realtime = msacc:stats(realtime, Stats),
    show(realtime_total, round(lists:sum([S || #{system := S} <- Realtime]))),
    File = "msacc_accounting.txt",
    show(to_file, msacc:to_file(File)),
    show(from_file, msacc:from_file(File) =:= Stats),
    ok = file:delete(File).

spin(0) -> ok;
spin(N) -> spin(N - 1).

sum(State, Threads) ->
    lists:sum([maps:get(State, C) || #{counters := C} <- Threads]).

all_counters_integers(Stats) ->
    lists:all(
        fun(#{counters := C}) -> lists:all(fun(V) -> is_integer(V) andalso V >= 0 end, maps:values(C)) end, Stats
    ).

%% --- Analyses of fixed counters ---

thread(Type, Id, Counters) ->
    Zero = #{aux => 0, check_io => 0, emulator => 0, gc => 0, other => 0, port => 0, sleep => 0},
    #{type => Type, id => Id, counters => maps:merge(Zero, Counters), '$type' => msacc_data}.

data() ->
    [
        thread(scheduler, 2, #{emulator => 10000, gc => 5000, sleep => 985000}),
        thread(scheduler, 1, #{emulator => 600000, port => 100000, other => 50000, sleep => 250000}),
        thread(aux, 1, #{aux => 20000, sleep => 980000}),
        thread(poll, 0, #{check_io => 1234, sleep => 998766}),
        thread(dirty_cpu_scheduler, 1, #{emulator => 3, sleep => 999997})
    ].

uneven() ->
    [
        thread(scheduler, 1, #{emulator => 7, sleep => 3}),
        thread(scheduler, 2, #{emulator => 4}),
        thread(aux, 1, #{aux => 1, sleep => 1})
    ].

analysing() ->
    D = data(),
    show(system_realtime, msacc:stats(system_realtime, D)),
    show(system_runtime, msacc:stats(system_runtime, D)),
    show(realtime, msacc:stats(realtime, D)),
    show(runtime, msacc:stats(runtime, D)),
    show(type, msacc:stats(type, D)),
    show(type_of_runtime, msacc:stats(type, msacc:stats(runtime, D))),
    show(runtime_of_type, msacc:stats(runtime, msacc:stats(type, D))),
    show(zeros, msacc:stats(realtime, [thread(scheduler, 1, #{}), thread(scheduler, 2, #{emulator => 5})])),
    show(empty, [msacc:stats(realtime, []), msacc:stats(system_realtime, []), msacc:stats(type, [])]),
    show(
        averaged,
        msacc:stats(
            type,
            msacc:stats(realtime, [
                thread(scheduler, I, #{emulator => I, sleep => 3})
             || I <- [1, 2, 3]
            ])
        )
    ).

printing() ->
    D = data(),
    io:format("--- print~n"),
    msacc:print(D),
    io:format("--- system~n"),
    msacc:print(D, #{system => true}),
    io:format("--- runtime~n"),
    msacc:print(msacc:stats(runtime, D)),
    io:format("--- type~n"),
    msacc:print(msacc:stats(type, D)),
    io:format("--- type of runtime, system~n"),
    msacc:print(msacc:stats(type, msacc:stats(runtime, D)), #{system => true}),
    io:format("--- uneven~n"),
    show(print, msacc:print(uneven())),
    io:format("--- uneven shares, system~n"),
    msacc:print(standard_io, msacc:stats(realtime, uneven()), #{system => true, other => ignored}),
    show(print_empty, caught(fun() -> msacc:print([]) end)),
    show(print_no_scheduler, caught(fun() -> msacc:print([thread(aux, 1, #{aux => 3, sleep => 7})]) end)),
    show(bad_analysis, caught(fun() -> msacc:stats(bogus, D) end)).

caught(Fun) ->
    try Fun() of
        Value -> Value
    catch
        Class:Reason -> {Class, Reason}
    end.
