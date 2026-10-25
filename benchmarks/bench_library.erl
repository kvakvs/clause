%% Library benchmarks: the qlc parse transform at run time over queries shaped like qlc_SUITE cases.
-module(bench_library).
-export([main/1]).

main(_Args) ->
    Expressions = [{parse_expression(Text), bindings(Vars)} || {Text, Vars} <- queries()],
    Modules = [parse_module(Name, Text) || {Name, Text} <- modules()],
    bench_util:run("libraries.qlc", 11, fun() -> qlc_transforms(Expressions, Modules) end),
    bench_util:done("bench_library").

%% Transform every query as qlc:q/1 in a module and in evaluated code would, then every module.
qlc_transforms(Expressions, Modules) ->
    Results = [
        {qlc_pt:transform_expression(Expr, Bindings), qlc_pt:transform_from_evaluator(Expr, Bindings)}
     || {Expr, Bindings} <- Expressions
    ],
    {Results, [qlc_pt:parse_transform(Forms, []) || Forms <- Modules]}.

%% Query texts with their bound variables, after the qlc_SUITE cases their names give.
queries() ->
    [
        %% lc, filter_var, single
        {"[X || X <- [1, 2, 3]]", []},
        {"[X || X <- L, X > 1, X < 10]", ['L']},
        {"[{X} || X <- L, Y, X =/= 2]", ['L', 'Y']},
        %% pattern, nomatch
        {"[{X, Y} || {X, Y, _} <- L, X =:= Y]", ['L']},
        {"[X || {X, a, [_ | _]} <- L]", ['L']},
        {"[X || {X, X} <- L, X > 3]", ['L']},
        %% lookup1, lookup2: constant key columns
        {"[X || {X, _} <- ets:table(E), X =:= 3]", ['E']},
        {"[X || {X, _} <- ets:table(E), (X =:= 1) or (X =:= 2) or (X =:= 3)]", ['E']},
        {"[Y || {X, Y} <- ets:table(E), X =:= K, Y > 2]", ['E', 'K']},
        {"[X || {X, Y, Z} <- ets:table(E), X == 1, Y =:= a, Z =:= b]", ['E']},
        {"[X || X = {K, _} <- ets:table(E), K =:= key]", ['E']},
        %% skip_filters
        {"[X || {X} <- L, is_integer(X), X > 0, X rem 2 =:= 0]", ['L']},
        {"[X || {X, Y} <- L, element(1, X) =:= Y, Y =:= 3]", ['L']},
        %% join_lookup, join_merge, join_filter
        {"[{X, Z} || {X, Y} <- L1, {Y2, Z} <- L2, Y =:= Y2]", ['L1', 'L2']},
        {"[{A, C} || {A, B} <- ets:table(E1), {B2, C} <- ets:table(E2), B == B2]", ['E1', 'E2']},
        {"[X || {X, _} <- L1, {Y, _} <- L2, X =:= Y, X > 5]", ['L1', 'L2']},
        {"[{X, Y} || X <- L1, Y <- L2, X < Y]", ['L1', 'L2']},
        %% join_complex
        {"[{X, Y, Z} || {X} <- L1, {Y} <- L2, {Z} <- L3, X =:= Y, Y =:= Z]", ['L1', 'L2', 'L3']},
        {"[{X, W} || {X, Y} <- L1, {Y, Z} <- L2, {Z, W} <- L3]", ['L1', 'L2', 'L3']},
        %% nested_qlc, generator_vars
        {"[{X, Y} || X <- qlc:q([A || {A} <- L1]), Y <- L2, X =:= Y]", ['L1', 'L2']},
        {"[Y || X <- L, Y <- qlc:q([Z || Z <- X, Z > 1])]", ['L']},
        {"[X || X <- L, Y <- X, Y > 1]", ['L']},
        %% binary and strict generators, matches in filters
        {"[X || <<X:8>> <= B, X > 32]", ['B']},
        {"[X || X <:- L, X =/= 0]", ['L']},
        {"[X || {X, Y} <- L, Y = X + 1]", ['L']},
        %% errors
        {"[X || X <- L, foo(X)]", ['L']},
        {"not_a_query", []}
    ].

%% Modules for parse_transform/2, after the module cases of the qlc_SUITE transform tests.
modules() ->
    [
        {join,
            "f(L, M) -> qlc:q([{X, Z} || {X, Y} <- L, {Y2, Z} <- M, Y =:= Y2]).\n"
            "g(T, K) -> qlc:q([V || {K2, V} <- ets:table(T), K2 =:= K]).\n"},
        {nested, "f(L) -> qlc:q([Y || X <- L, Y <- qlc:q([Z || Z <- X, Z > 1])]).\n"},
        {options,
            "f(L) -> qlc:q([X || {X, _} <- L, X > 1], [unique, cache]).\n"
            "g(L, M) -> qlc:q([{X, Y} || {X} <- L, {Y} <- M, X == Y], {join, merge}).\n"}
    ].

parse_expression(Text) ->
    {ok, Tokens, _} = erl_scan:string(Text ++ "."),
    {ok, [Expr]} = erl_parse:parse_exprs(Tokens),
    Expr.

bindings(Vars) ->
    [{V, 1} || V <- Vars].

%% The forms of one module text with f/N and g/N exported, between a file attribute and eof.
parse_module(Name, Text) ->
    Header = io_lib:format("-module(~p).\n-compile(nowarn_export_all).\n-compile(export_all).\n", [Name]),
    {ok, Tokens, End} = erl_scan:string(lists:flatten(Header) ++ Text, {1, 1}),
    [{attribute, {1, 1}, file, {"m.erl", 1}} | split(Tokens, [])] ++ [{eof, End}].

split([], []) ->
    [];
split([{dot, _} = Dot | Rest], Acc) ->
    {ok, Form} = erl_parse:parse_form(lists:reverse([Dot | Acc])),
    [Form | split(Rest, [])];
split([Token | Rest], Acc) ->
    split(Rest, [Token | Acc]).
