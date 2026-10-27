%% erl_comment_scan and erl_recomment (plan 21 LIB77E4b): comment lines of source text around strings, quoted atoms,
%% character literals, tabs and line breaks, joined comments, file/1, and comments attached to forms and trees by
%% recomment_forms/2, quick_recomment_forms/2 and recomment_tree/2, with -file attributes and malformed input.
-module(syntax_tools_comments).
-export([main/1]).

-define(SOURCE,
    "%% Module header\n"
    "%% second line\n"
    "\n"
    "-module(m).   % after module\n"
    "-export([f/1, g/0]).\n"
    "\n"
    "%% Standalone block\n"
    "\n"
    "% before f\n"
    "f(X) ->\n"
    "    %% inside f\n"
    "    Y = X + 1,   % trailing   \t\n"
    "    {Y, \"no % comment\", '%atom', $%, $\\\", \"esc \\\" % still\"}.\n"
    "% between\n"
    "g() ->\n"
    "\tcase a of\n"
    "\t\ta -> ok  % tabbed\r\n"
    "\tend.\r"
    "% last line\n"
    "%% trailing comment at end"
).

main(_) ->
    scanning(),
    files(),
    recommenting(),
    io:format("syntax_tools_comments: ok~n").

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

try_call(F) ->
    try
        {value, F()}
    catch
        Class:Reason -> {Class, Reason}
    end.

%% scan_lines/1, join_lines/1 and string/1 over texts with every kind of token.
scanning() ->
    io:format("== scanning~n"),
    show("lines", erl_comment_scan:scan_lines(?SOURCE)),
    show("joined", erl_comment_scan:join_lines(erl_comment_scan:scan_lines(?SOURCE))),
    show("string", erl_comment_scan:string(?SOURCE)),
    Texts = [
        "",
        "%",
        "% only",
        "  %%   indented   ",
        "a % b\n  % c\n  % d\n% e",
        "x(). %a\n      %b\n",
        "$\t% after tab char\n$\\% escaped\n$",
        "\"unterminated % string",
        "'quoted\n% in atom' % real\n",
        "\"a\\\\\" % after backslash\n",
        "\t%tab\n \t %mixed\n",
        "%\x{a0}nbsp kept\x{a0}\n%trailing\x{2029}\x{85} \n",
        "\r\r\n\n%lines",
        "\"ab\"%x",
        "%% a\n%% b\n\n%% c\n   %% d\n   %% e\nx %% f\n  %% g"
    ],
    [show("text", {T, erl_comment_scan:string(T)}) || T <- Texts],
    Lines = [{5, 1, 0, "e"}, {4, 1, 0, "d"}, {3, 3, 2, "c"}, {2, 3, 2, "b"}, {1, 3, 2, "a"}],
    show("join", erl_comment_scan:join_lines(Lines)),
    show("join odd", erl_comment_scan:join_lines([{a, 1, 0, "x"}, {b, 1, 0, "y"}])),
    show("join empty", erl_comment_scan:join_lines([])),
    show("join bad", try_call(fun() -> erl_comment_scan:join_lines([{1, 2, 3}]) end)),
    show("scan bad", try_call(fun() -> erl_comment_scan:scan_lines([$a | b]) end)),
    show("scan binary", try_call(fun() -> erl_comment_scan:string(<<"% x">>) end)).

%% file/1 of written files: UTF-8 by default, Latin-1 declared, and bytes that are not UTF-8.
files() ->
    io:format("== files~n"),
    Name = "syntax_tools_comments.tmp",
    Write = fun(Bytes) -> ok = file:write_file(Name, Bytes) end,
    Write(<<"% caf", 195, 169, "\n-module(x). % end\n">>),
    show("utf8", erl_comment_scan:file(Name)),
    Write(<<"%% coding: latin-1\n% caf", 233, "\n">>),
    show("latin1", erl_comment_scan:file(Name)),
    Write(<<"% bytes ", 233, 255, "\n">>),
    show("fallback", erl_comment_scan:file(Name)),
    ok = file:delete(Name).

%% Forms of a text with their positions: tokens split at each dot.
forms(Text) ->
    {ok, Tokens, _} = erl_scan:string(Text, 1),
    [Form || Group <- split(Tokens, [], []), {ok, Form} <- [erl_parse:parse_form(Group)]].

split([{dot, _} = Dot | Rest], Group, Groups) -> split(Rest, [], [lists:reverse([Dot | Group]) | Groups]);
split([Token | Rest], Group, Groups) -> split(Rest, [Token | Group], Groups);
split([], _, Groups) -> lists:reverse(Groups).

print(Tree) -> io:format("~ts~n--~n", [erl_prettypr:format(Tree)]).

%% recomment_forms/2 and quick_recomment_forms/2 printed, recomment_tree/2's leftovers, -file attributes and errors.
recommenting() ->
    io:format("== recommenting~n"),
    Forms = forms(?SOURCE),
    Comments = erl_comment_scan:string(?SOURCE),
    print(erl_recomment:recomment_forms(Forms, Comments)),
    print(erl_recomment:quick_recomment_forms(Forms, Comments)),
    print(erl_recomment:recomment_forms(erl_syntax:form_list([erl_syntax:form_list(Forms)]), Comments)),
    Tree = erl_recomment:recomment_forms(Forms, Comments),
    show("form types", [erl_syntax:type(F) || F <- erl_syntax:form_list_elements(Tree)]),
    show("comments", [
        {erl_syntax:get_precomments(F), erl_syntax:get_postcomments(F)}
     || F <- erl_syntax:form_list_elements(Tree)
    ]),
    [F | _] = [F || F <- Forms, erl_syntax:type(F) =:= function],
    {Recommented, Rest} = erl_recomment:recomment_tree(F, Comments),
    print(Recommented),
    show("rest", Rest),
    show("tree", Recommented),
    {Single, SingleRest} = erl_recomment:recomment_tree(erl_syntax:atom(a), [{0, 1, 0, ["% x"]}, {9, 1, 0, ["% y"]}]),
    show("single", {Single, SingleRest}),
    print(erl_recomment:recomment_forms(F, Comments)),
    Filed = forms(
        "-module(n).\n"
        "a() -> 1. % one\n"
        "-file(\"other.erl\", 10).\n"
        "b() -> 2. % two\n"
        "-file(\"n.erl\", 3).\n"
        "c() -> 3. % three\n"
        "-file(\"n.erl\", 1).\n"
        "d() -> 4. % four\n"
        "% end\n"
    ),
    FiledComments = erl_comment_scan:string(
        "\n% one\n\n% two\n\n% three\n\n% four\n% end\n% beyond\n"
    ),
    print(erl_recomment:recomment_forms(Filed, FiledComments)),
    print(erl_recomment:recomment_forms([], [{1, 1, 0, ["% alone"]}, {3, 5, 4, ["% two", "% lines"]}])),
    Bad = erl_syntax:set_pos(erl_syntax:atom(a), foo),
    show("bad position", try_call(fun() -> erl_recomment:recomment_tree(Bad, []) end)),
    show("bad comment", try_call(fun() -> erl_recomment:recomment_forms(Forms, [{1, 2}]) end)).
