%% erl_syntax_lib (plan 21 LIB77E4a): form and attribute analyses with their syntax errors, binding annotations of
%% every expression and pattern kind, variables, fresh variable names, limit/2,3, comments stripped and made, the
%% folds and maps in their visiting order, and function name expansions.
-module(syntax_tools_lib).
-export([main/1]).

-define(SOURCE, [
    "-module(corpus).",
    "-export([f/1, g/2, h/0]).",
    "-export([i/1]).",
    "-import(lists, [map/2, foldl/3]).",
    "-import(lists, [reverse/1]).",
    "-record(r, {a, b = 1, c :: integer(), d = 2 :: atom()}).",
    "-file(\"corpus.erl\", 1).",
    "-compile(export_all).",
    "-custom({1, \"two\", [3]}).",
    "-type t(A) :: [A] | m:n(1) | u().",
    "-spec f(X) -> X when X :: integer().",
    "f(X) when X > 0, is_integer(X); X =:= a -> Y = X + 1, {Y, X}; f(_) -> -1.",
    "g(A, B) -> case A of {x, Y} -> Y; Z -> {Z, B} end, catch B, Y = 1, Y.",
    "h() -> [1, 2 | T] = [1, 2, 3], <<X:4, _/binary>> = <<1>>, {T, X}.",
    "i(L) -> [X || X <- L, Y <- [X], X > Y], << <<B>> || <<B>> <= L >>, #{K => V || K := V <- L}.",
    "j(L) -> [X + Y || X <- L && Y <- L], [W || <<W>> <:= L], [Q || Q <:- L], [K || K := _ <:- L].",
    "k(R) -> receive {a, X} -> X; Y -> Y after R -> R end, receive Z -> Z end.",
    "l(A) -> try A of {ok, X} -> X; Y -> Y catch error:E:S -> {E, S}; Z -> Z after A end.",
    "m(A) -> F = fun(X) -> {A, X} end, G = fun Loop(0) -> Loop; Loop(N) -> Loop(N - 1) end, {F, G}.",
    "n(A) -> if A -> B = 1; true -> B = 2 end, begin C = B, D = C end, D.",
    "o(A) -> maybe {ok, X} ?= A, Y = X, Y else {error, E} -> E; _ -> A end, maybe 1 end.",
    "p(R) -> {R#r.a, R#r{b = 2}, #r{a = 1}, #r.b, fun p/1, fun m:p/1, A = B = R, A, B}.",
    "q() -> exit(a), throw(b), erlang:error(c), erlang:error(d, e), erlang:fault(f), erlang:exit(g), x:y(1)."
]).

main(_) ->
    Forms = parse(?SOURCE),
    analyses(Forms),
    errors(),
    bindings(Forms),
    variables(Forms),
    names(),
    limits(Forms),
    comments(),
    walks(Forms),
    io:format("syntax_tools_lib: ok~n").

%% The forms of the corpus, one per line.
parse(Lines) ->
    [
        begin
            {ok, Tokens, _} = erl_scan:string(Line, 1),
            {ok, Form} = erl_parse:parse_form(Tokens),
            Form
        end
     || Line <- Lines
    ].

%% A form rebuilt as a tree with make_tree/2.
tree(Node) ->
    case erl_syntax:subtrees(Node) of
        [] ->
            Node;
        Groups ->
            Tree = erl_syntax:make_tree(erl_syntax:type(Node), [[tree(C) || C <- G] || G <- Groups]),
            erl_syntax:copy_pos(Node, Tree)
    end.

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

%% What a function gives, or how it fails.
try_call(F) ->
    try
        {value, F()}
    catch
        Class:Reason -> {Class, Reason}
    end.

