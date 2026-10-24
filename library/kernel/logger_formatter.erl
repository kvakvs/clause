%% Clause's logger_formatter (plan 21 step LIB23, docs/library.md): the default formatter of log events. A template
%% of metadata keys and strings lays the event out; the message comes from a format and its arguments, a string, or a
%% report through its report callback, limited by depth, chars_limit and max_size, on one line or several, with
%% OTP's legacy header (=ERROR REPORT==== ...) when asked.
-module(logger_formatter).
-export([format/2, check_config/1]).

-callback check_config(FConfig) -> ok | {error, Reason} when FConfig :: logger:formatter_config(), Reason :: term().
-callback format(LogEvent, FConfig) -> FormattedLogEntry when
    LogEvent :: logger:log_event(), FConfig :: logger:formatter_config(), FormattedLogEntry :: unicode:chardata().

-define(HEADER_TEMPLATE, [[logger_formatter, header], "\n", msg, "\n"]).
-define(SINGLE_TEMPLATE, [time, " ", level, ": ", msg, "\n"]).
-define(MULTI_TEMPLATE, [time, " ", level, ":\n", msg, "\n"]).

format(#{level := Level, msg := Msg, meta := Meta0}, Config0) when is_map(Config0) ->
    Config = with_defaults(Config0),
    Meta = legacy_header(Level, Meta0, Config),
    Template = linear(Meta, maps:get(template, Config)),
    {Before, After0} = lists:splitwith(fun(Item) -> Item =/= msg end, Template),
    {HasMsg, After} =
        case After0 of
            [msg | Rest] -> {true, Rest};
            _ -> {false, After0}
        end,
    B = expand(Level, Meta, Before, Config),
    A = expand(Level, Meta, After, Config),
    Text =
        case HasMsg of
            true -> message_text(Msg, Meta, Config, [B, A]);
            false -> ""
        end,
    truncate(B, Text, A, maps:get(max_size, Config)).

%% The message, its chars_limit lowered by what the template around it takes; on a single line its newlines become
%% ", ".
message_text(Msg, Meta, Config, Around) ->
    Limited =
        case maps:get(chars_limit, Config) of
            unlimited -> Config;
            Limit -> Config#{chars_limit => max(Limit - io_lib:chars_length(Around), 0)}
        end,
    Text = format_msg(Msg, Meta, Limited),
    case maps:get(single_line, Config) of
        true ->
            Trimmed = lists:reverse(trim(lists:reverse(trim(Text, false)), true)),
            re:replace(Trimmed, ",?\r?\n\s*", ", ", [{return, list}, global, unicode]);
        false ->
            Text
    end.

%% The template with each conditional {Key, IfExists, Else} replaced by the branch the metadata selects.
linear(Meta, [{Key, IfExists, Else} | Rest]) ->
    Branch =
        case value(Key, Meta) of
            {ok, _} -> linear(Meta, IfExists);
            error -> linear(Meta, Else)
        end,
    Branch ++ linear(Meta, Rest);
linear(Meta, [Item | Rest]) ->
    [Item | linear(Meta, Rest)];
linear(_, []) ->
    [].

%% Leading blanks and newlines dropped, from the front (Reversed false) or from a reversed text (true).
trim([C | Rest], Reversed) when C =:= $\s; C =:= $\r; C =:= $\n ->
    trim(Rest, Reversed);
trim([H | Rest], false) when is_list(H) ->
    case trim(H, false) of
        [] -> trim(Rest, false);
        Trimmed -> [Trimmed | Rest]
    end;
trim([H | Rest], true) when is_list(H) ->
    case trim(lists:reverse(H), true) of
        [] -> trim(Rest, true);
        Trimmed -> [lists:reverse(Trimmed) | Rest]
    end;
trim(Text, _) ->
    Text.

%% The template's keys as text: the level, metadata values (time and mfa specially), and strings as they are.
expand(Level, Meta, [level | Rest], Config) ->
    [to_string(level, Level, Config) | expand(Level, Meta, Rest, Config)];
expand(Level, Meta, [Key | Rest], Config) when is_atom(Key); is_list(Key), is_atom(hd(Key)) ->
    Text =
        case value(Key, Meta) of
            {ok, Value} -> to_string(Key, Value, Config);
            error -> ""
        end,
    [Text | expand(Level, Meta, Rest, Config)];
expand(Level, Meta, [Text | Rest], Config) ->
    [Text | expand(Level, Meta, Rest, Config)];
expand(_, _, [], _) ->
    [].

%% A metadata value by key, or by a path of keys into nested maps.
value(Key, Meta) when is_map_key(Key, Meta) -> {ok, maps:get(Key, Meta)};
value([Key | Keys], Meta) when is_map_key(Key, Meta) -> value(Keys, maps:get(Key, Meta));
value([], Value) -> {ok, Value};
value(_, _) -> error.

to_string(time, Time, Config) -> format_time(Time, Config);
to_string(mfa, MFA, Config) -> format_mfa(MFA, Config);
to_string(_, Value, Config) -> to_string(Value, Config).

to_string(X, _) when is_atom(X) -> atom_to_list(X);
to_string(X, _) when is_integer(X) -> integer_to_list(X);
to_string(X, _) when is_pid(X) -> pid_to_list(X);
to_string(X, _) when is_reference(X) -> ref_to_list(X);
to_string(X, Config) when is_list(X) ->
    case printable(lists:flatten(X)) of
        true -> X;
        false -> io_lib:format(p(Config), [X])
    end;
to_string(X, Config) ->
    io_lib:format(p(Config), [X]).

printable([]) -> false;
printable(X) -> io_lib:printable_list(X).

%% The message of an event as text: a string, a format and its arguments, or a report through the report callback
%% of the configuration, the metadata, or logger:format_report/1.
format_msg({string, Text}, Meta, Config) ->
    format_msg({"~ts", [Text]}, Meta, Config);
format_msg({report, _} = Msg, Meta, #{report_cb := Callback} = Config) when
    is_function(Callback, 1); is_function(Callback, 2)
->
    format_msg(Msg, Meta#{report_cb => Callback}, maps:remove(report_cb, Config));
format_msg({report, Report}, #{report_cb := Callback} = Meta, Config) when is_function(Callback, 1) ->
    try Callback(Report) of
        {Format, Args} when is_list(Format), is_list(Args) ->
            format_msg({Format, Args}, maps:remove(report_cb, Meta), Config);
        Other ->
            P = p(Config),
            format_msg({"REPORT_CB/1 ERROR: " ++ P ++ "; Returned: " ++ P, [Report, Other]}, Meta, Config)
    catch
        C:R:S ->
            P = p(Config),
            format_msg(
                {"REPORT_CB/1 CRASH: " ++ P ++ "; Reason: " ++ P, [Report, {C, R, own_stack(S)}]},
                Meta,
                Config
            )
    end;
format_msg({report, Report}, #{report_cb := Callback} = Meta, Config) when is_function(Callback, 2) ->
    try Callback(Report, maps:with([depth, chars_limit, single_line], Config)) of
        Text when is_list(Text); is_binary(Text) ->
            case unicode:characters_to_list(Text, unicode) of
                List when is_list(List) ->
                    List;
                _ ->
                    P = p(Config),
                    format_msg({"REPORT_CB/2 ERROR: " ++ P ++ "; Returned: " ++ P, [Report, Text]}, Meta, Config)
            end;
        Other ->
            P = p(Config),
            format_msg({"REPORT_CB/2 ERROR: " ++ P ++ "; Returned: " ++ P, [Report, Other]}, Meta, Config)
    catch
        C:R:S ->
            P = p(Config),
            format_msg(
                {"REPORT_CB/2 CRASH: " ++ P ++ "; Reason: " ++ P, [Report, {C, R, own_stack(S)}]},
                Meta,
                Config
            )
    end;
format_msg({report, Report}, Meta, Config) ->
    format_msg({report, Report}, Meta#{report_cb => fun logger:format_report/1}, Config);
format_msg(Msg, _, #{depth := Depth, chars_limit := Limit, single_line := Single}) ->
    Options =
        case Limit of
            unlimited -> [];
            _ -> [{chars_limit, Limit}]
        end,
    build(Msg, Depth, Options, Single).

%% A format with its arguments; ~p and ~w get the depth (as ~P, ~W) and, on a single line, no width. A bad format
%% shows the format and arguments instead.
build({Format, Args}, Depth, Options, Single) ->
    try
        Scanned = io_lib:scan_format(Format, Args),
        io_lib:build_text(reformat(Scanned, Depth, Single), Options)
    catch
        C:R:S ->
            P = p(Single),
            Error = "FORMAT ERROR: " ++ P ++ " - " ++ P,
            case Format of
                Error -> erlang:raise(C, R, S);
                _ -> build({Error, [Format, Args]}, Depth, Options, Single)
            end
    end.

reformat(Format, unlimited, false) ->
    Format;
reformat([#{control_char := $p} = M | Rest], Depth, true) ->
    [limit_depth(M#{width => 0}, Depth) | reformat(Rest, Depth, true)];
reformat([#{control_char := $P} = M | Rest], Depth, true) ->
    [M#{width => 0} | reformat(Rest, Depth, true)];
reformat([#{control_char := C} = M | Rest], Depth, Single) when C =:= $p; C =:= $w ->
    [limit_depth(M, Depth) | reformat(Rest, Depth, Single)];
reformat([Item | Rest], Depth, Single) ->
    [Item | reformat(Rest, Depth, Single)];
reformat([], _, _) ->
    [].

limit_depth(M, unlimited) ->
    M;
limit_depth(#{control_char := C, args := Args} = M, Depth) ->
    M#{control_char := C - ($a - $A), args := Args ++ [Depth]}.

%% The stack of a failed callback up to this module.
own_stack(Stack) -> logger:filter_stacktrace(?MODULE, Stack).

%% Cuts the text to max_size characters, ending in "..." (and the newline it ended with).
truncate(B, Msg, A, unlimited) ->
    [B, Msg, A];
truncate(B, Msg, A, Size) ->
    Text = [B, Msg, A],
    case io_lib:chars_length(Text) > Size of
        false ->
            Text;
        true ->
            {Last, Flat} =
                case {A, Msg} of
                    {[], []} -> {last_char(B), lists:flatten(B)};
                    {[], _} -> {last_char(Msg), lists:flatten([B, Msg])};
                    _ -> {last_char(A), lists:flatten(Text)}
                end,
            case Last of
                $\n -> lists:sublist(Flat, 1, Size - 4) ++ "...\n";
                _ -> lists:sublist(Flat, 1, Size - 3) ++ "..."
            end
    end.

last_char(List) -> first_char(lists:reverse(List)).

first_char([]) ->
    error;
first_char([C | _]) when is_integer(C) -> C;
first_char([L | Rest]) when is_list(L) ->
    case last_char(L) of
        error -> first_char(Rest);
        C -> C
    end.

format_time(Time, #{time_offset := Offset, time_designator := Designator}) when is_integer(Time) ->
    calendar:system_time_to_rfc3339(Time, [{unit, microsecond}, {offset, Offset}, {time_designator, Designator}]).

format_mfa({M, F, A}, _) when is_atom(M), is_atom(F), is_integer(A) -> io_lib:fwrite("~tw:~tw/~w", [M, F, A]);
format_mfa({M, F, A}, Config) when is_atom(M), is_atom(F), is_list(A) -> format_mfa({M, F, length(A)}, Config);
format_mfa(MFA, Config) -> to_string(MFA, Config).

%% The legacy header (=ERROR REPORT==== 24-Oct-2026::18:00:09.628804 ===) under [logger_formatter, header].
legacy_header(Level, #{time := Time} = Meta, #{legacy_header := true} = Config) ->
    #{title := Title} = Mine = legacy_title(Level, Meta, Config),
    {{Y, Mo, D}, {H, Mi, S}, Micro, Utc} = local_time(Time, Config),
    Header = io_lib:format("=~ts==== ~w-~s-~4w::~2..0w:~2..0w:~2..0w.~6..0w ~s===", [
        Title, D, month(Mo), Y, H, Mi, S, Micro, Utc
    ]),
    Meta#{?MODULE => Mine#{header => Header}};
legacy_header(_, Meta, _) ->
    Meta.

legacy_title(Level, Meta, Config) ->
    case maps:get(?MODULE, Meta, #{}) of
        #{title := _} = Mine ->
            Mine;
        Mine ->
            TitleLevel =
                case Level =:= notice andalso is_map_key(error_logger, Meta) of
                    true -> maps:get(error_logger_notice_header, Config);
                    false -> Level
                end,
            Mine#{title => string:uppercase(atom_to_list(TitleLevel)) ++ " REPORT"}
    end.

local_time(Time, Config) ->
    Universal = erlang:posixtime_to_universaltime(Time div 1000000),
    {{Date, Clock}, Utc} =
        case utc(maps:get(time_offset, Config)) of
            true -> {Universal, "UTC "};
            false -> {erlang:universaltime_to_localtime(Universal), ""}
        end,
    {Date, Clock, Time rem 1000000, Utc}.

month(M) -> element(M, {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}).

utc(Offset) when Offset =:= 0; Offset =:= "z"; Offset =:= "Z" ->
    true;
utc([$+ | Zone]) ->
    case io_lib:fread("~d:~d", Zone) of
        {ok, [0, 0], []} -> true;
        _ -> false
    end;
utc(_) ->
    false.

with_defaults(Config) ->
    Defaults = #{
        chars_limit => unlimited,
        error_logger_notice_header => info,
        legacy_header => false,
        single_line => true,
        time_designator => $T
    },
    Merged = maps:merge(Defaults, Config#{
        max_size => max_size(maps:get(max_size, Config, undefined)),
        depth => depth(maps:get(depth, Config, undefined)),
        time_offset => offset(maps:get(time_offset, Config, undefined))
    }),
    case Merged of
        #{template := _} -> Merged;
        #{legacy_header := true} -> Merged#{template => ?HEADER_TEMPLATE};
        #{single_line := true} -> Merged#{template => ?SINGLE_TEMPLATE};
        _ -> Merged#{template => ?MULTI_TEMPLATE}
    end.

max_size(undefined) -> unlimited;
max_size(Size) -> max(10, Size).

depth(undefined) -> error_logger:get_format_depth();
depth(Depth) -> max(5, Depth).

offset(undefined) ->
    case utc_log() of
        true -> "Z";
        false -> ""
    end;
offset(Offset) ->
    Offset.

%% The utc_log setting of sasl, else of stdlib.
utc_log() ->
    case clause_logger:app_env(sasl, utc_log) of
        {ok, Value} when is_boolean(Value) ->
            Value;
        _ ->
            case clause_logger:app_env(stdlib, utc_log) of
                {ok, Value} when is_boolean(Value) -> Value;
                _ -> false
            end
    end.

check_config(Config) when is_map(Config) -> check(maps:to_list(Config));
check_config(Config) -> {error, {invalid_formatter_config, ?MODULE, Config}}.

check([{Key, Limit} | Rest]) when Key =:= chars_limit; Key =:= depth; Key =:= max_size ->
    case is_integer(Limit) andalso Limit > 0 orelse Limit =:= unlimited of
        true -> check(Rest);
        false -> {error, {invalid_formatter_config, ?MODULE, {Key, Limit}}}
    end;
check([{single_line, Value} | Rest]) when is_boolean(Value) ->
    check(Rest);
check([{legacy_header, Value} | Rest]) when is_boolean(Value) ->
    check(Rest);
check([{error_logger_notice_header, Value} | Rest]) when Value =:= info; Value =:= notice ->
    check(Rest);
check([{report_cb, Callback} | Rest]) when is_function(Callback, 1); is_function(Callback, 2) ->
    check(Rest);
check([{template, Template} | Rest]) ->
    case valid_template(Template) of
        true -> check(Rest);
        false -> {error, {invalid_formatter_template, ?MODULE, Template}}
    end;
check([{time_offset, Offset} | Rest]) ->
    case valid_offset(Offset) of
        true -> check(Rest);
        false -> {error, {invalid_formatter_config, ?MODULE, {time_offset, Offset}}}
    end;
check([{time_designator, Char} | Rest]) when is_integer(Char), Char >= 0, Char =< 255 ->
    case io_lib:printable_latin1_list([Char]) of
        true -> check(Rest);
        false -> {error, {invalid_formatter_config, ?MODULE, {time_designator, Char}}}
    end;
check([Other | _]) ->
    {error, {invalid_formatter_config, ?MODULE, Other}};
check([]) ->
    ok.

valid_template([Key | Rest]) when is_atom(Key) ->
    valid_template(Rest);
valid_template([Key | Rest]) when is_list(Key), is_atom(hd(Key)) ->
    lists:all(fun erlang:is_atom/1, Key) andalso valid_template(Rest);
valid_template([{Key, IfExists, Else} | Rest]) when is_atom(Key); is_list(Key), is_atom(hd(Key)) ->
    valid_template(IfExists) andalso valid_template(Else) andalso valid_template(Rest);
valid_template([Text | Rest]) when is_list(Text) ->
    io_lib:printable_unicode_list(Text) andalso valid_template(Rest);
valid_template([Binary | Rest]) when is_binary(Binary) ->
    case unicode:characters_to_list(Binary) of
        Text when is_list(Text) -> valid_template([Text | Rest]);
        _ -> false
    end;
valid_template([]) ->
    true;
valid_template(_) ->
    false.

valid_offset(Offset) when is_integer(Offset) ->
    true;
valid_offset(Offset) when Offset =:= ""; Offset =:= "Z"; Offset =:= "z" ->
    true;
valid_offset([Sign | Zone]) when Sign =:= $+; Sign =:= $- ->
    try io_lib:fread("~d:~d", Zone) of
        {ok, [_, _], []} -> true;
        _ -> false
    catch
        _:_ -> false
    end;
valid_offset(_) ->
    false.

p(#{single_line := Single}) -> p(Single);
p(true) -> "~0tp";
p(false) -> "~tp".
