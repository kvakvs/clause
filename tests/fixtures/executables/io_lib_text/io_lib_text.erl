-module(io_lib_text).
-export([main/1, id/1]).

%% io_lib (plan 21 step LIB4C): the write family, the character list checks, limit_term/2, the collect functions
%% and format/2,3 with every control sequence but ~p, each result printed with ~w so OTP's deep list shapes show.
main(_) ->
    show(write, [reason(fun() -> io_lib:write(T) end) || T <- terms()]),
    show(write_depth, [reason(fun() -> io_lib:write(T, D) end) || T <- terms(), D <- [1, 2, 3]]),
    show(write_options, [
        reason(fun() -> io_lib:write(T, O) end)
     || T <- [['\x{400}', "a"], #{2 => b, 1 => [a]}, {x, [1, 2, 3]}],
        O <- [
            [], [{depth, 2}], [{encoding, latin1}], [{encoding, unicode}], [{maps_order, reversed}]
        ]
    ]),
    show(write_false, [reason(fun() -> io_lib:write({a, [1, 2]}, 2, false) end)]),
    show(write5, [reason(fun() -> io_lib:write([a, b], -1, latin1, ordered, -1) end)]),
    show(write5_zero, [reason(fun() -> io_lib:write([a, b], 0, latin1, ordered, -1) end)]),
    show(bwrite, [reason(fun() -> io_lib:bwrite(T, [{depth, 3}]) end) || T <- terms()]),
    show(write_bin, [
        reason(fun() -> io_lib:write_bin(T, 3, unicode, undefined, -1) end)
     || T <- terms()
    ]),
    show(write_binary, [
        reason(fun() -> io_lib:write_binary(B, D, T) end)
     || {B, D, T} <- binaries()
    ]),
    show(write_binary_bin, [
        reason(fun() -> io_lib:write_binary_bin(B, D, T, <<"x">>) end)
     || {B, D, T} <- binaries()
    ]),
    show(atoms, [
        [reason(fun() -> io_lib:F(A) end) || F <- [write_atom, write_atom_as_latin1]]
     || A <- atoms()
    ]),
    show(quote_atom, [reason(fun() -> io_lib:quote_atom(A, atom_to_list(A)) end) || A <- atoms()]),
    show(strings, [
        [
            reason(fun() -> io_lib:F(S) end)
         || F <- [
                write_string,
                write_latin1_string,
                write_string_as_latin1,
                write_unicode_string
            ]
        ]
     || S <- strings()
    ]),
    show(strings_quoted, [
        [
            reason(fun() -> io_lib:F(S, $') end)
         || F <- [write_string, write_latin1_string, write_string_as_latin1]
        ]
     || S <- strings()
    ]),
    show(bwrite_string, [
        reason(fun() -> io_lib:bwrite_string(S, Q, E) end)
     || S <- ["a\"b", <<"a\"b\n">>, <<"x", 200, "y">>, <<"\x{400}"/utf8>>],
        Q <- [$", []],
        E <- [latin1, unicode]
    ]),
    show(chars, [
        [
            reason(fun() -> io_lib:F(C) end)
         || F <- [write_char, write_latin1_char, write_char_as_latin1, write_unicode_char]
        ]
     || C <- [$a, $\s, $\n, $\\, $", $', 0, 127, 160, 255, 256, 16#1F600, -1, a]
    ]),
    show(checks, [
        [
            io_lib:F(T)
         || F <- [
                char_list,
                latin1_char_list,
                deep_char_list,
                deep_latin1_char_list,
                deep_unicode_char_list,
                printable_list,
                printable_latin1_list,
                printable_unicode_list
            ]
        ]
     || T <- check_terms()
    ]),
    show(chars_length, [reason(fun() -> io_lib:chars_length(T) end) || T <- lengths()]),
    show(indentation, [
        reason(fun() -> io_lib:indentation(T, I) end)
     || T <- ["ab", "a\tb", "x\ny", ["a", [$\t | <<"b\tc">>]], <<"\x{400}\t"/utf8>>, [a]],
        I <- [0, 3]
    ]),
    show(nl, io_lib:nl()),
    show(limit_term, [
        reason(fun() -> io_lib:limit_term(T, D) end)
     || T <- terms(), D <- [-1, 0, 1, 2, 3]
    ]),
    show(collect_chars, collect_chars()),
    show(collect_line, collect_line()),
    show(format, [reason(fun() -> io_lib:format(F, A) end) || {F, A} <- formats()]),
    show(fwrite, [reason(fun() -> io_lib:fwrite("~w~n", [x]) end)]),
    show(bformat, [reason(fun() -> io_lib:bformat(F, A) end) || {F, A} <- formats()]),
    show(bfwrite, [reason(fun() -> io_lib:bfwrite("~s~c", [<<"x">>, 300], [{chars_limit, 5}]) end)]),
    show(scan_format, [reason(fun() -> specs(io_lib:scan_format(F, A)) end) || {F, A} <- formats()]),
    show(unscan_format, [
        reason(fun() -> io_lib:unscan_format(io_lib:scan_format(F, A)) end)
     || {F, A} <- formats()
    ]),
    show(build_text, [
        reason(fun() -> io_lib:build_text(io_lib:scan_format(F, A)) end)
     || {F, A} <- formats()
    ]),
    show(format_prompt, [
        reason(fun() -> io_lib:format_prompt(P, E) end)
     || P <- ["a> ", 'b>', <<"c>">>, {format, "~w>", [1]}, {format, "~q", []}],
        E <- [latin1, unicode]
    ]),
    show(format_prompt1, [reason(fun() -> io_lib:format_prompt("p") end)]),
    show(maps_order_funs, maps_order_funs()),
    io:format("~Kw ~Kp~n", [
        fun descending/2, #{1 => a, 2 => b}, fun descending/2, #{x => #{1 => a, 2 => b}}
    ]),
    io:format("io_lib_text: ok~n").

terms() ->
    [
        1,
        -20,
        1.5,
        a,
        'B c',
        '\x{400}',
        "abc",
        [],
        [1, 2, 3],
        [a | b],
        [[1], [2, [3]]],
        {},
        {a},
        {a, {b, c}, [d]},
        <<>>,
        <<1, 2, 3>>,
        <<"text">>,
        <<1:3>>,
        <<1, 2:4>>,
        #{},
        #{a => 1},
        #{k => [1, 2], 2 => {x}}
    ].

binaries() ->
    [
        {<<1, 2, 3>>, -1, -1},
        {<<1, 2, 3>>, 2, -1},
        {<<10, 200, 3>>, -1, 8},
        {<<1, 2, 3:2>>, -1, -1},
        {<<>>, -1, 0},
        {<<5>>, -1, 4}
    ].

atoms() ->
    [
        a,
        abc,
        'Abc',
        a_B1,
        'a-b',
        '',
        'é',
        'éA',
        '×',
        'ö@x',
        'and',
        'begin',
        'maybe',
        'else',
        '\x{400}',
        '$'
    ].

strings() ->
    [
        "",
        "abc",
        "a\"b'c\\",
        "\n\r\t\v\b\f\e\d",
        [0, 1, 31, 127, 159],
        [160, 255],
        [256, 1050, 16#1F600],
        [$a, 200]
    ].

check_terms() ->
    [
        [],
        "abc",
        [$a, [$b]],
        [[]],
        [255],
        [256],
        [16#D800],
        [16#FFFE],
        [16#10FFFF],
        [16#110000],
        [-1],
        "a\nb\e",
        [7],
        [$a | b],
        <<"a">>,
        [<<"a">>],
        a,
        [160, 1050]
    ].

lengths() ->
    [
        "abc",
        ["ab", [$c]],
        <<"abc">>,
        ["a", <<"bc">>],
        [1050, $a],
        [<<"\x{400}"/utf8>>],
        "e\x{301}",
        [a]
    ].

collect_chars() ->
    [
        reason(fun() -> io_lib:collect_chars(S, D, E, N) end)
     || {S, D, E, N} <- [
            {start, <<"abcdef">>, latin1, 3},
            {start, <<"abc">>, latin1, 3},
            {start, <<"ab">>, latin1, 3},
            {start, <<"\x{400}\x{401}x"/utf8>>, unicode, 2},
            {start, <<"\x{400}"/utf8>>, unicode, 2},
            {start, "abcdef", latin1, 3},
            {start, "ab", latin1, 3},
            {start, eof, latin1, 3},
            {{binary, [<<"ab">>], 2}, <<"cde">>, latin1, 0},
            {{binary, [<<"ab">>], 2}, eof, latin1, 0},
            {{binary, [<<>>], 2}, eof, latin1, 0},
            {{binary, [<<"b">>, <<"a">>], 1}, <<"\x{400}x"/utf8>>, unicode, 0},
            {{list, "ba", 2}, "cde", latin1, 0},
            {{list, "ba", 2}, eof, latin1, 0},
            {{list, [], 2}, eof, latin1, 0}
        ]
    ] ++
        [
            reason(fun() -> io_lib:collect_chars(S, D, N) end)
         || {S, D, N} <- [
                {[], "abcd", 2},
                {[], "a", 2},
                {{1, "a"}, "bc", 0},
                {[], eof, 2},
                {{2, "ba"}, eof, 0}
            ]
        ] ++
        [
            reason(fun() -> io_lib:collect_chars_eager(S, D, E, N) end)
         || {S, D, E, N} <- [
                {start, "ab", latin1, 3},
                {start, <<"ab">>, latin1, 3},
                {start, [], latin1, 3},
                {start, <<"abcd">>, latin1, 3}
            ]
        ] ++
        [io_lib:collect_chars({list, "a", 7})].

collect_line() ->
    [
        reason(fun() -> io_lib:collect_line(S, D, E, x) end)
     || {S, D, E} <- [
            {start, <<"ab\ncd">>, latin1},
            {start, <<"ab\r\ncd">>, latin1},
            {start, <<"ab\r">>, latin1},
            {start, <<"abc">>, latin1},
            {[<<"\r">>, <<"ab">>], <<"\nx">>, latin1},
            {[<<"ab">>], <<"c\n">>, unicode},
            {start, "ab\ncd", latin1},
            {start, "ab\r\ncd", latin1},
            {start, "ab", latin1},
            {"ba", "c\n", latin1},
            {start, eof, latin1},
            {[<<"ab">>], eof, latin1},
            {"ba", eof, latin1}
        ]
    ] ++
        [
            reason(fun() -> io_lib:collect_line_no_eol(S, D, latin1, x) end)
         || {S, D} <- [{start, "  ab \n"}, {start, <<"ab \r\nc">>}, {start, eof}, {start, "ab"}]
        ] ++
        [reason(fun() -> io_lib:collect_line(start, "x\n", y) end), io_lib:collect_line(anything)].

formats() ->
    [
        {"plain", []},
        {'atom~n', []},
        {<<"bin ~w">>, [1]},
        {"a~wb~n", [1]},
        {"~w|~10w|~-10w|~3w|~.2w|~10.3w|~*w", [foo, foo, foo, foo, foo, foo, 5, x]},
        {"~W|~4W", [[1, 2, 3, 4], 2, {a, b, c}, 2]},
        {"~s|~10s|~-10s|~3s|~.2s|~10.3s|~10.3.*s", [
            "abcde", "ab", "ab", "abcde", "abcde", "abcde", $x, "ab"
        ]},
        {"~s~s~s", [abc, <<"bin">>, ["de", <<"f">>, [$g]]]},
        {"~ts|~ts|~ts|~10ts", ["\x{400}b", <<"\x{400}"/utf8>>, <<200, 201>>, "e\x{301}x"]},
        {"~s", ["\x{400}"]},
        {"~c|~5c|~-5.2c|~.3c|~tc|~c", [$a, $b, $c, $d, 1050, 1050]},
        {"~e|~.3e|~12.2e|~-12e|~e", [1.0, 123.456, -0.000123, 2.5, 0.0]},
        {"~f|~.0f|~.2f|~10.1f|~-10f|~f|~f", [3.14159, 2.5, 0.005, -1.25, 1.0e10, -0.0, 1.0e-10]},
        {"~g|~.3g|~g|~g|~g|~.1g|~g", [0.5, 1234.5, 1.0e-5, 12345.678, 100.0, 0.5, 1.0]},
        {"~b|~B|~.16b|~.16B|~.2B|~10.8B|~-6B|~3B", [255, -255, 255, 255, 5, 64, 7, 123456]},
        {"~x|~X|~.16x|~.16X|~.16X|~10.16x", [
            255, "0x", 255, "0x", 255, '#', -255, abc, 31, [$0, "x"], 255, "z"
        ]},
        {"~+|~.16+|~#|~.16#|~12.2#", [255, 255, 255, -255, 5]},
        {"~i|~n|~3n|~~|~5~|~-5.2.x~", [ignored]},
        {"tab\t~w and ~w\n~w", [a, b, c]},
        {"~lw|~kw|~tw", ["ab", #{2 => b, 1 => a}, '\x{400}']},
        {"~Kw", [reversed, #{1 => a, 2 => b}]},
        {"~.*c|~*.*.*c", [3, $z, 6, 2, $., $y]},
        {"~w ~", [1]},
        {"~q", [1]},
        {"~w", []},
        {"~w", [1, 2]},
        {"~w", improper},
        {"~c", [a]},
        {"~e", [1]},
        {"~f", [1.0e300]},
        {"~.1e", [1.0]},
        {"~.0f", [1.0]},
        {"~.1B", [1]},
        {"~.37B", [1]},
        {"~x", [1, 2]},
        {"~s", [[300]]},
        {"~s", [[-1]]},
        {"~s", [1]},
        {"~5.6s", ["a"]},
        {"~-3n", []},
        {123, []},
        {["ab", <<"cd">>, "~w"], [1]},
        {[$a, "~w", $~, $w], [x]}
    ].

%% Spec maps as sorted lists: OTP prints atom keys in its internal order, Clause in key order.
specs(Scanned) ->
    [
        case Element of
            #{} -> lists:sort(maps:to_list(Element));
            _ -> Element
        end
     || Element <- Scanned
    ].

show(Name, Value) -> io:format("~w: ~w~n", [Name, Value]).

%% maps_order comparison funs (plan 21 step LIB4F): keys sorted by the fun in the writers, formats and the pretty
%% printer, maps nested in keys and values, depth and chars_limit, and funs that raise or return other terms.
maps_order_funs() ->
    Desc = fun descending/2,
    Nested = #{
        1 => #{b => 1, a => 2}, #{2 => x, 1 => y} => [#{3 => c, 4 => d}], 0 => {#{z => 1, y => 2}}
    },
    Crash = fun(_, _) -> error(boom) end,
    [
        reason(fun() -> io_lib:write(Nested, [{maps_order, Desc}]) end),
        reason(fun() -> io_lib:write(Nested, [{maps_order, Desc}, {depth, 3}]) end),
        reason(fun() -> io_lib:write(Nested, [{maps_order, Desc}, {chars_limit, 20}]) end),
        reason(fun() -> io_lib:write(Nested, -1, unicode, Desc, -1) end),
        reason(fun() -> io_lib:bwrite(Nested, [{maps_order, Desc}]) end),
        reason(fun() -> io_lib:write_bin(Nested, -1, unicode, Desc, -1) end),
        reason(fun() ->
            io_lib:format("~Kw|~Kp|~KP", [Desc, Nested, Desc, Nested, Desc, Nested, 3])
        end),
        reason(fun() -> io_lib:format("~Kp", [Desc, Nested], [{chars_limit, 30}]) end),
        reason(fun() -> io_lib:bformat("~Kp", [Desc, Nested]) end),
        reason(fun() -> io_lib:build_text(io_lib:scan_format("~Kw", [Desc, Nested])) end),
        reason(fun() -> io_lib:format("~Kw", [Crash, #{1 => a, 2 => b}]) end),
        reason(fun() -> io_lib:format("~Kw", [fun(_, _) -> notbool end, #{1 => a, 2 => b}]) end),
        reason(fun() -> io_lib:format("~Kw", [Crash, #{1 => a}]) end),
        reason(fun() -> io_lib:write(#{1 => a, 2 => b}, [{maps_order, Crash}]) end),
        reason(fun() -> io_lib:format(id("~Kw ~w"), [Desc, #{1 => a, 2 => b}]) end)
    ].

descending(A, B) -> A >= B.

id(X) -> X.

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> {error, Reason};
        throw:Thrown -> {thrown, Thrown}
    end.
