%% Clause's calendar (plan 21 step LIB5, docs/builtins.md#calendar): the Gregorian arithmetic is runtime builtins
%% with clause_builtin_placeholder bodies; local time, system time and RFC 3339 are Erlang over the erlang time
%% builtins.
-module(calendar).
-export([
    date_to_gregorian_days/1,
    date_to_gregorian_days/3,
    datetime_to_gregorian_seconds/1,
    day_of_the_week/1,
    day_of_the_week/3,
    gregorian_days_to_date/1,
    gregorian_seconds_to_datetime/1,
    is_leap_year/1,
    iso_week_number/0,
    iso_week_number/1,
    last_day_of_the_month/2,
    local_time/0,
    local_time_to_system_time/1,
    local_time_to_system_time/2,
    local_time_to_universal_time/1,
    local_time_to_universal_time/2,
    local_time_to_universal_time_dst/1,
    now_to_datetime/1,
    now_to_local_time/1,
    now_to_universal_time/1,
    rfc3339_to_system_time/1,
    rfc3339_to_system_time/2,
    seconds_to_daystime/1,
    seconds_to_time/1,
    system_time_to_local_time/2,
    system_time_to_rfc3339/1,
    system_time_to_rfc3339/2,
    system_time_to_universal_time/2,
    time_difference/2,
    time_to_seconds/1,
    universal_time/0,
    universal_time_to_local_time/1,
    universal_time_to_system_time/1,
    universal_time_to_system_time/2,
    valid_date/1,
    valid_date/3
]).
-include("calendar.types.hrl").

%% Seconds from year 0 to 1970, to 10000, and from year -9999 to 1970: the range RFC 3339 times must fall in.
-define(EPOCH, 62167219200).
-define(YEAR_10000, 253402300800).
-define(YEAR_MINUS_9999, 377705116800).

date_to_gregorian_days(_) -> clause_builtin_placeholder.
date_to_gregorian_days(_, _, _) -> clause_builtin_placeholder.
datetime_to_gregorian_seconds(_) -> clause_builtin_placeholder.
day_of_the_week(_) -> clause_builtin_placeholder.
day_of_the_week(_, _, _) -> clause_builtin_placeholder.
gregorian_days_to_date(_) -> clause_builtin_placeholder.
gregorian_seconds_to_datetime(_) -> clause_builtin_placeholder.
is_leap_year(_) -> clause_builtin_placeholder.
iso_week_number(_) -> clause_builtin_placeholder.
last_day_of_the_month(_, _) -> clause_builtin_placeholder.
seconds_to_daystime(_) -> clause_builtin_placeholder.
seconds_to_time(_) -> clause_builtin_placeholder.
time_difference(_, _) -> clause_builtin_placeholder.
time_to_seconds(_) -> clause_builtin_placeholder.
valid_date(_) -> clause_builtin_placeholder.
valid_date(_, _, _) -> clause_builtin_placeholder.

%% The local and universal time now, and conversions between them.
local_time() -> erlang:localtime().
universal_time() -> erlang:universaltime().
universal_time_to_local_time(DateTime) -> erlang:universaltime_to_localtime(DateTime).
local_time_to_universal_time(DateTime) -> erlang:localtime_to_universaltime(DateTime).
local_time_to_universal_time(DateTime, IsDst) -> erlang:localtime_to_universaltime(DateTime, IsDst).

%% The ISO week of today's local date.
iso_week_number() ->
    {Date, _} = local_time(),
    calendar:iso_week_number(Date).

%% The universal times a local time can stand for: none in a gap, two in an overlap.
local_time_to_universal_time_dst(DateTime) ->
    Dst = erlang:localtime_to_universaltime(DateTime, true),
    Std = erlang:localtime_to_universaltime(DateTime, false),
    case {erlang:universaltime_to_localtime(Dst), erlang:universaltime_to_localtime(Std)} of
        {DateTime, DateTime} when Dst =/= Std -> [Dst, Std];
        {DateTime, _} -> [Dst];
        {_, DateTime} -> [Std];
        _ -> []
    end.

%% The universal and local datetime of an erlang:timestamp().
now_to_datetime({MegaSecs, Secs, _}) -> datetime_of(MegaSecs * 1000000 + Secs).
now_to_universal_time(Now) -> now_to_datetime(Now).
now_to_local_time(Now) -> erlang:universaltime_to_localtime(now_to_datetime(Now)).

%% System time (from 1970, in a time unit) and datetimes.
local_time_to_system_time(LocalTime) -> local_time_to_system_time(LocalTime, []).

local_time_to_system_time(LocalTime, Options) ->
    case local_time_to_universal_time_dst(LocalTime) of
        [UniversalTime] -> universal_time_to_system_time(UniversalTime, Options);
        [] -> erlang:error({non_existing_local_time, LocalTime});
        [_, _] -> erlang:error({ambiguous_local_time, LocalTime})
    end.

