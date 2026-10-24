%% Clause's report writer until its logger lands (plan 21 step LIB20, docs/library.md): the log events proc_lib,
%% sys and the behaviours give OTP's logger, written at once on standard error as OTP's default handler formats them
%% with the legacy header. Events below the default primary level (notice) are dropped.
-module(clause_logger).
-export([log/3, format/2, format_report/1]).

-define(FORMAT_OPTIONS, #{depth => unlimited, chars_limit => unlimited, single_line => false}).

%% Created for LIB20: logs Msg ({report, Report} formatted by Meta's report_cb, {Format, Args} or {string, Text})
%% at Level.
log(Level, Msg, Meta) ->
    case severity(Level) =< severity(notice) of
        true -> write(Level, Msg, Meta);
        false -> ok
    end.

%% error_logger:format/2's event: an error report of a format and its arguments.
format(Format, Args) ->
    log(error, {Format, Args}, #{error_logger => #{tag => error}}).

%% logger:format_report/1: a report map or key-value list as indented lines, a string as itself.
format_report(Report) when is_map(Report) ->
    format_report(maps:to_list(Report));
format_report(Report) ->
    try lists:flatten(Report) of
        [] -> {"~tp", [[]]};
        Flat -> report_lines(io_lib:printable_unicode_list(Flat), Flat, Report)
    catch
        _:_ -> {"~tp", [Report]}
    end.

report_lines(true, Flat, _) ->
    {"~ts", [Flat]};
report_lines(false, _, Report) ->
    Lines = [report_line(Item) || Item <- Report],
    {
        lists:flatten(lists:join($\n, [Format || {Format, _} <- Lines])),
        lists:append([Args || {_, Args} <- Lines])
    }.

report_line({Tag, Data}) ->
    case is_string(Data) of
        true -> {"    ~tp: ~ts", [Tag, Data]};
        false -> {"    ~tp: ~tp", [Tag, Data]}
    end;
report_line(Data) ->
    {"    ~tp", [Data]}.

is_string(Data) ->
    try lists:flatten(Data) of
        [] -> false;
        Flat -> io_lib:printable_unicode_list(Flat)
    catch
        _:_ -> false
    end.

severity(emergency) -> 0;
severity(alert) -> 1;
severity(critical) -> 2;
severity(error) -> 3;
severity(warning) -> 4;
severity(notice) -> 5;
severity(info) -> 6;
severity(debug) -> 7.

write(Level, Msg, Meta) ->
    Text = [header(title(Level, Meta)), $\n, message(Msg, Meta), $\n],
    io:put_chars(standard_error, unicode:characters_to_binary(Text)).

%% The legacy title: the formatter's own from the metadata, else the level's ("ERROR REPORT"); error_logger's
%% notices are info reports.
title(Level, Meta) ->
    case Meta of
        #{logger_formatter := #{title := Title}} -> Title;
        #{error_logger := _} when Level =:= notice -> "INFO REPORT";
        _ -> string:uppercase(atom_to_list(Level)) ++ " REPORT"
    end.

%% =TITLE==== 24-Oct-2026::18:00:09.628804 ===, in local time.
header(Title) ->
    Now = os:system_time(microsecond),
    {{Y, Mo, D}, {H, Mi, S}} = calendar:system_time_to_local_time(Now, microsecond),
    io_lib:format("=~ts==== ~w-~s-~4w::~2..0w:~2..0w:~2..0w.~6..0w ===", [
        Title, D, month(Mo), Y, H, Mi, S, Now rem 1000000
    ]).

month(M) ->
    element(
        M, {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}
    ).

message({report, Report}, #{report_cb := Callback}) when is_function(Callback, 2) ->
    guarded(fun() -> Callback(Report, ?FORMAT_OPTIONS) end, Report);
message({report, Report}, #{report_cb := Callback}) when is_function(Callback, 1) ->
    guarded(
        fun() ->
            {Format, Args} = Callback(Report),
            io_lib:format(Format, Args)
        end,
        Report
    );
message({report, Report}, _) ->
    {Format, Args} = format_report(Report),
    io_lib:format(Format, Args);
message({string, Text}, _) ->
    Text;
message({Format, Args}, _) ->
    guarded(fun() -> io_lib:format(Format, Args) end, {Format, Args}).

%% A formatter that fails shows the raw term instead.
guarded(Format, Term) ->
    try
        Format()
    catch
        _:_ -> io_lib:format("~tp", [Term])
    end.
