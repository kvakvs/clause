-module(io_ansi).
-export([
    alternate_character_set_mode/0,
    alternate_character_set_mode_off/0,
    alternate_screen/0,
    alternate_screen_off/0,
    background/1,
    background/3,
    black/0,
    black_background/0,
    black_underline/0,
    blink/0,
    blink_off/0,
    blue/0,
    blue_background/0,
    blue_underline/0,
    bold/0,
    bold_off/0,
    clear/0,
    color/1,
    color/3,
    color_name_to_index/1,
    curly_underline/0,
    cursor/2,
    cursor_backward/0,
    cursor_backward/1,
    cursor_down/0,
    cursor_down/1,
    cursor_forward/0,
    cursor_forward/1,
    cursor_get_position/0,
    cursor_hide/0,
    cursor_home/0,
    cursor_horizontal_absolute/1,
    cursor_horizontal_vertical/2,
    cursor_move/2,
    cursor_next_line/0,
    cursor_previous_line/0,
    cursor_report_position/0,
    cursor_restore/0,
    cursor_save/0,
    cursor_show/0,
    cursor_up/0,
    cursor_up/1,
    cursor_vertical_absolute/1,
    cyan/0,
    cyan_background/0,
    cyan_underline/0,
    dashed_underline/0,
    default_background/0,
    default_color/0,
    default_underline_color/0,
    delete_character/0,
    delete_character/1,
    delete_line/0,
    delete_line/1,
    device_report_attributes/0,
    dim/0,
    dim_off/0,
    dotted_underline/0,
    double_underline/0,
    enabled/0,
    enabled/1,
    erase_character/1,
    erase_display/0,
    erase_line/0,
    format/1,
    format/2,
    format/3,
    fwrite/1,
    fwrite/2,
    fwrite/3,
    fwrite/4,
    green/0,
    green_background/0,
    green_underline/0,
    hyperlink_reset/0,
    hyperlink_start/1,
    hyperlink_start/2,
    insert_character/1,
    insert_line/0,
    insert_line/1,
    inverse/0,
    inverse_off/0,
    invisible/0,
    invisible_off/0,
    italic/0,
    italic_off/0,
    keypad_transmit_mode/0,
    keypad_transmit_mode_off/0,
    light_black/0,
    light_black_background/0,
    light_black_underline/0,
    light_blue/0,
    light_blue_background/0,
    light_blue_underline/0,
    light_cyan/0,
    light_cyan_background/0,
    light_cyan_underline/0,
    light_green/0,
    light_green_background/0,
    light_green_underline/0,
    light_magenta/0,
    light_magenta_background/0,
    light_magenta_underline/0,
    light_red/0,
    light_red_background/0,
    light_red_underline/0,
    light_white/0,
    light_white_background/0,
    light_white_underline/0,
    light_yellow/0,
    light_yellow_background/0,
    light_yellow_underline/0,
    magenta/0,
    magenta_background/0,
    magenta_underline/0,
    modify_color/4,
    overline/0,
    overline_off/0,
    red/0,
    red_background/0,
    red_underline/0,
    render/1,
    render/2,
    reset/0,
    reverse_index/0,
    scan/1,
    scroll_backward/0,
    scroll_backward/1,
    scroll_change_region/2,
    scroll_forward/0,
    scroll_forward/1,
    strikethrough/0,
    strikethrough_off/0,
    tab/0,
    tab_backward/0,
    tab_clear/0,
    tab_clear_all/0,
    tab_set/0,
    tigetflag/1,
    tigetnum/1,
    tinfo/0,
    tput/1,
    tput/2,
    underline/0,
    underline_color/1,
    underline_color/3,
    underline_off/0,
    white/0,
    white_background/0,
    white_underline/0,
    yellow/0,
    yellow_background/0,
    yellow_underline/0
]).
-include("io_ansi.types.hrl").

% Clause's io_ansi (plan 21 step LIB63, docs/library.md): OTP 29's virtual terminal sequences. A Clause program reads
% no terminfo database, so every sequence is the built-in ANSI one OTP uses where terminfo is unavailable (as on
% Windows); the terminfo queries answer {error, enotsup}. The sequences are literals, the rest Erlang over io_lib.