universal_time_to_system_time(DateTime) -> universal_time_to_system_time(DateTime, []).

universal_time_to_system_time(DateTime, Options) ->
    Factor = factor(proplists:get_value(unit, Options, second)),
    seconds_since_epoch(DateTime) * Factor.

system_time_to_universal_time(Time, Unit) ->
    datetime_of(erlang:convert_time_unit(Time, Unit, second)).

system_time_to_local_time(Time, Unit) ->
    erlang:universaltime_to_localtime(system_time_to_universal_time(Time, Unit)).

seconds_since_epoch(DateTime) -> calendar:datetime_to_gregorian_seconds(DateTime) - ?EPOCH.

datetime_of(Seconds) -> calendar:gregorian_seconds_to_datetime(Seconds + ?EPOCH).

%% An RFC 3339 date and time as system time in the unit option (seconds by default).
rfc3339_to_system_time(Text) -> rfc3339_to_system_time(Text, []).

rfc3339_to_system_time(Bin, Options) when is_binary(Bin) -> rfc3339_fields(Bin, Bin, Options);
rfc3339_to_system_time(List, Options) when is_list(List) -> rfc3339_fields(List, List, Options).

%% The fields of YYYY-MM-DD?HH:MM:SS (a year with a leading minus is negative), then the fraction and offset.
rfc3339_fields(
    <<
        $-,
        Y:4/binary,
        $-,
        Mo:2/binary,
        $-,
        D:2/binary,
        _,
        H:2/binary,
        $:,
        Mi:2/binary,
        $:,
        S:2/binary,
        Rest/binary
    >>,
    In,
    Options
) ->
    rfc3339_time(In, Options, -binary_to_integer(Y), [Mo, D, H, Mi, S], binary_to_list(Rest));
rfc3339_fields(
    <<Y:4/binary, $-, Mo:2/binary, $-, D:2/binary, _, H:2/binary, $:, Mi:2/binary, $:, S:2/binary,
        Rest/binary>>,
    In,
    Options
) ->
    rfc3339_time(In, Options, binary_to_integer(Y), [Mo, D, H, Mi, S], binary_to_list(Rest));
rfc3339_fields(
    [$-, Y1, Y2, Y3, Y4, $-, Mo1, Mo2, $-, D1, D2, _, H1, H2, $:, Mi1, Mi2, $:, S1, S2 | Rest],
    In,
    Options
) ->
    Fields = [[Mo1, Mo2], [D1, D2], [H1, H2], [Mi1, Mi2], [S1, S2]],
    rfc3339_time(In, Options, -list_to_integer([Y1, Y2, Y3, Y4]), Fields, Rest);
rfc3339_fields(
    [Y1, Y2, Y3, Y4, $-, Mo1, Mo2, $-, D1, D2, _, H1, H2, $:, Mi1, Mi2, $:, S1, S2 | Rest],
    In,
    Options
) ->
    Fields = [[Mo1, Mo2], [D1, D2], [H1, H2], [Mi1, Mi2], [S1, S2]],
    rfc3339_time(In, Options, list_to_integer([Y1, Y2, Y3, Y4]), Fields, Rest).

rfc3339_time(In, Options, Year, Fields, Rest) ->
    [Month, Day, Hour, Min, Sec] = [to_integer(Field) || Field <- Fields],
    {Second, Leap} =
        case {Hour, Min, Sec} of
            {23, 59, 60} -> {59, 1};
            _ -> {Sec, 0}
        end,
    Unit = proplists:get_value(unit, Options, second),
    {Fraction, Offset} = lists:splitwith(
        fun(C) -> (C >= $0 andalso C =< $9) orelse C =:= $. end, Rest
    ),
    Time = seconds_since_epoch({{Year, Month, Day}, {Hour, Min, Second}}) + Leap,
    Secs = Time - offset_seconds(Offset),
    in_range(In, Options, Secs),
    erlang:convert_time_unit(Secs, second, Unit) + fraction(Unit, Fraction).

to_integer(Field) when is_binary(Field) -> binary_to_integer(Field);
to_integer(Field) -> list_to_integer(Field).

%% The seconds of an offset: Z (or z), or +HH:MM, -HH:MM, +HH, -HH.
offset_seconds("Z") ->
    0;
offset_seconds("z") ->
    0;
offset_seconds(Offset) ->
    [Sign, H1, H2 | MinutesPart] = Offset,
    Minutes =
        case MinutesPart of
            [$:, M1, M2] -> list_to_integer([M1, M2]);
            [] -> 0
        end,
    Seconds = 3600 * list_to_integer([H1, H2]) + 60 * Minutes,
    case Sign of
        $- -> -Seconds;
        $+ -> Seconds
    end.

