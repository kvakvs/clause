-module(re_module).
-export([main/1]).

%% The re module over the pinned PCRE2 (plan step 41ZB): compiling and its
%% errors, single and global matches with every capture shape, empty matches,
%% Unicode subjects, replace and split with their options, and the badarg
%% answers. Non-ASCII text is built from code points, so the source stays ASCII.
main([]) ->
    io:format("~p~n", [compiling()]),
    io:format("~p~n", [single()]),
    io:format("~p~n", [captures()]),
    io:format("~p~n", [global()]),
    io:format("~p~n", [unicode()]),
    io:format("~p~n", [replacing()]),
    io:format("~p~n", [splitting()]),
    io:format("~p~n", [bad()]),
    io:format("re_module: ok~n").

compiling() ->
    {ok, MP} = re:compile("a(b)?(?<name>c)", [caseless]),
    steps([
        fun() -> element(1, MP) end,
        fun() -> element(2, MP) end,
        fun() -> re:inspect(MP, namelist) end,
        fun() -> re:compile("a(") end,
        fun() -> re:compile(<<"[z-a]">>) end,
        fun() -> re:compile("x", [bad_option]) end,
        fun() -> re:compile("x", [global]) end,
        fun() -> element(1, re:compile([$a, <<"b">>, [$c]])) end,
        fun() -> re:run("ABC", MP, [{capture, all, list}]) end
    ]).

single() ->
    steps([
        fun() -> re:run("hello world", "o") end,
        fun() -> re:run("hello world", "z") end,
        fun() -> re:run(<<"hello world">>, <<"w(or)">>) end,
        fun() -> re:run("hello world", "o", [{offset, 5}]) end,
        fun() -> re:run("hello", "^l", [multiline]) end,
        fun() -> re:run("line1\nline2", "^line2$", [multiline]) end,
        fun() -> re:run("abc", "B", [caseless, {capture, first, binary}]) end,
        fun() -> re:run("abc", "b", [{capture, none}]) end,
        fun() -> re:run("abc", "b", [anchored]) end,
        fun() -> re:run("", "x*") end,
        fun() -> re:run("aaa", "(a+)+b", [{match_limit, 10}, report_errors]) end,
        fun() -> re:run("aaa", "(a+)+b", [{match_limit, 10}]) end,
        fun() -> re:run("abc", "(", [report_errors]) end,
        fun() -> re:run([<<"ab">>, "cd"], "bc", [{capture, all, list}]) end
    ]).

captures() ->
    S = "2024-10-14",
    R = "(?<y>\\d+)-(?<m>\\d+)-(?<d>\\d+)",
    steps([
        fun() -> re:run(S, R, [{capture, all, list}]) end,
        fun() -> re:run(S, R, [{capture, all_but_first, binary}]) end,
        fun() -> re:run(S, R, [{capture, all_names, list}]) end,
        fun() -> re:run(S, R, [{capture, [d, "m", <<"y">>, 0], list}]) end,
        fun() -> re:run(S, R, [{capture, [3, 1, 7], index}]) end,
        fun() -> re:run(S, R, [{capture, [unknown], list}]) end,
        fun() -> re:run("ac", "a(b)?(c)", [{capture, all, index}]) end,
        fun() -> re:run("ac", "a(b)?(c)", [{capture, all, binary}]) end,
        fun() -> re:run("ab", "a(b)?(c)?", [{capture, all, list}]) end,
        fun() -> re:run("x2", "(?<n>x1)|(?<n>x2)", [dupnames, {capture, [n], list}]) end,
        fun() -> re:run("abc", "b", [{capture, [], list}]) end
    ]).

global() ->
    steps([
        fun() -> re:run("a1b22c333", "\\d+", [global]) end,
        fun() -> re:run("a1b22c333", "\\d+", [global, {capture, all, list}]) end,
        fun() -> re:run("a1b22c333", "[a-z](\\d)", [global, {capture, all_but_first, binary}]) end,
        fun() -> re:run("a1b22c333", "(\\d)", [global, {capture, first, list}]) end,
        fun() -> re:run("abc", "x*", [global]) end,
        fun() -> re:run("abc", "", [global, {capture, all, list}]) end,
        fun() -> re:run("aXbX", "X?", [global]) end,
        fun() -> re:run("a\r\nb", "(?m)$", [global, {newline, crlf}]) end,
        fun() -> re:run("a\r\nb", "", [global, {newline, crlf}]) end,
        fun() -> re:run("a1b2", "\\d", [global, {offset, 2}]) end,
        fun() -> re:run("abc", "z", [global]) end,
        fun() -> re:run("abc", "b", [global, {capture, none}]) end,
        fun() -> re:run("ABab", "a", [global, caseless, {capture, all, list}]) end,
        fun() -> re:run("x=1 y=2", "(?<k>\\w)=(?<v>\\d)", [global, {capture, all_names, list}]) end
    ]).

