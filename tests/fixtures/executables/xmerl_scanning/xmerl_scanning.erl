%% xmerl's scanner and UTF helpers (plan 21 step MIM3a, docs/libraries/xmerl.md): xmerl_scan:string/1,2 over
%% elements, text pieces, references, comments, processing instructions, CDATA, namespaces, languages and xmlbase, its
%% options and fatal errors; xmerl_ucs's UTF-8 and UTF-16 conversions. The xmlbase of the current directory prints
%% as "$CWD", and fatal errors without their line and column.
-module(xmerl_scanning).
-export([main/1]).

-include_lib("xmerl/include/xmerl.hrl").

main(_) ->
    {ok, Cwd} = file:get_cwd(),
    Docs = [
        "<a/>",
        "<a x=\"1\" y='2'>hi</a>",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<r>\n  <b>t</b>\n  <c/>\n</r>\n",
        "<r><!-- c --><?pi data?><![CDATA[<x>]]>a&amp;b&#65;&#x42;&lt;&gt;&quot;&apos;</r>",
        "<r xmlns=\"urn:d\" xmlns:p=\"urn:p\"><p:e p:at=\"v\"/></r>",
        "<r>" ++ [16#c3, 16#a9] ++ "</r>",
        "<r>x</r> trailing",
        "<r a=\"x&amp;y\tz\"/>",
        "<!DOCTYPE r><r/>",
        "<r><b/><c/></r>",
        "<r><b>t</b><c/></r>",
        "<r>t<c/></r>",
        "<r><b/><c/><d/></r>",
        "<r><b x=\"1\"/><c x=\"1\"/></r>",
        "<r><b><c/><d/></b><e/></r>",
        "<r a=\"1\"><b/><c/></r>",
        "<r><b a=\"1\"><c/></b><d/></r>",
        "<r><b/><c x=\"1\"/></r>",
        "<r xml:base=\"http://h/p/\"><b/><c xml:base=\"q/\"/><d/></r>",
        "<r><b/><c xml:base=\"q\"/></r>",
        "<r xml:base=\"x\"><b xml:base=\"y\"><c/></b><d/></r>",
        "<r xml:lang=\"en\"><b>t</b><c xml:lang=\"fr\" a=\"1\">u</c></r>",
        "<r xmlns:a=\"urn:a\" xmlns:b=\"urn:b\"><a:x xmlns:c=\"urn:c\" xmlns=\"urn:d\"/></r>",
        "<r/><!-- c --><?p x?> tail",
        "<r/>\n\n",
        "<r>&foo;</r>",
        "<r><b></r>",
        "<r",
        "",
        "  <r/>",
        "<r>" ++ [233] ++ "</r>",
        "<r>" ++ [16#1F600] ++ "</r>",
        "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><r>" ++ [233] ++ "</r>",
        "<r>&lt;x &#10; y</r>",
        "<r a=\"1\" a=\"2\"/>",
        "<r>]]></r>",
        "<r>a\r\nb\rc</r>",
        "<r a=\"x\ny\"/>",
        "<r><![CDATA[]]></r>",
        "<r>t<!--c-->u</r>",
        "<r>a&#66;c</r>",
        "<r>&#65;&#66;</r>",
        "<r>ab&#13;cd</r>",
        "<r><!--c-->a&amp;b&#65;</r>",
        "<r><![CDATA[x]]>a&amp;b&#65;</r>",
        "<r>x<b/>a&amp;b&#65;c</r>",
        "<r>a<!--c-->b&amp;c</r>",
        "<r><![CDATA[x]]><!--c--><![CDATA[y]]>z</r>",
        "<r>a<![CDATA[b]]>c</r>",
        "<r><b/>t<![CDATA[x]]>u</r>",
        "<r>&amp;<![CDATA[x]]></r>",
        "<r xmlns:p=\"u1\" xmlns:q=\"u2\"><b xmlns:p=\"u3\"/><c xmlns=\"d\" xmlns:p=\"u1\"/></r>",
        "<r/>" ++ [16#c3, 16#a9],
        "<r a=\"&#10;&#9;x&lt;&#13;\" b='\"' c=\"'\"/>",
        "<r a=\"<\"/>",
        "<a.b-c_d e:f.g=\"1\"><_x/><x9/></a.b-c_d>",
        "<1a/>",
        "<r  a = '1'\n b='2' ></r >",
        "<r a='1'b='2'/>",
        "<!DOCTYPE r [<!ENTITY e \"v\">]><r>&e;</r>",
        "<r/> <x/>",
        "<r/>abc<x/>",
        "<?xml version=\"1.0\"?>",
        "<?xml version=\"2.0\"?><r/>",
        "<?xml version=\"1.0\" encoding=\"UTF-16\"?><r/>",
        "<r><?xml x?></r>",
        "<r><!-- a -- b --></r>",
        "<r>a>b</r>",
        "<r" ++ [16#c3, 16#a9] ++ "/>",
        "<r>&#0;</r>",
        "<r>&#x110000;</r>",
        "<r>a & b</r>",
        "<r><!-- x</r>",
        "<r>a<</r>",
        "<r><b>x</b>",
        "<r/><r/>",
        "<r>" ++ [1] ++ "</r>",
        "<?xml version=\"1.0\" encoding=\"iso-8859-1\"?><r>" ++ [300] ++ "</r>",
        " <?xml version=\"1.0\"?><r/>",
        "<r><?XML x?></r>",
        "<r><?p?x?></r>",
        "<r><?p   a b ?></r>",
        "<r a=\"\"/>",
        "<r a/>",
        "<r></r  >",
        "<r></x>",
        "<r><!---></r>",
        "<r xml:lang=\"en\"><b xml:lang=\"\"><c/></b></r>",
        "<r a=\"1\" b>",
        "<r>&#;</r>",
        "<r><![CDATA[x</r>",
        "<r/",
        "<r a='1/>",
        "<!-- x -->",
        "<!DOCTYPE r [<!ELEMENT r ANY>]><r/>",
        "<r a=\"&#32;&#32;\"/>",
        "<r a=\"a&#32;&#10;b\"/>",
        "<r a=\"&#10; &#9;\"/>",
        "<r a=\" &#9;\"/>",
        "<r a=\"&#9; \"/>",
        "<r a=\"\r\n\"/>",
        "<r a=\"&#13;\"/>"
    ],
    Options = [
        [{space, normalize}],
        [{comments, false}],
        [{xmlbase, "/base"}],
        [{namespace_conformant, true}],
        [{document, true}],
        [{encoding, "iso-8859-1"}]
    ],
    OptDocs = [
        "<?xml version=\"1.0\"?><r xmlns=\"urn:d\" xmlns:p=\"urn:p\">\n  <p:b> x  y </p:b><!--c-->\n</r>",
        "<?xml version='1.0'?>\n<!--a--><?p x?>\n<r/>\n<!--b--><?q?>\n",
        "<r><!--c--><b/>a&amp;b</r>",
        "<r xmlns:p=\"urn:p\" a=\"1\" p:b=\"2\"><x/><p:y/><z xmlns=\"\"/></r>",
        "<q:r/>",
        "<r xml:lang=\"en\"/>",
        "<r> a &amp; b </r>",
        "<r><b/> a &amp;  b </r>",
        "<r>\t\n</r>",
        "<r>a&#10;&#10;b</r>",
        "<r><![CDATA[ x  y ]]></r>",
        "<r>a<b/>  </r>",
        "<r><!--c-->a&amp;b</r>",
        "<r><!--c--><?p?>a</r>",
        "<r>" ++ [233] ++ "</r>"
    ],
    Responses = [
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<DescribeInstancesResponse xmlns=\"http://ec2.amazonaws.com/doc/2016-11-15/\">\n    <requestId>8f7724cf-496f-496e-8fe3-example</requestId>\n    <reservationSet>\n        <item>\n            <reservationId>r-1234567890abcdef0</reservationId>\n            <instancesSet>\n                <item><instanceId>i-1</instanceId><tagSet><item><key>Name</key><value>a &amp; b</value></item></tagSet></item>\n                <item><instanceId>i-2</instanceId></item>\n            </instancesSet>\n        </item>\n    </reservationSet>\n</DescribeInstancesResponse>",
        "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"><Name>b</Name><Contents><Key>k&lt;1&gt;</Key><Size>10</Size></Contents><Contents><Key>" ++
            [16#e2, 16#82, 16#ac] ++ "</Key></Contents></ListBucketResult>",
        "<Error><Code>NoSuchKey</Code><Message>The resource you requested does not exist</Message><Resource>/mybucket/myfoto.jpg</Resource></Error>",
        "<r>a<<123456789012345</r>",
        "<r><!--a--></r>",
        "<r>x<!--a--></r>",
        "<r><b/><!--a--><!--b--> y <c/></r>"
    ],
    [show(Cwd, D, scan(D, [{quiet, true}])) || D <- Docs ++ Responses],
    [show(Cwd, {D, O}, scan(D, [{quiet, true} | O])) || D <- OptDocs, O <- Options],
    show(Cwd, bogus_option, scan("<r/>", [bogus])),
    show(Cwd, binary, scan(<<"<r/>">>, [])),
    % Reported: an unknown entity, a mismatched end tag.
    show(Cwd, reported, [scan("<r>&foo;</r>", []), scan("<r><b></r>", [])]),
    records(),
    ucs(),
    io:format("xmerl_scanning: ok~n").

scan(Text, Options) ->
    try xmerl_scan:string(Text, Options) of
        Result -> Result
    catch
        exit:{fatal, {Reason, File, {line, _}, {col, _}}} -> {fatal, Reason, File};
        Class:Reason -> {Class, Reason}
    end.

%% The records as user code reads them.
records() ->
    {#xmlElement{name = r, content = [#xmlText{value = V}], attributes = [#xmlAttribute{value = A}]}, _} =
        xmerl_scan:string("<r a='x'>v</r>"),
    io:format("records: ~p~n", [{V, A, #xmlElement{}, #xmlText{}, record_info(fields, xmlElement)}]).

ucs() ->
    Utf8 = [
        "abc",
        [16#c3, 16#a9],
        <<16#c3, 16#a9>>,
        [16#c3],
        [255],
        [16#e2, 16#82, 16#ac],
        [16#f0, 16#9f, 16#98, 16#80],
        [16#ed, 16#a0, 16#80],
        [16#c0, 16#80],
        [16#e0, 16#80, 16#80],
        [16#f8, 16#88, 16#80, 16#80, 16#80],
        [16#f4, 16#90, 16#80, 16#80],
        [16#80],
        [],
        [256],
        a
    ],
    io:format("from_utf8: ~p~n", [[catching(fun() -> xmerl_ucs:from_utf8(X) end) || X <- Utf8]]),
    Codes = [
        "abc",
        [233],
        8364,
        [16#1F600],
        [16#110000],
        [16#1FFFFF],
        [16#200000],
        [-1],
        [16#D800],
        [16#FFFE],
        <<"a">>,
        [],
        0,
        [[65]],
        a
    ],
    io:format("to_utf8: ~p~n", [[catching(fun() -> xmerl_ucs:to_utf8(X) end) || X <- Codes]]),
    Unicode = [
        0,
        65,
        16#D7FF,
        16#D800,
        16#DFFF,
        16#E000,
        16#FFFD,
        16#FFFE,
        16#FFFF,
        16#10000,
        16#10FFFF,
        16#110000,
        -1,
        a,
        [65],
        1.0
    ],
    io:format("is_unicode: ~p~n", [[xmerl_ucs:is_unicode(X) || X <- Unicode]]),
    Utf16 = [
        [0, 65],
        <<0, 65>>,
        [16#d8, 16#3d, 16#de, 16#00],
        [16#ff, 16#fd],
        [16#d8, 0, 16#dc, 0],
        [],
        <<>>,
        [1, 0, 0, 2],
        [16#ff, 16#fe],
        a
    ],
    io:format("from_utf16be: ~p~n", [[catching(fun() -> xmerl_ucs:from_utf16be(X) end) || X <- Utf16]]),
    % Reported: an odd byte, a lone surrogate.
    io:format("from_utf16be_errors: ~p~n", [[xmerl_ucs:from_utf16be(X) || X <- [[0, 65, 0, 66, 0], [16#dc, 0]]]]).

catching(Fun) ->
    try Fun() of
        Value -> Value
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Cwd, Label, Value) ->
    io:format("~w~n ~w~n", [Label, cwd(Cwd, Value)]).

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
