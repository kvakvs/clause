-module(console).
-export([main/1]).

-record(point, {x, y}).

%% io:format/1,2 and io:put_chars/1 on standard output: every supported
%% control sequence with field widths, precisions, pads and modifiers,
%% ~p line breaking, Unicode text and the badarg cases.
main([]) ->
    io:format("plain text~n"),
    io:format(<<"binary format ~w~n">>, [ok]),
    io:format('atom format~n'),
    [show(F, A) || {F, A} <- id(directives())],
    [show(F, A) || {F, A} <- id(errors())],
    pretty(),
    unicode(),
    put_chars(),
    values(),
    large();
main(["nesting", Depth]) ->
    io:format("~0p~n", [nest(list_to_integer(Depth))]).

id(X) -> X.

%% N copies of X.
dup(N, X) -> [X || _ <- lists:seq(1, N)].

%% One format call followed by "|", or the class and reason it raised.
show(Format, Args) ->
    try io:format(id(Format), id(Args)) of
        ok -> io:put_chars("|\n")
    catch
        Class:Reason -> io:format("~w:~w~n", [Class, Reason])
    end.

directives() ->
    [
        {"~w", [atom]},
        {"~w", [{1, -2, 3.5, [a | b], "str", <<1, 2>>, <<5:3>>}]},
        {"~w", [#{1 => one, 2 => [two]}]},
        {"~w", ['quoted atom']},
        {"~w", [123456789012345678901234567890]},
        {"~w", [fun lists:map/2]},
        {"~10w", [abc]},
        {"~-10w", [abc]},
        {"~2w", [abcdef]},
        {"~.2w", [abcdef]},
        {"~10.2w", [abcdef]},
        {"~10..w", [abc]},
        {"~*w", [6, x]},
        {"~*w", [-6, x]},
        {"~-*w", [6, x]},
        {"~.*w", [3, abcdef]},
        {"~10.*w", [2, abcdef]},
        {"~10.3.*w", [$!, abcdef]},
        {"~s", ["string"]},
        {"~s", [<<"binary">>]},
        {"~s", [atom]},
        {"~s", [["deep", [$l, "ist"], <<"bin">> | <<"tail">>]]},
        {"~10s", ["abc"]},
        {"~-10s", ["abc"]},
        {"~2s", ["abcdef"]},
        {"~.2s", ["abcdef"]},
        {"~.10s", ["abc"]},
        {"~10.3s", ["abcdef"]},
        {"~10.8s", ["abc"]},
        {"~-10.8.xs", ["abc"]},
        {"~6.6s", ["abc"]},
        {"~6.6s", ["abcdefgh"]},
        {"~c", [$a]},
        {"~5c", [$a]},
        {"~.3c", [$a]},
        {"~5.3c", [$a]},
        {"~-5.3c", [$a]},
        {"~5.3.-c", [$a]},
        {"~c", [256 + $a]},
        {"~c", [-1]},
        {"~b", [255]},
        {"~b", [-255]},
        {"~.16b", [255]},
        {"~.16B", [255]},
        {"~.2B", [10]},
        {"~.36B", [123456789]},
        {"~.36b", [123456789]},
        {"~8.16.0B", [255]},
        {"~-8.16.0B", [255]},
        {"~2B", [12345]},
        {"~B", [123456789012345678901234567890]},
        {"~.16b", [-123456789012345678901234567890]},
        {"~~", []},
        {"~5~", []},
        {"a~nb", []},
        {"~3n", []},
        {"~i", [ignored]},
        {"~p", [{ok, "text"}]},
        {"~lp", [{ok, "text"}]},
        {"~kp", [#{3 => c, 1 => a, 2 => b}]},
        {"~10p", [[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]]},
        {"~0p", [lists:seq(1, 40)]},
        {"~.40p", [lists:seq(1, 25)]},
        {"tab\t~p", [lists:seq(1, 30)]},
        {"tab\tx\t~p", [lists:seq(1, 30)]},
        {"line~nnext ~p", [lists:seq(1, 30)]},
        {["list ", "format ", $~, $w], [x]},
        {[$a, <<"bin">>, "~w"], []},
        {"~w ~w", [1, 2]}
    ].

errors() ->
    [
        {"~z", []},
        {"~w", []},
        {"~w", [a, b]},
        {"x", notlist},
        {"~w", [a | b]},
        {"~", []},
        {"~5", [x]},
        {"~s", [1]},
        {"~s", [[256]]},
        {"~s", [[a]]},
        {"~s", [["ok" | tail]]},
        {"~s", [<<1:3>>]},
        {"~b", [a]},
        {"~b", [1.0]},
        {"~.1b", [1]},
        {"~.37b", [1]},
        {"~c", [a]},
        {"~tc", [-1]},
        {"~tc", [16#110000]},
        {"~tc", [16#D800]},
        {"~2.5s", ["abc"]},
        {"~2.5c", [$a]},
        {"~-3n", []},
        {"~-5p", [a]},
        {"~*w", [x, a]},
        {"~.*w", [x, a]},
        {[$~, "w"], [a]},
        {[-1], []},
        {[16#D800], []},
        {[a], []},
        {12, []},
        {"~w", x}
    ].

%% Valid OTP control sequences Clause does not implement (docs/libraries/io.md).
%% Tuples nested `Depth` deep around an atom.
nest(0) -> leaf;
nest(Depth) -> {Depth, nest(Depth - 1)}.

%% ~p line breaking: what fits is written whole, the rest breaks between
%% elements, tagged tuples and maps choose their indentation.
pretty() ->
    Long = lists:seq(1, 50),
    Words = [list_to_atom("word" ++ integer_to_list(N)) || N <- lists:seq(1, 30)],
    Nested =
        {config, [
            {name, "server"},
            {port, 8080},
            {hosts, ["alpha", "beta", "gamma", "delta"]},
            {options, Words}
        ]},
    Map = #{1 => Long, 2 => {tag, Words}, 3 => "short", 4 => #{5 => [deep, Words]}},
    Terms = [
        Long,
        Words,
        Nested,
        Map,
        {Long, Long},
        {tagged_with_a_very_long_tag_name_that_is_wide, Long, Words},
        [Long | tail],
        [{N, Words} || N <- [1, 2]],
        #point{x = Long, y = Words},
        list_to_binary(lists:seq(0, 120)),
        list_to_binary(dup(100, $x)),
        <<"short binary">>,
        <<1, 2, 3, 4:4>>,
        list_to_binary(lists:seq(1, 60) ++ [5]),
        ["a string", "another string", "strings are written whole when they fit", "x"],
        [dup(90, $s)],
        "string with \"quotes\", \\backslash, \t tab and \n newline",
        [[[[[[[[[[Long]]]]]]]]]],
        {{{{{{{{{{Words}}}}}}}}}},
        #{Long => Words},
        #{{key, Long} => {value, Words}},
        [#{}, {}, [], <<>>, ''],
        {a, [x | y], {b, [Long | Long]}}
    ],
    [io:format("~p~n", [T]) || T <- Terms],
    [io:format("prefix: ~p~n", [T]) || T <- [Nested, Map]],
    [io:format("~40p~n", [T]) || T <- [Nested, Words]],
    [io:format("~.20p~n", [T]) || T <- [Nested, Words]],
    io:format("~p ~p~n", [Long, Words]),
    io:format("~lp~n", [[lists:seq($a, $z), lists:seq($a, $z), lists:seq($a, $z)]]),
    io:format("~w~n", [Nested]).

unicode() ->
    Cyrillic = [1055, 1088, 1080, 1074, 1077, 1090],
    io:format("~ts|~n", [Cyrillic]),
    io:format("~ts|~n", [unicode_binary(Cyrillic)]),
    io:format("~ts|~n", [<<228, 246>>]),
    io:format("~s|~n", [[228, 246]]),
    io:format("~s|~n", [unicode_binary([228])]),
    io:format("~10ts|~n", [Cyrillic]),
    io:format("~-10.3ts|~n", [Cyrillic]),
    io:format("~tc~tc|~n", [1055, 8364]),
    io:format("~c|~n", [1055]),
    io:format("~p ~tp~n", [Cyrillic, Cyrillic]),
    io:format("~p ~tp~n", [[228, 246], [228, 246]]),
    io:format("~p ~tp~n", [unicode_binary([228]), unicode_binary([228])]),
    io:format("~p ~tp~n", [unicode_binary(Cyrillic), unicode_binary(Cyrillic)]),
    io:format("~p ~tp~n", [<<228, 246>>, <<228, 246>>]),
    Atom = list_to_atom(Cyrillic),
    io:format("~w ~tw ~p ~tp ~s ~ts~n", [Atom, Atom, Atom, Atom, x, Atom]),
    io:format("~tp~n", [{Atom, [Atom, Cyrillic, lists:seq(1, 25)]}]),
    io:format("~p~n", [list_to_atom([228, 246])]),
    io:format(Cyrillic ++ " ~w~n", [format]),
    io:format(<<(unicode_binary(Cyrillic))/binary, " bytes~n">>),
    io:format([Cyrillic, " nested~n"]),
    io:format("~n"),
    show("~s", [[1055]]),
    show("~s", [Atom]),
    show("~ts", [[16#110000]]).

%% A UTF-8 binary of the code points.
unicode_binary(Chars) -> <<<<C/utf8>> || C <- Chars>>.

put_chars() ->
    io:put_chars("put_chars\n"),
    io:put_chars(<<"binary\n">>),
    io:put_chars(["deep", [$\s, <<"chardata">>], $\n | <<"tail\n">>]),
    io:put_chars([[1055, 1088, 1080], unicode_binary([1074, 1077, 1090]), $\n]),
    io:put_chars([]),
    [
        try io:put_chars(id(X)) of
            R -> io:format("~w~n", [R])
        catch
            C:E -> io:format("~w:~w~n", [C, E])
        end
     || X <- [abc, [-1], <<228>>, [16#D800], [16#110000], ["x" | y], <<1:3>>, [<<1:3>>], [1.0], 12]
    ].

%% io functions as values and through dynamic calls.
values() ->
    Format = fun io:format/2,
    Format("fun ~w~n", [call]),
    io:format("~p~n", [Format]),
    apply(io, put_chars, ["apply/3\n"]),
    Module = id(io),
    Module:format("dynamic ~s~n", ["call"]),
    erlang:apply(fun io:format/1, ["fun/1~n"]),
    try Format(x, []) of
        R -> R
    catch
        Class:Reason -> io:format("~w:~w~n", [Class, Reason])
    end,
    [ok, ok] = [io:format(""), io:put_chars(<<>>)].

%% Long output: a 10,000-element list and a long string.
large() ->
    io:format("~w~n", [lists:seq(1, 10000)]),
    io:format("~p~n", [lists:seq(1, 2000)]),
    io:format("~s~n", [dup(5000, $z)]),
    io:put_chars([dup(5000, $y), $\n]).
