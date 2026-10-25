%% erl_lint over snippet modules (plan 21 step LIB34C): module/1,2,3 errors and warnings of each family (module and
%% export attributes, imports, variables, patterns and guards, binaries, comprehensions, records, types and specs,
%% behaviours, deprecated and removed calls, format strings, attributes checked after the traversal), with options,
%% then exprs/2, exprs_opt/3, used_vars/2, is_guard_test/1, is_guard_expr/1 and format_error/1 texts.
-module(erl_lint_checks).
-export([main/1]).

main(_) ->
    %% The behaviours the snippets name, so their behaviour_info/1 is linked.
    _ = [erl_lint_checks_behaviour:behaviour_info(callbacks), gen_server:behaviour_info(callbacks)],
    lists:foreach(fun run/1, cases()),
    expressions(),
    io:format("erl_lint_checks: ok~n").

run({Name, Options, Source}) ->
    run({Name, Options, Source, []});
run({Name, Options, Source, ScanOptions}) ->
    io:format("== ~s ~p~n", [Name, Options]),
    Forms = [{attribute, {1, 1}, file, {"snippet.erl", 1}} | forms(Source, ScanOptions)],
    Result =
        case Options of
            none -> erl_lint:module(Forms);
            {file, File} -> erl_lint:module(Forms, File);
            _ -> erl_lint:module(Forms, "snippet.erl", Options)
        end,
    report(Result).

report({ok, Warnings}) ->
    io:format("ok~n"),
    infos(warning, Warnings);
report({error, Errors, Warnings}) ->
    io:format("error~n"),
    infos(error, Errors),
    infos(warning, Warnings).

infos(Kind, PerFile) ->
    [
        [
            begin
                io:format("  ~s ~ts ~p ~p~n", [Kind, File, Location, Descriptor]),
                io:format("    ~ts~n", [Module:format_error(Descriptor)])
            end
         || {Location, Module, Descriptor} <- Infos
        ]
     || {File, Infos} <- PerFile
    ].

forms(Text, ScanOptions) -> forms(Text, ScanOptions, {1, 1}, []).

forms(Text, ScanOptions, Location, Acc) ->
    case erl_scan:tokens([], Text, Location, [text | ScanOptions]) of
        {done, {ok, Tokens, End}, Rest} ->
            Form =
                case erl_parse:parse_form(Tokens) of
                    {ok, F} -> F;
                    {error, E} -> {error, E}
                end,
            forms(Rest, ScanOptions, End, [Form | Acc]);
        _ ->
            lists:reverse(Acc, [{eof, Location}])
    end.

