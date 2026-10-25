-module(io_ansi_codes).
-moduledoc "io_ansi's sequences, formatting and rendering without terminfo (plan 21 LIB63).".
-export([main/1]).

% Without terminfo OTP's scan/1 and tput/1,2 fail on Windows (try_clause): their run is authored.
main(["scan"]) ->
    show(scan, io_ansi:scan(<<"\e[34mhi\e[0m">>)),
    show(scan_plain, io_ansi:scan("plain")),
    show(scan_csi, io_ansi:scan("\e[1;1Hx")),
    show(scan_format, io_ansi:scan(io_ansi:format([bold, "x"], [], [{enabled, true}]))),
    show(tput, catch_error(fun() -> io_ansi:tput("setaf", [3]) end)),
    show(tput1, catch_error(fun() -> io_ansi:tput("cud1") end)),
    io:format("io_ansi_codes: ok~n");
main(_) ->
    sequences(),
    parameters(),
    formatting(),
    io:format("io_ansi_codes: ok~n").

sequences() ->
    Names = [
        black,
        red,
        green,
        yellow,
        blue,
        magenta,
        cyan,
        white,
        default_color,
        black_background,
        red_background,
        green_background,
        yellow_background,
        blue_background,
        magenta_background,
        cyan_background,
        white_background,
        default_background,
        light_black,
        light_red,
        light_green,
        light_yellow,
        light_blue,
        light_magenta,
        light_cyan,
        light_white,
        light_black_background,
        light_red_background,
        light_green_background,
        light_yellow_background,
        light_blue_background,
        light_magenta_background,
        light_cyan_background,
        light_white_background,
        black_underline,
        red_underline,
        green_underline,
        yellow_underline,
        blue_underline,
        magenta_underline,
        cyan_underline,
        white_underline,
        light_black_underline,
        light_red_underline,
        light_green_underline,
        light_yellow_underline,
        light_blue_underline,
        light_magenta_underline,
        light_cyan_underline,
        light_white_underline,
        default_underline_color,
        bold,
        bold_off,
        underline,
        underline_off,
        double_underline,
        curly_underline,
        dotted_underline,
        dashed_underline,
        overline,
        overline_off,
        blink,
        blink_off,
        dim,
        dim_off,
        invisible,
        invisible_off,
        italic,
        italic_off,
        inverse,
        inverse_off,
        strikethrough,
        strikethrough_off,
        clear,
        erase_display,
        delete_character,
        insert_line,
        delete_line,
        erase_line,
        alternate_character_set_mode,
        alternate_character_set_mode_off,
        cursor_up,
        cursor_down,
        cursor_forward,
        cursor_backward,
        cursor_home,
        reverse_index,
        cursor_save,
        cursor_restore,
        cursor_show,
        cursor_hide,
        cursor_next_line,
        cursor_previous_line,
        cursor_report_position,
        alternate_screen,
        alternate_screen_off,
        scroll_forward,
        scroll_backward,
        tab,
        tab_backward,
        tab_set,
        tab_clear,
        tab_clear_all,
        keypad_transmit_mode,
        keypad_transmit_mode_off,
        reset,
        device_report_attributes,
        hyperlink_reset
    ],
    [show(Name, io_ansi:Name()) || Name <- Names].

