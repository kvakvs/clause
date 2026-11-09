%% xmerl's export (plan 21 step MIM3c, docs/libraries/xmerl.md#xmerl-export): simple forms and scanned records
%% through xmerl_xml and through a callback module inheriting it (xmerl_export_tags), as erlcloud builds requests and
%% reads responses: OTP's nested lists, escaping, prolog option, callback calls and errors.
-module(xmerl_export).
-export([main/1]).

-include_lib("xmerl/include/xmerl.hrl").

main(_) ->
    Simple = [
        {r, [{a, "1"}], ["text"]},
        {r, ["t"]},
        r,
        {r, [], []},
        {r, [{a, "<&\"'>"}], ["<&>\"'"]},
        {'p:r', [{'xmlns:p', "urn:p"}], [{'p:c', []}]},
        {r, [], [{b, [], [[["deep"], "list"]]}]},
        {r, [], [a]},
        {r, [], ["é", [233], [16#20AC]]},
        {r, [{a, "é"}], []},
        {r, [], [#xmlText{value = "rec"}]},
        {r, [], [#xmlComment{value = "c"}]},
        {r, [], [#xmlPI{name = p, value = "d"}]},
        {r, [], [#xmlText{value = "<cdata>", type = cdata}]},
        {r, [], [""]},
        {r, [{a, ""}], []},
        {r, [{a, [["n", "ested"]]}], []},
        {r, [{a, <<"v">>}, {b, b}, {c, 1}], []},
        {r, [{a, 1.5}], []},
        {r, [], [<<"bin">>]},
        {r, [], [1]},
        {"r", [], ["s"]},
        {r, [{"a", "1"}], []}
    ],
    [show(S, fun() -> xmerl:export_simple([S], xmerl_xml) end) || S <- Simple],
    show(two, fun() -> xmerl:export_simple([{a, []}, {b, []}], xmerl_xml) end),
    show(top_text, fun() -> xmerl:export_simple(["top", {r, []}], xmerl_xml) end),
    show(empty, fun() -> xmerl:export_simple([], xmerl_xml) end),
    Prolog = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>",
    show(prolog, fun() -> xmerl:export_simple([{r, []}], xmerl_xml, [{prolog, Prolog}]) end),
    show(prolog_empty, fun() -> xmerl:export_simple([{r, []}], xmerl_xml, [{prolog, ""}]) end),
    show(prolog_binary, fun() -> xmerl:export_simple([{r, []}], xmerl_xml, [{prolog, <<"P">>}]) end),
    show(other_option, fun() -> xmerl:export_simple([{r, []}], xmerl_xml, [{bogus, 1}]) end),
    show(not_list, fun() -> xmerl:export_simple({r, []}, xmerl_xml) end),
    show(unknown_callback, fun() -> xmerl:export_simple([{r, []}], xmerl_export_none) end),
    show(simple_content, fun() -> xmerl:export_simple_content([{r, []}, "x"], xmerl_xml) end),
    show(simple_element, fun() -> xmerl:export_simple_element({r, [{a, "1"}], ["x"]}, xmerl_xml) end),
    {Doc, _} = xmerl_scan:string("<r a=\"1\"><b>t&amp;</b><!--c--><?p d?><![CDATA[x<]]></r>"),
    show(export, fun() -> xmerl:export([Doc], xmerl_xml) end),
    show(export_element, fun() -> xmerl:export_element(Doc, xmerl_xml) end),
    show(export_content, fun() -> xmerl:export_content([Doc], xmerl_xml) end),
    show(callbacks, fun() -> xmerl:callbacks(xmerl_export_tags) end),
    Mixed = [
        {r, [{a, "1"}], [
            "t",
            {b, [{x, "y"}], ["u"]},
            {c, []},
            #xmlComment{value = "c"},
            #xmlText{value = "d", type = cdata},
            #xmlPI{name = p, value = "v"}
        ]}
    ],
    show(tags, fun() -> xmerl:export_simple(Mixed, xmerl_export_tags) end),
    show(tags_prolog, fun() -> xmerl:export_simple(Mixed, xmerl_export_tags, [{prolog, "P"}, {other, 1}]) end),
    show(tags_records, fun() -> xmerl:export([Doc], xmerl_export_tags) end),
    round_trip(),
    io:format("xmerl_export: ok~n").

%% A request built as erlcloud builds one, read back as erlcloud reads a response.
round_trip() ->
    Tags =
        {'Tagging', [{'TagSet', [{'Tag', [{'Key', [K]}, {'Value', [V]}]} || {K, V} <- [{"a", "1 & 2"}, {"b", "<3>"}]]}]},
    Body = lists:flatten(xmerl:export_simple([Tags], xmerl_xml)),
    io:format("body: ~ts~n", [Body]),
    {Xml, []} = xmerl_scan:string(Body),
    Pairs = [
        {Key, Value}
     || Tag <- xmerl_xpath:string("/Tagging/TagSet/Tag", Xml),
        [#xmlText{value = Key}] <- [xmerl_xpath:string("Key/text()", Tag)],
        [#xmlText{value = Value}] <- [xmerl_xpath:string("Value/text()", Tag)]
    ],
    io:format("pairs: ~p~n", [Pairs]).

show(Label, Fun) ->
    Result =
        try Fun() of
            Value -> Value
        catch
            Class:Reason -> {Class, Reason}
        end,
    io:format("~w~n ~w~n", [Label, Result]).
