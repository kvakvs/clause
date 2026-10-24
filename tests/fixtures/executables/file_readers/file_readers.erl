%% Readers of Erlang terms and expressions (plan 21 step LIB33): file:consult/1, eval/1,2, script/1,2, their path_
%% variants and path_open/3 over files this program writes, and io:read/1..4, scan_erl_*, parse_erl_* over open files
%% and standard input. Stack traces print as 'stack', pids as 'pid'.
-module(file_readers).
-export([main/1]).

main(_) ->
    Files = files(),
    [ok = file:write_file(Name, Content) || {Name, Content} <- Files],
    [
        io:format("~s:~n  consult ~p~n  eval ~p~n  script ~p~n", [
            Name, clean(file:consult(Name)), clean(file:eval(Name)), clean(file:script(Name))
        ])
     || {Name, _} <- Files
    ],
    invalid(),
    errors(),
    paths(),
    io_reads(),
    stdin(),
    io:format("file_readers: ok~n").

files() ->
    [
        {"terms", <<"{a, 1}.\n[b, \"c\"].\n%% comment\n#{k => v}.\n">>},
        {"empty", <<>>},
        {"spaces", <<"  \n\n">>},
        {"nodot", <<"{a, 1}">>},
        {"nodot2", <<"{a, 1}.\n{b">>},
        {"syntax", <<"{a, 1}.\n{b,}.\n{c}.\n">>},
        {"scan", <<"{a, 1}.\n'abc.\n">>},
        {"expr", <<"1 + 2.\n">>},
        {"utf8", <<"\"h", 16#c3, 16#a9, "\".\n">>},
        {"latin1", <<"%% coding: latin-1\n\"h", 16#e9, "\".\n">>},
        {"latin1b", <<"%% -*- coding: Latin-1 -*-\n\"h", 16#e9, "\".\n">>},
        {"latin1c", <<"\n%% encoding=latin-1\n\"h", 16#e9, "\".\n">>},
        {"utf8b", <<"%% coding: utf-8\n\"h", 16#c3, 16#a9, "\".\n">>},
        {"other", <<"%% coding: ascii\n\"h", 16#c3, 16#a9, "\".\n">>},
        {"thirdline", <<"\n\n%% coding: latin-1\n\"h", 16#c3, 16#a9, "\".\n">>},
        {"script", <<"X = 1,\nY = X + 1.\n{X, Y}.\n">>},
        {"scripterr", <<"1 + a.\n2.\n">>},
        {"scripterr2", <<"X = 1.\nX = 2.\nok.\n">>},
        {"scriptsyntax", <<"1 +.\n2.\n">>},
        {"scriptunbound", <<"Z.\n">>},
        {"scriptbind", <<"B + 1.\n">>},
        {"scriptthrow", <<"throw(oops).\nexit(gone).\n">>},
        {"scriptfun", <<"F = fun(X) -> X * 2 end.\nlists:map(F, [1, 2]).\n">>},
        {"multi", <<"a. b. c.">>},
        {"comment_end", <<"a.\n% done">>},
        {"lines", <<"\n\n{a,\n b}.\n\n'c'.\n">>}
    ].

%% Error terms with their stack traces as 'stack'.
clean({error, {Line, file, {Class, Reason, Stack}}}) when is_list(Stack) ->
    {error, {Line, file, {Class, Reason, stack}}};
clean({ok, Fd, Name}) when is_pid(Fd) -> {ok, pid, Name};
clean(Other) ->
    Other.

%% A byte that is not UTF-8: the form reaching it fails; eval and script then find the file server gone.
invalid() ->
    ok = file:write_file("badutf8", <<"a.\nb.\n\"h", 16#e9, "\".\n">>),
    ok = file:write_file("badutf8b", <<"\"h", 16#e9, "\".\n">>),
    io:format("badutf8 ~p ~p~n", [file:consult("badutf8"), file:consult("badutf8b")]),
    io:format("badutf8 eval ~p~n", [exception(fun() -> file:eval("badutf8") end)]),
    io:format("badutf8 script ~p~n", [exception(fun() -> file:script("badutf8") end)]).

exception(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

errors() ->
    io:format("missing ~p ~p ~p~n", [file:consult("nope"), file:eval("nope"), file:script("nope")]),
    ok = file:make_dir("dir"),
    io:format("dir ~p ~p ~p~n", [file:consult("dir"), file:eval("dir"), file:script("dir")]),
    io:format("bad name ~p ~p~n", [file:consult({bad}), file:script(1)]),
    io:format("bind ~p~n", [file:script("scriptbind", [{'B', 41}])]),
    io:format("bind map ~p~n", [file:script("scriptbind", #{'B' => 1})]),
    io:format("bind eval ~p~n", [file:eval("scriptbind", [{'B', 41}])]).

paths() ->
    ok = file:write_file("dir/inner", <<"inner.\n">>),
    io:format("path ~p~n", [file:path_consult(["nope", ".", "dir"], "terms")]),
    io:format("path ~p~n", [file:path_consult(["nope", "dir"], "inner")]),
    io:format("path ~p~n", [file:path_consult(["nope"], "terms")]),
    io:format("path ~p~n", [file:path_consult([], "terms")]),
    io:format("path ~p~n", [file:path_consult(["."], "syntax")]),
    io:format("path ~p~n", [file:path_script([".", "dir"], "script")]),
    io:format("path ~p~n", [file:path_script(["."], "scriptbind", [{'B', 1}])]),
    io:format("path ~p~n", [file:path_script(["."], "empty")]),
    io:format("path ~p~n", [file:path_eval(["."], "script")]),
    io:format("path ~p~n", [file:path_eval(["dir"], "inner", [])]),
    io:format("path ~p~n", [clean(file:path_eval(["."], "scripterr"))]),
    io:format("path ~p~n", [clean(file:path_open([".", "dir"], "terms", [read]))]),
    io:format("path ~p~n", [clean(file:path_open(["nope", "dir"], "inner", [read]))]),
    io:format("path ~p~n", [file:path_open(["nope"], "terms", [read])]),
    io:format("path ~p~n", [file:path_open(["."], {bad}, [read])]),
    {ok, Cwd} = file:get_cwd(),
    Absolute = filename:join(Cwd, "terms"),
    {ok, Terms, Full} = file:path_consult(["nope"], Absolute),
    io:format("path absolute ~p ~p~n", [Terms, Full =:= Absolute]).

io_reads() ->
    Reads = [
        {"read2", "terms", fun(F) -> io:read(F, '') end, 4},
        {"read3", "terms", fun(F) -> io:read(F, '', 1) end, 4},
        {"read syntax", "syntax", fun(F) -> io:read(F, '', 1) end, 4},
        {"read scan", "scan", fun(F) -> io:read(F, '', 1) end, 3},
        {"read4", "script", fun(F) -> io:read(F, '', {1, 1}, [text]) end, 3},
        {"parse exprs", "script", fun(F) -> io:parse_erl_exprs(F, '', 1) end, 3},
        {"parse exprs2", "script", fun(F) -> io:parse_erl_exprs(F, '') end, 3},
        {"parse exprs4", "script", fun(F) -> io:parse_erl_exprs(F, '', {1, 1}, []) end, 2},
        {"scan exprs", "script", fun(F) -> io:scan_erl_exprs(F, '', 5) end, 3},
        {"scan exprs2", "lines", fun(F) -> io:scan_erl_exprs(F, '') end, 3},
        {"scan exprs4", "script", fun(F) -> io:scan_erl_exprs(F, '', 7, [return_comments]) end, 2},
        {"parse form", "form", fun(F) -> io:parse_erl_form(F, '', 1) end, 4},
        {"parse form2", "form", fun(F) -> io:parse_erl_form(F, '') end, 2},
        {"parse form4", "form", fun(F) -> io:parse_erl_form(F, '', {1, 1}, []) end, 2},
        {"scan form", "form", fun(F) -> io:scan_erl_form(F, '') end, 4},
        {"scan form3", "form", fun(F) -> io:scan_erl_form(F, '', 3) end, 2},
        {"scan form4", "form", fun(F) -> io:scan_erl_form(F, '', 1, [text]) end, 1}
    ],
    ok = file:write_file("form", <<"-module(m).\nf() -> ok.\n-bad(.\n">>),
    [io_read(Label, Name, Read, Count) || {Label, Name, Read, Count} <- Reads],
    ok.

io_read(Label, Name, Read, Count) ->
    {ok, F} = file:open(Name, [read]),
    Results = [Read(F) || _ <- lists:seq(1, Count)],
    ok = file:close(F),
    io:format("~s ~p~n", [Label, Results]).

%% Terms and expressions from standard input, with an empty prompt.
stdin() ->
    io:format("stdin ~p~n", [io:read('')]),
    io:format("stdin ~p~n", [io:read(standard_io, '', 1)]),
    io:format("stdin ~p~n", [io:parse_erl_exprs('')]),
    io:format("stdin ~p~n", [io:scan_erl_exprs('')]),
    io:format("stdin ~p~n", [io:parse_erl_form(standard_io, '', 1)]),
    io:format("stdin ~p~n", [io:scan_erl_form('')]),
    io:format("stdin ~p~n", [io:read('')]).
