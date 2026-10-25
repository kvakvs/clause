-module(qlc_pt_transforms).
-moduledoc """
The qlc parse transform at run time (plan 21 step LIB73): qlc_pt:transform_expression/2,
transform_from_evaluator/2 and parse_transform/2 over queries and modules. Each result prints as
an md5 digest of its external format, with the analysis parts (the qualifier table and the options
fun) through erl_pp and errors and warnings in full.
""".
-export([main/1]).

main(_) ->
    lists:foreach(fun expression/1, expressions()),
    lists:foreach(fun module/1, modules()),
    io:format("qlc_pt_transforms: ok~n").

expressions() ->
    [
        {"[X || X <- [1, 2, 3]]", []},
        {"[X || X <- L, true]", ['L']},
        {"[{X, Y} || {X, Y} <- L, X =:= 1]", ['L']},
        {"[X || {X, a} <- L, X == 1]", ['L']},
        {"[{A, B} || {A, B} <- L, A =:= K, B =:= 2]", ['L', 'K']},
        {"[X || {X, _} <- ets:table(T), X > 2]", ['T']},
        {"[{X, Z} || {X, Y} <- L, {Y, Z} <- M]", ['L', 'M']},
        {"[X || X <- L, Y <- M, X =:= Y]", ['L', 'M']},
        {"[X || {X, Y} <- L, {Z, W} <- M, X == Z, Y =:= W]", ['L', 'M']},
        {"[element(1, T) || T <- L, element(2, T) =:= a, is_integer(element(1, T))]", ['L']},
        {"[X || X <- L, (X =:= 1) or (X =:= 2)]", ['L']},
        {"[{X, Y} || X <- qlc:q([A || {A} <- L]), Y <- M, X =:= Y]", ['L', 'M']},
        {"[X || X <- L, foo(X)]", ['L']},
        {"[X || {X, Y} <- L, Y = X + 1]", ['L']},
        {"[X || X <- L, Y <- X]", ['L']},
        {"[X || <<X>> <= B]", ['B']},
        {"[X || X <:- L]", ['L']},
        {"[X || X <- L, Z]", ['L']},
        {"foo", []}
    ].

expression({Text, Vars}) ->
    {ok, Tokens, _} = erl_scan:string(Text ++ "."),
    {ok, [Expr]} = erl_parse:parse_exprs(Tokens),
    Bindings = [{V, 1} || V <- Vars],
    io:format("~s~n", [Text]),
    report(qlc_pt:transform_expression(Expr, Bindings)),
    io:format("  evaluator: ~s~n", [
        describe_evaluator(qlc_pt:transform_from_evaluator(Expr, Bindings))
    ]).

describe_evaluator({ok, Expr}) -> ["ok ", digest(Expr)];
describe_evaluator(Error) -> io_lib:format("~0p", [Error]).

report({ok, Expr}) ->
    io:format("  ok ~s~n", [digest(Expr)]),
    parts(Expr);
report(Other) ->
    io:format("  ~0p~n", [Other]).

%% The #qlc_lc{} the query became: a simple_v1 tuple in full, else the qualifier table and the options fun.
parts({call, _, _, [{tuple, _, [_, {'fun', _, {clauses, [{clause, _, [], [], [Value]}]}}, _]}]}) ->
    case Value of
        {tuple, _, [{atom, _, simple_v1} | _]} ->
            io:format("  ~s~n", [erl_pp:expr(Value)]);
        {tuple, _, [{atom, _, qlc_v1}, _QFun, _CodeF, Qdata, Opt]} ->
            io:format("  qdata ~s~n  opt ~s~n", [erl_pp:expr(Qdata), erl_pp:expr(Opt)])
    end.

modules() ->
    [
        {join, [],
            "f(L, M) -> qlc:q([{X, Z} || {X, Y} <- L, {Y2, Z} <- M, Y =:= Y2]).\n"
            "g(T, K) -> qlc:q([V || {K2, V} <- ets:table(T), K2 =:= K]).\n"},
        {generator_variable, [], "f(L) -> qlc:q([X || X <- L, Y <- X, Y > 1]).\n"},
        {not_a_query, [], "f() -> qlc:q(foo).\n"},
        {unused, [], "f(L) -> qlc:q([X || {X, Y} <- L]).\n"},
        {complex_join, [],
            "f(A, B, C) -> qlc:q([{X, Y, Z} || {X} <- A, {Y} <- B, {Z} <- C, X =:= Y, Y =:= Z]).\n"},
        {imported, [], "-import(qlc, [q/1]).\nf(L) -> q([X || X <- L, X > 1]).\n"},
        {nested, [], "f(L) -> qlc:q([Y || X <- L, Y <- qlc:q([Z || Z <- X, Z > 1])]).\n"},
        {type_checker, [type_checker], "f(L) -> qlc:q([X || X <- L, X > 1]).\n"}
    ].

module({Name, Options, Text}) ->
    Header = io_lib:format("-module(~p).\n-export([f/~w]).\n", [Name, arity(Text)]),
    Forms = forms(lists:flatten(Header) ++ Text),
    Result = qlc_pt:parse_transform(Forms, Options),
    io:format("module ~p~n  ~s~n", [Name, digest(Result)]),
    [io:format("  ~0p~n", [F]) || F <- Result, not is_code(F)],
    ok.

%% The arity of the first f/N clause of a module text.
arity(Text) ->
    {match, [Args]} = re:run(Text, "^f\\(([^)]*)\\)", [multiline, {capture, all_but_first, list}]),
    length([A || A <- string:split(Args, ",", all), string:trim(A) =/= ""]).

is_code({function, _, _, _, _}) -> true;
is_code({attribute, _, module, _}) -> true;
is_code({attribute, _, export, _}) -> true;
is_code({attribute, _, import, _}) -> true;
is_code({eof, _}) -> true;
is_code(_) -> false.

%% The forms of a module text, after a file attribute and before eof, as epp:parse_file/2 gives them.
forms(Text) ->
    {ok, Tokens, End} = erl_scan:string(Text, {1, 1}),
    [{attribute, {1, 1}, file, {"m.erl", 1}} | split(Tokens, [])] ++ [{eof, End}].

split([], []) ->
    [];
split([{dot, _} = Dot | Rest], Acc) ->
    {ok, Form} = erl_parse:parse_form(lists:reverse([Dot | Acc])),
    [Form | split(Rest, [])];
split([Token | Rest], Acc) ->
    split(Rest, [Token | Acc]).

digest(Term) ->
    [io_lib:format("~2.16.0b", [B]) || <<B>> <= erlang:md5(term_to_binary(Term))].
