%% erl_eval over parsed command strings (plan 21 step LIB32): each command is scanned and parsed, then evaluated with
%% exprs/2 (which checks it first) and exprs/3 (which does not). Funs, maps and stack traces are printed in a stable
%% shape: funs as 'fun', maps as sorted lists.
-module(erl_eval_exprs).
-export([main/1]).

main(_) ->
    [command(S) || S <- commands()],
    bindings(),
    handlers(),
    results(),
    funs(),
    bits(),
    constants(),
    strings(),
    extended(),
    io:format("erl_eval_exprs: ok~n").

commands() ->
    [
        "X.",
        "X = 1, Y.",
        "case a of a -> Z = 1; b -> ok end, Z.",
        "foo(1).",
        "#r{}.",
        "X = 1, X = 2.",
        "fun(X) -> X end(1, 2).",
        "if foo() -> 1 end.",
        "[X || X <- [1,2], foo(X)].",
        "case 1 of X when foo(X) -> 1 end.",
        "fun() -> Y end.",
        "lists:map(fun(X) -> X + 1 end, [1,2]).",
        "try 1 catch C:R:S when S =:= 1 -> 2 end.",
        "S = 1, try ok catch C:R:S -> ok end.",
        "A = 1, fun(A) -> A end(2).",
        "{X, X} = {1, 2}.",
        "1 + a.",
        "erlang:foo().",
        "apply(lists, reverse, [[1, 2]]).",
        "receive after 0 -> t end.",
        "[1 | 2] ++ [].",
        "f = 1.",
        "X = fun F(0) -> 0; F(N) -> N + F(N - 1) end, X(4).",
        "maybe {ok, A} ?= {ok, 1}, A end.",
        "maybe {ok, A} ?= {err, 1}, A else {err, E} -> E end.",
        "maybe {ok, A} ?= {err, 1}, A else {bad, E} -> E end.",
        "maybe X = 1, ok end, X.",
        "#{a => 1}#{a := 2}.",
        "M = #{a => 1}, M#{b := 2}.",
        "M = #{a => 1, b => 2}, #{a := A} = M, A.",
        "#{a => X} = #{a => 1}.",
        "K = a, #{K := V} = #{a => 9}, V.",
        "<< <<X>> || <<X>> <= <<1,2,3>> >>.",
        "[{X, Y} || X <- [1,2] && Y <- [a,b]].",
        "[{X, Y} || X <- [1,2,3] && Y <- [a,b]].",
        "[{X, Y} || {X} <- [{1},2,{3}] && Y <- [a,b,c]].",
        "[X || X <- [1,2] && X <- [1,3]].",
        "[X || {X} <:- [{1}] && X <- [2]].",
        "[X || {X} <- [{1}, 2] && X <:- [1, 3]].",
        "[X || {X} <- [{1}, b] && {X} <:- [{1}, {3}]].",
        "[{X, Y} || X <- [1, 2] && <<Y>> <= <<5, 6>> && _ := Y <- #{k => 7}].",
        "[{X, Y} || X <:- [1, 2] && Y <- [a]].",
        "[X || X <- [1, 2], _ <- [] && _ <- [a]].",
        "[{K, V} || K := V <- #{a => 1}].",
        "#{V => K || K := V <- #{a => 1, b => 2}}.",
        "[X || X <- [1, 2, 3], X > 1].",
        "[X || X <- [1, 2, 3], X].",
        "[X || X <- [1, 2, 3], is_odd(X)].",
        "[X || X <- [1, 2, 3], X rem 2 =:= 1].",
        "[X || X <- a].",
        "[X || {X} <:- [{1}, 2]].",
        "[X || <<X>> <:= <<1, 2:4>>].",
        "[X || X <- [1, 2 | c]].",
        "[{X, Y} || X <- [1, 2], Y <- [X, X * 10]].",
        "<<1:4>>.",
        "<<\"abc\"/utf16>>.",
        "<<X:4, _/bits>> = <<16#ab>>, X.",
        "<<A/binary>> = <<1,2>>, A.",
        "<<A:2/binary, B/binary>> = <<1,2,3>>, {A, B}.",
        "<<-1/float>>.",
        "<<1:(-1)>>.",
        "<<a>>.",
        "<<1/foo>>.",
        "<<1/utf8-unit:8>>.",
        "<<1:8/integer-integer>>.",
        "<<1:8/integer-float>>.",
        "<<X:Y>> = <<1>>.",
        "<<A/binary, B>> = <<1>>.",
        "catch throw(x).",
        "catch exit(x).",
        "begin X = 1, Y = 2, X + Y end.",
        "fun lists:reverse/1.",
        "fun length/1.",
        "\"ab\" ++ X = \"abc\", X.",
        "[$a] ++ X = \"abc\", X.",
        "X ++ [1] = [1, 2].",
        "-1 = -1.",
        "1 + 2 = 3.",
        "X + 1 = 2.",
        "foo() = 1.",
        "a andalso b.",
        "true andalso b.",
        "x orelse b.",
        "false orelse (1 < 2).",
        "true andalso (X = true), X.",
        "if 1 > 2 -> a end.",
        "if 1 > 2 -> a; true -> b end.",
        "case x of y -> 1 end.",
        "try x of y -> 1 catch _ -> 2 end.",
        "try throw(t) catch t -> caught end.",
        "try error(e) catch error:e:S when is_list(S) -> stack end.",
        "try exit(x) catch throw:_ -> no end.",
        "try 1 of 1 -> throw(of_body) catch throw:_ -> no end.",
        "try 1 after self() ! done end, receive done -> ok end.",
        "try X = 1 of _ -> ok catch _ -> ok end, X.",
        "self() ! hi, receive hi -> got end.",
        "self() ! {m, 2}, receive {m, N} when N > 1 -> N; {m, _} -> small end.",
        "receive nothing -> x after 5 -> timeout end.",
        "X = 1, fun() -> X end().",
        "F = fun(X) when is_integer(X) -> X end, F(a).",
        "F = fun(X) when integer(X) -> X end, F(3).",
        "length(a).",
        "erlang:length(a).",
        "abs(-1).",
        "node().",
        "element(1, {a}).",
        "{X, Y} = {1, 2}, X + Y.",
        "A = [1,2], [H|T] = A, {H, T}.",
        "M = lists, F = reverse, M:F([1, 2]).",
        "F = {lists, reverse}, F([1]).",
        "F = 3, F().",
        "fun(A, B) -> A + B end(1, 2).",
        "(fun(A) -> A end)(1).",
        "A = 5, case A of 5 -> five; _ -> other end.",
        "case {1, 2} of {X, Y} when X < Y -> lt; _ -> ge end.",
        "case 3 of X when X > 5; X < 4 -> yes end.",
        "case [] of X when length(X) > 0 -> some; _ -> none end.",
        "case a of _ when [] ++ [] =:= [] -> 1 end.",
        "case a of _ when is_record(a, r) -> 1 end.",
        "case a of _ when lists:member(a, []) -> 1 end.",
        "X#r.a.",
        "#r.a.",
        "{1, 2} =:= {1, 2.0}.",
        "{1, 2} == {1, 2.0}.",
        "Pid = self(), is_pid(Pid).",
        "erlang:raise(error, x, []).",
        "{a, X = 1, X}.",
        "f(X = 1, X).",
        "[1, 2, 3] -- [2].",
        "2 * 3 + 4 div 3 - 7 rem 4.",
        "1 bsl 70.",
        "bnot 5.",
        "not true.",
        "- (3).",
        "+ 3.",
        "\"abc\".",
        "$a.",
        "'quoted atom'.",
        "1.5e3.",
        "16#ff.",
        "[].",
        "{}.",
        "[1, 2 | [3]].",
        "<<>>.",
        "#{}.",
        "#{1 => a, 1.0 => b}.",
        "{ok, X} = {ok, 1}, {error, Y} = {error, X}, Y.",
        "X = 1, Y = fun() -> X + 1 end, Y().",
        "F = fun(G) -> G(G) end, F(fun(_) -> ok end).",
        "lists:foldl(fun(X, {A, B}) -> {A + X, [X | B]} end, {0, []}, [1, 2, 3]).",
        "X = 1, self() ! 1, receive X -> yes after 0 -> no end.",
        "X = 2, self() ! 1, receive X -> yes after 0 -> no end.",
        "T = infinity, self() ! m, receive m -> got after T -> never end.",
        "erlang:'+'(1, 2).",
        "apply(fun(A) -> A end, [1]).",
        "apply(erlang, apply, [lists, reverse, [[1, 2]]]).",
        "catch 1 / 0.",
        "catch error(e).",
        "1 / 0.",
        "M = #{a => #{b => 1}}, #{a := #{b := B}} = M, B.",
        "M = #{a => 1}, M#{a := 2, b => 3}.",
        "[X || {X, _} <- [{1, a}, {2, b}]].",
        "case #{a => 1} of #{a := X} when X > 0 -> X end.",
        "fun(X) -> X end =:= fun(X) -> X end.",
        "A = 1, B = 2, fun() -> A end =:= fun() -> A end.",
        "try error(x) after self() ! cleanup end.",
        "receive cleanup -> cleaned after 0 -> none end.",
        "F = fun(0, Acc) -> Acc; (N, Acc) -> N * Acc end, F(3, 4).",
        "fun F(N) when N > 0 -> F(N - 1); F(_) -> done end(5).",
        "G = fun(X) -> fun(Y) -> X + Y end end, (G(1))(2).",
        "[F(2) || F <- [fun(X) -> X * 2 end, fun(X) -> X + 2 end]].",
        "{A, B} = {1, 2}, [A, B | [3]].",
        "X = [1, 2, 3], [Y * 2 || Y <- X, Y =/= 2].",
        "case {a, b} of {a, _} = T -> T end.",
        "{a, _} = T = {a, b}, T.",
        "<<A:8, _/binary>> = B = <<1, 2>>, {A, B}.",
        "\"abc\" = [$a, $b, $c].",
        "'a' = a.",
        "1.0 = 1.",
        "X = 1.0, X == 1.",
        "-0.0 = 0.0.",
        "begin X = 1, X end + 1.",
        "if is_atom(a), is_integer(1) -> both; true -> no end.",
        "if is_atom(1); is_integer(1) -> one; true -> no end.",
        "case 1 of N when N > 0, N < 2 -> one end.",
        "maybe ok ?= ok, {ok, B} ?= {ok, 2}, B end.",
        "maybe {ok, B} ?= {ok, 2}, B + 1 else _ -> no end."
    ].