%% analyze_forms/1 of the forms, of a form list and of trees; analyze_form/1 and the attribute analyses of each.
analyses(Forms) ->
    io:format("== analyses~n"),
    Errors = [{error, {3, erl_parse, ["syntax error"]}}, {warning, {4, erl_lint, unused}}],
    Markers = [erl_syntax:error_marker(element(2, E)) || E <- Errors] ++ [erl_syntax:warning_marker(w)],
    show("forms", erl_syntax_lib:analyze_forms(Forms ++ Markers ++ [{eof, 9}])),
    show(
        "form list",
        erl_syntax_lib:analyze_forms(erl_syntax:form_list([erl_syntax:form_list(Forms), erl_syntax:comment(["% c"])]))
    ),
    show("trees", erl_syntax_lib:analyze_forms([tree(F) || F <- Forms])),
    show("empty", erl_syntax_lib:analyze_forms([])),
    [show("form", try_call(fun() -> erl_syntax_lib:analyze_form(F) end)) || F <- Forms ++ Markers],
    [
        show("form", try_call(fun() -> erl_syntax_lib:analyze_form(F) end))
     || F <- [erl_syntax:comment(["% x"]), erl_syntax:text("t"), erl_syntax:eof_marker(), erl_syntax:atom(a)]
    ],
    Attributes = [F || F <- Forms, erl_syntax:type(F) =:= attribute],
    [show("attribute", try_call(fun() -> erl_syntax_lib:analyze_attribute(F) end)) || F <- Attributes],
    Analyses = [
        analyze_module_attribute,
        analyze_export_attribute,
        analyze_import_attribute,
        analyze_file_attribute,
        analyze_record_attribute,
        analyze_wild_attribute
    ],
    [show(atom_to_list(A), [try_call(fun() -> erl_syntax_lib:A(F) end) || F <- Attributes]) || A <- Analyses],
    Directives = [
        erl_syntax:attribute(erl_syntax:atom(D), [erl_syntax:atom(x)])
     || D <- [define, undef, include, include_lib, ifdef, ifndef, 'if', elif, 'else', endif]
    ],
    show("directives", [erl_syntax_lib:analyze_attribute(D) || D <- Directives]),
    Module = erl_syntax:attribute(erl_syntax:atom(module), [
        erl_syntax:atom(m), erl_syntax:list([erl_syntax:variable('A'), erl_syntax:variable('B')])
    ]),
    show("parameterized", erl_syntax_lib:analyze_module_attribute(Module)),
    Import = erl_syntax:attribute(erl_syntax:atom(import), [erl_syntax:atom(orddict)]),
    show("module forms", erl_syntax_lib:analyze_forms([Module, hd(Forms), Import, Import])),
    show("import", erl_syntax_lib:analyze_import_attribute(Import)),
    Functions = [F || F <- Forms, erl_syntax:type(F) =:= function],
    show("functions", [erl_syntax_lib:analyze_function(F) || F <- Functions]),
    expressions(Functions).

%% The analyses of expressions found in the function bodies.
expressions(Functions) ->
    Nodes = lists:append([erl_syntax_lib:fold(fun(N, Acc) -> [N | Acc] end, [], F) || F <- Functions]),
    Of = fun(Types) -> [N || N <- lists:reverse(Nodes), lists:member(erl_syntax:type(N), Types)] end,
    show("applications", [try_call(fun() -> erl_syntax_lib:analyze_application(N) end) || N <- Of([application])]),
    show("fail", [erl_syntax_lib:is_fail_expr(N) || N <- Of([application])]),
    show("implicit funs", [erl_syntax_lib:analyze_implicit_fun(N) || N <- Of([implicit_fun])]),
    show("records", [erl_syntax_lib:analyze_record_expr(N) || N <- Of([record_expr, record_access, record_index_expr])]),
    show("other record", erl_syntax_lib:analyze_record_expr(erl_syntax:atom(a))),
    show(
        "qualified record",
        erl_syntax_lib:analyze_record_expr(
            erl_syntax:record_expr(erl_syntax:list([erl_syntax:atom(m), erl_syntax:atom(r)]), [
                erl_syntax:record_field(erl_syntax:atom(a))
            ])
        )
    ),
    Types = [
        N
     || F <- parse(["-type t() :: a() | m:b(c) | [d].", "-spec s() -> list(integer())."]),
        N <- erl_syntax_lib:fold(fun(X, A) -> [X | A] end, [], F)
    ],
    show("type applications", [
        erl_syntax_lib:analyze_type_application(N)
     || N <- Types, lists:member(erl_syntax:type(N), [type_application, user_type_application])
    ]),
    Name = fun(Parts) -> name_tree(Parts) end,
    show("function names", [try_call(fun() -> erl_syntax_lib:analyze_function_name(Name(P)) end) || P <- name_shapes()]),
    show("type names", [try_call(fun() -> erl_syntax_lib:analyze_type_name(Name(P)) end) || P <- name_shapes()]),
    show("expansions", erl_syntax_lib:function_name_expansions([f, {g, 1}, {m, h}, {m, {i, 2}}, {m, {n, {j, 3}}}])),
    show("not fail", erl_syntax_lib:is_fail_expr(erl_syntax:atom(a))).