cases() ->
    [
        {"no module", none, "f() -> ok.\n"},
        {"module attributes", none,
            "-module(m).\n"
            "-module(m2).\n"
            "-export([f/0, f/0, g/1, h/2]).\n"
            "-export_type([t/0]).\n"
            "-type t() :: atom().\n"
            "f() -> ok.\n"
            "f() -> again.\n"
            "g(X) -> X.\n"
            "unused() -> ok.\n"
            "-export([late/0]).\n"
            "late() -> ok.\n"},
        {"export all", none, "-module(m).\n-compile(export_all).\nf() -> ok.\ng() -> f().\n"},
        {"module names", none,
            "-module(m).\n"
            "-export([f/0]).\n"
            "-import('', [a/0]).\n"
            "-import(' ', [b/0]).\n"
            "f() -> {'':x(), ' ':y(), 'a\\tb':z()}.\n"},
        {"imports", [warn_unused_import],
            "-module(m).\n"
            "-export([f/1, g/0, h/1]).\n"
            "-import(lists, [reverse/1, map/2, unused/0]).\n"
            "-import(ordsets, [reverse/1]).\n"
            "-import(mymod, [length/1, size/1]).\n"
            "-compile({no_auto_import, [size/1]}).\n"
            "f(L) -> reverse(L).\n"
            "g() -> fun map/2.\n"
            "h(X) -> length(X).\n"
            "map(_, _) -> defined.\n"},
        {"bif clashes", none,
            "-module(m).\n"
            "-export([f/1, element/2, g/1, k/1]).\n"
            "-compile({no_auto_import, [hd/1]}).\n"
            "element(_, T) -> T.\n"
            "f(T) -> element(1, T).\n"
            "g(L) -> hd(L).\n"
            "hd(L) -> lists:nth(1, L).\n"
            "k(X) when is_list(X) -> length(X).\n"},
        {"unbound and unsafe variables", none,
            "-module(m).\n"
            "-export([f/1, g/1, h/1, i/1, j/1]).\n"
            "f(X) -> Y = Xx, {X, Y, Z}.\n"
            "g(X) -> case X of a -> A = 1; b -> B = 2 end, {A, B}.\n"
            "h(X) -> try X of Y -> Y catch _ -> Z = 1 end, Z.\n"
            "i(X) -> catch (Y = X), Y.\n"
            "j(X) -> if X -> W = 1; true -> ok end, W.\n"},
        {"exported variables", [warn_export_vars],
            "-module(m).\n"
            "-export([f/1, g/1, h/1, k/1]).\n"
            "f(X) -> case X of a -> A = 1; _ -> A = 2 end, A.\n"
            "g(X) -> {B = X, B}.\n"
            "h(X) -> [C = X | C].\n"
            "k(X) -> case X of {D} -> ok; _ -> D = 1 end, D.\n"},
        {"subexpression exports", none,
            "-module(m).\n"
            "-export([f/1, g/1]).\n"
            "f(X) -> {A = X, A}.\n"
            "g(X) -> foo(B = X), B.\n"
            "foo(X) -> X.\n"},
        {"shadowing and unused", none,
            "-module(m).\n"
            "-export([f/1, g/1, h/1, k/2, n/0]).\n"
            "f(X) -> F = fun(X) -> X end, F(1).\n"
            "g(X) -> [X || X <- [1, 2]].\n"
            "h(X) -> fun F(X) -> F end.\n"
            "k(A, _B) -> Unused = A, ok.\n"
            "n() -> F = fun N() -> ok end, F.\n"},
        {"underscore matches", none,
            "-module(m).\n"
            "-export([f/2, g/1]).\n"
            "f(_Same, _Same) -> ok.\n"
            "g(X) -> _V = X, _V = 2.\n"},
        {"stacktraces", none,
            "-module(m).\n"
            "-export([f/1, g/1]).\n"
            "f(X) -> try X catch _:_:S when S =:= [] -> S end.\n"
            "g(S) -> try S catch _:_:S -> ok end.\n"},
        {"patterns", none,
            "-module(m).\n"
            "-export([f/1, g/1, h/1, k/1, n/1]).\n"
            "f(1 + 2) -> three;\n"
            "f(X + 1) -> plus;\n"
            "f(\"ab\" ++ T) -> T;\n"
            "f(0.0) -> zero;\n"
            "f(#{a => 1}) -> map.\n"
            "g({a, B} = {Y, Z}) -> {B, Y, Z}.\n"
            "h(X) -> #{X := 1}.\n"
            "k(-0.0) -> ok.\n"
            "n(X) when X =:= 0.0 -> zero.\n"},
        {"guards", none,
            "-module(m).\n"
            "-export([f/1, g/1, h/1, k/1, is_atom/1]).\n"
            "f(X) when g(X) -> ok.\n"
            "g(X) when atom(X) -> ok; g(X) when integer(X), list(X) -> ok.\n"
            "h(X) when X ! 1 -> ok; h(X) when X ++ [] -> ok; h(X) when is_record(X, r) -> ok.\n"
            "k(X) when record(X, s) -> ok.\n"
            "is_atom(X) -> X.\n"},
        {"obsolete guard overridden", none,
            "-module(m).\n"
            "-export([f/1, is_list/1]).\n"
            "f(X) when list(X) -> ok.\n"
            "is_list(X) -> X.\n"},
        {"binaries", none,
            "-module(m).\n"
            "-export([f/1, g/1, h/1, k/0, n/1]).\n"
            "f(<<A/binary, B/binary>>) -> {A, B}.\n"
            "g(<<\"abc\"/utf8, \"d\":8>>) -> ok.\n"
            "h(<<X:4/foo, Y:3/integer-binary, Z/unit:8, W:all>>) -> {X, Y, Z, W}.\n"
            "k() -> <<1.0:7/float, 2:8/utf8, <<>>:a>>.\n"
            "n(<<X:(f(1))>>) -> X.\n"},
        {"comprehensions", none,
            "-module(m).\n"
            "-export([f/1, g/1, h/1, k/1]).\n"
            "f(L) -> [X || X <- L, Y = X].\n"
            "g(B) -> [X || <<X/binary>> <= B].\n"
            "h(M) -> #{K := V || K := V <- M}.\n"
            "k(L) -> [{X, Y} || X <- L && Y <- L && is_list(L)].\n"},
        {"records", none,
            "-module(m).\n"
            "-export([f/1, g/1, h/1, k/1, n/0]).\n"
            "-record(r, {a = 1, b, a}).\n"
            "-record(r, {c}).\n"
            "-record(used, {x = 1, y}).\n"
            "-record(unused, {z}).\n"
            "f(X) -> X#used{x = 2, x = 3, w = 4}.\n"
            "g(X) -> {X#used.q, X#nothere.a, #user{}}.\n"
            "h(#used{x = X, _ = Y}) -> {X, Y}.\n"
            "k(X) -> {X#used{_ = 1}, #used{_ = 1, x = 1, y = 2}}.\n"
            "n() -> {record_info(fields, used), record_info(other, used), record_info(size, nope), #used.x}.\n"},
        {"record definition variables", none,
            "-module(m).\n"
            "-export([f/0]).\n"
            "-record(v, {a = X, b = fun() -> Y = 1, Y end}).\n"
            "f() -> #v{}.\n"},
        {"untyped records", [warn_untyped_record],
            "-module(m).\n"
            "-export([f/0]).\n"
            "-record(t, {a :: integer(), b}).\n"
            "-record(u, {a :: integer()}).\n"
            "-record(e, {}).\n"
            "f() -> {#t{}, #u{}, #e{}}.\n"},
        {"literal updates", none,
            "-module(m).\n"
            "-export([f/0]).\n"
            "-record(r, {a}).\n"
            "f() -> {#{a => 1}#{a => 2}, #r{a = 1}#r{a = 2}, (1)#r{a = 3}, ([])(1), 1(2), {}(3)}.\n"},
        {"types", none,
            "-module(m).\n"
            "-export([f/1]).\n"
            "-export_type([t/0, t/0, missing/0]).\n"
            "-type t() :: atom().\n"
            "-type t() :: integer().\n"
            "-type single(X) :: integer().\n"
            "-type unused() :: ok.\n"
            "-type uses() :: undefined_one() | bool() | 3..1 | <<_:-1>> | fun((a) -> b).\n"
            "-opaque hidden() :: integer().\n"
            "-type integer() :: atom().\n"
            "-type rec() :: #r{a :: integer()} | #nope{}.\n"
            "-record(r, {a}).\n"
            "-spec f(uses()) -> rec() | hidden().\n"
            "f(X) -> X.\n"},
        {"specs", none,
            "-module(m).\n"
            "-export([f/1, g/0]).\n"
            "-spec f(atom()) -> atom().\n"
            "-spec f(atom()) -> atom().\n"
            "-spec m:g() -> ok.\n"
            "-spec other:h() -> ok.\n"
            "-spec gone() -> ok.\n"
            "-spec f(a, b) -> c.\n"
            "f(X) -> X.\n"
            "g() -> ok.\n"},
        {"missing specs", [warn_missing_spec],
            "-module(m).\n"
            "-export([f/0, g/0]).\n"
            "-spec f() -> ok.\n"
            "f() -> ok.\n"
            "g() -> ok.\n"},
        {"callbacks", none,
            "-module(m).\n"
            "-export([behaviour_info/1]).\n"
            "-callback init(term()) -> ok.\n"
            "-callback init(term()) -> ok.\n"
            "-callback other:init() -> ok.\n"
            "-optional_callbacks([init/1, init/1, nothere/0]).\n"
            "behaviour_info(callbacks) -> [].\n"},
        {"behaviours", none,
            "-module(m).\n"
            "-behaviour(gen_server).\n"
            "-behaviour(nonexistent_behaviour_module).\n"
            "-behavior(erl_lint_checks_behaviour).\n"
            "-export([init/1, handle_call/3, format_status/2]).\n"
            "init(X) -> {ok, X}.\n"
            "handle_call(_, _, S) -> {reply, ok, S}.\n"
            "format_status(_, S) -> S.\n"},
        {"deprecated calls", none,
            "-module(m).\n"
            "-export([f/0, g/0, h/0]).\n"
            "f() -> {erlang:now(), now(), erlang:hash(a, 1), random:uniform(), http_uri:parse(x)}.\n"
            "g() -> fun erlang:now/0.\n"
            "-type t() :: erl_scan:line().\n"
            "-spec h() -> t().\n"
            "h() -> ok.\n"},
        {"deprecated calls suppressed",
            [{nowarn_deprecated_function, [{erlang, now, 0}]}, nowarn_removed],
            "-module(m).\n"
            "-export([f/0]).\n"
            "-compile({nowarn_deprecated_function, random}).\n"
            "f() -> {erlang:now(), random:uniform(), erlang:hash(a, 1)}.\n"},
        {"deprecated attributes", none,
            "-module(m).\n"
            "-export([f/0, g/1]).\n"
            "-deprecated([{f, 0}, {g, '_'}, {h, 1}, {f, 0, \"use g/1\"}, {f, 0, 42}, bad, module]).\n"
            "-removed([{g, 1}, {k, 0, \"gone\"}, bad]).\n"
            "-unsafe([{h, 2}, {f, 0, possibly}]).\n"
            "f() -> ok.\n"
            "g(_) -> ok.\n"},
        {"format strings", none,
            "-module(m).\n"
            "-export([f/1]).\n"
            "f(X) ->\n"
            "    io:format(\"~p~n\"),\n"
            "    io:format(\"~p ~p~n\", [X]),\n"
            "    io:format(\"plain~n\", [X]),\n"
            "    io:fwrite(\"~q~n\", [X]),\n"
            "    io_lib:format(\"~\", []),\n"
            "    io:format(user, \"~ltp~n\", [X]),\n"
            "    io:format(\"~*.*.*w~n\", [1, 2, 3, X]),\n"
            "    io:format(\"~p~n\", X),\n"
            "    io:format(X, []),\n"
            "    io:format(\"~p\", a),\n"
            "    io_lib:format(\"~kkp\", [X]),\n"
            "    io:format(a, b, c, d).\n"},
        {"format strings level 2", [{warn_format, 2}],
            "-module(m).\n-export([f/1]).\nf(X) -> io:format(X, []), io:format(\"~p\", [X | X]).\n"},
        {"format strings off", [nowarn_format],
            "-module(m).\n-export([f/1]).\nf(X) -> io:format(\"~p ~p\", [X]).\n"},
        {"after traversal", none,
            "-module(m).\n"
            "-export([f/0]).\n"
            "-on_load(init/1).\n"
            "-on_load(other/0).\n"
            "-nifs([f/0, nif/1]).\n"
            "-compile({inline, [nothere/0, f/0]}).\n"
            "-compile({nowarn_unused_function, [gone/2]}).\n"
            "-dialyzer({nowarn_function, [missing/0]}).\n"
            "-dialyzer([bogus_option, {no_match, f/0}, {no_return, 3}]).\n"
            "-export_record([a, 3]).\n"
            "init(_) -> ok.\n"
            "f() -> ok.\n"},
        {"nifs without load", none, "-module(m).\n-export([f/0]).\n-nifs([f/0]).\nf() -> ok.\n"},
        {"nowarn bif clash", none,
            "-module(m).\n-compile(nowarn_bif_clash).\n-compile({nowarn_bif_clash, {length, 1}}).\nf() -> ok.\n"},
        {"catch and boolean operators", [warn_obsolete_bool_op],
            "-module(m).\n-export([f/2]).\nf(X, Y) -> {catch X, X and Y, X or Y}.\n"},
        {"documentation attributes", none,
            "-module(m).\n"
            "-moduledoc \"a\".\n"
            "-moduledoc \"b\".\n"
            "-export([f/0]).\n"
            "-doc \"one\".\n"
            "-doc \"two\".\n"
            "f() -> ok.\n"},
        {"keywords", [warn_keywords],
            "-module(m).\n-export([f/0]).\nf() -> {'maybe', else, maybe, ok}.\n", [
                {reserved_word_fun, fun(W) -> W =:= 'when' end}
            ]},
        {"misplaced attributes", none,
            "-module(m).\n-export([f/0]).\nf() -> ok.\n-behaviour(gen_server).\n-foo(bar).\n"},
        {"unused vars off", [nowarn_unused_vars, nowarn_shadow_vars, nowarn_unused_function],
            "-module(m).\n-export([f/1]).\nf(X) -> Y = 1, F = fun(X) -> X end, F.\nu() -> ok.\n"},
        {"named file", {file, "named.erl"}, "-module(m).\n-export([f/0]).\nf() -> X.\n"},
        {"errors and warnings in forms", none,
            "-module(m).\n-export([f/0]).\nf() -> ok\n.\ng( -> ok.\n"},
        {"native records", none,
            "-module(m).\n"
            "-export([f/1, g/1, h/1]).\n"
            "-record #n{a = 1, b, c = self()}.\n"
            "-record #n{d}.\n"
            "-import_record(other, [ext, n]).\n"
            "-import_record(again, [ext]).\n"
            "-export_record([n, n]).\n"
            "f(X) -> {#n{a = 2}, #n{z = 1}, X#n.z, X#n{z = 2}, #n.a, record_info(size, n)}.\n"
            "g(#n{z = Z}) -> Z.\n"
            "h(X) when X =:= #n{} -> {#ext{a = 1}, X#ext.b, #_{a = 1}}.\n"},
        {"record_info fun", none, "-module(m).\n-export([f/0]).\nf() -> fun record_info/2.\n"}
    ].