%% The built-in sequence of a VTS key with its arguments; throws {invalid_code, Key, Args} for an unknown pair, as
%% OTP's lookup does.
sequence(Key, Args) ->
    case sequence_of(Key, Args) of
        undefined -> throw({invalid_code, Key, Args});
        Sequence -> Sequence
    end.

sequence_of(Key, []) -> fixed(Key);
sequence_of(Key, [A]) -> one(Key, A);
sequence_of(Key, [A, B]) -> two(Key, A, B);
sequence_of(Key, [A, B, C]) -> three(Key, A, B, C);
sequence_of(modify_color, [Index, R, G, B]) -> modify(Index, R, G, B);
sequence_of(_, _) -> undefined.

% The sequences without arguments: binaries, as OTP keeps its sequence strings (hyperlink_reset is built each time).
fixed(hyperlink_reset) ->
    hyperlink("", []);
fixed(Key) ->
    case fixed_text(Key) of
        undefined -> binary(fixed_style(Key));
        Text -> binary(Text)
    end.

binary(undefined) -> undefined;
binary(Text) -> unicode:characters_to_binary(Text).

fixed_text(cursor_home) -> "\eH";
fixed_text(cursor_up) -> "\e[A";
fixed_text(cursor_down) -> "\n";
fixed_text(cursor_backward) -> "\b";
fixed_text(cursor_forward) -> "\e[C";
fixed_text(reverse_index) -> "\eM";
fixed_text(cursor_save) -> "\e7";
fixed_text(cursor_restore) -> "\e8";
fixed_text(cursor_show) -> "\e[?25h";
fixed_text(cursor_hide) -> "\e[?25l";
fixed_text(cursor_next_line) -> "\e[E";
fixed_text(cursor_previous_line) -> "\e[F";
fixed_text(alternate_screen) -> "\e[?1049h";
fixed_text(alternate_screen_off) -> "\e[?1049l";
fixed_text(scroll_forward) -> "\eS";
fixed_text(scroll_backward) -> "\eT";
fixed_text(delete_character) -> "\e[P";
fixed_text(insert_line) -> "\e[L";
fixed_text(delete_line) -> "\e[M";
fixed_text(erase_display) -> "\e[J";
fixed_text(erase_line) -> "\e[K";
fixed_text(clear) -> "\e[H\e[2J";
fixed_text(keypad_transmit_mode) -> "\e=";
fixed_text(keypad_transmit_mode_off) -> "\e>";
fixed_text(kcursor_home) -> "\eH";
fixed_text(kcursor_end) -> "\eF";
fixed_text(kcursor_up) -> "\e[A";
fixed_text(kcursor_down) -> "\e[B";
fixed_text(kcursor_backward) -> "\e[D";
fixed_text(kcursor_forward) -> "\e[C";
fixed_text(cursor_report_position) -> "\e[6n";
fixed_text(device_report_attributes) -> "\e[0c";
fixed_text(tab_set) -> "\eH";
fixed_text(tab) -> "\e[I";
fixed_text(tab_backward) -> "\eI";
fixed_text(tab_clear) -> "\e[0g";
fixed_text(tab_clear_all) -> "\e[3g";
fixed_text(alternate_character_set_mode) -> "\e(0";
fixed_text(alternate_character_set_mode_off) -> "\e(B";
fixed_text(_) -> undefined.

% The Select Graphic Rendition sequences without arguments.
fixed_style(Key) ->
    case style_code(Key) of
        undefined -> undefined;
        Code -> ["\e[", Code, "m"]
    end.

style_code(reset) -> "0";
style_code(default_color) -> "39";
style_code(default_background) -> "49";
style_code(default_underline_color) -> "59";
style_code(bold) -> "1";
style_code(bold_off) -> "22";
style_code(dim) -> "2";
style_code(dim_off) -> "22";
style_code(italic) -> "3";
style_code(italic_off) -> "23";
style_code(blink) -> "5";
style_code(blink_off) -> "25";
style_code(inverse) -> "7";
style_code(inverse_off) -> "27";
style_code(invisible) -> "8";
style_code(invisible_off) -> "28";
style_code(strikethrough) -> "9";
style_code(strikethrough_off) -> "29";
style_code(overline) -> "53";
style_code(overline_off) -> "55";
style_code(underline) -> "4";
style_code(underline_off) -> "24";
style_code(double_underline) -> "4:2";
style_code(curly_underline) -> "4:3";
style_code(dotted_underline) -> "4:4";
style_code(dashed_underline) -> "4:5";
style_code(Key) -> color_code(Key).

