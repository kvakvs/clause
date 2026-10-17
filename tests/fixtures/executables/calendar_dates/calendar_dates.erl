-module(calendar_dates).
-export([main/1]).

%% calendar (plan 21 step LIB5): Gregorian arithmetic over large and negative years, ISO weeks, RFC 3339 parsing and
%% formatting with explicit offsets, and error reasons. Local-time results depend on the host's zone, so only their
%% shapes are shown.
main(_) ->
    Dates = [
        {2000, 1, 1},
        {2000, 2, 29},
        {1999, 12, 31},
        {1970, 1, 1},
        {0, 1, 1},
        {-1, 12, 31},
        {-400, 3, 1},
        {12345, 6, 7}
    ],
    show(days, [calendar:date_to_gregorian_days(D) || D <- Dates]),
    show(round_trip, [
        calendar:gregorian_days_to_date(calendar:date_to_gregorian_days(D)) =:= D
     || D <- Dates
    ]),
    show(arithmetic, [
        calendar:date_to_gregorian_days(2024, 13, 40),
        calendar:date_to_gregorian_days(10000000000000000000000, 1, 1),
        calendar:gregorian_days_to_date(-1),
        calendar:gregorian_days_to_date(1000000000000),
        calendar:datetime_to_gregorian_seconds({{2024, 2, 29}, {23, 59, 59}}),
        calendar:gregorian_seconds_to_datetime(63870000000),
        calendar:gregorian_seconds_to_datetime(-5),
        [calendar:day_of_the_week(D) || D <- Dates],
        calendar:day_of_the_week(2024, 7, 4),
        [calendar:is_leap_year(Y) || Y <- [1900, 2000, 2023, 2024, -4, 0]],
        [calendar:last_day_of_the_month(2023, M) || M <- lists:seq(1, 12)],
        calendar:last_day_of_the_month(2024, 2),
        [
            calendar:valid_date(D)
         || D <- [{2023, 2, 29}, {2024, 2, 29}, {2024, 4, 31}, {2024, 0, 1}, {2024, 1, 0}]
        ],
        calendar:valid_date(2024, 13, 1),
        calendar:seconds_to_time(0),
        calendar:seconds_to_time(86399),
        calendar:seconds_to_daystime(-1),
        calendar:seconds_to_daystime(1000000),
        calendar:time_to_seconds({12, 34, 56}),
        calendar:time_difference({{2024, 1, 1}, {0, 0, 0}}, {{2023, 12, 31}, {23, 0, 1}}),
        calendar:time_difference({{2024, 1, 1}, {0, 0, 0}}, {{2024, 3, 1}, {12, 0, 0}}),
        [
            calendar:iso_week_number(D)
         || D <- [{2021, 1, 1}, {2020, 12, 31}, {2024, 12, 30}, {2026, 6, 15}, {2015, 12, 31}]
        ],
        calendar:now_to_datetime({1700, 123456, 789}),
        calendar:now_to_universal_time({0, 0, 0}),
        calendar:universal_time_to_system_time({{2024, 1, 1}, {0, 0, 0}}),
        calendar:universal_time_to_system_time({{2024, 1, 1}, {0, 0, 0}}, [{unit, millisecond}]),
        calendar:system_time_to_universal_time(1704067200123, millisecond),
        calendar:system_time_to_universal_time(-1, second)
    ]),
    show(rfc3339, [
        calendar:rfc3339_to_system_time("2018-02-01T16:17:58+01:00"),
        calendar:rfc3339_to_system_time(<<"2018-02-01 16:17:58.123456Z">>, [{unit, microsecond}]),
        calendar:rfc3339_to_system_time("2016-12-31T23:59:60Z"),
        calendar:rfc3339_to_system_time("-0001-01-01T00:00:00-05:30"),
        calendar:rfc3339_to_system_time("2000-01-01T00:00:00.5z", [{unit, millisecond}]),
        calendar:rfc3339_to_system_time("2000-01-01T00:00:00+02"),
        calendar:system_time_to_rfc3339(1517498278, [{offset, "+01:00"}]),
        calendar:system_time_to_rfc3339(1517498278, [{offset, "Z"}, {return, binary}]),
        calendar:system_time_to_rfc3339(1517498278123, [{unit, millisecond}, {offset, 3600000}]),
        calendar:system_time_to_rfc3339(-1, [
            {unit, microsecond}, {offset, -3600000000}, {time_designator, $\s}
        ]),
        calendar:system_time_to_rfc3339(-62167219200, [{offset, "-02:00"}]),
        calendar:system_time_to_rfc3339(5, [{unit, nanosecond}, {offset, 0}])
    ]),
    show(local_shapes, [
        is_tuple(calendar:local_time()),
        is_tuple(calendar:universal_time()),
        is_tuple(calendar:iso_week_number()),
        is_tuple(calendar:universal_time_to_local_time({{2024, 1, 1}, {0, 0, 0}})),
        is_integer(calendar:local_time_to_system_time({{2024, 1, 15}, {12, 0, 0}})),
        length(calendar:local_time_to_universal_time_dst({{2024, 1, 15}, {12, 0, 0}})),
        is_list(calendar:system_time_to_rfc3339(0)),
        is_tuple(calendar:now_to_local_time({0, 0, 0})),
        is_tuple(calendar:system_time_to_local_time(0, second))
    ]),
    show(errors, [
        reason(fun() -> calendar:date_to_gregorian_days(2024, 1, 0) end),
        reason(fun() -> calendar:date_to_gregorian_days(2024, id(a), 1) end),
        reason(fun() -> calendar:date_to_gregorian_days(id(2024.0), 1, 1) end),
        reason(fun() -> calendar:date_to_gregorian_days(id(x)) end),
        reason(fun() -> calendar:gregorian_days_to_date(id(1.5)) end),
        reason(fun() -> calendar:datetime_to_gregorian_seconds({{2024, 1, 1}, {24, 0, 0}}) end),
        reason(fun() -> calendar:is_leap_year(id(2024.0)) end),
        reason(fun() -> calendar:last_day_of_the_month(2024, 13) end),
        reason(fun() -> calendar:valid_date(id(a), 1, 1) end),
        reason(fun() -> calendar:seconds_to_time(86400) end),
        reason(fun() -> calendar:seconds_to_time(id(1.5)) end),
        reason(fun() -> calendar:seconds_to_daystime(id(1.5)) end),
        reason(fun() -> calendar:time_to_seconds({1, 60, 0}) end),
        reason(fun() -> calendar:time_difference(id(x), {{2024, 1, 1}, {0, 0, 0}}) end),
        reason(fun() -> calendar:rfc3339_to_system_time("2018-02-01T16:17:58") end),
        reason(fun() -> calendar:rfc3339_to_system_time("2018-02-01") end),
        reason(fun() -> calendar:rfc3339_to_system_time("2018-0x-01T16:17:58Z") end),
        reason(fun() -> calendar:rfc3339_to_system_time("2018-02-01T25:17:58Z") end),
        reason(fun() -> calendar:rfc3339_to_system_time("2018-02-01T16:17:58+0100") end),
        reason(fun() -> calendar:rfc3339_to_system_time("2018-02-01T16:17:58/01:00") end),
        reason(fun() -> calendar:rfc3339_to_system_time(id(x)) end),
        reason(fun() -> calendar:system_time_to_rfc3339(1 bsl 40, [{offset, "Z"}]) end),
        reason(fun() -> calendar:system_time_to_rfc3339(0, [{offset, "Z"}, {return, other}]) end),
        reason(fun() -> calendar:system_time_to_rfc3339(0, [{offset, foo}]) end),
        reason(fun() ->
            calendar:universal_time_to_system_time({{2024, 1, 1}, {0, 0, 0}}, [{unit, hours}])
        end),
        reason(fun() -> calendar:now_to_datetime(id(x)) end)
    ]),
    io:format("calendar_dates: ok~n").

id(Value) -> Value.

show(Name, Values) -> io:format("~w: ~w~n", [Name, Values]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.
