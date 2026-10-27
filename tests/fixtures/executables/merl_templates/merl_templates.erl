%% merl (plan 21 LIB77E5): quoting text as forms, expressions and clauses, metavariables of every kind (atoms,
%% variables, integers, strings, globs and lifting), templates and their meta form, substitution, renaming, matching,
%% switch/2, print/1 and show/1, compile_and_load/1, and merl.hrl's ?Q macros rewritten by merl_transform.
-module(merl_templates).
-export([main/1]).

-include_lib("syntax_tools/include/merl.hrl").

main(_) ->
    quoting(),
    templates(),
    matching(),
    switching(),
    printing(),
    loading(),
    transformed(),
    io:format("merl_templates: ok~n").

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

text(Trees) when is_list(Trees) -> [text(T) || T <- Trees];
text(Tree) -> lists:flatten(erl_prettypr:format(Tree)).

try_call(F) ->
    try
        {value, F()}
    catch
        Class:Reason -> {Class, Reason}
    end.

% The text to quote, held in a variable so merl_transform leaves the calls to run time.
quote(Text) -> merl:quote(Text).

quoting() ->
    io:format("== quoting~n"),
    Texts = [
        "foo(X) + 1",
        "a, b, c",
        "f(X) -> X. g() -> ok.",
        "-module(m).",
        "X when X > 0 -> pos; _ -> other",
        "error:E -> E; throw:T:S -> {T, S}",
        "(a) -> 1; (B) -> {B}",
        "{a, \"s\"} %% comment\n",
        "%% before\nx() -> 1. %% after\n%% last\n",
        ["first(1),", "second(2)"],
        [<<"bin(1)">>],
        <<"binary + text">>
    ],
    [show("quote", {T, text(quote(T))}) || T <- Texts],
    show("located", merl:quote({3, 5}, "f(x)")),
    show("line", merl:quote(7, "g(y)")),
    show("raw", quote("f(X) -> X.")),
    Bad = ["f(", "f() ->", "a b", "x. y", ") ->"],
    [show("error", {T, try_call(fun() -> quote(T) end)}) || T <- Bad].

templates() ->
    io:format("== templates~n"),
    Patterns = [
        "f('@name', _@Arg, 909123, \"'@str\")",
        "g(_@@args)",
        "h(_@_lifted)",
        "f({_@__twice})",
        "[909912 | _@Tail]",
        "#r{a = _@A, b = '@@rest'}",
        "case _@X of _@P -> _@@body end",
        "<<_@B:8>>",
        "receive _@M -> ok after _@T -> timeout end",
        "fun (_@P) when _@G -> _@B end",
        "plain(1, 2)",
        "909"
    ],
    [template(P) || P <- Patterns],
    show("bad", try_call(fun() -> merl:template(erl_syntax:atom('@')) end)),
    show("bad lift", try_call(fun() -> merl:template(quote("{_@__twice}")) end)),
    show("given", merl:template({x})),
    show("list", merl:template([quote("_@a"), quote("b")])),
    T = merl:template(quote("f(_@a, _@@rest, 90917)")),
    show(
        "subst",
        text(
            merl:subst(quote("f(_@a, _@@rest, 90917)"), [
                {a, merl:term(1)}, {rest, [merl:term(2), merl:term(3)]}, {17, merl:term(x)}
            ])
        )
    ),
    show("tsubst", merl:tsubst(quote("f(_@a, _@b)"), [{a, merl:term(1)}])),
    show("alpha", merl:alpha(T, [{a, z}, {rest, others}])),
    show("tree", text(merl:tree(merl:alpha(T, [{a, z}])))),
    show("qquote", text(merl:qquote("{_@x, _@@y}", [{x, merl:term(a)}, {y, [merl:term(b), merl:term(c)]}]))),
    show("qquote line", merl:qquote(4, "_@x", [{x, merl:var('V')}])),
    show("subst list", text(merl:subst([quote("_@a"), quote("[_@a]")], [{a, merl:term(1)}]))),
    show("subst clash", try_call(fun() -> merl:subst(quote("_@a"), [{a, x, y}]) end)).

template(Text) ->
    Template = merl:template(quote(Text)),
    show("template", {Text, Template}),
    show("vars", merl:template_vars(Template)),
    show("tree", text(merl:tree(Template))),
    show("meta", text(merl:meta_template(Template))).