% The named colors: foreground 30-37 and 90-97, background 40-47 and 100-107, underline 58;5;Index.
color_code(Key) ->
    case color_parts(atom_to_list(Key)) of
        {Index, ""} -> integer_to_list(color_base(Index, 30, 90) + Index rem 8);
        {Index, "_background"} -> integer_to_list(color_base(Index, 40, 100) + Index rem 8);
        {Index, "_underline"} -> "58;5;" ++ integer_to_list(Index);
        _ -> undefined
    end.

color_base(Index, Normal, _) when Index < 8 -> Normal;
color_base(_, _, Light) -> Light.

% The color index and suffix of a named color key, or none.
color_parts(Name) ->
    Colors = ["black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"],
    {Offset, Rest} =
        case Name of
            "light_" ++ Light -> {8, Light};
            _ -> {0, Name}
        end,
    color_parts(Rest, Colors, Offset).

color_parts(Name, [Color | Colors], Index) ->
    case string:prefix(Name, Color) of
        nomatch -> color_parts(Name, Colors, Index + 1);
        Suffix -> {Index, Suffix}
    end;
color_parts(_, [], _) ->
    none.

% The sequences with one argument; most take a count, as lists:concat/1 writes it.
one(cursor_up, N) -> concat(["\e[", N, "A"]);
one(cursor_down, N) -> concat(["\e[", N, "B"]);
one(cursor_backward, N) -> concat(["\e[", N, "D"]);
one(cursor_forward, N) -> concat(["\e[", N, "C"]);
one(cursor_horizontal_absolute, X) -> concat(["\e[", X, "G"]);
one(cursor_vertical_absolute, Y) -> ["\e[", Y, "d"];
one(scroll_forward, N) -> concat(["\e", N, "S"]);
one(scroll_backward, N) -> concat(["\e", N, "T"]);
one(insert_character, N) -> concat(["\e[", N, "@"]);
one(delete_character, N) -> concat(["\e[", N, "P"]);
one(erase_character, N) -> concat(["\e[", N, "X"]);
one(insert_line, N) -> concat(["\e[", N, "L"]);
one(delete_line, N) -> concat(["\e[", N, "M"]);
one(color, Index) -> sgr(["38", "5", s(Index)]);
one(background, Index) -> sgr(["48", "5", s(Index)]);
one(underline_color, Index) -> sgr(["58", "5", s(Index)]);
one(hyperlink_start, Url) -> hyperlink(Url, []);
one(_, _) -> undefined.

two(cursor, Line, Column) -> concat(["\e[", Line + 1, ";", Column + 1, "H"]);
two(cursor_horizontal_vertical, X, Y) -> concat(["\e[", X, ";", Y, "f"]);
two(scroll_change_region, Line1, Line2) -> concat(["\e[", Line1, ";", Line2, "r"]);
two(hyperlink_start, Url, Params) -> hyperlink(Url, Params);
two(_, _, _) -> undefined.

three(color, R, G, B) -> sgr(["38", "2", s(R), s(G), s(B)]);
three(background, R, G, B) -> sgr(["48", "2", s(R), s(G), s(B)]);
three(underline_color, R, G, B) -> sgr(["58", "2", s(R), s(G), s(B)]);
three(_, _, _, _) -> undefined.

modify(Index, R, G, B) ->
    io_lib:format("\e]4;~.16b;rgb:~.16b/~.16b/~.16b\e\\", [Index, R, G, B]).

sgr(Args) -> ["\e[", lists:join($;, Args), "m"].

s(Int) when is_integer(Int) -> integer_to_list(Int).

concat(Things) -> lists:concat(Things).

hyperlink(Url, Params) ->
    StringParams = lists:join($:, [[K, "=", V] || {K, V} <- Params]),
    io_lib:format("\e]8;~s;~s\e\\", [Url, StringParams]).

% A sequence as a binary.
emit(Key, Args) -> unicode:characters_to_binary(sequence(Key, Args)).

