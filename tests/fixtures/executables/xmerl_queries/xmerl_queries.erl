%% XPath over scanned XML (plan 21 step MIM3b, docs/libraries/xmerl.md#xmerl_xpath): xmerl_xpath:string/2,3 and
%% xmerl_xs:select/2 over documents, elements and other nodes, with xmerl's own answers (steps joined over their
%% context nodes, predicates over a step's whole result, unions in term order, string values of own text, equality
%% on first nodes, values OTP does not evaluate). The xmlbase of the current directory prints as "$CWD"; the functions
%% OTP's xmerl fails in (id, string-length, normalize-space, sum, round) are left out.
-module(xmerl_queries).
-export([main/1]).

main(_) ->
    {ok, Cwd} = file:get_cwd(),
    D1 =
        "<r xmlns:p=\"urn:p\" id=\"1\"><a k=\"x\">t1<b>u</b></a><a k=\"y\"><b>v</b><b>w</b></a>"
        "<!--c--><?pi d?><p:c>z</p:c><d><a k=\"z\"/></d></r>",
    D3 =
        "<r xmlns:p=\"urn:p\" id=\"1\"><a k=\"x\">t1<b>u</b>t2</a><a k=\"y\" n=\"2\"><b>v</b><b>w</b></a>"
        "<!--c--><?pi d?><p:c p:q=\"9\">z</p:c><d><a k=\"z\" n=\"10\"/></d></r>",
    D4 = "<r><a k=\"1\"/><b k=\"2\"/><a k=\"3\"><a k=\"4\"/></a></r>",
    D5 = "<r><a k=\"1\"><b>v</b><b>w</b></a><a k=\"x\"><b>w</b></a><c>1</c><c>w</c></r>",
    Paths =
        [
            "/",
            "/r",
            "/*",
            "/r/a",
            "a",
            "a/b",
            "//b",
            "//a/@k",
            "/r/@id",
            "@id",
            "a/@*",
            "a/text()",
            "//text()",
            "a[1]",
            "a[2]/b[last()]",
            "a[@k='y']",
            "a[@k=\"y\"]/b",
            "a[b='u']",
            "a[b]",
            "*",
            "node()",
            "comment()",
            "processing-instruction()",
            "processing-instruction('pi')",
            "p:c",
            "p:*",
            "//*",
            ".",
            "..",
            "./a",
            "descendant::b",
            "descendant-or-self::a",
            "child::a",
            "a/following-sibling::*",
            "d/a/ancestor::*",
            "a/parent::*",
            "self::r",
            "a | d",
            "count(a)",
            "count(//b)",
            "string(a)",
            "name()",
            "local-name(p:c)",
            "contains(a, 't')",
            "starts-with(a, 'u')",
            "concat('x', 'y')",
            "a[position() = 2]",
            "a[last()]",
            "a[1]/b",
            "a[k]",
            "1 + 2",
            "3 div 2",
            "5 mod 2",
            "true()",
            "not(a)",
            "a = 'y'",
            "boolean(a)",
            "number('1.5')",
            "translate('abc', 'b', 'x')",
            "substring('abcdef', 2, 3)",
            "substring-before('a-b', '-')",
            "substring-after('a-b', '-')",
            "//b[. = 'v']",
            "/r/a/b[1]",
            "//a[@k]/@k",
            "a[@k='y' or @k='x']",
            "a[@k='y' and b]",
            "a[2]/b[2]/text()",
            "/r/",
            "a/",
            "",
            "a[",
            "foo(",
            "//b/..",
            "following::*",
            "preceding-sibling::a",
            "attribute::k",
            "/descendant::b[2]",
            "//b[2]",
            "lang('en')",
            "a/@k = 'x'",
            "-1",
            "a[1.5]",
            "a[0]",
            "*[2]",
            "/r/a[1]/../d",
            "$x",
            "//p:c/text()",
            "string(/r/@id)",
            "count(@*)"
        ] ++
            [
                "a[5]",
                "a[3]",
                "a/b[2]",
                "a[string(@k)]",
                "a[count(b)]",
                "a[count(b) = 2]",
                "a[@n = 2]",
                "a[@n > 1]",
                "a[@n < 5]",
                "a[@n != 2]",
                "a[2 = @n]",
                "a['y' = @k]",
                "a[@k != 'y']",
                "a[b = 'v']",
                "a[b != 'v']",
                "a[@k = //a/@k]",
                "a/b/preceding-sibling::*",
                "a/b/preceding-sibling::node()",
                "a[2]/b[2]/preceding::*",
                "d/a/ancestor-or-self::*",
                "a[1]/following::*",
                "a[1]/following::node()",
                "a/b/ancestor::*",
                "string(/r)",
                "string(a[1])",
                "//a[. = 't1ut2']",
                "//a[. = 't1']",
                "name(a)",
                "name(@id)",
                "local-name()",
                "namespace-uri(p:c)",
                "string(@*)",
                "'lit'",
                "1",
                "a and b",
                "a or x",
                "a[1] | a[2] | a[1]",
                "/r/a[1] | //b",
                "count(//node())",
                "count(//*)",
                "count(//text())",
                "//@*",
                "//p:c/@p:q",
                "number(@id)",
                "number('x')",
                "floor(1.5)",
                "ceiling(1.5)",
                "false()",
                "lang('x')",
                "a[position() < 2]",
                "a[position() != 1]",
                "a[last() = 2]",
                "a[b][2]",
                "a[2][b]",
                "string(p:c)",
                "string(comment())",
                "node()[3]",
                "text()",
                "a[1]/node()",
                "a[1]/text()[2]",
                "count(a/b)",
                "concat(a, '-', @id)",
                "starts-with(@id, '1')",
                "contains(a[2], 'w')",
                "a[contains(., 'v')]",
                "a[starts-with(@k, 'x')]",
                "boolean(x)",
                "not(x)",
                "a[not(@n)]",
                "/r/a[@k='x']/b/text()",
                "/*/a",
                "//d//a",
                "d//a",
                ".//b",
                "//a[@k='x']//text()"
            ] ++
            [
                "//a | b",
                "b | //a",
                "a/a | a",
                "//a[1]",
                "(//a)[1]",
                "//a[2]",
                "a[1]/following-sibling::*",
                "a[2]/preceding-sibling::*",
                "//a[@k=4]/preceding::*",
                "a[@k > 1]",
                "a[@k < 2]",
                "a[@k >= 3]",
                "a[@k <= 1]",
                "a[1 < @k]",
                "a[b = 'w']",
                "a[b = //c]",
                "a[@k = 1.0]",
                "c[. = 1]",
                "a[true() = b]",
                "/r/..",
                "a/ancestor::node()",
                "position()",
                "last()",
                "f(a)",
                "a/@*[1]",
                "name(p:c)",
                "local-name(a)",
                "namespace-uri(a)"
            ],
    [
        begin
            {R, _} = xmerl_scan:string(D),
            io:format("document ~p~n", [I]),
            [show(Cwd, P, fun() -> xmerl_xpath:string(P, R) end) || P <- Paths]
        end
     || {I, D} <- lists:enumerate([D1, D3, D4, D5])
    ],
    {R1, _} = xmerl_scan:string(D3),
    {Doc, _} = xmerl_scan:string(D3, [{document, true}]),
    [show(Cwd, {document, P}, fun() -> xmerl_xpath:string(P, Doc) end) || P <- ["/r/a", "/", "r", "comment()", "//b"]],
    [A | _] = xmerl_xpath:string("a", R1),
    [
        show(Cwd, {element, P}, fun() -> xmerl_xpath:string(P, A) end)
     || P <- ["..", "/a", "//b", "name()", "/", "b/text()"]
    ],
    [T | _] = xmerl_xpath:string("a/text()", R1),
    [show(Cwd, {text, P}, fun() -> xmerl_xpath:string(P, T) end) || P <- [".", "string()"]],
    show(Cwd, options, fun() -> xmerl_xpath:string("a", R1, [{namespace, [{"q", "urn:p"}]}]) end),
    show(Cwd, bad_option, fun() -> xmerl_xpath:string("a", R1, [bogus]) end),
    show(Cwd, bad_node, fun() -> xmerl_xpath:string("a", foo) end),
    show(Cwd, select, fun() -> xmerl_xs:select("a/b", R1) end),
    io:format("xmerl_queries: ok~n").

show(Cwd, Label, Fun) ->
    Result =
        try Fun() of
            Value -> cwd(Cwd, Value)
        catch
            error:{badmatch, _} = Reason -> {error, Reason};
            error:Reason when Reason =:= function_clause; element(1, Reason) =:= badarg -> {error, Reason};
            exit:Reason -> {exit, Reason};
            Class:_ -> Class
        end,
    io:format("~w~n ~w~n", [Label, Result]).

%% Strings that begin with the current directory, with "$CWD" in its place.
cwd(Cwd, Value) when is_list(Value) ->
    case lists:prefix(Cwd, Value) andalso Value =/= [] of
        true -> "$CWD" ++ lists:nthtail(length(Cwd), Value);
        false -> [cwd(Cwd, V) || V <- Value]
    end;
cwd(Cwd, Value) when is_tuple(Value) ->
    list_to_tuple(cwd(Cwd, tuple_to_list(Value)));
cwd(_, Value) ->
    Value.