matching() ->
    io:format("== matching~n"),
    Tree = quote("f(1, two, [3, 4 | T], {five}, \"six\")"),
    Patterns = [
        "f(_@A, _@b, _@C, _@D, _@E)",
        "f(_@@all)",
        "f(1, _@@middle, \"six\")",
        "f(_, _@_, _@0, _, _)",
        "f(1, two, [_@x, _@y | _@z], {_@w}, _@v)",
        "f(2, _@@_)",
        "g(_@@_)",
        "f(_@a)",
        "f(1, two, [3, 4 | T], {five}, \"six\")",
        "f(1.0, two, [3, 4 | T], {five}, \"six\")"
    ],
    [show("match", {P, merl:match(quote(P), Tree)}) || P <- Patterns],
    show("lists", merl:match([quote("_@a"), quote("x")], [merl:term(1), merl:term(x)])),
    show("list pattern", merl:match([quote("_@a")], merl:term(1))),
    show("list tree", merl:match(quote("_@a"), [merl:term(1)])),
    show("arity", try_call(fun() -> merl:match([quote("_@a")], [merl:term(1), merl:term(2)]) end)),
    show("globs", try_call(fun() -> merl:match(quote("f(_@@a, _@@b)"), quote("f(1, 2)")) end)),
    show("nil", merl:match(quote("[]"), quote("[]"))),
    show("empty tuple", merl:match(quote("{}"), quote("{_@a}"))).

switching() ->
    io:format("== switching~n"),
    Clauses = [
        {quote("f(_@x)"), fun([{x, X}]) -> erl_syntax:type(X) =:= atom end, fun([{x, X}]) -> {atom, text(X)} end},
        {quote("f(_@x)"), [{fun([{x, _}]) -> false end, fun(_) -> never end}, fun([{x, X}]) -> {other, text(X)} end]},
        {quote("g(_@@xs)"), fun([{xs, Xs}]) -> {g, length(Xs)} end},
        fun() -> default end
    ],
    [show("switch", merl:switch(quote(T), Clauses)) || T <- ["f(a)", "f(1)", "g(1, 2)", "h()"]],
    show("no clause", try_call(fun() -> merl:switch(quote("x"), []) end)),
    show("bad clause", try_call(fun() -> merl:switch(quote("x"), [bad]) end)),
    show("bad action", try_call(fun() -> merl:switch(quote("x"), [{quote("x"), [bad]}]) end)),
    show("fallthrough", merl:switch(quote("x"), [{quote("x"), []}, fun() -> next end])).

printing() ->
    io:format("== printing~n"),
    merl:print(quote("f(X) -> case X of 1 -> one; _ -> {many, X} end.")),
    merl:print([quote("a"), quote("b + c")]),
    merl:show(quote("f(X) -> case X of 1 -> one; _ -> {many, X, [1, 2], <<\"bin\">>} end.")),
    merl:show(merl:template(quote("g(_@A, _@@Rest)"))),
    merl:show(quote("very_long_function_name_that_goes_on(with_a_long_argument, and_another_long_argument, more)")).

loading() ->
    io:format("== loading~n"),
    Forms = quote(["-module(merl_loaded).", "-export([twice/1]).", "twice(X) -> 2 * X."]),
    {ok, Binary} = merl:compile_and_load(Forms),
    show("loaded", is_binary(Binary)),
    Loaded = list_to_atom("merl_loaded"),
    show("call", Loaded:twice(21)),
    Form = merl:compile(erl_syntax:form_list(quote(["-module(merl_other).", "f() -> ok."]))),
    show("compile", element(1, Form)).

%% ?Q matches, cases and literal calls that merl_transform rewrites at compile time.
transformed() ->
    io:format("== transformed~n"),
    Tree = ?Q("foo(1, Bar, [x])"),
    ?Q("foo(_@One, _@Var, _@@Rest)") = Tree,
    show("bound", {text(One), text(Var), text(Rest)}),
    show("Q env", text(?Q("{_@a, _@@b}", [{a, merl:term(1)}, {b, [merl:term(2)]}]))),
    Num = 7,
    show("Q var", text(?Q("[_@Num@, _@Tree]"))),
    [show("case", classify(?Q(T))) || T <- ["f(a)", "f(1, 2)", "g()", "h(9)"]],
    show("literal", merl:term({a, [1]})),
    show("literal quote", text(merl:quote("x + y"))).

classify(Tree) ->
    case Tree of
        ?Q("f(_@A)") when erl_syntax:type(A) =:= atom -> {atom, text(A)};
        ?Q("f(_@@Args)") -> {args, length(Args)};
        ?Q("h(_@N)") when erl_syntax:type(N) =:= integer, N =/= none; false -> {h, text(N)};
        _ -> other
    end.