color_name_to_index(black) -> 0;
color_name_to_index(red) -> 1;
color_name_to_index(green) -> 2;
color_name_to_index(yellow) -> 3;
color_name_to_index(blue) -> 4;
color_name_to_index(magenta) -> 5;
color_name_to_index(cyan) -> 6;
color_name_to_index(white) -> 7;
color_name_to_index(light_black) -> 8;
color_name_to_index(light_red) -> 9;
color_name_to_index(light_green) -> 10;
color_name_to_index(light_yellow) -> 11;
color_name_to_index(light_blue) -> 12;
color_name_to_index(light_magenta) -> 13;
color_name_to_index(light_cyan) -> 14;
color_name_to_index(light_white) -> 15.

%% A named color's sequence, or the 256-color index's.
color(Color) when is_atom(Color) -> emit(Color, []);
color(Index) -> emit(color, [Index]).

color(R, G, B) -> emit(color, [R, G, B]).

background(Color) when is_atom(Color) -> emit(background, [color_name_to_index(Color)]);
background(Index) -> emit(background, [Index]).

background(R, G, B) -> emit(background, [R, G, B]).

underline_color(Color) when is_atom(Color) -> emit(underline_color, [color_name_to_index(Color)]);
underline_color(Index) -> emit(underline_color, [Index]).

underline_color(R, G, B) -> emit(underline_color, [R, G, B]).

modify_color(Index, R, G, B) -> emit(modify_color, [Index, R, G, B]).

cursor(Line, Column) -> emit(cursor, [Line, Column]).

cursor_move(DL, DC) ->
    unicode:characters_to_binary([cursor_rel_line(DL), cursor_rel_col(DC)]).

cursor_rel_line(DL) when DL < 0 -> cursor_up(-DL);
cursor_rel_line(DL) when DL > 0 -> cursor_down(DL);
cursor_rel_line(0) -> [].

cursor_rel_col(DC) when DC < 0 -> cursor_backward(-DC);
cursor_rel_col(DC) when DC > 0 -> cursor_forward(DC);
cursor_rel_col(0) -> [].

%% Ask the terminal for the cursor position and read its {Row, Column} reply from standard input.
cursor_get_position() ->
    io_ansi:fwrite([cursor_report_position]),
    parse_cursor_position(io:get_chars("", 20)).

parse_cursor_position("\e[" ++ Rest) ->
    case string:split(Rest, ";") of
        [Row, ColumnR] ->
            Column = string:substr(ColumnR, 1, string:len(ColumnR) - 1),
            {list_to_integer(Row), list_to_integer(Column)};
        _ ->
            {error, "Not a cursor position response"}
    end;
parse_cursor_position(_) ->
    {error, "Not a cursor position response"}.

%% No terminfo database: OTP's answers where it has none.
tput(Capability) -> tput(Capability, []).

tput(Capability, _Args) -> erlang:error({enotsup, Capability}).

tigetnum(_Capability) -> {error, enotsup}.

tigetflag(_Capability) -> {error, enotsup}.

tinfo() -> {error, enotsup}.

