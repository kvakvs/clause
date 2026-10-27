%% Clause's merl_transform (plan 21 LIB77E5, docs/libraries/syntax_tools.md#merl_transform): the parse transform of
%% merl.hrl. It turns merl:quote/2 matches and case clauses of literal text into merl:match/2 and merl:switch/2 calls
%% with templates built at compile time, and evaluates merl calls whose arguments are all literals. Library modules
%% are compiled without it, so the patterns here are quoted at run time (once per parse_transform/2 call).
-module(merl_transform).
-export([parse_transform/2]).

% Nodes this transform puts into the code it rewrites carry the lines where OTP's merl_transform quotes them.
-define(DUMMY_USE_LINE, 266).
-define(ORELSE_LINE, 275).
-define(ANDALSO_LINE, 281).

parse_transform(Forms, _Options) ->
    erl_syntax:revert_forms(expand(erl_syntax:form_list(Forms), patterns())).

% The patterns the rewriting matches nodes against.
patterns() ->
    #{
        quote_match => merl:quote(0, "merl:quote(_@line, _@text) = _@expr"),
        quote_case => merl:quote(0, [
            "case _@expr of",
            "  merl:quote(_@_, _@text) when _@__@_ -> _@@_; _@_@_ -> 0",
            "end"
        ]),
        merl_call => merl:quote(0, "merl:_@function(_@@args)"),
        qquote => merl:quote(0, "qquote"),
        subst => merl:quote(0, "subst"),
        match => merl:quote(0, "match"),
        quote_clause => merl:quote(0, "(merl:quote(_@line, _@text)) when _@__@guard -> _@@body"),
        default_clause => merl:quote(0, "_ -> _@@body")
    }.

% Each node rewritten before its subtrees are expanded (quote matches and cases), and after (literal merl calls).
expand(Tree0, P) ->
    Tree = pre(Tree0, P),
    Expanded =
        case erl_syntax:subtrees(Tree) of
            [] -> Tree;
            Groups -> erl_syntax:update_tree(Tree, [[expand(T, P) || T <- G] || G <- Groups])
        end,
    post(Expanded, P).

pre(T, P) ->
    merl:switch(T, [
        {
            maps:get(quote_match, P),
            fun([{expr, _}, {line, Line}, {text, Text}]) ->
                erl_syntax:is_literal(Text) andalso erl_syntax:is_literal(Line)
            end,
            fun([{expr, Expr}, {line, Line}, {text, Text}]) ->
                quote_match(Expr, erl_syntax:concrete(Line), erl_syntax:concrete(Text))
            end
        },
        {maps:get(quote_case, P), fun([{expr, _}, {text, Text}]) -> erl_syntax:is_literal(Text) end, fun(
            [{expr, Expr}, {text, _}]
        ) ->
            quote_case(Expr, erl_syntax:case_expr_clauses(T), location(T), P)
        end},
        fun() -> T end
    ]).

post(T, P) ->
    merl:switch(T, [
        {maps:get(merl_call, P), [
            {fun([{args, As}, {function, F}]) -> lists:all(fun erl_syntax:is_literal/1, [F | As]) end, fun(
                [{args, As}, {function, F}]
            ) ->
                [F1 | As1] = lists:map(fun erl_syntax:concrete/1, [F | As]),
                eval_call(location(F), F1, As1, T)
            end},
            fun([{args, As}, {function, F}]) -> partial_call(F, As, T, P) end
        ]},
        fun() -> T end
    ]).

% A merl call with some arguments that are not literals: qquote/2,3, subst/2 and match/2 with literal text or
% patterns are prepared at compile time.
partial_call(F, As, T, P) ->
    merl:switch(F, [
        {maps:get(qquote, P), fun([]) -> expand_qquote(As, T, 1, P) end},
        {maps:get(subst, P), fun([]) -> expand_template(F, As, T) end},
        {maps:get(match, P), fun([]) -> expand_template(F, As, T) end},
        fun() -> T end
    ]).

expand_qquote([Line, Text, Env], T, _, P) ->
    case erl_syntax:is_literal(Line) of
        true -> expand_qquote([Text, Env], T, erl_syntax:concrete(Line), P);
        false -> T
    end;
expand_qquote([Text, Env], T, Line, P) ->
    case erl_syntax:is_literal(Text) of
        true ->
            case eval_call(Line, quote, [Line, erl_syntax:concrete(Text)], failed) of
                failed -> T;
                Quoted -> expand(merl:qquote(Line, "merl:subst(_@tree, _@env)", [{tree, Quoted}, {env, Env}]), P)
            end;
        false ->
            T
    end;
expand_qquote(_, T, _, _) ->
    T.

expand_template(F, [Pattern | Args], T) ->
    case erl_syntax:is_literal(Pattern) of
        true ->
            Line = location(Pattern),
            Template = eval_call(Line, template, [erl_syntax:concrete(Pattern)], T),
            merl:qquote(Line, "merl:_@function(_@pattern, _@args)", [
                {function, F}, {pattern, Template}, {args, Args}
            ]);
        false ->
            T
    end;
expand_template(_, _, T) ->
    T.

