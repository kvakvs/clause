-module(strings).
-export([main/1]).

%% string (plan 21 step LIB4): the Unicode API over lists, binaries and deep chardata (nested lists, embedded
%% binaries, combining marks, CR LF, emoji sequences, Hangul), printed with ~w so result shapes are compared; the
%% obsolete Latin-1 API; the number parsers; OTP's errors for bad input.
main(_) ->
    Texts = texts(),
    show(is_empty, [string:is_empty(T) || T <- Texts ++ [[[], <<>>], [<<>>, [[]]], [[], 0]]]),
    show(length, [reason(fun() -> string:length(T) end) || T <- Texts]),
    show(to_graphemes, [reason(fun() -> string:to_graphemes(T) end) || T <- Texts]),
    show(reverse, [reason(fun() -> string:reverse(T) end) || T <- Texts]),
    show(slice, [
        reason(fun() -> string:slice(T, N) end)
     || T <- Texts, N <- [0, 1, 3, 100]
    ]),
    show(slice3, [
        reason(fun() -> string:slice(T, N, L) end)
     || T <- Texts, {N, L} <- [{0, 0}, {1, 2}, {2, infinity}, {5, 1}, {0, 100}]
    ]),
    show(pad, [
        [string:pad(T, 6), string:pad(T, 6, leading), string:pad(T, 7, both, $*), string:pad(T, 1)]
     || T <- short_texts()
    ]),
    show(trim, [
        [
            string:trim(T),
            string:trim(T, leading),
            string:trim(T, trailing),
            string:trim(T, both, "ab"),
            string:trim(T, leading, [$\s, [$\r, $\n]]),
            string:trim(T, trailing, "\n"),
            string:trim(T, both, [])
        ]
     || T <- space_texts()
    ]),
    show(chomp, [string:chomp(T) || T <- space_texts()]),
    takes(),
    lexemes(),
    cases(),
    show(prefix, [
        reason(fun() -> string:prefix(T, P) end)
     || T <- short_texts(), P <- ["", "ab", <<"abc">>, "abcd", [<<"a">>, "b"], [$e, 16#301], "x"]
    ]),
    splits(),
    finds(),
    numbers(),
    show(jaro, [
        string:jaro_similarity(A, B)
     || {A, B} <- [
            {"martha", "marhta"},
            {<<"dixon">>, "dicksonx"},
            {"", ""},
            {"a", ""},
            {"", <<"b">>},
            {"abc", "xyz"},
            {[<<"ab">>, "c"], "abc"},
            {"e\x{301}", [16#E9]},
            {"same", "same"}
        ]
    ]),
    show(next, [
        [
            reason(fun() -> string:next_codepoint(T) end),
            reason(fun() -> string:next_grapheme(T) end)
        ]
     || T <- Texts
    ]),
    latin1(),
    show(errors, [
        reason(fun() -> string:length(id(bad)) end),
        reason(fun() -> string:length([bad]) end),
        reason(fun() -> string:length(<<255>>) end),
        reason(fun() -> string:to_graphemes(<<"a", 255>>) end),
        reason(fun() -> string:slice("abc", -1) end),
        reason(fun() -> string:slice("abc", 1, -1) end),
        reason(fun() -> string:pad("a", x) end),
        reason(fun() -> string:uppercase(<<255>>) end),
        reason(fun() -> string:uppercase(id(5)) end),
        reason(fun() -> string:trim(id(x)) end),
        reason(fun() -> string:lexemes(id(x), " ") end),
        reason(fun() -> string:split("abc", id(x)) end),
        reason(fun() -> string:find("abc", id(5)) end),
        reason(fun() -> string:equal(id(x), "x") end),
        reason(fun() -> string:take("abc", id(x)) end)
    ]),
    io:format("strings: ok~n").

texts() ->
    [
        "",
        <<>>,
        "abc",
        <<"abc">>,
        "a\r\nb",
        <<"a\r\nb">>,
        [$a, [$b, <<"c">>], "d"],
        [<<"ab">>, [[<<"cd">>, "e"]], [[]]],
        "e\x{301}x",
        <<"e", 16#CC, 16#81, "x">>,
        [$e, [16#301], <<"x">>],
        "\x{1F600}\x{1F468}\x{200D}\x{1F469}",
        <<"\x{1F468}\x{200D}\x{1F469}x"/utf8>>,
        "\x{1100}\x{1161}\x{11A8}\x{AC00}\x{11A8}z",
        [<<"a">>, "\x{301}", <<"b">>],
        "\x{1F1F8}\x{1F1EA}\x{1F1F8}",
        [$a | <<"bc">>],
        "x\x{FFFD}y",
        "\x{3042}\x{3099}"
    ].

short_texts() ->
    ["", <<>>, "abc", <<"abc">>, [$a, [<<"bc">>]], "e\x{301}", <<"e", 16#CC, 16#81>>, "\x{1F600}"].

space_texts() ->
    [
        "",
        "  abc  ",
        <<"  abc  ">>,
        [" ", [<<" ab">>], "c ", <<" ">>],
        "\r\n\tab\r\n",
        <<"\s\x{2003}ab\x{3000}"/utf8>>,
        "abab",
        <<"ba">>,
        "\n\n",
        "a\x{301} ",
        [<<"  ">>, "\x{301}", <<" ">>],
        "  "
    ].

takes() ->
    Seps = ["ab", [$a, [$b]], " ", [$e, 16#301], "", [[$\r, $\n], $\s]],
    show(take, [
        [
            reason(fun() -> string:take(T, S) end),
            reason(fun() -> string:take(T, S, true) end),
            reason(fun() -> string:take(T, S, false, trailing) end),
            reason(fun() -> string:take(T, S, true, trailing) end)
        ]
     || T <-
            space_texts() ++
                ["abcab", <<"abcab">>, [<<"ab">>, "cab"], "e\x{301}e", <<"e", 16#CC, 16#81, "e">>],
        S <- Seps
    ]).

lexemes() ->
    Inputs = [
        "a b  c",
        <<"a b  c">>,
        [<<"a ">>, "b", [<<"  c">>]],
        "",
        <<>>,
        " ",
        "a,b;c",
        <<"a\r\nb\nc">>,
        "e\x{301} e",
        [<<"e">>, [16#301], " e"]
    ],
    Seps = [" ", ", ;", [[$\r, $\n], $\n], [$e], []],
    show(lexemes, [[reason(fun() -> string:lexemes(T, S) end) || S <- Seps] || T <- Inputs]),
    show(nth_lexeme, [
        [reason(fun() -> string:nth_lexeme(T, N, S) end) || S <- Seps, N <- [1, 2, 3, 9]]
     || T <- Inputs
    ]).

cases() ->
    Inputs = [
        "",
        <<>>,
        "abc",
        <<"ABC">>,
        "Hello World",
        <<"x{DF}trax{DF}e">>,
        "x{1C5}ungla",
        [<<"a">>, "B", [<<"c">>]],
        "\x{130}i",
        <<"\x{3A3}\x{3C2}"/utf8>>,
        "e\x{301}",
        "already",
        <<"x", 16#C3, 16#9F>>,
        [$a | <<"B">>],
        "\x{1F600}a"
    ],
    show(upper, [reason(fun() -> string:uppercase(T) end) || T <- Inputs]),
    show(lower, [reason(fun() -> string:lowercase(T) end) || T <- Inputs]),
    show(title, [reason(fun() -> string:titlecase(T) end) || T <- Inputs]),
    show(casefold, [reason(fun() -> string:casefold(T) end) || T <- Inputs]),
    show(equal, [
        [
            string:equal(A, B),
            string:equal(A, B, true),
            string:equal(A, B, false, nfc),
            string:equal(A, B, true, nfd),
            string:equal(A, B, true, nfkc),
            string:equal(A, B, false, none)
        ]
     || {A, B} <- [
            {"abc", <<"abc">>},
            {"ABC", "abc"},
            {[<<"a">>, "bc"], "abc"},
            {"e\x{301}", [16#E9]},
            {<<"e", 16#CC, 16#81>>, <<16#C3, 16#A9>>},
            {"\x{FB01}", "fi"},
            {"x{DF}", "SS"},
            {"", <<>>},
            {"a", "ab"},
            {[$a, [$b]], [[$a], $b]}
        ]
    ]).

splits() ->
    Inputs = [
        "ab..cd..ef",
        <<"ab..cd..ef">>,
        [<<"ab.">>, ".cd", [<<"..ef">>]],
        "",
        <<>>,
        "..",
        "a.b",
        <<"no">>,
        "e\x{301}.e\x{301}",
        [<<"e">>, [16#301], ".e"]
    ],
    Patterns = ["..", ".", <<"..">>, [$., [$.]], "", "e\x{301}", "x"],
    show(split, [
        [reason(fun() -> string:split(T, P) end) || P <- Patterns]
     || T <- Inputs
    ]),
    show(split3, [
        [
            reason(fun() -> string:split(T, P, W) end)
         || P <- ["..", ".", "e\x{301}"], W <- [leading, trailing, all]
        ]
     || T <- Inputs
    ]),
    show(replace, [
        [
            string:replace(T, "..", "::"),
            string:replace(T, ".", <<"-">>, all),
            string:replace(T, "..", "", trailing),
            string:replace(T, "zz", "y", all)
        ]
     || T <- Inputs
    ]).

finds() ->
    Inputs = [
        "ab..cd..ef",
        <<"ab..cd..ef">>,
        [<<"ab.">>, ".cd", [<<"..ef">>]],
        "",
        "e\x{301}xe\x{301}",
        [<<"e">>, [16#301], "x"]
    ],
    show(find, [
        [reason(fun() -> string:find(T, P) end), reason(fun() -> string:find(T, P, trailing) end)]
     || T <- Inputs, P <- ["..", ".", <<"ef">>, "x", "", [$e, 16#301], "\x{301}"]
    ]).

numbers() ->
    Inputs = [
        "123",
        <<"-45abc">>,
        "+7",
        "1.0-1.0e-1",
        <<"3/2=1.5">>,
        "-1.5eX",
        "abc",
        "",
        <<>>,
        [<<"12">>, "3x"],
        "1e5",
        "1,5",
        "0x10",
        "1.",
        ".5",
        "-",
        "12\x{301}",
        [$1, $2 | <<"3">>],
        "999999999999999999999999"
    ],
    show(to_integer, [reason(fun() -> string:to_integer(T) end) || T <- Inputs]),
    show(to_float, [reason(fun() -> string:to_float(T) end) || T <- Inputs]),
    show(list_to, [
        [
            reason(fun() -> string:list_to_integer(T) end),
            reason(fun() -> string:list_to_float(T) end)
        ]
     || T <- ["12x", "1.5e2y", "", "x", "-3", "1e", "+1.0"]
    ]).

latin1() ->
    S = "  hello  world  ",
    show(old, [
        string:len(S),
        string:concat("ab", "cd"),
        string:chr(S, $l),
        string:rchr(S, $l),
        string:chr(S, $z),
        string:str(S, "wor"),
        string:rstr("abab", "ab"),
        string:str(S, "zz"),
        string:span("abc123", "abc"),
        string:cspan("abc123", "123"),
        string:substr(S, 3),
        string:substr(S, 3, 5),
        string:tokens("a,b;;c", ",;"),
        string:tokens(",,a,,", ","),
        string:tokens("", ","),
        string:tokens("abc", ""),
        string:chars($x, 3),
        string:chars($x, 2, "yz"),
        string:copies("ab", 3),
        string:copies("ab", 0),
        string:words(S),
        string:words("a,b,,c", $,),
        string:sub_word(S, 2),
        string:sub_word("a,b,,c", 3, $,),
        string:sub_word("a b", 5),
        string:strip(S),
        string:strip(S, left),
        string:strip("xxaxx", both, $x),
        string:left("ab", 5),
        string:left("abcdef", 3),
        string:left("ab", 4, $.),
        string:right("ab", 5),
        string:right("abcdef", 3),
        string:centre("ab", 6, $*),
        string:centre("abcdef", 3),
        string:centre("ab", 0),
        string:sub_string(S, 3),
        string:sub_string(S, 3, 7),
        string:join(["a", "b", "c"], ", "),
        string:join([], "x"),
        string:to_upper("abc\x{E5}\x{FF}"),
        string:to_lower("ABC\x{C5}\x{DE}"),
        string:to_upper($a),
        string:to_lower($A)
    ]),
    show(old_errors, [
        reason(fun() -> string:len(id(<<"a">>)) end),
        reason(fun() -> string:chr("abc", id(a)) end),
        reason(fun() -> string:substr("abc", 0) end),
        reason(fun() -> string:substr("abc", 5) end),
        reason(fun() -> string:tokens(id(<<"a">>), ",") end),
        reason(fun() -> string:strip(id(<<"a">>)) end),
        reason(fun() -> string:left("ab", 1.0) end),
        reason(fun() -> string:join(id(x), ",") end),
        reason(fun() -> string:copies("a", -1) end),
        reason(fun() -> string:words([a]) end)
    ]).

id(Value) -> Value.

show(Name, Value) -> io:format("~w: ~w~n", [Name, Value]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> {error, Reason};
        throw:Thrown -> {thrown, Thrown}
    end.