%% Whether the device is a terminal that interprets the sequences: io:getopts/1's terminal flag, and on Linux and
%% macOS a TERM naming a terminal type (terminfo's sgr0 decides in OTP).
enabled() -> enabled(user).

enabled(Device) ->
    IsTerminal =
        case io:getopts(Device) of
            {error, _} -> false;
            Options -> proplists:get_value(terminal, Options, false)
        end,
    IsTerminal andalso smart_terminal(os:type()).

smart_terminal({win32, _}) ->
    true;
smart_terminal(_) ->
    case os:getenv("TERM") of
        false -> false;
        "" -> false;
        "dumb" -> false;
        _ -> true
    end.

%% The sequences and text of Data, as io_ansi:scan/1 recognizes them: known sequences as their VTS, other control
%% sequences as {csi, Bytes}.
scan(Data) ->
    scan_binary(unicode:characters_to_binary(Data), <<>>, []).

scan_binary(<<CSI/utf8, R/binary>> = Data, Text, Acc) when CSI =:= $\e; CSI =:= 155 ->
    case longest_vts(Data) of
        undefined ->
            case re:run(Data, ansi_regexp(), [unicode]) of
                {match, [{0, N}]} ->
                    <<Ansi:N/binary, Rest/binary>> = Data,
                    scan_binary(Rest, <<>>, [{csi, Ansi}, Text | Acc]);
                nomatch ->
                    scan_binary(R, <<Text/binary, CSI/utf8>>, Acc)
            end;
        {Key, Rest} ->
            scan_binary(Rest, <<>>, [Key, Text | Acc])
    end;
scan_binary(<<C/utf8, R/binary>>, Text, Acc) ->
    scan_binary(R, <<Text/binary, C/utf8>>, Acc);
scan_binary(<<>>, Text, Acc) ->
    [Part || Part <- lists:reverse([Text | Acc]), Part =/= <<>>].

% The VTS whose sequence starts Data, the longest one when several do; ESC and the 8-bit CSI (155) are alike.
longest_vts(<<_, Data/binary>>) ->
    Matches = [
        {byte_size(Value), Key, Rest}
     || {Key, <<_, Value/binary>>} <- vts_sequences(),
        <<Prefix:(byte_size(Value))/binary, Rest/binary>> <- [Data],
        Prefix =:= Value
    ],
    case lists:keysort(1, Matches) of
        [] -> undefined;
        Sorted -> {element(2, lists:last(Sorted)), element(3, lists:last(Sorted))}
    end.

% Every VTS without arguments whose sequence starts with ESC, with its sequence.
vts_sequences() ->
    case persistent_term:get({?MODULE, sequences}, undefined) of
        undefined ->
            Sequences = [
                {Key, Bytes}
             || Key <- vts_keys(),
                <<$\e, _/binary>> = Bytes <- [unicode:characters_to_binary(fixed(Key))]
            ],
            persistent_term:put({?MODULE, sequences}, Sequences),
            Sequences;
        Sequences ->
            Sequences
    end.

vts_keys() ->
    Colors = [black, red, green, yellow, blue, magenta, cyan, white],
    Named = [
        list_to_atom(Light ++ atom_to_list(Color) ++ Suffix)
     || Light <- ["", "light_"], Color <- Colors, Suffix <- ["", "_background", "_underline"]
    ],
    [
        cursor_home,
        cursor_up,
        cursor_down,
        cursor_backward,
        cursor_forward,
        reverse_index,
        cursor_save,
        cursor_restore,
        cursor_show,
        cursor_hide,
        cursor_next_line,
        cursor_previous_line,
        alternate_screen,
        alternate_screen_off,
        scroll_forward,
        scroll_backward,
        delete_character,
        insert_line,
        delete_line,
        erase_display,
        erase_line,
        clear,
        keypad_transmit_mode,
        keypad_transmit_mode_off,
        kcursor_home,
        kcursor_end,
        kcursor_up,
        kcursor_down,
        kcursor_backward,
        kcursor_forward,
        cursor_report_position,
        device_report_attributes,
        tab_set,
        tab,
        tab_backward,
        tab_clear,
        tab_clear_all,
        reset,
        default_color,
        default_background,
        default_underline_color,
        bold,
        bold_off,
        dim,
        dim_off,
        italic,
        italic_off,
        blink,
        blink_off,
        inverse,
        inverse_off,
        invisible,
        invisible_off,
        strikethrough,
        strikethrough_off,
        overline,
        overline_off,
        underline,
        underline_off,
        double_underline,
        curly_underline,
        dotted_underline,
        dashed_underline,
        alternate_character_set_mode,
        alternate_character_set_mode_off,
        hyperlink_reset
        | Named
    ].

% OTP's pattern of a control sequence (prim_tty's).
ansi_regexp() ->
    <<"^[\e", 194, 155,
        "][[\\]()#;?]*(?:(?:(?:(?:;[-a-zA-Z\\d\\/#&.:=?%@~_]+)*|[a-zA-Z\\d]+(?:;[-a-zA-Z\\d\\/#&.:=?%@~_]*)*)?", 7,
        ")|(?:(?:\\d{1,4}(?:;\\d{0,4})*)?[\\dA-PR-TZcf-nq-uy=><~]))">>.

%% Data with its VTSs rendered (when enabled) and the reset appended, as format/3 renders them.
render(Data) -> render(Data, []).

render(Data, Options) ->
    render_internal(Data, [], Options, false, fun
        (Fmt, []) when is_list(Fmt); is_binary(Fmt); is_integer(Fmt) -> {Fmt, []};
        (_, _) -> erlang:error(badarg, [Data, Options])
    end).