%% Name shapes as trees: atoms, arity and module qualifiers, and malformed ones.
name_shapes() ->
    [f, {f, 1}, {m, f}, {m, {f, 2}}, {{m, f}, 1}, {{f, 1}, 1}, {{f, 1}, 2}, {1, f}, {f, a}, {{f, x}, 1}, 7].

name_tree(A) when is_atom(A) -> erl_syntax:atom(A);
name_tree(N) when is_integer(N) -> erl_syntax:integer(N);
name_tree({F, N}) when is_integer(N) -> erl_syntax:arity_qualifier(name_tree(F), erl_syntax:integer(N));
name_tree({M, F}) when is_atom(M) -> erl_syntax:module_qualifier(erl_syntax:atom(M), name_tree(F));
name_tree({M, F}) -> erl_syntax:module_qualifier(name_tree(M), name_tree(F)).

%% The analyses of nodes they do not accept: syntax_error, or erl_syntax's own errors.
errors() ->
    io:format("== errors~n"),
    A = erl_syntax:atom(a),
    V = erl_syntax:variable('V'),
    Attr = fun(Name, Args) -> erl_syntax:attribute(erl_syntax:atom(Name), Args) end,
    Cases = [
        {analyze_form, [V]},
        {analyze_attribute, [erl_syntax:attribute(V, [A])]},
        {analyze_module_attribute, [A]},
        {analyze_module_attribute, [Attr(module, [V])]},
        {analyze_module_attribute, [Attr(module, [A, A])]},
        {analyze_module_attribute, [Attr(module, none)]},
        {analyze_export_attribute, [Attr(export, [A])]},
        {analyze_export_attribute, [Attr(export, [erl_syntax:list([A], V)])]},
        {analyze_import_attribute, [Attr(import, [A, A, A])]},
        {analyze_file_attribute, [Attr(file, [A, erl_syntax:integer(1)])]},
        {analyze_record_attribute, [Attr(record, [V, erl_syntax:tuple([])])]},
        {analyze_record_attribute, [Attr(record, [A, A])]},
        {analyze_record_attribute, [Attr(record, [A, erl_syntax:tuple([A])])]},
        {analyze_wild_attribute, [Attr(x, [V])]},
        {analyze_wild_attribute, [Attr(x, [A, A])]},
        {analyze_wild_attribute, [erl_syntax:attribute(V, [A])]},
        {analyze_wild_attribute, [A]},
        {analyze_record_field, [A]},
        {analyze_record_field, [erl_syntax:record_field(V)]},
        {analyze_record_expr, [erl_syntax:record_expr(V, [])]},
        {analyze_record_expr, [erl_syntax:record_access(V, A, V)]},
        {analyze_record_expr, [erl_syntax:record_index_expr(A, V)]},
        {analyze_function, [A]},
        {analyze_function, [erl_syntax:function(V, [erl_syntax:clause([], none, [A])])]},
        {analyze_implicit_fun, [A]},
        {analyze_application, [A]},
        {analyze_application, [erl_syntax:application(V, [A])]},
        {analyze_type_application, [A]},
        {analyze_type_application, [erl_syntax:type_application(V, [])]},
        {analyze_function_name, [V]},
        {analyze_type_name, [V]},
        {annotate_bindings, [A]},
        {annotate_bindings, [erl_syntax:add_ann({env, x, y}, A)]},
        {analyze_forms, [[V]]},
        {analyze_form, [{bad}]}
    ],
    [show(atom_to_list(F), try_call(fun() -> apply(erl_syntax_lib, F, Args) end)) || {F, Args} <- Cases],
    ok.

