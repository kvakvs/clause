%% erl_scan (plan 21 step LIB30): tokens, annotations, options, errors, the re-entrant scanner and the token
%% accessors, compared with OTP.
-module(erl_scan_tokens).
-export([main/1]).

main(_) ->
    basics(),
    numbers(),
    texts(),
    sigils(),
    options(),
    white_space(),
    errors(),
    reentrant(),
    accessors(),
    bad_arguments(),
    io:format("erl_scan_tokens: ok~n").

show(Label, Term) ->
    io:format("~s: ~p~n", [Label, Term]).

scan(Label, String) ->
    show(Label, erl_scan:string(String)).

basics() ->
    scan("atoms", "foo 'Quoted atom' 'a\\nb' ab@c_D9 ßeta 'ok'"),
    scan("variables", "X _ _Y Über Abc@1"),
    scan(
        "reserved", "after begin case try cond catch andalso orelse end fun if let of receive when"
    ),
    scan("operators", "bnot not div rem band and bor bxor bsl bsr or xor maybe else"),
    scan(
        "punctuation",
        ", ( ) { } [ ] ; ... .. . && & ?= ? << <:- <- <:= <= < >> >= > -> -- - ++ + =:= =/= =< => == ="
    ),
    scan("more punctuation", "/= / || | := :: : # #_ * ! @ \\ ^ ` "),
    scan("latin1 symbols", [$¡, $\s, $§, $\s, $×, $\s, $÷]),
    scan("dots", "a.b. c.\nd.%x\ne."),
    scan(
        "chars",
        "$a $\\n $\\t $\\s $\\e $\\d $\\v $\\b $\\f $\\r $\\^a $\\^? $\\^@ $\\101 $\\7 $\\x41 $\\x{1F600} $\\q $  $\n$ä"
    ),
    scan("char escapes", "$\\0 $\\00 $\\000 $\\0000 $\\x{0041} $\\x{10FFFF}"),
    scan("empty hex escape", "$\\x{} "),
    scan("hex escape beyond unicode", "$\\x{110000}"),
    scan("bad caret", "$\\^1"),
    scan("comment skipped", "a % comment\nb"),
    scan("unicode atom", [$', 16#1F600, $', $\s, $a]),
    scan("emoji after name", [$a, 16#1F600]),
    show("start line", erl_scan:string("a\nb", 7)),
    show("start location", erl_scan:string("a\n b c", {3, 5})),
    show("empty", erl_scan:string("")),
    show("only white space", erl_scan:string("  \n\t ", {1, 1})).

numbers() ->
    scan("integers", "0 42 1_000_000 16#ff 2#1010 36#zZ 16#F_F 007"),
    scan("big integers", "123456789012345678901234567890 16#ffffffffffffffffffffffff"),
    scan("floats", "1.5 0.0 1.0e10 1.0E-3 2.5e+2 1_0.2_5 1.0e1_0"),
    scan("based floats", "2#1.1 16#1.8#e1 10#1.5#e-1 8#0.4 2#1.0#e+2"),
    scan("float underflow", "1.0e-400"),
    scan("float overflow", "1.0e400"),
    scan("integer then dot", "1.a"),
    scan("bad base", "1#0"),
    scan("base 37", "37#1"),
    scan("missing digits", "16#"),
    scan("bad digit", "2#12"),
    scan("trailing underscore", "1_"),
    scan("double underscore", "1__0"),
    scan("name after integer", "12ab"),
    scan("name after float", "1.5x"),
    scan("bad exponent", "1.0e"),
    scan("exponent sign only", "1.0e+"),
    scan("name after exponent", "1.0e5x"),
    scan("based fraction name", "16#1.8x"),
    scan("based name", "16#1g").

texts() ->
    scan("strings", "\"\" \"abc\" \"a\\\"b\" \"x\\x{263A}y\" \"tab\\tend\" \"\\101\\102\""),
    scan("string newline", "\"a\nb\" c"),
    scan("escaped newline", "\"a\\\nb\" c"),
    scan("adjacent strings", "\"a\" \"b\""),
    scan("string concat", "\"a\"\"b\""),
    scan("triple quoted", "\"\"\"\n  abc\n   def\n  \"\"\""),
    scan("triple quoted empty line", "\"\"\"\n    one\n\n    two\n    \"\"\" x"),
    scan("triple quoted quotes", "\"\"\"\"\n\"\"\"\n\"\"\"\""),
    scan("triple quoted verbatim", "\"\"\"\n a\\nb\n \"\"\""),
    scan("triple quoted crlf", "\"\"\"\r\n x\r\n \"\"\""),
    show("triple quoted columns", erl_scan:string("\"\"\"\n  a\n  \"\"\" b", {1, 1}, [text])),
    scan("bad indentation", "\"\"\"\n  a\n b\n  \"\"\""),
    scan("text after triple quotes", "\"\"\" x\n\"\"\""),
    scan("unterminated triple quoted", "\"\"\"\nabc"),
    scan("unterminated string", "\"abcdefghijklmnopqrstuvwxyz"),
    scan("unterminated escape", "\"abc\\"),
    scan("unterminated atom", "'abc"),
    scan("unterminated char", "$"),
    scan("unterminated char escape", "$\\"),
    scan("long atom", lists:duplicate(256, $a)),
    scan("long quoted atom", "'" ++ lists:duplicate(256, $b) ++ "'"),
    scan("long variable", "V" ++ lists:duplicate(255, $c)),
    scan("max atom", lists:duplicate(255, $d)).

sigils() ->
    scan(
        "sigil plain",
        "~\"abc\" ~b\"a\\nb\" ~s(x) ~B[a\\nb] ~S{c} ~<d> ~/e/ ~|f| ~#g# ~`h` ~'i' ~\"j\"x"
    ),
    scan("sigil suffix", "~s\"a\"foo ~\"b\"Bar"),
    scan("sigil triple", "~\"\"\"\n  a\n  \"\"\" ~B\"\"\"\n x\\n\n \"\"\""),
    scan("sigil escapes", "~s\"\"\"\n  a\\tb\n  \"\"\""),
    scan("sigil bad delimiter", "~s?x?"),
    scan("sigil unterminated", "~s(abc"),
    scan("sigil verbatim unterminated", "~B<abc"),
    scan("sigil concat", "~s\"a\"\"b\"").

options() ->
    show("text", erl_scan:string("f(X) -> 'a b' + 1.5.", 1, [text])),
    show("text columns", erl_scan:string("f(X) ->\n  $a.", {1, 1}, text)),
    show("return", erl_scan:string("a % c\n  b", 1, [return])),
    show("return comments", erl_scan:string("a %% c\nb", {1, 1}, [return_comments, text])),
    show("return white spaces", erl_scan:string("a \t\n\tb", {1, 1}, [return_white_spaces])),
    ResWord = fun
        (foo) -> true;
        (_) -> false
    end,
    show("reserved_word_fun", erl_scan:string("foo bar case", 1, [{reserved_word_fun, ResWord}])),
    TextFun = fun
        (atom, _) -> true;
        (_, Text) -> length(Text) > 2
    end,
    show("text_fun", erl_scan:string("ab 1 123 'x' \"yy\"", {1, 1}, [{text_fun, TextFun}])),
    show(
        "text wins over text_fun",
        erl_scan:string("1 2", 1, [text, {text_fun, fun(_, _) -> false end}])
    ),
    show("unknown option", erl_scan:string("a", 1, [bogus, {bogus, 1}])),
    show("compiler_internal", erl_scan:string("a", 1, [{compiler_internal, []}])).

white_space() ->
    WS = fun(String) -> erl_scan:string(String, {1, 1}, [return_white_spaces, text]) end,
    show("spaces", WS("a" ++ lists:duplicate(20, $\s) ++ "b")),
    show("tabs", WS("a" ++ lists:duplicate(12, $\t) ++ "b")),
    show("newline spaces", WS("a\n" ++ lists:duplicate(20, $\s) ++ "b")),
    show("newline tabs", WS("a\n" ++ lists:duplicate(12, $\t) ++ "b")),
    show("newline cr", WS("a\n\r\n\fb")),
    show("newlines", WS("a\n\n\n b")),
    show("mixed white", WS([$a, $\v, $\s, 16#A0, $\n, $\v, $b])),
    show("cr", WS("a\r\r\nb")),
    show("white no columns", erl_scan:string("a \n  \t\n\vb", 1, [return_white_spaces])),
    show("white text no columns", erl_scan:string("a \n  b", 1, [return_white_spaces, text])),
    show("dot white", WS("a.\tb.\nc.")),
    show("dot comment", erl_scan:string("a.%c", {1, 1}, [return, text])).

errors() ->
    Errors =
        [
            "a " ++ [16#D800] ++ " b",
            "\"a" ++ [16#D800] ++ "\"",
            "$" ++ [16#DFFF],
            "'a" ++ [16#FFFE] ++ "'",
            "\"\\x{110000}\"",
            "\"\\xz\"",
            "\"\\^1\"",
            "\"\\x{12\"",
            "% " ++ [16#D800],
            "~s\"\"\"\n " ++ [16#D800] ++ "\n \"\"\"",
            "~B(" ++ [16#D800] ++ ")"
        ],
    lists:foreach(
        fun(String) ->
            Result = erl_scan:string(String, {1, 1}),
            show("error", Result),
            case Result of
                {error, {_, erl_scan, Descriptor}, _} ->
                    show("format_error", erl_scan:format_error(Descriptor));
                _ ->
                    ok
            end
        end,
        Errors
    ),
    Descriptors =
        [
            {unterminated, char},
            {unterminated, atom, "ab"},
            {unterminated, string, "a\"b"},
            {unterminated, {string, 3}, "x"},
            {unterminated, {sigil, s, $(, $)}, "y"},
            {unterminated, {sigil, b, 4}, "z"},
            {illegal, float},
            {base, 40},
            indentation,
            white_space,
            string_concat,
            {something, other}
        ],
    show("format_error", [erl_scan:format_error(D) || D <- Descriptors]),
    show("not character", catch_error(fun() -> erl_scan:string([$a, $\s, foo]) end)),
    show("beyond unicode", catch_error(fun() -> erl_scan:string([$a, $\s, 16#110000]) end)).

reentrant() ->
    show("chunks", feed(["f(X", ") -", "> X", ".", "\n g() -> 'a", " b'. h"], 1)),
    show("single chars", feed([[C] || C <- "a(B, \"s\\n\") -> 1.5e3 + 16#f. "], {1, 1})),
    show("eof after form", feed(["a. ", eof], 1)),
    show("eof inside form", feed(["a b", eof], 1)),
    show("eof only", erl_scan:tokens([], eof, 1)),
    show("eof white", feed(["  \n ", eof], {1, 1})),
    show("left over", erl_scan:tokens([], "a. b. c", 1)),
    show("dot at end", feed(["a.", " b."], 1)),
    show("error left over", erl_scan:tokens([], "a " ++ [16#D800] ++ " b. c", 1)),
    show("error at eof", feed(["\"abc", eof], {1, 1})),
    show("error in continuation", feed(["1_", eof], 1)),
    show("tokens options", feed(["a %c\n", "b.", " "], {1, 1}, [return, text])),
    show(
        "reserved_word_fun tokens",
        feed(["foo ba", "r.", "\n"], 1, [
            {reserved_word_fun, fun
                (bar) -> true;
                (_) -> false
            end}
        ])
    ),
    {more, Cont} = erl_scan:tokens([], "abc", {2, 3}),
    show("continuation location", erl_scan:continuation_location(Cont)),
    {more, Cont1} = erl_scan:tokens([], "abc", 2),
    show("continuation line", erl_scan:continuation_location(Cont1)),
    show("triple quoted chunks", feed(["\"\"", "\"\n a", "\n \"\"", "\". "], {1, 1})),
    Inputs = [
        "f(X) when X >= 1, X =< 2 -> X ++ [a | b] -- c. ",
        "A = <<1:8, B/binary>>, #{k := v, j => w}, #r{a = 1}#_ <:- <:= ... .. ",
        "$a $\\n $\\x{41} $\\101 $\\x4F $\\^G 'q\\'a' \"s\\t\\x{263A}\\101\" ",
        "1_000 16#fF 2#1_0 1.5e-3 3.0E+2 2#1.1 16#1.8#e1 10#2.5#e-1 ",
        "~s\"x\"y ~B[z] ~\"\"\"\n  t\n  \"\"\"s ",
        "\"\"\"\n  a\n\n   b\n  \"\"\" ",
        "a %% comment\n\tb \r\n  c. ",
        "x.%c\ny. ",
        "1e5 ",
        "16#_1 ",
        "'abc",
        "\"\\x{12",
        "1.",
        "$\\x",
        "\"\"\"\n ab"
    ],
    lists:foreach(
        fun(Input) ->
            Chunks = [[C] || C <- Input] ++ [eof],
            show("char by char", {feed(Chunks, {1, 1}, [return, text]), feed(Chunks, 1)})
        end,
        Inputs
    ).

feed(Chunks, Location) ->
    feed(Chunks, Location, []).

feed(Chunks, Location, Options) ->
    feed(Chunks, [], Location, Options, []).

feed([], _Cont, _Location, _Options, Acc) ->
    lists:reverse([unfinished | Acc]);
feed([Chunk | Chunks], Cont, Location, Options, Acc) ->
    case erl_scan:tokens(Cont, Chunk, Location, Options) of
        {more, Cont1} ->
            feed(Chunks, Cont1, Location, Options, Acc);
        {done, Result, eof} ->
            lists:reverse([{Result, eof} | Acc]);
        {done, Result, []} ->
            feed(Chunks, [], Location, Options, [Result | Acc]);
        {done, Result, Rest} ->
            feed([Rest | Chunks], [], Location, Options, [Result | Acc])
    end.

accessors() ->
    {ok, Tokens, _} = erl_scan:string("foo 'X' ( 1.5 \"s\"\n$c", {1, 1}, [text]),
    Rows = [
        {
            erl_scan:category(T),
            erl_scan:symbol(T),
            erl_scan:text(T),
            erl_scan:line(T),
            erl_scan:column(T),
            erl_scan:location(T),
            erl_scan:end_location(T)
        }
     || T <- Tokens
    ],
    show("accessors", Rows),
    {ok, Plain, _} = erl_scan:string("a\nb", 1),
    show("plain accessors", [
        {erl_scan:text(T), erl_scan:column(T), erl_scan:end_location(T), erl_scan:location(T)}
     || T <- Plain
    ]),
    show("reserved_word", [
        erl_scan:reserved_word(W)
     || W <- ['case', 'maybe', 'else', foo, 'xor', 'cond']
    ]),
    show("f_reserved_word", [erl_scan:f_reserved_word(W) || W <- ['let', 'maybe', foo]]).

bad_arguments() ->
    show("category badarg", catch_error(fun() -> erl_scan:category(id(foo)) end)),
    show("symbol badarg", catch_error(fun() -> erl_scan:symbol(id({a, b, c, d})) end)),
    show("string not list", catch_error(fun() -> erl_scan:string(id(foo)) end)),
    show("bad location", catch_error(fun() -> erl_scan:string("a", id({1, 0})) end)),
    show(
        "bad reserved_word_fun",
        catch_error(fun() -> erl_scan:string("a", 1, [{reserved_word_fun, id(x)}]) end)
    ),
    show("negative line", catch_error(fun() -> erl_scan:string("a", id(-1)) end)),
    show("tokens bad location", catch_error(fun() -> erl_scan:tokens([], "a", id(x)) end)).

catch_error(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

id(X) -> X.