render_internal(Format, InitAcc, Options, FormatOnly, Callback) ->
    UseAnsi =
        case proplists:get_value(enabled, Options) of
            undefined -> enabled();
            Enabled -> Enabled
        end,
    AppendReset = proplists:get_value(reset, Options, true),
    NoColor = os:getenv("NO_COLOR"),
    Color = proplists:get_value(color, Options, NoColor =:= false orelse NoColor =:= ""),
    Step = fun(Item, Acc) -> render_item(Item, Acc, {UseAnsi, Color, FormatOnly}, Callback) end,
    try lists:foldl(Step, {[], InitAcc}, Format) of
        {Rendered, []} when UseAnsi, AppendReset -> lists:reverse([reset() | Rendered]);
        {Rendered, []} -> lists:reverse(Rendered);
        _ -> erlang:error(badarg, [Format, InitAcc, Options])
    catch
        throw:{invalid_code, Code, []} -> erlang:error({invalid_code, Code});
        throw:{invalid_code, Code, Args} -> erlang:error({invalid_code, {Code, Args}})
    end.

render_item(Ansi, {Acc, Args}, {_, _, true}, _) when is_atom(Ansi); is_tuple(Ansi) ->
    {[Ansi | Acc], Args};
render_item(Ansi, {Acc, Args}, {UseAnsi, Color, false}, _) when is_atom(Ansi); is_tuple(Ansi) ->
    {Key, AnsiArgs} =
        case Ansi of
            Atom when is_atom(Atom) -> {Atom, []};
            Tuple -> {element(1, Tuple), tl(tuple_to_list(Tuple))}
        end,
    case UseAnsi andalso (Color orelse not is_color(Key)) of
        true -> {[sequence(Key, AnsiArgs) | Acc], Args};
        false -> {Acc, Args}
    end;
render_item(Fmt, {Acc, Args}, _, Callback) ->
    {Entry, Rest} = Callback(Fmt, Args),
    {[Entry | Acc], Rest}.

% The keys format/3 drops with {color, false}: OTP's list (named colors but black, and the color tuples' keys).
is_color(Key) ->
    Colors = ["blue", "red", "green", "yellow", "magenta", "cyan", "white"],
    Names = [
        Prefix ++ Color ++ Suffix
     || Suffix <- ["", "_background", "_underline"], Prefix <- ["", "light_"], Color <- Colors
    ],
    (is_atom(Key) andalso lists:member(atom_to_list(Key), Names)) orelse
        lists:member(Key, [
            color,
            background_color,
            underline_color,
            default_color,
            default_background,
            default_underline_color
        ]).

%% Format like io_lib:bformat/2 with VTSs between the format strings.
format(Format) -> format(Format, []).

format(Format, Data) -> format(Format, Data, []).

format(Format, Data, Options) ->
    unicode:characters_to_binary(format_internal(group(Format), Data, Options, false)).

format_internal(Format, Data, Options, FormatOnly) ->
    render_internal(group(Format), Data, Options, FormatOnly, fun
        (Fmt, Args) when is_list(Fmt) -> format_part(Fmt, Args);
        (_, _) -> erlang:error(badarg, [Format, Data, Options])
    end).

% One format string with as many of Args as it takes, and the rest of Args.
format_part(Fmt, Args) ->
    Count = format_arguments(Fmt),
    case length(Args) >= Count of
        true ->
            {Taken, Rest} = lists:split(Count, Args),
            {unicode:characters_to_binary(io_lib:format(Fmt, Taken)), Rest};
        false ->
            erlang:error(function_clause, [Fmt, Args])
    end.

% How many arguments a format string takes: one per control but ~n and ~~, one more per * field and for K.
format_arguments([$~ | Rest]) -> control_arguments(Rest, 0, 0);
format_arguments([_ | Rest]) -> format_arguments(Rest);
format_arguments([]) -> 0.

% A control's field width, precision and pad character (after the second dot) come before its letter.
control_arguments([$* | Rest], Count, Dots) -> control_arguments(Rest, Count + 1, Dots);
control_arguments([$. | Rest], Count, Dots) -> control_arguments(Rest, Count, Dots + 1);
control_arguments([_ | Rest], Count, 2) -> control_arguments(Rest, Count, 3);
control_arguments([$K | Rest], Count, Dots) -> control_arguments(Rest, Count + 1, Dots);
control_arguments([C | Rest], Count, Dots) when C =:= $t; C =:= $l; C =:= $k -> control_arguments(Rest, Count, Dots);
control_arguments([C | Rest], Count, _) when C =:= $n; C =:= $~ -> Count + format_arguments(Rest);
control_arguments([C | Rest], Count, _) when C >= $a, C =< $z; C >= $A, C =< $Z -> Count + 1 + format_arguments(Rest);
control_arguments([_ | Rest], Count, Dots) -> control_arguments(Rest, Count, Dots);
control_arguments([], Count, _) -> Count.