command(S) ->
    Es = parse(S),
    io:format("~ts~n  2: ~p~n  3: ~p~n  m: ~p~n", [
        S,
        run(fun() -> erl_eval:exprs(Es, []) end),
        run(fun() -> erl_eval:exprs(Es, [], none) end),
        run(fun() -> erl_eval:exprs(Es, #{}) end)
    ]).

run(F) ->
    try F() of
        {value, V, Bs} -> {value, clean(V), clean(Bs)};
        Other -> {other, clean(Other)}
    catch
        Class:Reason -> {Class, clean(Reason)}
    end.

parse(S) ->
    {ok, Ts, _} = erl_scan:string(S),
    {ok, Es} = erl_parse:parse_exprs(Ts),
    Es.

%% Funs as 'fun', maps as sorted lists, pids as 'pid', stack traces (lists of stack frames) as 'stack'.
clean(F) when is_function(F) -> 'fun';
clean(P) when is_pid(P) -> pid;
clean(M) when is_map(M) -> {map, lists:sort([{clean(K), clean(V)} || K := V <- M])};
clean({'EXIT', {Reason, Stack}}) when is_list(Stack) -> {'EXIT', {clean(Reason), stack}};
clean([{M, F, A, Info} | _]) when
    is_atom(M), is_atom(F), is_list(Info), (is_integer(A) orelse is_list(A))
->
    stack;
clean(T) when is_tuple(T) -> list_to_tuple(clean(tuple_to_list(T)));
clean([H | T]) ->
    [clean(H) | clean(T)];
clean(X) ->
    X.

bindings() ->
    B0 = erl_eval:new_bindings(),
    B1 = erl_eval:add_binding('B', 2, erl_eval:add_binding('A', 1, B0)),
    io:format("orddict: ~p ~p ~p ~p~n", [
        B0, erl_eval:bindings(B1), erl_eval:binding('A', B1), erl_eval:binding('C', B1)
    ]),
    io:format("orddict del: ~p~n", [erl_eval:del_binding('A', B1)]),
    M1 = erl_eval:add_binding('B', 2, erl_eval:add_binding('A', 1, #{})),
    io:format("map: ~p ~p ~p~n", [
        lists:sort(erl_eval:bindings(M1)), erl_eval:binding('A', M1), erl_eval:binding('C', M1)
    ]),
    io:format("map del: ~p~n", [clean(erl_eval:del_binding('A', M1))]),
    io:format("new: ~p ~p~n", [
        erl_eval:new_bindings([{'A', 1}]), erl_eval:new_bindings(#{'A' => 1})
    ]),
    io:format("map exprs: ~p~n", [run(fun() -> erl_eval:exprs(parse("X = 1, Y = X + 1."), #{}) end)]),
    io:format("map exprs: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("Y = X + 1."), #{'X' => 3}) end)
    ]),
    io:format("map merge: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("{X = 1, X = 2}."), #{}, none) end)
    ]),
    io:format("list merge: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("{X = 1, X = 2}."), [], none) end)
    ]),
    io:format("bound: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("X + Y."), [{'X', 1}, {'Y', 2}]) end)
    ]).

handlers() ->
    Value = {value, fun(N, As) -> {local, N, As} end},
    io:format("lf value: ~p~n", [run(fun() -> erl_eval:exprs(parse("foo(1, 2)."), [], Value) end)]),
    io:format("lf value bif: ~p~n", [run(fun() -> erl_eval:exprs(parse("abs(-2)."), [], Value) end)]),
    Eval = {eval, fun(N, As, Bs) -> {value, {N, As}, erl_eval:add_binding('Z', 1, Bs)} end},
    io:format("lf eval: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("foo(X, 2)."), [{'X', 9}], Eval) end)
    ]),
    io:format("lf eval fun: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("F = fun(A) -> bar(A) end, F(5)."), [], Value) end)
    ]),
    Trace = fun(Spec, As) ->
        io:format("  ef ~p ~p~n", [clean(Spec), clean(As)]),
        case Spec of
            {M, F} -> apply(M, F, As);
            Fun -> apply(Fun, As)
        end
    end,
    Ef = {value, Trace},
    Calls =
        "lists:reverse([1]), 1 + 2, length([]), F = fun(X) -> X end, F(1), apply(fun erlang:abs/1, [-1]), "
        "G = fun erlang:abs/1, G(-3), apply(lists, sort, [[2, 1]]), erlang:apply(erlang, abs, [-4]).",
    io:format("ef: ~p~n", [run(fun() -> erl_eval:exprs(parse(Calls), [], none, Ef) end)]),
    io:format("ef error: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("X = 1, X = 2."), [], none, Ef) end)
    ]),
    io:format("ef guard: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("if 1 < 2 -> yes end."), [], none, Ef) end)
    ]),
    Ef3 =
        {value, fun(Anno, Spec, As) ->
            io:format("  ef3 ~p ~p ~p~n", [Anno, Spec, As]),
            {M, F} = Spec,
            apply(M, F, As)
        end},
    io:format("ef3: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("lists:reverse([1])."), [], none, Ef3) end)
    ]),
    Deny =
        {value, fun
            ({erlang, raise}, [C, R, _]) -> erlang:raise(C, {denied, R}, []);
            ({os, _}, _) -> exit(forbidden);
            ({M, F}, As) -> apply(M, F, As)
        end},
    io:format("deny: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("os:getpid()."), [], none, Deny) end)
    ]),
    io:format("deny raise: ~p~n", [
        run(fun() -> erl_eval:exprs(parse("case 1 of 2 -> x end."), [], none, Deny) end)
    ]),
    io:format("old lf: ~p~n", [
        run(fun() ->
            erl_eval:exprs(parse("foo(1)."), [], {value, fun(N, As, X) -> {N, As, X} end, [extra]})
        end)
    ]).

