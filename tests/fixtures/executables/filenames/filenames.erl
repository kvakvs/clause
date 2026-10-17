-module(filenames).
-export([main/1, family_results/0]).

%% filename (plan 21 step LIB5): names whose answers agree on Unix and Windows are shown, in list, deep list, atom
%% and binary forms; the answers that follow the host's rules (drive letters, backslashes, volume-relative names)
%% are compared with each family's table. basedir/3 runs under a fixed environment.
main(_) ->
    Names = [
        "a/b/c.erl",
        "a/b/",
        "a",
        "",
        ".",
        "..",
        ".emacs",
        "a/.emacs",
        "a.b/c",
        "a/b.c.d",
        "dir.ext/",
        "a//b///c",
        ["a", [$/, "b"], c, ".erl"],
        'x/y.z',
        <<"a/b/c.erl">>,
        <<"a/b/">>,
        <<>>,
        <<".emacs">>,
        <<"a.b/c">>,
        <<"x.tar.gz">>,
        <<"a//b/">>
    ],
    show(basename, [filename:basename(N) || N <- Names]),
    show(dirname, [filename:dirname(N) || N <- Names]),
    show(extension, [filename:extension(N) || N <- Names]),
    show(rootname, [filename:rootname(N) || N <- Names]),
    show(split, [filename:split(N) || N <- Names]),
    show(join1, [filename:join([N]) || N <- Names]),
    show(pathtype, [filename:pathtype(N) || N <- Names]),
    show(with_extension, [
        filename:basename("a/b/c.erl", ".erl"),
        filename:basename("a/b/c.erl", "erl"),
        filename:basename("c.erl", ".beam"),
        filename:basename(<<"a/b.erl">>, ".erl"),
        filename:basename("a/b.erl", <<".erl">>),
        filename:basename(<<"b.erl">>, <<"b.erl.x">>),
        filename:basename("a/b/", "b"),
        filename:basename("a.erl/", ".erl"),
        filename:rootname("a/b.erl", ".erl"),
        filename:rootname("a/.erl", ".erl"),
        filename:rootname(".erl", ".erl"),
        filename:rootname("a.erl.erl", ".erl"),
        filename:rootname(<<"x.erl">>, ".erl"),
        filename:rootname("x.erl", <<".erl">>),
        filename:rootname("x.beam", ".erl")
    ]),
    show(join, [
        filename:join(["a", "b"]),
        filename:join(["a/", "/b"]),
        filename:join(["a", "./b"]),
        filename:join(["a/./b"]),
        filename:join(["./a", "."]),
        filename:join(["a//b/"]),
        filename:join("a", 'b'),
        filename:join(a, "b"),
        filename:join(<<"a">>, "b"),
        filename:join("a", <<"b/">>),
        filename:join(["a", "b", "c/d"]),
        filename:join([<<"x">>, <<"y">>, "z"]),
        filename:join(["a", ""]),
        filename:join(["", "a"]),
        filename:join([["a", [b]], "c"]),
        filename:absname_join("base", ["x", "y"]),
        filename:absname("a/b", "/base"),
        filename:absname(<<"a">>, "/base"),
        filename:absname("../x", <<"/b/c">>),
        filename:append("a", "b"),
        filename:append(<<"a">>, "b"),
        filename:append("a", <<"b">>),
        filename:flatten([a, "b", [<<"c">>, [$d]]]),
        filename:flatten(<<"x">>),
        filename:flatten(x)
    ]),
    show(validate, [
        filename:validate(N)
     || N <- ["abc", "", [0], <<>>, <<"a", 0>>, [a, "b"], [[], []], <<"ok">>, [-1]]
    ]),
    show(absname_shape, [
        filename:pathtype(filename:absname("a")),
        lists:suffix("/a", filename:absname("a")),
        is_binary(filename:absname(<<"a">>))
    ]),
    show(basedir, basedirs()),
    Family = element(1, os:type()),
    case family_results() =:= expected(Family) of
        true -> show(family, matches);
        false -> show(family, {Family, family_results()})
    end,
    show(errors, [
        reason(fun() -> filename:basename(id(1)) end),
        reason(fun() -> filename:dirname(id(1)) end),
        reason(fun() -> filename:extension(id(1)) end),
        reason(fun() -> filename:join(id([])) end),
        reason(fun() -> filename:join(id(1), id(2)) end),
        reason(fun() -> filename:pathtype(id(1)) end),
        reason(fun() -> filename:split(id(1)) end),
        reason(fun() -> filename:join(<<"a">>, [16#110000]) end),
        reason(fun() -> filename:rootname(<<"a">>, [-1]) end),
        reason(fun() -> filename:basedir(foo, "a", #{os => linux}) end),
        reason(fun() -> filename:basedir(foo, "a", #{os => windows}) end),
        reason(fun() -> filename:basedir(user_data, id(a)) end)
    ]),
    odd(),
    io:format("filenames: ok~n").

%% Unusual names, answered alike on both families: nested lists and atoms, improper tails, binaries and big
%% integers inside lists.
odd() ->
    Names = [
        [["a", [$/]], b, ".c"],
        ['a/b', [[]], "c"],
        ["a" | b],
        ["a" | 5],
        ["a", <<"b">>],
        [<<"a/b">>],
        [1 bsl 70, $/, $x],
        [[$a | b], "c"],
        "a/.b.c",
        ".a.b",
        "a.b/.c",
        "a/"
    ],
    Functions = [basename, dirname, extension, rootname, split, pathtype],
    show(odd, [[reason(fun() -> filename:F(N) end) || F <- Functions] || N <- Names]),
    show(odd_join, [reason(fun() -> filename:join(N) end) || N <- Names ++ [[N || N <- Names]]]),
    show(odd_two, [
        reason(fun() -> F() end)
     || F <- [
            fun() -> filename:join("a", 5) end,
            fun() -> filename:join(["a", <<"b">>, c]) end,
            fun() -> filename:join([["a"], ["/b"]]) end,
            fun() -> filename:join(["a" | b]) end,
            fun() -> filename:join([[$a | b]]) end,
            fun() -> filename:append(a, "b") end,
            fun() -> filename:append("a", 5) end,
            fun() -> filename:basename("a/b/c", [$c]) end,
            fun() -> filename:basename(<<"x">>, x) end,
            fun() -> filename:basename(x, x) end,
            fun() -> filename:basename("a/b.erl", [[$.], erl]) end,
            fun() -> filename:rootname("a.b", [$., <<"b">>]) end,
            fun() -> filename:rootname(<<"a.b.c">>, <<".c">>) end,
            fun() -> filename:rootname(<<"a.b">>, b) end,
            fun() -> filename:validate(["a" | $b]) end,
            fun() -> filename:validate(['', ""]) end,
            fun() -> filename:validate([<<"a">>]) end,
            fun() -> filename:validate(["a" | 5.0]) end,
            fun() -> filename:flatten(["a" | 5]) end,
            fun() -> filename:flatten(5) end,
            fun() -> filename:absname_join(<<"/b">>, <<"x">>) end,
            fun() -> filename:pathtype([[], "x"]) end,
            fun() -> filename:pathtype([<<"/">>]) end
        ]
    ]).

%% basedir/3 for every type and system, under a fixed HOME, XDG and APPDATA environment.
basedirs() ->
    true = os:putenv("HOME", "/home/u"),
    true = os:putenv("XDG_CONFIG_HOME", "/xdg/config"),
    true = os:putenv("XDG_DATA_DIRS", "/d1::/d2"),
    true = os:putenv("APPDATA", "/appdata/roaming"),
    [
        true = os:unsetenv(V)
     || V <- ["XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_CONFIG_DIRS", "LOCALAPPDATA"]
    ],
    Types = [user_cache, user_config, user_data, user_log, site_config, site_data],
    Options = [#{}, #{version => "1.0"}, #{author => "me", version => "2"}, #{author => "me"}],
    First = [
        filename:basedir(T, "app", O#{os => Os})
     || Os <- [linux, darwin, windows], T <- Types, O <- Options
    ],
    true = os:putenv("LOCALAPPDATA", "/appdata/local"),
    Local = [filename:basedir(T, <<"app">>, #{os => windows}) || T <- Types],
    true = os:unsetenv("APPDATA"),
    Home = [filename:basedir(T, "app", #{os => windows, author => "me"}) || T <- Types],
    First ++ Local ++ Home ++ [is_list(filename:basedir(user_cache, "app"))].

%% Answers that follow the host family's rules.
family_results() ->
    Names = [
        "/usr/lib",
        "//host/share/x",
        "\\a\\b.c",
        "c:/x/y",
        "C:\\X\\y.z",
        "c:",
        "c:x",
        "D:",
        "/",
        "a\\b",
        "a/b\\c.d",
        <<"C:\\a\\b">>,
        <<"//h/s">>,
        <<"/a/b">>,
        <<"c:x">>,
        <<"a\\b.c">>
    ],
    [
        {
            filename:pathtype(N),
            filename:basename(N),
            filename:dirname(N),
            filename:extension(N),
            filename:rootname(N),
            filename:split(N),
            filename:join([N]),
            filename:nativename(N)
        }
     || N <- Names
    ] ++
        [
            filename:absname("/x", "c:/base"),
            filename:absname("c:y", "c:/base"),
            filename:absname("/x", "/base"),
            filename:absname(<<"/x">>, <<"d:/base">>),
            filename:join("c:/a", "b"),
            filename:join("a", "c:b"),
            filename:join(<<"//h">>, <<"x">>),
            filename:basename("c:/x.y", ".y"),
            filename:validate([16#D800]),
            filename:validate([16#10000])
        ].

%% Each family's answers, observed from OTP (the Unix table from its filename code with os:type() fixed).
expected(win32) ->
    [
        {volumerelative, "lib", "/usr", [], "/usr/lib", ["/", "usr", "lib"], "/usr/lib",
            "\\usr\\lib"},
        {absolute, "x", "//host/share", [], "//host/share/x", ["//", "host", "share", "x"],
            "//host/share/x", "\\\\host\\share\\x"},
        {volumerelative, "b.c", "/a", ".c", "/a/b", ["/", "a", "b.c"], "/a/b.c", "\\a\\b.c"},
        {absolute, "y", "c:/x", [], "c:/x/y", ["c:/", "x", "y"], "c:/x/y", "c:\\x\\y"},
        {absolute, "y.z", "C:/X", ".z", "C:/X/y", ["c:/", "X", "y.z"], "c:/X/y.z", "c:\\X\\y.z"},
        {volumerelative, [], "c:", [], "c:", ["c:"], "c:", "c:"},
        {volumerelative, "x", "c:", [], "c:x", ["c:", "x"], "c:x", "c:x"},
        {volumerelative, [], "D:", [], "D:", ["d:"], "d:", "d:"},
        {volumerelative, [], "/", [], "/", ["/"], "/", "\\"},
        {relative, "b", "a", [], "a/b", ["a", "b"], "a/b", "a\\b"},
        {relative, "c.d", "a/b", ".d", "a/b/c", ["a", "b", "c.d"], "a/b/c.d", "a\\b\\c.d"},
        {absolute, <<"b">>, <<"C:/a">>, <<>>, <<"C:/a/b">>, [<<"c:/">>, <<"a">>, <<"b">>],
            <<"c:/a/b">>, <<"c:\\a\\b">>},
        {absolute, <<"s">>, <<"//h">>, <<>>, <<"//h/s">>, [<<"//">>, <<"h">>, <<"s">>], <<"//h/s">>,
            <<"\\\\h\\s">>},
        {volumerelative, <<"b">>, <<"/a">>, <<>>, <<"/a/b">>, [<<"/">>, <<"a">>, <<"b">>],
            <<"/a/b">>, <<"\\a\\b">>},
        {volumerelative, <<"x">>, <<"c:">>, <<>>, <<"c:x">>, [<<"c:">>, <<"x">>], <<"c:x">>,
            <<"c:x">>},
        {relative, <<"b.c">>, <<"a">>, <<".c">>, <<"a/b">>, [<<"a">>, <<"b.c">>], <<"a/b.c">>,
            <<"a\\b.c">>},
        "c:/x",
        "c:/base/y",
        "/x",
        <<"d:/x">>,
        "c:/a/b",
        "c:b",
        <<"//h/x">>,
        "x",
        true,
        false
    ];
expected(unix) ->
    [
        {absolute, "lib", "/usr", [], "/usr/lib", ["/", "usr", "lib"], "/usr/lib", "/usr/lib"},
        {absolute, "x", "//host/share", [], "//host/share/x", ["/", "host", "share", "x"],
            "/host/share/x", "/host/share/x"},
        {relative, "\\a\\b.c", ".", ".c", "\\a\\b", ["\\a\\b.c"], "\\a\\b.c", "\\a\\b.c"},
        {relative, "y", "c:/x", [], "c:/x/y", ["c:", "x", "y"], "c:/x/y", "c:/x/y"},
        {relative, "C:\\X\\y.z", ".", ".z", "C:\\X\\y", ["C:\\X\\y.z"], "C:\\X\\y.z", "C:\\X\\y.z"},
        {relative, "c:", ".", [], "c:", ["c:"], "c:", "c:"},
        {relative, "c:x", ".", [], "c:x", ["c:x"], "c:x", "c:x"},
        {relative, "D:", ".", [], "D:", ["D:"], "D:", "D:"},
        {absolute, [], "/", [], "/", ["/"], "/", "/"},
        {relative, "a\\b", ".", [], "a\\b", ["a\\b"], "a\\b", "a\\b"},
        {relative, "b\\c.d", "a", ".d", "a/b\\c", ["a", "b\\c.d"], "a/b\\c.d", "a/b\\c.d"},
        {relative, <<"C:\\a\\b">>, <<".">>, <<>>, <<"C:\\a\\b">>, [<<"C:\\a\\b">>], <<"C:\\a\\b">>,
            <<"C:\\a\\b">>},
        {absolute, <<"s">>, <<"//h">>, <<>>, <<"//h/s">>, [<<"/">>, <<"h">>, <<"s">>], <<"/h/s">>,
            <<"/h/s">>},
        {absolute, <<"b">>, <<"/a">>, <<>>, <<"/a/b">>, [<<"/">>, <<"a">>, <<"b">>], <<"/a/b">>,
            <<"/a/b">>},
        {relative, <<"c:x">>, <<".">>, <<>>, <<"c:x">>, [<<"c:x">>], <<"c:x">>, <<"c:x">>},
        {relative, <<"a\\b.c">>, <<".">>, <<".c">>, <<"a\\b">>, [<<"a\\b.c">>], <<"a\\b.c">>,
            <<"a\\b.c">>},
        "/x",
        "c:/base/c:y",
        "/x",
        <<"/x">>,
        "c:/a/b",
        "a/c:b",
        <<"/h/x">>,
        "x",
        false,
        true
    ].

id(Value) -> Value.

show(Name, Values) -> io:format("~w: ~w~n", [Name, Values]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.
