%% OTP's xmerl_xml, the XML callback module of xmerl's export (plan 21 step MIM3c,
%% docs/libraries/xmerl.md#xmerl-export): tags, escaped text and attributes in OTP's nested lists.
-module(xmerl_xml).
-include("include/xmerl.hrl").
-export(['#xml-inheritance#'/0, '#root#'/4, '#element#'/5, '#text#'/1, '#cdata#'/1, '#comment#'/1]).

'#xml-inheritance#'() ->
    [].

% The document: the prolog option when it is the only option (as OTP's), else the XML declaration, before the content.
'#root#'(Data, [#xmlAttribute{name = prolog, value = Prolog}], [], _E) ->
    [Prolog, Data];
'#root#'(Data, _Attrs, [], _E) ->
    ["<?xml version=\"1.0\"?>", Data].

'#element#'(Tag, [], Attrs, _Parents, _E) ->
    ["<", atom_to_list(Tag)] ++ attributes(Attrs) ++ ["/>"];
'#element#'(Tag, Data, Attrs, _Parents, _E) ->
    Name = atom_to_list(Tag),
    [["<", Name] ++ attributes(Attrs) ++ [">"], Data, ["</", Name, ">"]].

'#text#'(Text) ->
    escape(Text, false).

'#cdata#'(Text) ->
    "<![CDATA[" ++ Text ++ "]]>".

'#comment#'(Text) ->
    "<!--" ++ Text ++ "-->".

% The attributes of a start tag as one more element of it, each [" ", Name, "=\"", Value, "\""]; none without them.
attributes([]) ->
    [];
attributes(Attrs) ->
    [
        [
            [" ", atom_to_list(Name), "=\"", escape(Value, true), "\""]
         || #xmlAttribute{name = Name, value = Value} <- Attrs
        ]
    ].

% Text escaped for content, or for a quoted attribute value.
escape(Text, Attribute) ->
    lists:append([escaped(C, Attribute) || C <- lists:flatten(Text)]).

escaped($&, _) -> "&amp;";
escaped($<, _) -> "&lt;";
escaped($>, false) -> "&gt;";
escaped($", true) -> "&quot;";
escaped(C, _) -> [C].
