-module(fread_scanning).
-export([main/1]).

%% io_lib:fread/2,3 and io:fread/2,3 (plan 21 step LIB4E): every control, field widths, suppression, ~t,
%% continuations over several chunks and lines, eof, and the error terms of malformed input and formats.
main(_) ->
    controls(),
    widths(),
    based(),
    strings(),
    literals(),
    errors(),
    continuations(),
    standard_input(),
    io:format("fread_scanning: ok~n").

controls() ->
    show(controls, [
        io_lib:fread("~d", "42"),
        io_lib:fread("~d", "  -17 rest"),
        io_lib:fread("~d~d", "1 2"),
        io_lib:fread("~d ~d", "1   2"),
        io_lib:fread("~f", "3.25"),
        io_lib:fread("~f", "-1.5e3x"),
        io_lib:fread("~f", "+2.0E-2"),
        io_lib:fread("~f", "7"),
        io_lib:fread("~f", "7."),
        io_lib:fread("~u", "123abc"),
        io_lib:fread("~16u", "fFz"),
        io_lib:fread("~2u", "1012"),
        io_lib:fread("~36u", "zZ9"),
        io_lib:fread("~u", " 1"),
        io_lib:fread("~-~d", "-5"),
        io_lib:fread("~-~d", "+5"),
        io_lib:fread("~-~d", "5"),
        io_lib:fread("~a", "  hello world"),
        io_lib:fread("~a~a", "x y"),
        io_lib:fread("~s", "\tword\n"),
        io_lib:fread("~c", "abc"),
        io_lib:fread("~c~c", " xy"),
        io_lib:fread("~l~d~l", "  12"),
        io_lib:fread("~*d~d", "1 2"),
        io_lib:fread("~*s~s", "skip keep"),
        io_lib:fread("~*-~d", "-9"),
        io_lib:fread("~*l~d", "3"),
        io_lib:fread("", "left"),
        io_lib:fread("", "")
    ]).

widths() ->
    show(widths, [
        io_lib:fread("~3d", "12345"),
        io_lib:fread("~3d", " 12x"),
        io_lib:fread("~3d", "12 4"),
        io_lib:fread("~5f", "1.5e1x"),
        io_lib:fread("~4f", " 2.5"),
        io_lib:fread("~3s", "abcdef"),
        io_lib:fread("~5s", " ab  c"),
        io_lib:fread("~3a", "xyzw"),
        io_lib:fread("~3c", "abcd"),
        io_lib:fread("~0c", "abc"),
        io_lib:fread("~3c", "a\nb"),
        io_lib:fread("~3c", "ab"),
        io_lib:fread("~2d~2d", "1234"),
        io_lib:fread("~10s", "short"),
        io_lib:fread("~*3c~c", "abcd")
    ]).

based() ->
    show(based, [
        io_lib:fread("~#", "16#ff"),
        io_lib:fread("~#", "  -16#ff "),
        io_lib:fread("~#", "2#101 x"),
        io_lib:fread("~#", "36#Zz"),
        io_lib:fread("~#", "37#1"),
        io_lib:fread("~#", "1#1"),
        io_lib:fread("~#", "16#"),
        io_lib:fread("~#", "16ff"),
        io_lib:fread("~#", "#ff"),
        io_lib:fread("~5#", "8#17x"),
        io_lib:fread("~5#", " 8#17"),
        io_lib:fread("~5#", "-8#17"),
        io_lib:fread("~4#", "8#1 "),
        io_lib:fread("~#~d", "8#7 9"),
        io_lib:fread("~#~f", "8#7 9"),
        io_lib:fread("~*#~d", "2#11 4")
    ]).