fwrite(Format) -> fwrite(standard_io, Format, [], []).

fwrite(Format, Data) -> fwrite(standard_io, Format, Data, []).

fwrite(Format, Data, Options) -> fwrite(standard_io, Format, Data, Options).

%% Write to Device: text as put_chars requests, each VTS as a put_ansi request the device renders, or drops when it
%% renders none (unless {enabled, true} forces the built-in sequences).
fwrite(Device, Format, Data, Options) ->
    Ref = make_ref(),
    Write = fun
        F(Chars, ok) when is_binary(Chars) ->
            io:request(Device, {put_chars, unicode, Chars});
        F(Ansi, ok) when is_atom(Ansi); is_tuple(Ansi) ->
            case io:request(Device, {put_ansi, Options, Ansi}) of
                {error, request} ->
                    case proplists:get_value(enabled, Options, Ref) of
                        true -> F(format([Ansi], [], Options), ok);
                        _ -> ok
                    end;
                Else ->
                    Else
            end;
        F(_, Error) ->
            throw({Ref, Error})
    end,
    try
        lists:foldl(Write, ok, lists:flatten(format_internal(Format, Data, Options, true)))
    catch
        {Ref, Error} -> erlang:error(Error, [Device, Format, Data, Options])
    end.

% Characters of the format grouped into strings between the VTSs.
group(Fmt) ->
    group(lists:flatten(Fmt), []).

group([Ansi | T], []) when is_atom(Ansi); is_tuple(Ansi) ->
    [Ansi | group(T, [])];
group([Ansi | T], Acc) when is_atom(Ansi); is_tuple(Ansi) ->
    [lists:reverse(Acc), Ansi | group(T, [])];
group([C | T], Acc) ->
    group(T, [C | Acc]);
group([], []) ->
    [];
group([], Acc) ->
    [lists:reverse(Acc)].

