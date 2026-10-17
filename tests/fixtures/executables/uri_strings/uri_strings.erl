-module(uri_strings).
-export([main/1]).

%% uri_string (plan 21 step LIB5): parsing with OTP's error fragments, recomposition, normalization, RFC 3986's
%% resolution examples, transcoding, percent coding and form-urlencoded queries, in list and binary forms; maps
%% print in key order.
main(_) ->
    Uris = [
        "foo://user@example.com:8042/over/there?name=ferret#nose",
        <<"foo://user@example.com:8042/over/there?name=ferret#nose">>,
        "urn:example:animal:ferret:nose",
        "mailto:a@b.c",
        "//host",
        "//user@host:80",
        "/path/a?q#f",
        "?q",
        "#f",
        "",
        <<>>,
        "rel/path",
        "a:b",
        "1a:b",
        "http://[::1]:8080/x",
        "http://[::1",
        "http://[zz]/",
        "http://[::1]x",
        "http://1.2.3.4/",
        "http://1.2.3.400/",
        "http://1.2.3/",
        "http://host:65536",
        "http://host:123456",
        "http://host:/p",
        "http://host:8a",
        "http://user@bad host",
        "http://a b",
        "http://ex.com/a b",
        "foo:",
        "foo:/",
        "foo://",
        "foo://@",
        "foo://u@",
        "http://h?#",
        "http://h#a#b",
        "%zz",
        "http://h/%E4",
        <<"http://h/ä"/utf8>>,
        "http://ex.com:8080?q",
        "s://h/p/../q",
        <<"//[v1.x]">>,
        "a:%",
        ":x",
        "-x:y",
        "x@y:z"
    ],
    show(parse, [uri_string:parse(U) || U <- Uris]),
    show(parse_crash, [
        reason(fun() -> uri_string:parse(<<255>>) end), reason(fun() -> uri_string:parse(id(x)) end)
    ]),
    show(recompose, [
        recompose(M)
     || M <- [
            #{
                scheme => "foo",
                host => "example.com",
                port => 8042,
                path => "/over/there",
                query => "name=ferret",
                fragment => "nose"
            },
            #{
                scheme => <<"foo">>,
                userinfo => <<"u">>,
                host => <<"h">>,
                path => <<"/p">>,
                query => <<"q">>
            },
            #{path => <<>>},
            #{path => ""},
            #{host => <<"h">>, path => <<"rel">>},
            #{host => "h", path => "rel"},
            #{scheme => "a b", path => ""},
            #{scheme => "", path => ""},
            #{scheme => <<>>, path => <<>>},
            #{host => "::1", path => ""},
            #{host => <<"::1">>, path => <<>>},
            #{host => "1.2.3.4", path => ""},
            #{host => "h st", path => ""},
            #{host => "", path => ""},
            #{userinfo => "u s", host => "h", path => "/"},
            #{path => "a b?"},
            #{path => "ä"},
            #{query => "a b", path => ""},
            #{fragment => "x"},
            #{path => "", foo => 1},
            #{scheme => <<"http">>, host => "h", path => <<"/p">>},
            #{scheme => "http", host => <<"h">>, path => "/p", fragment => <<"f">>},
            #{port => undefined, host => "h", path => ""},
            #{host => "h", path => [<<"a">>, "b"]},
            #{path => "", port => undefined},
            #{host => "h", path => "", port => x},
            #{path => "", scheme => [[$a]]},
            not_a_map
        ]
    ]),
    show(normalize, [
        uri_string:normalize(U)
     || U <- [
            "HTTP://www.Example.com:80/a/./b/../c/%7euser",
            <<"https://h:443">>,
            "ftp://h:21/",
            "http://h:/",
            "s://h/%41%2f",
            "http://h/%zz",
            "http://h/%C3%A4",
            "http://h/%ff",
            <<"/a/b/../../../c">>,
            "mailto:A@B",
            "tftp://h:69",
            "ssh://h:22",
            "sftp://h:23",
            "bad uri",
            "http://h/a/..",
            "http://h/a/.",
            "../a/./b",
            ".",
            "HTTP://[::A]/",
            #{scheme => "HTTP", host => "H", path => "", port => 80},
            #{path => "a", port => undefined}
        ]
    ]),
    show(normalize_map, [
        uri_string:normalize("HTTP://H:80/%7e", [return_map]),
        uri_string:normalize(#{host => <<"H">>, path => <<"/%7E%41">>}, [return_map]),
        uri_string:normalize("http://h/%zz", [return_map]),
        reason(fun() -> uri_string:normalize("h", [other]) end),
        reason(fun() -> uri_string:normalize(#{host => 1, path => ""}) end)
    ]),
    Base = "http://a/b/c/d;p?q",
    References = [
        "g:h",
        "g",
        "./g",
        "g/",
        "/g",
        "//g",
        "?y",
        "g?y",
        "#s",
        "g#s",
        "g?y#s",
        ";x",
        "g;x",
        "g;x?y#s",
        "",
        ".",
        "./",
        "..",
        "../",
        "../g",
        "../..",
        "../../",
        "../../g",
        "../../../g",
        "../../../../g",
        "/./g",
        "/../g",
        "g.",
        ".g",
        "g..",
        "..g",
        "./../g",
        "./g/.",
        "g/./h",
        "g/../h",
        "g;x=1/./y",
        "g;x=1/../y",
        "g?y/./x",
        "g?y/../x",
        "g#s/./x",
        "g#s/../x",
        "http:g"
    ],
    show(resolve, [uri_string:resolve(R, Base) || R <- References]),
    show(resolve_forms, [
        uri_string:resolve(<<"../g">>, <<"http://a/b/c/d;p?q">>),
        uri_string:resolve(<<"g">>, Base),
        uri_string:resolve("g", uri_string:parse(Base)),
        uri_string:resolve(#{path => <<"g">>}, Base, [return_map]),
        uri_string:resolve("g", "http://a"),
        uri_string:resolve(<<"g">>, "http://a"),
        uri_string:resolve("g", "foo:bar"),
        uri_string:resolve("g", "foo:bar/baz"),
        uri_string:resolve("g", "/no/scheme"),
        uri_string:resolve("g", #{path => "/x"}),
        uri_string:resolve("g", "bad base"),
        uri_string:resolve("bad ref", Base),
        uri_string:resolve("?q2", "http://a/b?q"),
        uri_string:resolve("", "http://a/b?q#f"),
        uri_string:resolve("//h2/p/../q", "s://u@a:1/b"),
        uri_string:resolve("s2:/x/../y", Base),
        reason(fun() -> uri_string:resolve("g", Base, [other]) end)
    ]),
    show(transcode, [
        uri_string:transcode(<<"foo%00%00%00%F6bar"/utf32>>, [
            {in_encoding, utf32}, {out_encoding, utf8}
        ]),
        uri_string:transcode("foo%F6bar", [{in_encoding, latin1}, {out_encoding, utf8}]),
        uri_string:transcode(<<"foo%C3%B6bar">>, [{in_encoding, utf8}, {out_encoding, latin1}]),
        uri_string:transcode(["a", <<"%41">>, [$%, "42"]], []),
        uri_string:transcode("x%4", []),
        uri_string:transcode("%41%zz", []),
        uri_string:transcode(<<"%F6">>, [{in_encoding, utf8}]),
        uri_string:transcode("abc", []),
        reason(fun() -> uri_string:transcode("", []) end),
        reason(fun() -> uri_string:transcode(<<255>>, []) end),
        reason(fun() -> uri_string:transcode(["a" | b], []) end)
    ]),
    show(compose_query, [
        uri_string:compose_query([{"foo bar", "1"}, {"city", "örebro"}]),
        uri_string:compose_query([{<<"foo bar">>, <<"1">>}, {<<"city">>, <<"örebro"/utf8>>}]),
        uri_string:compose_query([{<<"key">>, true}, {<<"k">>, <<"v">>}]),
        uri_string:compose_query([{<<"a">>, "b"}]),
        uri_string:compose_query([{"ŝ", "*-._~!"}], [{encoding, latin1}]),
        uri_string:compose_query([{<<"ŝ"/utf8>>, <<"x">>}], [{encoding, latin1}]),
        uri_string:compose_query([{"a", "b"}], [{encoding, utf16}]),
        uri_string:compose_query([{"a", "b"}], []),
        uri_string:compose_query([{a, "b"}]),
        uri_string:compose_query([]),
        uri_string:compose_query([{"a", "b"}], [{encoding, unicode}]),
        reason(fun() -> uri_string:compose_query([x]) end)
    ]),
    show(dissect_query, [
        uri_string:dissect_query(Q)
     || Q <- [
            "foo+bar=1&city=%C3%B6rebro",
            <<"foo+bar=1&city=%26%2316%3B">>,
            "a&b=&c=d",
            "=x",
            "a=%zz",
            <<"a=%zz">>,
            "a=&#228;",
            "&#228;=1",
            <<"&#228;=1">>,
            "a=b=c",
            "&",
            "a&&b",
            "",
            <<>>,
            <<"x=", 255>>,
            "a=%4"
        ]
    ]),
    show(dissect_crash, [
        reason(fun() -> uri_string:dissect_query("&#12") end),
        reason(fun() -> uri_string:dissect_query(id(x)) end)
    ]),
    show(percent_decode, [
        reason(fun() -> uri_string:percent_decode(U) end)
     || U <- [
            "a%20b",
            <<"%C3%A4">>,
            "%zz",
            <<"%zz">>,
            <<"%ff">>,
            "%ff",
            "%4",
            #{path => "%20", host => <<"h">>, port => 1},
            #{path => "%zz"},
            #{path => <<"%zz">>},
            #{query => "%ff", scheme => "%20"}
        ]
    ]),
    show(quote, [
        uri_string:quote("a b/c"),
        uri_string:quote(<<"a b">>),
        uri_string:quote("a/b", "/"),
        uri_string:quote("ä"),
        uri_string:quote("ä", "ä"),
        uri_string:quote(<<"~-._">>),
        uri_string:unquote("%41%20"),
        reason(fun() -> uri_string:unquote(<<"%E4">>) end),
        uri_string:unquote("%zz"),
        reason(fun() -> uri_string:quote(<<255>>) end),
        reason(fun() -> uri_string:unquote(<<"%zz">>) end)
    ]),
    show(classes, [
        uri_string:allowed_characters(),
        [uri_string:is_host(C) || C <- ":/a%"],
        [uri_string:is_path(C) || C <- "/?a%"]
    ]),
    io:format("uri_strings: ok~n").

recompose(Map) -> reason(fun() -> uri_string:recompose(Map) end).

id(Value) -> Value.

show(Name, Values) -> io:format("~w: ~kw~n", [Name, Values]).

reason(Fun) ->
    try Fun() of
        Value -> Value
    catch
        error:Reason -> {error, Reason};
        throw:Thrown -> {thrown, Thrown}
    end.