%% annotate_bindings/2 over every function: each node's type with its env, bound and free annotations.
bindings(Forms) ->
    io:format("== bindings~n"),
    [print_bindings(erl_syntax_lib:annotate_bindings(F, [])) || F <- Forms, erl_syntax:type(F) =:= function],
    Env = erl_syntax:add_ann({env, ['A']}, erl_syntax:match_expr(erl_syntax:variable('B'), erl_syntax:variable('A'))),
    print_bindings(erl_syntax_lib:annotate_bindings(Env)),
    Again = erl_syntax_lib:annotate_bindings(erl_syntax_lib:annotate_bindings(Env), ['C']),
    show("again", erl_syntax:get_ann(Again)),
    Kept = erl_syntax:add_ann(keep, erl_syntax:variable('X')),
    show("kept", erl_syntax:get_ann(erl_syntax_lib:annotate_bindings(Kept, ['X']))),
    Macro = erl_syntax:macro(erl_syntax:variable('M'), [erl_syntax:variable('X'), erl_syntax:variable('Y')]),
    print_bindings(erl_syntax_lib:annotate_bindings(erl_syntax:match_expr(Macro, erl_syntax:variable('Z')), ['X'])),
    print_bindings(erl_syntax_lib:annotate_bindings(erl_syntax:macro(erl_syntax:atom(m)), [])),
    show(
        "tree",
        erl_syntax_lib:annotate_bindings(erl_syntax:match_expr(erl_syntax:variable('P'), erl_syntax:integer(1)), [])
    ).

print_bindings(Tree) ->
    Rows = erl_syntax_lib:fold(fun(N, Acc) -> [row(N) | Acc] end, [], Tree),
    [io:format("  ~p~n", [Row]) || Row <- lists:reverse(Rows)],
    io:format("--~n").

row(Node) ->
    Ann = erl_syntax:get_ann(Node),
    Get = fun(Key) -> proplists:get_value(Key, Ann, '-') end,
    {erl_syntax:type(Node), Get(env), Get(bound), Get(free)}.

%% variables/1 of each form and of a macro, as sorted lists.
variables(Forms) ->
    io:format("== variables~n"),
    [show("variables", lists:sort(sets:to_list(erl_syntax_lib:variables(F)))) || F <- Forms],
    Macro = erl_syntax:macro(erl_syntax:variable('M'), [erl_syntax:variable('X')]),
    show("macro", sets:to_list(erl_syntax_lib:variables(Macro))),
    show("set", erl_syntax_lib:variables(erl_syntax:tuple([erl_syntax:variable('A'), erl_syntax:variable('A')]))).

%% Fresh names from the process's first rand state, which erl_syntax_lib seeds.
names() ->
    io:format("== names~n"),
    Taken = sets:from_list(['V1', 'V2']),
    show("name", erl_syntax_lib:new_variable_name(Taken)),
    show("names", erl_syntax_lib:new_variable_names(5, Taken)),
    Own = fun(N) -> list_to_atom("X" ++ integer_to_list(N rem 3)) end,
    show("own", erl_syntax_lib:new_variable_names(3, Own, sets:new())),
    Big = sets:from_list([list_to_atom("V" ++ integer_to_list(N)) || N <- lists:seq(1, 120)]),
    show("big", erl_syntax_lib:new_variable_names(4, Big)),
    show("crowded", erl_syntax_lib:new_variable_name(Own, sets:from_list(['X0', 'X1']))),
    show("zero", erl_syntax_lib:new_variable_names(0, Taken)),
    show("negative", try_call(fun() -> erl_syntax_lib:new_variable_names(-1, Taken) end)).