%% The fraction digits as a count of the unit.
fraction(second, _) -> 0;
fraction(_, "") -> 0;
fraction(Unit, Digits) -> round(factor(Unit) * list_to_float([$0 | Digits])).

%% badarg unless the seconds from 1970 fall in OTP's range: from year -9999, below the seconds from year 0 to 10000.
in_range(_, _, Secs) when Secs >= -?YEAR_MINUS_9999, Secs < ?YEAR_10000 -> ok;
in_range(Arg, Options, _) -> erlang:error({badarg, [Arg, Options]}).

factor(second) -> 1;
factor(millisecond) -> 1000;
factor(microsecond) -> 1000000;
factor(nanosecond) -> 1000000000;
factor(native) -> erlang:convert_time_unit(1, second, native).

%% A system time as an RFC 3339 string, with the unit, offset, time_designator and return options.
system_time_to_rfc3339(Time) -> system_time_to_rfc3339(Time, []).

system_time_to_rfc3339(Time, Options) ->
    Offset = proplists:get_value(offset, Options, ""),
    case proplists:get_value(unit, Options, second) of
        native ->
            Millis = erlang:convert_time_unit(Time, native, millisecond),
            NativeOffset =
                case is_integer(Offset) of
                    true -> erlang:convert_time_unit(Offset, native, millisecond);
                    false -> Offset
                end,
            rfc3339_text(Millis, Options, millisecond, NativeOffset);
        Unit ->
            rfc3339_text(Time, Options, Unit, Offset)
    end.

rfc3339_text(Time, Options, Unit, Offset) ->
    Designator = proplists:get_value(time_designator, Options, $T),
    Adjustment = offset_adjustment(Time, Unit, Offset),
    Zone = zone_text(Offset, Adjustment),
    Adjusted = Time + erlang:convert_time_unit(Adjustment, second, Unit),
    Factor = factor(Unit),
    Secs =
        case Adjusted rem Factor < 0 of
            true -> Adjusted div Factor - 1;
            false -> Adjusted div Factor
        end,
    in_range(Time, Options, Secs),
    {{Year, Month, Day}, {Hour, Min, Sec}} = datetime_of(Secs),
    Parts = [
        year_text(Year),
        "-",
        two(Month),
        "-",
        two(Day),
        [Designator],
        two(Hour),
        ":",
        two(Min),
        ":",
        two(Sec),
        fraction_text(Factor, Adjusted),
        Zone
    ],
    case proplists:get_value(return, Options, string) of
        string -> lists:append(Parts);
        binary -> iolist_to_binary(Parts)
    end.

%% The seconds a time zone adds: the local offset, an offset string's, or an integer offset in the unit.
offset_adjustment(Time, Unit, "") ->
    Universal = system_time_to_universal_time(Time, Unit),
    Local = erlang:universaltime_to_localtime(Universal),
    calendar:datetime_to_gregorian_seconds(Local) -
        calendar:datetime_to_gregorian_seconds(Universal);
offset_adjustment(_, _, Offset) when is_list(Offset) ->
    offset_seconds(Offset);
offset_adjustment(_, Unit, Offset) when is_integer(Offset) ->
    erlang:convert_time_unit(Offset, Unit, second).

%% The zone of the text: +HH:MM or -HH:MM for the local or an integer offset, else the offset string itself.
zone_text(Offset, Seconds) when Offset =:= ""; is_integer(Offset) ->
    Sign =
        case Seconds < 0 of
            true -> $-;
            false -> $+
        end,
    Abs = abs(Seconds),
    [Sign | two(Abs div 3600) ++ ":" ++ two((Abs rem 3600) div 60)];
zone_text(Offset, _) ->
    Offset.

fraction_text(1, _) ->
    "";
fraction_text(Factor, Time) ->
    Digits = integer_to_list(non_negative(Time rem Factor, Factor)),
    [$. | lists:duplicate(digits_of(Factor) - length(Digits), $0) ++ Digits].

non_negative(R, D) when R < 0 -> non_negative(R + D, D);
non_negative(R, _) -> R.

digits_of(1000) -> 3;
digits_of(1000000) -> 6;
digits_of(1000000000) -> 9.

two(N) when N < 10 -> [$0 | integer_to_list(N)];
two(N) -> integer_to_list(N).

year_text(N) when N < 0 -> [$- | year_text(-N)];
year_text(N) when N < 10 -> [$0, $0, $0 | integer_to_list(N)];
year_text(N) when N < 100 -> [$0, $0 | integer_to_list(N)];
year_text(N) when N < 1000 -> [$0 | integer_to_list(N)];
year_text(N) -> integer_to_list(N).
