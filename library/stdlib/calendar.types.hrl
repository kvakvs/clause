%% OTP 29 calendar types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([date/0,datetime/0,datetime1970/0,time/0]).
-type year() :: integer().
-type year1970() :: 1970..10000.
-type month() :: 1..12.
-type day() :: 1..31.
-type hour() :: 0..23.
-type minute() :: 0..59.
-type second() :: 0..59.
-type date() :: {year(), month(), day()}.
-type time() :: {hour(), minute(), second()}.
-type datetime() :: {date(), time()}.
-type datetime1970() :: {{year1970(), month(), day()}, time()}.