unicode() ->
    Text = [16#43F, 16#440, 16#438, 16#432, 16#435, 16#442, $\s, 16#43C, 16#438, 16#440],
    Bytes = unicode:characters_to_binary(Text),
    steps([
        fun() -> re:run(Text, [16#438], [unicode]) end,
        fun() -> re:run(Text, [16#438], [unicode, {capture, all, list}]) end,
        fun() -> re:run(Text, [16#438], [unicode, global, {capture, all, list}]) end,
        fun() -> re:run(Bytes, "\\w+", [unicode, ucp, global, {capture, all, binary}]) end,
        fun() -> re:run(Text, "\\w+", [unicode, ucp, global]) end,
        fun() -> re:run(Text, "", [unicode, global]) end,
        fun() -> re:run(Text, ".", [unicode, {capture, all, list}, {offset, 2}]) end,
        fun() ->
            {ok, MP} = re:compile([16#432, $(, $., $)], [unicode]),
            re:run(Text, MP, [{capture, all, list}])
        end,
        fun() -> re:replace(Text, [16#438], "I", [unicode, global, {return, list}]) end,
        fun() -> re:split(Text, " ", [unicode, {return, list}]) end,
        fun() -> re:run(<<255, 254>>, "x", [unicode]) end
    ]).

replacing() ->
    steps([
        fun() -> re:replace("abcabc", "b", "X") end,
        fun() -> re:replace("abcabc", "b", "X", [{return, list}]) end,
        fun() -> re:replace("abcabc", "b", "X", [global, {return, binary}]) end,
        fun() -> re:replace("abcabc", "(b)(c)", "[\\2\\1&]", [global, {return, list}]) end,
        fun() -> re:replace("abc", "b", "\\\\&\\&", [{return, list}]) end,
        fun() -> re:replace("abc", "(b)", "\\g1\\g{1}x\\9", [{return, list}]) end,
        fun() -> re:replace("abc", "z", "X") end,
        fun() -> re:replace("abc", "", "-", [global, {return, list}]) end,
        fun() -> re:replace("a1b2", "\\d", fun(M, []) -> [M, M] end, [global, {return, list}]) end,
        fun() ->
            re:replace("k=v", "(\\w)=(\\w)(x)?", fun(_, Gs) -> lists:reverse(Gs) end, [
                {return, binary}
            ])
        end,
        fun() -> re:replace("ABC", "b", "x", [caseless, {return, list}]) end,
        fun() -> iolist_to_binary(re:replace(<<"aaa">>, "a", <<"bb">>, [global])) end
    ]).

splitting() ->
    steps([
        fun() -> re:split("a,b,,c,,", ",") end,
        fun() -> re:split("a,b,,c,,", ",", [{return, list}]) end,
        fun() -> re:split("a,b,,c,,", ",", [trim, {return, list}]) end,
        fun() -> re:split("a,b,,c,,", ",", [{parts, 2}, {return, list}]) end,
        fun() -> re:split("a,b,,c,,", ",", [{parts, 0}, {return, list}]) end,
        fun() -> re:split("a1b2c", "(\\d)", [{return, list}]) end,
        fun() -> re:split("a1b2c", "(\\d)", [group, {return, list}]) end,
        fun() -> re:split("abc", "", [{return, list}]) end,
        fun() -> re:split("abc", "x*", [{return, binary}]) end,
        fun() -> re:split("", ",") end,
        fun() -> re:split("abc", "z", [group]) end,
        fun() -> re:split("a-b_c", "(-)|(_)", [{return, list}]) end,
        fun() -> re:split("a,b,", ",", [group, trim, {return, list}]) end,
        fun() -> re:split("AxBXc", "x", [caseless, {return, list}]) end,
        fun() -> re:split("a b  c", " +", [{parts, infinity}, {return, list}]) end
    ]).

bad() ->
    steps([
        fun() -> re:run(1, "a") end,
        fun() -> re:run("a", 1) end,
        fun() -> re:run("a", "a", bad) end,
        fun() -> re:run("a", "a", [{capture, all, text}]) end,
        fun() -> re:run("a", "(", []) end,
        fun() -> re:run("a", "a", [{offset, -1}]) end,
        fun() ->
            {ok, MP} = re:compile("a"),
            re:run("a", MP, [caseless])
        end,
        fun() -> re:replace("a", "a", "b", [{capture, all}]) end,
        fun() -> re:replace("a", "a", "b", [{return, atom}]) end,
        fun() -> re:split("a", "a", [global]) end,
        fun() -> re:split("a", "(", []) end,
        fun() -> re:split("a", "a", [{parts, -1}]) end
    ]).

steps(Steps) -> [outcome(Step) || Step <- Steps].

outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