results() ->
    [E] = parse("1 + 2."),
    io:format("expr value: ~p~n", [erl_eval:expr(E, [], none, none, value)]),
    io:format("expr none: ~p~n", [erl_eval:expr(E, [], none, none, none)]),
    io:format("expr/2: ~p~n", [run(fun() -> erl_eval:expr(E, []) end)]),
    [U] = parse("X."),
    io:format("expr/2 unbound: ~p~n", [run(fun() -> erl_eval:expr(U, []) end)]),
    io:format("expr/3 unbound: ~p~n", [run(fun() -> erl_eval:expr(U, [], none) end)]),
    io:format("expr_list: ~p~n", [erl_eval:expr_list(parse("X = 1, X = 1, Y = 2."), [])]),
    io:format("expr_list conflict: ~p~n", [
        run(fun() -> erl_eval:expr_list(parse("X = 1, X = 2."), []) end)
    ]),
    io:format("expr_list map: ~p~n", [clean(erl_eval:expr_list(parse("X = 1, Y = 2."), #{}))]),
    io:format("expr_list/4: ~p~n", [erl_eval:expr_list(parse("a, b."), [], none, none)]),
    Cs = [C || {'case', _, _, C} <- parse("case x of {A, B} when A > 1 -> A + B; _ -> no end.")],
    io:format("match_clause: ~p~n", [
        [erl_eval:match_clause(hd(Cs), [V], [], none) || V <- [{2, 3}, {1, 3}, z]]
    ]),
    io:format("check: ~p~n", [
        [erl_eval:check_command(parse(S), []) || S <- ["X.", "1.", "foo(1).", "#r{}."]]
    ]),
    io:format("check bound: ~p~n", [erl_eval:check_command(parse("X."), [{'X', 1}])]),
    R = records:create(m, r, [{a, 1}, {b, 2}], #{is_exported => true}),
    [
        io:format("record ~ts~n  ~p~n", [
            S, run(fun() -> erl_eval:exprs(parse(S), [{'R', R}], none) end)
        ])
     || S <- [
            "R#m:r.a.",
            "R#_.b.",
            "R#m:x.a.",
            "R#m:r.z.",
            "is_record(R).",
            "#m:r{a = A} = R, A.",
            "case R of #m:r{b = 3} -> three; #m:r{b = B} -> B end.",
            "R2 = R#m:r{a = 10}, R2#m:r.a.",
            "R#_{b = 20}.",
            "#m:r{a = 1}.",
            "X = 1, X#m:r.a."
        ]
    ],
    [
        io:format("record check: ~p~n", [erl_eval:check_command(parse(S), [{'R', R}])])
     || S <- ["R#m:r.a.", "R#r.a.", "#r{a = 1}."]
    ].

funs() ->
    {value, F, _} = erl_eval:exprs(parse("fun(X) -> X + 1 end."), []),
    io:format("map fun: ~p~n", [lists:map(F, [1, 2, 3])]),
    io:format("fun info: ~p~n", [[erlang:fun_info(F, I) || I <- [module, arity, type]]]),
    {env, [{_, EnvBs, EnvLf, EnvEf, EnvFuvs, EnvCs}]} = erlang:fun_info(F, env),
    io:format("fun env: ~p~n", [{EnvBs, EnvLf, EnvEf, clean(EnvFuvs), EnvCs}]),
    io:format("fun_data: ~p~n", [erl_eval:fun_data(F)]),
    {value, F2, _} = erl_eval:exprs(parse("A = 1, B = 2, fun(X) -> X + A end."), []),
    io:format("fun_data: ~p~n", [erl_eval:fun_data(F2)]),
    {value, F3, _} = erl_eval:exprs(parse("A = 1, fun G(X) -> X + A end."), []),
    io:format("fun_data: ~p~n", [erl_eval:fun_data(F3)]),
    {value, F4, _} = erl_eval:exprs(
        parse("W = 1, Y = 2, fun(X) -> fun(Z) -> X + Z + W end end."), []
    ),
    {env, [{_, _, _, _, Fuvs4, _}]} = erlang:fun_info(F4, env),
    io:format("nested: ~p ~p~n", [(F4(10))(100), clean(Fuvs4)]),
    io:format("fun_data other: ~p ~p~n", [
        erl_eval:fun_data(fun lists:reverse/1), erl_eval:fun_data(x)
    ]),
    {value, F5, _} = erl_eval:exprs(parse("fun F(0) -> []; F(N) -> [N | F(N - 1)] end."), []),
    io:format("named: ~p~n", [F5(3)]),
    io:format("arity: ~p~n", [run(fun() -> F(1, 2) end)]),
    io:format("clause: ~p~n", [run(fun() -> F(a) end)]),
    {value, F6, _} = erl_eval:exprs(
        parse("fun(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T) -> T end."), []
    ),
    io:format("arity 20: ~p ~p~n", [
        erlang:fun_info(F6, arity),
        F6(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20)
    ]),
    Many = "fun(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U) -> U end.",
    io:format("arity 21: ~p~n", [element(1, run(fun() -> erl_eval:exprs(parse(Many), []) end))]),
    {value, F7, _} = erl_eval:exprs(parse("fun foo/2."), [], {value, fun(N, As) -> {N, As} end}),
    io:format("local fun: ~p~n", [F7(1, 2)]),
    {value, F8, _} = erl_eval:exprs(parse("fun() -> self() ! from_fun, ok end."), []),
    Pid = spawn(fun() ->
        receive
            go -> F8()
        end
    end),
    Pid ! go,
    io:format("spawned: ~p~n", [is_pid(Pid)]),
    {value, Sum, _} = erl_eval:exprs(
        parse("lists:foldl(fun(X, Acc) -> X + Acc end, 0, [1, 2, 3])."), []
    ),
    io:format("fold: ~p~n", [Sum]).

bits() ->
    Cases = [
        "<<1, 2, 3>>.",
        "<<256:16/little, 1:1, 0:7>>.",
        "<<-1:8/signed, 1>>.",
        "<<3.5/float, 1.0:32/float-little>>.",
        "<<1:16/float>>.",
        "<<1:13/float>>.",
        "<<\"ab\"/binary>>.",
        "<<\"abc\":2>>.",
        "<<\"\":2>>.",
        "B = <<1, 2, 3>>, <<B:2/binary>>.",
        "B = <<1, 2, 3>>, <<B:4/binary>>.",
        "B = <<1:3>>, <<B/binary>>.",
        "B = <<1:3>>, <<B/bitstring>>.",
        "<<16#10FFFF/utf8, 128/utf16, 65/utf32-little>>.",
        "<<16#110000/utf8>>.",
        "<<X/utf8, R/binary>> = <<240,159,152,128,1>>, {X, R}.",
        "<<X:16/signed-little, Y/float>> = <<255, 255, 64, 0, 0, 0, 0, 0, 0, 0>>, {X, Y}.",
        "<<X:2/unit:8, _/binary>> = <<1, 2, 3>>, X.",
        "<<S:8, B:S/binary, R/binary>> = <<2, 7, 8, 9>>, {S, B, R}.",
        "<<\"ab\", R/binary>> = <<\"abc\">>, R.",
        "<<\"ab\"/utf16, R/binary>> = <<0, 97, 0, 98, 1>>, R.",
        "<<1.5/float>> = <<1.5/float>>.",
        "<<1:32/float>> = <<1.0:32/float>>.",
        "case <<1, 2>> of <<1, X>> -> X; _ -> no end.",
        "case <<1, 2>> of <<2, X>> -> X; _ -> no end.",
        "case <<1>> of <<X:16>> -> X; _ -> short end.",
        "<< <<(X * 2)>> || <<X>> <= <<1, 2, 3>> >>.",
        "[X || <<X:4>> <= <<16#12, 16#3:4>>].",
        "[X || <<X:4>> <= <<16#12, 16#3:3>>].",
        "[{X, Y} || <<X, Y>> <= <<1, 2, 3, 4, 5>>].",
        "[X || <<1, X>> <= <<1, 2, 3, 4, 1, 6>>].",
        "[X || <<X/float>> <= <<1.0/float, 1:64>>].",
        "<< <<X:3>> || X <- [1, 2, 3] >>.",
        "<< X || X <- [1] >>.",
        "<<(1 + 2):8, (-1):4>>.",
        "Sz = 3, <<5:Sz>>.",
        "Sz = a, <<5:Sz>>.",
        "<<5:all>>.",
        "<<X:a>> = <<1>>."
    ],
    [
        io:format("bits ~ts~n  ~p~n", [S, run(fun() -> erl_eval:exprs(parse(S), [], none) end)])
     || S <- Cases
    ],
    ok.

constants() ->
    Es = parse("1 + 2, -3, a, X, {1}, 2.0 * 3, foo(1), 1 / 0, [1], 1 > 2, not true, $a + 1."),
    io:format("partial: ~p~n", [[erl_eval:partial_eval(E) || E <- Es]]),
    io:format("constant: ~p~n", [[erl_eval:is_constant_expr(E) || E <- Es]]).

strings() ->
    Strings = [
        "1 + 2.",
        "1 + 2. ",
        "1 +",
        "1 + . ",
        "X. ",
        "1. 2. ",
        "1/0. ",
        "throw(a). ",
        "exit(a). ",
        <<"3. ">>,
        "1 + 2.  \n ",
        "\"abc\" ++ \"d\". "
    ],
    io:format("eval_str: ~p~n", [[erl_eval:eval_str(S) || S <- Strings]]),
    io:format("eval_str scan error: ~p~n", [run(fun() -> erl_eval:eval_str("'a. ") end)]).

extended() ->
    Pid = pid_to_list(self()),
    Ref = make_ref(),
    Items =
        "{" ++ Pid ++ ", " ++ ref_to_list(Ref) ++ ", #Fun<lists.reverse.1>, #Fun<erlang.abs.1>}.",
    {ok, {P, R, F, A}} = erl_eval:extended_parse_term(tokens(Items)),
    io:format("extended items: ~p~n", [{P =:= self(), R =:= Ref, F =:= fun lists:reverse/1, A(-1)}]),
    Texts = [
        "[1, a, \"b\"].",
        "{1, X}.",
        "1. 2.",
        "<<1, 2>>.",
        "-1.",
        "fun lists:sort/1.",
        "#{a := 1}.",
        "#{a => [1]}.",
        "#Fun<erl_eval.1.2>.",
        "{<0.1.2.3>}."
    ],
    [
        io:format("extended ~ts~n  ~p~n", [T, clean(erl_eval:extended_parse_term(tokens(T)))])
     || T <- Texts
    ],
    {ok, [Call]} = erl_eval:extended_parse_exprs(tokens("f(" ++ Pid ++ ").")),
    {call, 1, {atom, 1, f}, [
        {call, 1, {remote, 1, {atom, 1, erlang}, {atom, 1, list_to_pid}}, [{string, 1, S}]}
    ]} =
        Call,
    io:format("extended exprs: ~p~n", [S =:= Pid]),
    io:format("extended bad: ~p~n", [erl_eval:extended_parse_exprs(tokens("f(<0.1.2.3>)."))]),
    io:format("extended error: ~p~n", [erl_eval:extended_parse_exprs(tokens("f(."))]).

tokens(Text) ->
    {ok, Ts, _} = erl_scan:string(Text, 1, [text]),
    Ts.