expressions() ->
    Exprs = fun(Text) ->
        {ok, Tokens, _} = erl_scan:string(Text, {1, 1}),
        {ok, Es} = erl_parse:parse_exprs(Tokens),
        Es
    end,
    Commands = [
        "X + Y.",
        "A = 1, A.",
        "case x of a -> B = 1; b -> ok end, B.",
        "#r{}.",
        "<<X/foo>>.",
        "fun(Z) -> Z + W end.",
        "[X || {X, _} <- L].",
        "try ok catch _:_:S when S -> S end."
    ],
    lists:foreach(
        fun(Text) ->
            Es = Exprs(Text),
            io:format("exprs ~ts ~p ~p~n", [
                Text, erl_lint:exprs(Es, [{'X', 1}]), erl_lint:exprs_opt(Es, [], [])
            ])
        end,
        Commands
    ),
    Funs = Exprs("F = fun(A) -> {A, B, fun() -> C end} end, G = fun() -> D end."),
    io:format("used_vars ~p~n", [
        lists:sort(maps:to_list(erl_lint:used_vars(Funs, [{'B', 1}, {'C', 2}, {'D', 3}])))
    ]),
    Guards = [
        "is_atom(X).",
        "atom(X).",
        "record(X, r).",
        "is_record(X, r).",
        "X > 1 andalso Y.",
        "f(X).",
        "erlang:abs(X) > 2.",
        "{erlang, is_list}(X).",
        "X ! Y.",
        "[X | Y].",
        "#{a => X}.",
        "<<X:8>>.",
        "is_record(X, r, 2).",
        "X#r.a.",
        "length(X) + 1."
    ],
    lists:foreach(
        fun(Text) ->
            [E] = Exprs(Text),
            io:format("guard ~ts ~p ~p~n", [
                Text, erl_lint:is_guard_test(E), erl_lint:is_guard_expr(E)
            ])
        end,
        Guards
    ),
    %% OTP's is_guard_expr/1 has no clause for a record construction.
    [Record] = Exprs("#r{a = 1}."),
    io:format("guard record ~p~n", [erl_lint:is_guard_test(Record)]),
    Descriptors = [
        undefined_module,
        {unbound_var, 'X'},
        {unsafe_var, 'Y', {'case', {3, 4}}},
        {unsafe_var, 'Y', {'try', 7}},
        {format_error, {"~p and ~p", [a, b]}},
        {deprecated, {m, f, 1}, "use g/1", "OTP 30"},
        {deprecated, {m, f, 1}, next_major_release},
        {removed, {m, f, [1, 2]}, "gone"},
        {undefined_field, {m, r}, a},
        {redefine_field, r, a},
        {undefined_function, {f, 1}, {g, [1, 2]}},
        {undefined_type, {t, 3}},
        {deprecated_builtin_type, {t, 1}, {m, n}, "OTP 30"},
        {deprecated_builtin_type, {t, 1}, {m, n, 2}, "OTP 30"},
        {obsolete_bool_op, "'and'", "'andalso'"},
        {unsafe, {m, f, 0}, possibly},
        {bad_inline, {f, 1}, {g, [1]}},
        {conflicting_behaviours, {init, 1}, b, {4, 2}, a},
        {undefined_on_load, {i, 0}, ini}
    ],
    [io:format("text ~p~n  ~ts~n", [D, erl_lint:format_error(D)]) || D <- Descriptors].