black() -> emit(black, []).
red() -> emit(red, []).
green() -> emit(green, []).
yellow() -> emit(yellow, []).
blue() -> emit(blue, []).
magenta() -> emit(magenta, []).
cyan() -> emit(cyan, []).
white() -> emit(white, []).
default_color() -> emit(default_color, []).
black_background() -> emit(black_background, []).
red_background() -> emit(red_background, []).
green_background() -> emit(green_background, []).
yellow_background() -> emit(yellow_background, []).
blue_background() -> emit(blue_background, []).
magenta_background() -> emit(magenta_background, []).
cyan_background() -> emit(cyan_background, []).
white_background() -> emit(white_background, []).
default_background() -> emit(default_background, []).
light_black() -> emit(light_black, []).
light_red() -> emit(light_red, []).
light_green() -> emit(light_green, []).
light_yellow() -> emit(light_yellow, []).
light_blue() -> emit(light_blue, []).
light_magenta() -> emit(light_magenta, []).
light_cyan() -> emit(light_cyan, []).
light_white() -> emit(light_white, []).
light_black_background() -> emit(light_black_background, []).
light_red_background() -> emit(light_red_background, []).
light_green_background() -> emit(light_green_background, []).
light_yellow_background() -> emit(light_yellow_background, []).
light_blue_background() -> emit(light_blue_background, []).
light_magenta_background() -> emit(light_magenta_background, []).
light_cyan_background() -> emit(light_cyan_background, []).
light_white_background() -> emit(light_white_background, []).
black_underline() -> emit(black_underline, []).
red_underline() -> emit(red_underline, []).
green_underline() -> emit(green_underline, []).
yellow_underline() -> emit(yellow_underline, []).
blue_underline() -> emit(blue_underline, []).
magenta_underline() -> emit(magenta_underline, []).
cyan_underline() -> emit(cyan_underline, []).
white_underline() -> emit(white_underline, []).
light_black_underline() -> emit(light_black_underline, []).
light_red_underline() -> emit(light_red_underline, []).
light_green_underline() -> emit(light_green_underline, []).
light_yellow_underline() -> emit(light_yellow_underline, []).
light_blue_underline() -> emit(light_blue_underline, []).
light_magenta_underline() -> emit(light_magenta_underline, []).
light_cyan_underline() -> emit(light_cyan_underline, []).
light_white_underline() -> emit(light_white_underline, []).
default_underline_color() -> emit(default_underline_color, []).
bold() -> emit(bold, []).
bold_off() -> emit(bold_off, []).
underline() -> emit(underline, []).
underline_off() -> emit(underline_off, []).
double_underline() -> emit(double_underline, []).
curly_underline() -> emit(curly_underline, []).
dotted_underline() -> emit(dotted_underline, []).
dashed_underline() -> emit(dashed_underline, []).
overline() -> emit(overline, []).
overline_off() -> emit(overline_off, []).
blink() -> emit(blink, []).
blink_off() -> emit(blink_off, []).
dim() -> emit(dim, []).
dim_off() -> emit(dim_off, []).
invisible() -> emit(invisible, []).
invisible_off() -> emit(invisible_off, []).
italic() -> emit(italic, []).
italic_off() -> emit(italic_off, []).
inverse() -> emit(inverse, []).
inverse_off() -> emit(inverse_off, []).
strikethrough() -> emit(strikethrough, []).
strikethrough_off() -> emit(strikethrough_off, []).
clear() -> emit(clear, []).
erase_display() -> emit(erase_display, []).
insert_character(Chars) -> emit(insert_character, [Chars]).
delete_character() -> emit(delete_character, []).
delete_character(Chars) -> emit(delete_character, [Chars]).
erase_character(Chars) -> emit(erase_character, [Chars]).
insert_line() -> emit(insert_line, []).
insert_line(Lines) -> emit(insert_line, [Lines]).
delete_line() -> emit(delete_line, []).
delete_line(Lines) -> emit(delete_line, [Lines]).
erase_line() -> emit(erase_line, []).
alternate_character_set_mode() -> emit(alternate_character_set_mode, []).
alternate_character_set_mode_off() -> emit(alternate_character_set_mode_off, []).
cursor_up() -> emit(cursor_up, []).
cursor_up(N) -> emit(cursor_up, [N]).
cursor_down() -> emit(cursor_down, []).
cursor_down(N) -> emit(cursor_down, [N]).
cursor_forward() -> emit(cursor_forward, []).
cursor_forward(N) -> emit(cursor_forward, [N]).
cursor_backward() -> emit(cursor_backward, []).
cursor_backward(N) -> emit(cursor_backward, [N]).
cursor_home() -> emit(cursor_home, []).
reverse_index() -> emit(reverse_index, []).
cursor_save() -> emit(cursor_save, []).
cursor_restore() -> emit(cursor_restore, []).
cursor_show() -> emit(cursor_show, []).
cursor_hide() -> emit(cursor_hide, []).
cursor_next_line() -> emit(cursor_next_line, []).
cursor_previous_line() -> emit(cursor_previous_line, []).
cursor_horizontal_absolute(X) -> emit(cursor_horizontal_absolute, [X]).
cursor_vertical_absolute(X) -> emit(cursor_vertical_absolute, [X]).
cursor_horizontal_vertical(X, Y) -> emit(cursor_horizontal_vertical, [X, Y]).
cursor_report_position() -> emit(cursor_report_position, []).
alternate_screen() -> emit(alternate_screen, []).
alternate_screen_off() -> emit(alternate_screen_off, []).
scroll_forward() -> emit(scroll_forward, []).
scroll_forward(N) -> emit(scroll_forward, [N]).
scroll_backward() -> emit(scroll_backward, []).
scroll_backward(Steps) -> emit(scroll_backward, [Steps]).
scroll_change_region(Line1, Line2) -> emit(scroll_change_region, [Line1, Line2]).
tab() -> emit(tab, []).
tab_backward() -> emit(tab_backward, []).
tab_set() -> emit(tab_set, []).
tab_clear() -> emit(tab_clear, []).
tab_clear_all() -> emit(tab_clear_all, []).
keypad_transmit_mode() -> emit(keypad_transmit_mode, []).
keypad_transmit_mode_off() -> emit(keypad_transmit_mode_off, []).
reset() -> emit(reset, []).
device_report_attributes() -> emit(device_report_attributes, []).
hyperlink_start(Url) -> emit(hyperlink_start, [Url]).
hyperlink_start(Url, Params) -> emit(hyperlink_start, [Url, Params]).
hyperlink_reset() -> emit(hyperlink_reset, []).
