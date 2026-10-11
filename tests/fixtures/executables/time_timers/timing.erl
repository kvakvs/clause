-module(timing).
-export([main/1]).

%% Time, unique values and timers (plan step 41F). Clock readings differ between runs, so only their relations,
%% shapes and fixed conversions are printed.
main(_) ->
    units(),
    clocks(),
    calendar(),
    timers(),
    io:format("timing: ok~n").

units() ->
    Cases = [
        {1999, millisecond, second},
        {-1999, millisecond, second},
        {3, second, millisecond},
        {1, second, 3},
        {1, 7, 3},
        {1 bsl 70, nanosecond, second},
        {5, seconds, milli_seconds},
        {1, micro_seconds, nano_seconds},
        {1, bogus, second}
    ],
    [
        io:format("~p~n", [run(fun() -> erlang:convert_time_unit(T, F, U) end)])
     || {T, F, U} <- Cases
    ],
    io:format("~p~n", [erlang:convert_time_unit(1, second, native) > 0]).

clocks() ->
    M1 = erlang:monotonic_time(),
    M2 = erlang:monotonic_time(millisecond),
    M3 = erlang:monotonic_time(),
    io:format("~p~n", [{M3 >= M1, is_integer(M2), erlang:system_time(second) > 1700000000}]),
    io:format("~p~n", [
        erlang:system_time(second) - erlang:time_offset(second) - erlang:monotonic_time(second) =< 1
    ]),
    {Mega, Sec, Micro} = erlang:timestamp(),
    io:format("~p~n", [Mega > 1000 andalso Sec < 1000000 andalso Micro < 1000000]),
    Uniques = [erlang:unique_integer([positive, monotonic]) || _ <- lists:seq(1, 100)],
    io:format("~p~n", [increasing(Uniques) andalso hd(Uniques) > 0]),
    io:format("~p~n", [
        is_integer(erlang:unique_integer()) andalso is_integer(erlang:unique_integer([]))
    ]),
    io:format("~p~n", [run(fun() -> erlang:unique_integer([bogus]) end)]),
    io:format("~p~n", [run(fun() -> erlang:monotonic_time(bogus) end)]).

calendar() ->
    io:format("~p~n", [erlang:universaltime_to_posixtime({{1970, 1, 2}, {0, 0, 0}})]),
    io:format("~p~n", [erlang:universaltime_to_posixtime({{2024, 2, 29}, {23, 59, 59}})]),
    io:format("~p~n", [erlang:posixtime_to_universaltime(86400)]),
    io:format("~p~n", [erlang:posixtime_to_universaltime(-1)]),
    io:format("~p~n", [erlang:posixtime_to_universaltime(1709251199)]),
    Invalid = [
        {{2020, 13, 1}, {0, 0, 0}},
        {{2023, 2, 29}, {0, 0, 0}},
        {{2020, 1, 1}, {24, 0, 0}},
        {{2020, 1, 1}, {0, 0}},
        bogus
    ],
    [io:format("~p~n", [run(fun() -> erlang:universaltime_to_localtime(D) end)]) || D <- Invalid],
    Utc = {{2024, 6, 15}, {12, 30, 0}},
    Local = erlang:universaltime_to_localtime(Utc),
    io:format("~p~n", [erlang:localtime_to_universaltime(Local) =:= Utc]),
    io:format("~p~n", [run(fun() -> erlang:localtime_to_universaltime(Local, bogus) end)]),
    {Date, Time} = erlang:localtime(),
    {UDate, _} = erlang:universaltime(),
    io:format("~p~n", [{tuple_size(Date), tuple_size(Time), tuple_size(UDate)}]),
    io:format("~p~n", [{tuple_size(date()), tuple_size(time())}]).

timers() ->
    Self = self(),
    erlang:send_after(30, Self, third),
    erlang:send_after(10, Self, first),
    erlang:send_after(20, Self, second),
    io:format("~p~n", [
        [
            receive
                M -> M
            end
         || _ <- [1, 2, 3]
        ]
    ]),
    Ref = erlang:start_timer(5, Self, {payload, [1, 2]}),
    receive
        {timeout, Ref, Payload} -> io:format("~p~n", [Payload])
    end,
    Long = erlang:send_after(10000, Self, never),
    Left = erlang:read_timer(Long),
    io:format("~p~n", [is_integer(Left) andalso Left =< 10000]),
    io:format("~p~n", [is_integer(erlang:cancel_timer(Long))]),
    io:format("~p~n", [{erlang:cancel_timer(Long), erlang:read_timer(Long)}]),
    Async = erlang:send_after(10000, Self, never),
    io:format("~p~n", [erlang:cancel_timer(Async, [{async, true}])]),
    receive
        {cancel_timer, Async, Result} -> io:format("~p~n", [is_integer(Result)])
    end,
    Quiet = erlang:send_after(10000, Self, never),
    io:format("~p~n", [erlang:cancel_timer(Quiet, [{info, false}])]),
    Read = erlang:send_after(10000, Self, never),
    io:format("~p~n", [erlang:read_timer(Read, [{async, true}])]),
    receive
        {read_timer, Read, Value} -> io:format("~p~n", [is_integer(Value)])
    end,
    erlang:cancel_timer(Read),
    register(timing_name, Self),
    erlang:send_after(5, timing_name, by_name),
    receive
        by_name -> io:format("by_name~n")
    end,
    Fun = fun(X) -> {Self, X} end,
    erlang:send_after(5, Self, {fun_message, Fun}),
    receive
        {fun_message, Received} -> io:format("~p~n", [Received(7) =:= {Self, 7}])
    end,
    Dead = spawn(fun() -> ok end),
    erlang:send_after(1, Dead, lost),
    Abs = erlang:monotonic_time(millisecond) + 10,
    erlang:send_after(Abs, Self, absolute, [{abs, true}]),
    receive
        absolute -> io:format("absolute~n")
    end,
    Bad = [
        fun() -> erlang:send_after(-1, Self, x) end,
        fun() -> erlang:send_after(10, 1.5, x) end,
        fun() -> erlang:send_after(10, Self, x, [bogus]) end,
        fun() -> erlang:start_timer(1 bsl 60, Self, x) end,
        fun() -> erlang:cancel_timer(abc) end,
        fun() -> erlang:cancel_timer(make_ref()) end,
        fun() -> erlang:read_timer(make_ref()) end
    ],
    [io:format("~p~n", [run(B)]) || B <- Bad],
    receive
        Unexpected -> io:format("unexpected ~p~n", [Unexpected])
    after 50 -> io:format("no stray messages~n")
    end.

% Whether a list strictly increases.
increasing([A, B | Rest]) -> A < B andalso increasing([B | Rest]);
increasing(_) -> true.

% The value of Fun, or {error, Reason}.
run(Fun) ->
    try
        Fun()
    catch
        error:Reason -> {error, Reason}
    end.