% The tree of a merl call's result at compile time; quoted text with Erlang-variable metavariables becomes a
% merl:tree/1 call of its meta template. Default when the call throws.
eval_call(Line, F, As, Default) ->
    try apply(merl, F, As) of
        Result when F =:= quote ->
            Template = merl:template(Result),
            case lists:any(fun is_inline_metavar/1, merl:template_vars(Template)) of
                true when is_list(Result) ->
                    merl:qquote(Line, "merl:tree([_@template])", [{template, merl:meta_template(Template)}]);
                true ->
                    merl:qquote(Line, "merl:tree(_@template)", [{template, merl:meta_template(Template)}]);
                false ->
                    merl:term(Result)
            end;
        Result ->
            merl:term(Result)
    catch
        throw:_ -> Default
    end.

% merl:quote(Line, Text) = Expr: a merl:match/2 of the template that binds the Erlang-variable metavariables.
quote_match(Expr, Line, Text) ->
    {Template, Out, _} = pattern_of(Line, Text),
    merl:qquote(Line, "{ok, _@out} = merl:match(_@template, _@expr)", [
        {expr, Expr}, {out, Out}, {template, erl_syntax:abstract(Template)}
    ]).

% The template of quoted text with its Erlang-variable metavariables renamed to lowercase tags, the list of
% {Tag, Variable} tuples that binds them, and those metavariables.
pattern_of(Line, Text) ->
    Template = merl:template(merl:quote(Line, Text)),
    Vars = [V || V <- merl:template_vars(Template), is_inline_metavar(V)],
    Out = erl_syntax:list([
        erl_syntax:tuple([erl_syntax:abstract(tag_of(V)), erl_syntax:variable(variable_of(V))])
     || V <- Vars
    ]),
    {merl:alpha(Template, [{V, tag_of(V)} || V <- Vars]), Out, Vars}.

variable_of(V) when is_integer(V), V > 99, V rem 100 =:= 99 -> list_to_atom("Q" ++ integer_to_list(V div 100));
variable_of(V) when is_integer(V), V > 9, V rem 10 =:= 9 -> list_to_atom("Q" ++ integer_to_list(V div 10));
variable_of(V) when is_integer(V) -> list_to_atom("Q" ++ integer_to_list(V));
variable_of(V) -> V.

tag_of(V) when is_integer(V) -> V;
tag_of(V) -> list_to_atom(string:lowercase(atom_to_list(V))).

% case Expr of merl:quote(Line, Text) when Guard -> Body; ...; _ -> Default end: a merl:switch/2.
quote_case(Expr, Clauses, Line, P) ->
    merl:qquote(Line, "merl:switch(_@expr, _@clauses)", [
        {clauses, erl_syntax:list([switch_clause(C, P) || C <- Clauses])}, {expr, Expr}
    ]).

switch_clause(T, P) ->
    merl:switch(T, [
        {
            maps:get(quote_clause, P),
            fun([{body, _}, {guard, _}, {line, Line}, {text, Text}]) ->
                erl_syntax:is_literal(Text) andalso erl_syntax:is_literal(Line)
            end,
            fun([{body, Body}, {guard, Guard}, {line, Line}, {text, Text}]) ->
                switch_clause(Body, Guard, erl_syntax:concrete(Line), erl_syntax:concrete(Text))
            end
        },
        {maps:get(default_clause, P), fun(Env) -> merl:qquote("fun () -> _@body end", Env) end}
    ]).

switch_clause(Body, Guard, Line, Text) ->
    {Template, Out, Vars} = pattern_of(Line, Text),
    GuardExprs = guard_exprs(Guard),
    Env = [
        {body, Body},
        {guard, GuardExprs},
        {out, Out},
        {template, erl_syntax:abstract(Template)},
        {unused, [dummy_use(V) || V <- Vars]}
    ],
    case GuardExprs of
        [] ->
            merl:qquote(Line, ["{_@template, ", " fun (_@out) -> _@unused, _@body end}"], Env);
        _ ->
            merl:qquote(
                Line,
                ["{_@template, ", " fun (_@out) -> _@unused, _@guard end, ", " fun (_@out) -> _@unused, _@body end}"],
                Env
            )
    end.

% `_ = Var`, so a metavariable the body does not use draws no warning.
dummy_use(V) -> merl:qquote(?DUMMY_USE_LINE, "_ = _@var", [{var, erl_syntax:variable(variable_of(V))}]).

% A clause guard as one expression: disjunctions joined by orelse, conjunctions by andalso.
guard_exprs([]) -> [];
guard_exprs([Disjunction]) -> [orelse_of(erl_syntax:disjunction_body(Disjunction))].

orelse_of([]) ->
    [];
orelse_of([C]) ->
    andalso_of(erl_syntax:conjunction_body(C));
orelse_of([C | Cs]) ->
    merl:qquote(?ORELSE_LINE, "_@expr orelse _@rest", [
        {expr, andalso_of(erl_syntax:conjunction_body(C))}, {rest, orelse_of(Cs)}
    ]).

andalso_of([E]) -> E;
andalso_of([E | Es]) -> merl:qquote(?ANDALSO_LINE, "_@expr andalso _@rest", [{expr, E}, {rest, andalso_of(Es)}]).

is_inline_metavar(V) when is_atom(V) -> is_upper(atom_to_list(V));
is_inline_metavar(V) when is_integer(V) -> V > 9 andalso V rem 10 =:= 9;
is_inline_metavar(_) -> false.

is_upper([C | _]) when C >= $A, C =< $Z; C >= $À, C =< $Þ, C =/= $× -> true;
is_upper(_) -> false.

location(T) ->
    Pos = erl_syntax:get_pos(T),
    case erl_anno:is_anno(Pos) of
        true -> erl_anno:location(Pos);
        false -> Pos
    end.