strings() ->
    show(strings, [
        io_lib:fread("~ts", [16#3B1, 16#3B2, $\s, $x]),
        io_lib:fread("~s", [16#3B1, $x]),
        io_lib:fread("~s", [$a, 16#3B1, $x]),
        io_lib:fread("~ta", [16#3B1]),
        io_lib:fread("~a", [$a, 16#3B1]),
        io_lib:fread("~tc", [16#3B1, $b]),
        io_lib:fread("~c", [16#3B1, $b]),
        io_lib:fread("~2tc", [16#3B1, 16#3B2, 16#3B3]),
        io_lib:fread("~3ts", [16#3B1, $\s, $b]),
        catching(fun() -> io_lib:fread("~s", [$a, $b | id(tail)]) end),
        io_lib:fread("~s", [$a, id(atom), $b]),
        io_lib:fread("~s", [id(atom), $b]),
        io_lib:fread("~ts", [$a, id(atom), $b]),
        io_lib:fread("~s", [$a, id(66.0)]),
        io_lib:fread("~s", "")
    ]).

literals() ->
    show(literals, [
        io_lib:fread("x~dy", "x5y"),
        io_lib:fread("x~dy", "x5z"),
        io_lib:fread("a b", "a    b!"),
        io_lib:fread("a b", "ab"),
        io_lib:fread("~~~d", "~3"),
        io_lib:fread("~~~d", "3"),
        io_lib:fread("~d,~d", "1,2"),
        io_lib:fread("~d,~d", "1"),
        io_lib:fread("~d,~d", "1,"),
        io_lib:fread("abc", "ab"),
        io_lib:fread("~d", ""),
        io_lib:fread("~d", eof),
        io_lib:fread("~d~d", eof),
        io_lib:fread("x", eof),
        io_lib:fread("\n", "\n\n x")
    ]).

errors() ->
    show(errors, [
        io_lib:fread("~d", "x"),
        io_lib:fread("~d", "-"),
        io_lib:fread("~f", "1.x"),
        io_lib:fread("~u", "x"),
        io_lib:fread("~1u", "1"),
        io_lib:fread("~37u", "1"),
        io_lib:fread("~q", "1"),
        io_lib:fread("~", "1"),
        io_lib:fread("~td", "1"),
        io_lib:fread("~t-", "1"),
        io_lib:fread("~3ta", "abc"),
        io_lib:fread("~s", "\n"),
        io_lib:fread("~c", "\nx"),
        io_lib:fread("~3d", "1\n2"),
        io_lib:fread("~d", [$1 | id(tail)]),
        io_lib:fread("~d", id(<<"12">>)),
        io_lib:fread("~d", [id(1.0)]),
        catching(fun() -> io_lib:fread("x", id(<<"x">>)) end),
        catching(fun() -> io_lib:fread("~s", [$a | id(tail)]) end),
        catching(fun() -> io_lib:fread("~d", [$1, $\s | id(tail)]) end),
        catching(fun() -> io_lib:fread(id(format), "x") end),
        catching(fun() -> io_lib:fread("~a", lists:duplicate(256, $a)) end),
        catching(fun() -> io_lib:fread("~#~a", "2#1 " ++ lists:duplicate(256, $a)) end),
        catching(fun() -> io_lib:fread("~a~#", lists:duplicate(256, $a) ++ " 2#1") end),
        catching(fun() -> io_lib:fread([$a | id(tail)], "ab") end)
    ]).

continuations() ->
    {more, C1} = io_lib:fread([], "12 ", "~d ~d"),
    {more, C2} = io_lib:fread(C1, "3", "~d ~d"),
    show(continuations, [
        C1,
        C2,
        io_lib:fread(C2, "4\nnext", "ignored"),
        io_lib:fread(C2, eof, "ignored"),
        io_lib:fread([], "5\nrest\n", "~d"),
        io_lib:fread([], "5 6\nrest", "~d"),
        io_lib:fread([], "1\n2\n", "~d~d"),
        io_lib:fread([], "1\r\n2\r\nz", "~d~d"),
        io_lib:fread([], "1\r2", "~d~d"),
        io_lib:fread([], "x\n", "~d"),
        io_lib:fread([], eof, "~d"),
        io_lib:fread([], "7", "~d"),
        chain(["1", " 2", "\n", "3\n"], "~d ~d ~d"),
        chain(["ab", "c", eof], "~s"),
        chain(["ab", "c\r", eof], "~s"),
        chain(["4", eof], "~d ~d"),
        chain(["", "8\n"], "~d"),
        chain(["1\n", eof], "~d~d"),
        chain(["1", "\r", "\n2\n"], "~d~d"),
        io_lib:fread([], [$1, $\n | id(tail)], "~d"),
        io_lib:fread([], "1 2\nx", "~d"),
        catching(fun() -> io_lib:fread(id(bad), "1", "~d") end),
        catching(fun() -> io_lib:fread({"~d", [], 0, []}, id(<<"1\n">>), "~d") end)
    ]).

%% Feed chunks one after another, as an I/O server does; the first {done, ...} or the last {more, ...}.
chain(Chunks, Format) -> chain(Chunks, [], Format).

chain([Chunk | Rest], Cont, Format) ->
    case io_lib:fread(Cont, Chunk, Format) of
        {more, Next} when Rest =/= [] -> chain(Rest, Next, Format);
        Result -> Result
    end.

standard_input() ->
    R1 = io:fread("first> ", "~d ~a"),
    R2 = io:fread(standard_io, "", "~s~d"),
    R3 = io:fread('third> ', "~f"),
    R4 = io:fread("", "~d"),
    R5 = io:fread("", "~d"),
    io:format("~nstandard_input: ~w~n", [[R1, R2, R3, R4, R5]]).

catching(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Values) -> io:format("~s: ~w~n", [Label, Values]).

id(X) -> X.
