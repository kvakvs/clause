%% epp_dodger (plan 21 LIB77E4c): source files parsed without the preprocessor by parse_file/1,2,
%% quick_parse_file/1,2, parse/1,2,3, quick_parse/1,2,3 and the form functions: directives, macros with and without
%% arguments in every position, strings next to macros (clever and quick), -define bodies that do not parse, -error
%% and -warning, parse and macro argument errors with and without no_fail, Latin-1 files, tokens_to_string/1 and
%% format_error/1.
-module(epp_dodger_forms).
-export([main/1]).

-define(SOURCE, <<
    "-module(m).\n"
    "-feature(maybe_expr, enable).\n"
    "-export([f/1, g/0]).\n"
    "-define(ONE, 1).\n"
    "-define(ADD(X, Y), X + Y).\n"
    "-define(TEXT, a b c).\n"
    "-define(OPEN, (1).\n"
    "-undef(ONE).\n"
    "-include(\"x.hrl\").\n"
    "-include_lib(\"kernel/include/file.hrl\").\n"
    "-ifdef(DEBUG).\n"
    "-ifndef(DEBUG).\n"
    "-if(?ONE > 0).\n"
    "-elif(true).\n"
    "-else.\n"
    "-endif.\n"
    "-error(\"broken \" ++ ?TEXT).\n"
    "-warning(careful).\n"
    "-?ATTR(value).\n"
    "?NAME(X) -> X.\n"
    "f(X) -> ?ADD(X, ?ONE) + ?MODULE:g() + m:?F(X) + ?Var.\n"
    "g() -> {?M({a, [b]}, <<1>>, begin x end, fun() -> ok end), ?EMPTY()}.\n"
    "h() -> \"pre\" ?S \"post\" ?T.\n"
    "i() -> try a() catch ?C(E):R -> R; error:?P(x) when true -> ok end.\n"
    "j() -> case x of ?P(1) -> 1; ?Q -> 2 end.\n"
    "k() -> ?ONE(.\n"
    "l( -> .\n"
    "m() -> 'quoted atom', $c, 1.5, \"s\\n\", [1|T], #{a => 2}.\n"
>>).

main(_) ->
    File = "epp_dodger_forms.tmp",
    ok = file:write_file(File, ?SOURCE),
    files(File),
    devices(File),
    latin1(),
    text(),
    ok = file:delete(File),
    io:format("epp_dodger_forms: ok~n").

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

%% Each form printed by erl_prettypr, or the error it is.
print({error, ErrorInfo}) ->
    io:format("  error ~p~n", [ErrorInfo]);
print(Form) ->
    io:format("  ~p: ~ts~n", [erl_syntax:type(Form), erl_prettypr:format(Form)]).

files(File) ->
    io:format("== parse_file~n"),
    {ok, Forms} = epp_dodger:parse_file(File),
    [print(F) || F <- Forms],
    show("raw", Forms),
    io:format("== parse_file clever no_fail~n"),
    {ok, Clever} = epp_dodger:parse_file(File, [clever, no_fail]),
    [print(F) || F <- Clever],
    io:format("== quick_parse_file~n"),
    {ok, Quick} = epp_dodger:quick_parse_file(File),
    [print(F) || F <- Quick],
    show("quick raw", Quick),
    io:format("== quick_parse_file clever~n"),
    {ok, QuickClever} = epp_dodger:quick_parse_file(File, [clever]),
    [print(F) || F <- QuickClever],
    show("missing", epp_dodger:parse_file("no_such_file.erl")),
    show("missing quick", epp_dodger:quick_parse_file("no_such_file.erl")).

devices(File) ->
    io:format("== devices~n"),
    {ok, D1} = file:open(File, [read]),
    {ok, Forms} = epp_dodger:parse(D1),
    ok = file:close(D1),
    show("parse/1 count", length(Forms)),
    {ok, D2} = file:open(File, [read]),
    {ok, Located} = epp_dodger:parse(D2, 100),
    ok = file:close(D2),
    show("parse/2 first", hd(Located)),
    {ok, D3} = file:open(File, [read]),
    {ok, Quick} = epp_dodger:quick_parse(D3, 1, [no_fail]),
    ok = file:close(D3),
    show("quick_parse/3 count", length(Quick)),
    {ok, D4} = file:open(File, [read]),
    show("quick_parse/1", length(element(2, epp_dodger:quick_parse(D4)))),
    ok = file:close(D4),
    {ok, D5} = file:open(File, [read]),
    Steps = forms(D5, 1, fun epp_dodger:parse_form/2, []),
    ok = file:close(D5),
    [show("parse_form", S) || S <- Steps],
    {ok, D6} = file:open(File, [read]),
    QuickSteps = forms(D6, {1, 1}, fun(D, L) -> epp_dodger:quick_parse_form(D, L, [no_fail]) end, []),
    ok = file:close(D6),
    [show("quick_parse_form", S) || S <- lists:sublist(QuickSteps, 6)],
    {ok, D7} = file:open(File, [read]),
    show("parse_form/3", epp_dodger:parse_form(D7, 1, [clever])),
    show("quick_parse_form/2", epp_dodger:quick_parse_form(D7, 2)),
    ok = file:close(D7).

% The results of reading forms one by one up to the end.
forms(Device, Location, Read, Acc) ->
    case Read(Device, Location) of
        {eof, _} = Eof -> lists:reverse([Eof | Acc]);
        {_, _, Next} = Step -> forms(Device, Next, Read, [Step | Acc])
    end.

%% A Latin-1 file without an encoding comment parses again as Latin-1; one declaring UTF-8 does not.
latin1() ->
    io:format("== latin1~n"),
    File = "epp_dodger_latin1.tmp",
    ok = file:write_file(File, <<"-module(l).\nf() -> \"caf", 233, "\".\n">>),
    show("latin1", epp_dodger:parse_file(File)),
    ok = file:write_file(File, <<"%% coding: utf-8\n-module(l).\nf() -> \"caf", 233, "\".\n">>),
    show("declared utf8", epp_dodger:parse_file(File)),
    ok = file:delete(File).

text() ->
    io:format("== text~n"),
    {ok, Tokens, _} = erl_scan:string("f(X, 'a b', $c, 1.5, \"s\") -> [X | 10]. g. 'if'", 1),
    show("tokens_to_string", epp_dodger:tokens_to_string(Tokens)),
    show("empty", epp_dodger:tokens_to_string([])),
    Errors = [macro_args, {error, "-error x."}, {warning, "-warning y."}, {unknown, {badarg, [{m, f, 1}]}}],
    [show("format_error", lists:flatten(epp_dodger:format_error(E))) || E <- Errors].