parameters() ->
    show(color_index, io_ansi:color(80)),
    show(color_name, io_ansi:color(red)),
    show(color_other_name, io_ansi:color(bold)),
    show(color_rgb, io_ansi:color(255, 0, 10)),
    show(background_index, io_ansi:background(17)),
    show(background_name, io_ansi:background(light_cyan)),
    show(background_rgb, io_ansi:background(1, 2, 3)),
    show(underline_color_index, io_ansi:underline_color(9)),
    show(underline_color_name, io_ansi:underline_color(white)),
    show(underline_color_rgb, io_ansi:underline_color(4, 5, 6)),
    show(modify_color, io_ansi:modify_color(12, 255, 128, 0)),
    show(cursor, io_ansi:cursor(0, 4)),
    show(cursor_move, [io_ansi:cursor_move(L, C) || {L, C} <- [{-2, 3}, {4, -1}, {0, 0}, {1, 0}]]),
    show(counts, [
        io_ansi:cursor_up(3),
        io_ansi:cursor_down(4),
        io_ansi:cursor_forward(5),
        io_ansi:cursor_backward(6),
        io_ansi:insert_character(2),
        io_ansi:delete_character(3),
        io_ansi:erase_character(4),
        io_ansi:insert_line(5),
        io_ansi:delete_line(6),
        io_ansi:scroll_forward(7),
        io_ansi:scroll_backward(8)
    ]),
    show(cursor_horizontal_absolute, io_ansi:cursor_horizontal_absolute(12)),
    show(cursor_vertical_absolute, io_ansi:cursor_vertical_absolute(65)),
    show(cursor_horizontal_vertical, io_ansi:cursor_horizontal_vertical(3, 4)),
    show(scroll_change_region, io_ansi:scroll_change_region(2, 20)),
    show(count_atom, io_ansi:cursor_up(id(many))),
    show(hyperlink_start, io_ansi:hyperlink_start("https://x.org")),
    show(hyperlink_params, io_ansi:hyperlink_start("file:/t", [{"id", "7"}, {"k", "v"}])),
    show(color_name_to_index, [io_ansi:color_name_to_index(C) || C <- [black, light_white, cyan]]),
    show(color_bad, catch_error(fun() -> io_ansi:color(id(1.5)) end)),
    show(background_bad, catch_error(fun() -> io_ansi:background(id(bold)) end)),
    show(tigetnum, io_ansi:tigetnum("colors")),
    show(tigetflag, io_ansi:tigetflag("xn")),
    show(tinfo, io_ansi:tinfo()),
    show(enabled, io_ansi:enabled()),
    show(enabled_error, io_ansi:enabled(standard_error)).

formatting() ->
    show(format_plain, io_ansi:format([blue, "~p"], [red])),
    show(format_on, io_ansi:format([blue, underline, "Hello ~p"], [world], [{enabled, true}])),
    show(
        format_no_reset,
        io_ansi:format([blue, "~p ~s"], [a, "b"], [{enabled, true}, {reset, false}])
    ),
    show(
        format_no_color,
        io_ansi:format([blue, bold, "x", red_background], [], [{enabled, true}, {color, false}])
    ),
    show(
        format_tuples, io_ansi:format([{color, 3}, {cursor, 1, 1}, "~w"], [[1]], [{enabled, true}])
    ),
    show(format_groups, io_ansi:format(["a", ["b", bold], "c~b", $d], [5], [{enabled, true}])),
    show(
        format_stars,
        io_ansi:format([red, "~*.*.0w|", bold, "~n~~~s"], [5, 3, 7, "z"], [{enabled, true}])
    ),
    show(format_no_args, io_ansi:format(["text"])),
    show(
        format_invalid,
        catch_error(fun() -> io_ansi:format([id(invalid_code)], [], [{enabled, true}]) end)
    ),
    show(
        format_invalid_args,
        catch_error(fun() -> io_ansi:format([{color, 1, 2}], [], [{enabled, true}]) end)
    ),
    show(format_too_few, catch_error(fun() -> io_ansi:format(["~p ~p"], [1]) end)),
    show(render, io_ansi:render([blue, underline, "Hello world"], [{enabled, true}])),
    show(render_no_reset, io_ansi:render([blue, <<"x">>], [{enabled, true}, {reset, false}])),
    show(render_off, io_ansi:render([blue, underline, "Hello world"], [{enabled, false}])),
    show(
        render_no_color,
        io_ansi:render([blue, underline, "Hello ", $\n, <<"world">>], [
            {enabled, true}, {color, false}
        ])
    ),
    show(render_plain, io_ansi:render(["x"])),
    show(render_bad, catch_error(fun() -> io_ansi:render([id({1, 2})], [{enabled, true}]) end)),
    show(fwrite, io_ansi:fwrite([blue, "plain ~p~n"], [text])),
    show(fwrite_forced, io_ansi:fwrite(standard_io, [bold, "forced~n"], [], [{enabled, true}])),
    show(fwrite1, io_ansi:fwrite(["one~n"])),
    show(fwrite3, io_ansi:fwrite([underline, "three ~w~n"], [3], [])).

show(Label, Value) -> io:format("~p ~w~n", [Label, Value]).

catch_error(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

id(X) -> X.