%% limit/2,3 of a function at several depths, printed.
limits(Forms) ->
    io:format("== limits~n"),
    [F | _] = [F || F <- Forms, erl_syntax:type(F) =:= function],
    Deep = hd(parse(["d() -> {[a, b, c, d, e], f(1, 2, 3, 4), [x], {}, \"s\", 1.5, #{a => b}}."])),
    [
        io:format("~p: ~ts~n", [D, erl_prettypr:format(erl_syntax_lib:limit(T, D))])
     || T <- [F, Deep], D <- [-1, 0, 1, 2, 3, 4, 6]
    ],
    Stub = erl_syntax:atom('$'),
    [io:format("~p: ~ts~n", [D, erl_prettypr:format(erl_syntax_lib:limit(Deep, D, Stub))]) || D <- [1, 2, 3]],
    show("leaf", [erl_syntax_lib:limit(L, 0) || L <- [erl_syntax:atom(a), erl_syntax:string("s"), erl_syntax:tuple([])]]).

%% strip_comments/1 and to_comment/1,2,3.
comments() ->
    io:format("== comments~n"),
    A = erl_syntax:set_precomments(erl_syntax:atom(a), [erl_syntax:comment(["% pre"])]),
    T = erl_syntax:set_postcomments(erl_syntax:tuple([A, erl_syntax:integer(1)]), [erl_syntax:comment(["% post"])]),
    List = erl_syntax:form_list([erl_syntax:comment(["% top"]), T, erl_syntax:form_list([erl_syntax:comment(["% in"])])]),
    show("stripped", erl_syntax_lib:strip_comments(List)),
    show("comment", erl_syntax_lib:strip_comments(erl_syntax:comment(["% c"]))),
    Fun = hd(parse(["f(X) -> case X of\n\t1 -> a;\r\n\t_ -> b end."])),
    show("to_comment", erl_syntax:comment_text(erl_syntax_lib:to_comment(Fun))),
    show("prefix", erl_syntax:comment_text(erl_syntax_lib:to_comment(Fun, "%% "))),
    Text = fun(_) -> "a\tb\tc\r\nab\t\rx\n\n\ty\n" end,
    show("text", erl_syntax:comment_text(erl_syntax_lib:to_comment(Fun, "", Text))),
    show("empty", erl_syntax:comment_text(erl_syntax_lib:to_comment(Fun, "", fun(_) -> "" end))).

%% The folds and maps: visiting order and results.
walks(Forms) ->
    io:format("== walks~n"),
    F = lists:nth(12, Forms),
    show("fold", lists:reverse(erl_syntax_lib:fold(fun(N, Acc) -> [erl_syntax:type(N) | Acc] end, [], F))),
    show("fold_subtrees", erl_syntax_lib:fold_subtrees(fun(N, Acc) -> [erl_syntax:type(N) | Acc] end, [], F)),
    show("foldl_listlist", erl_syntax_lib:foldl_listlist(fun(X, Acc) -> [X | Acc] end, [], [[1, 2], [], [3]])),
    Upper = fun(N) ->
        case erl_syntax:type(N) of
            atom -> erl_syntax:atom(string:uppercase(erl_syntax:atom_name(N)));
            _ -> N
        end
    end,
    io:format("~ts~n", [erl_prettypr:format(erl_syntax_lib:map(Upper, F))]),
    io:format("~ts~n", [erl_prettypr:format(erl_syntax_lib:map_subtrees(Upper, F))]),
    Count = fun(N, K) -> {erl_syntax:add_ann({n, K}, N), K + 1} end,
    {Counted, Total} = erl_syntax_lib:mapfold(Count, 0, F),
    show("mapfold", {Total, erl_syntax_lib:fold(fun(N, Acc) -> [erl_syntax:get_ann(N) | Acc] end, [], Counted)}),
    {Sub, Subs} = erl_syntax_lib:mapfold_subtrees(Count, 10, F),
    show("mapfold_subtrees", {Subs, [erl_syntax:get_ann(N) || G <- erl_syntax:subtrees(Sub), N <- G]}),
    show("mapfold leaf", erl_syntax_lib:mapfold_subtrees(Count, 0, erl_syntax:atom(a))),
    show("mapfoldl_listlist", erl_syntax_lib:mapfoldl_listlist(fun(X, S) -> {X * 2, S + X} end, 0, [[1, 2], [], [3]])),
    show("map leaf", erl_syntax_lib:map_subtrees(Upper, erl_syntax:atom(b))).
