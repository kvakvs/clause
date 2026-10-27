%% erl_syntax (plan 21 LIB77E2): types, accessors, subtrees and reverting of erl_parse forms and of the trees the
%% constructors build, abstract/concrete, comments, positions and annotations, list skeletons, meta/1 and errors.
%% Trees are printed with ~p, so their representation (OTP's tree, wrapper and attr records) is compared too.
-module(erl_syntax_trees).
-export([main/1]).

-define(SOURCE, [
    "-module(corpus).",
    "-export([f/1, g/2]).",
    "-import(lists, [map/2, foldl/3]).",
    "-file(\"corpus.erl\", 1).",
    "-record(r, {a, b = 1, c :: integer(), d = 2 :: atom()}).",
    "-type t(A) :: [A] | {A, binary()} | #{atom() => A, b := t()} | 1..9 | fun((A) -> ok).",
    "-type u() :: fun() | fun((...) -> any()) | <<_:8, _:_*4>> | tuple() | map() | #r{a :: x} | m:n(1).",
    "-spec f(X) -> X when X :: integer().",
    "-opaque o() :: {}.",
    "-custom({1, \"two\", [3]}).",
    "f(X) when X > 0, is_integer(X); X =:= a -> X + 1; f(_) -> -1.",
    "g(A, B) -> case A of {x, Y} -> Y; _ -> catch B end.",
    "h() -> [1, 2 | T] = [1, 2, 3], {T, <<1:8, \"ab\"/binary, X:4/little-unit:8>>, X}.",
    "i(L) -> [X || X <- L, X > 1] ++ [Y || <<Y>> <= <<1>>] ++ [Z || Z <:- L] ++ [W || W && V <- L, V].",
    "j(B) -> << <<X>> || X <- B >>, #{K => V || K := V <- #{}}, #{a => 1, b := 2}, M#{a := 1}.",
    "k(R) -> {R#r.a, R#r{b = 2}, #r{a = 1, _ = 2}, #r.b, fun k/1, fun m:k/1, fun(X) -> X end, fun F(0) -> 0 end}.",
    "l() -> receive {a, X} -> X; _ -> b after 100 -> c end, receive Y -> Y end, begin 1, 2 end.",
    "m() -> try a() of 1 -> b; _ -> c catch throw:x -> d; error:E:S -> {E, S}; Z -> Z after e end.",
    "n() -> if true -> 1 end, maybe {ok, X} ?= a(), X else {error, E} -> E end, maybe 1 end.",
    "o() -> not true, - 1, 'A', $a, \"s\", 1.5, [], {}, a:b(c), F = fun() -> ok end, F().",
    "p(X) -> X#{a => 1}, #{} = X, <<>>, <<1, 2>>, [a | b], 'try' andalso orelse_x orelse y."
]).

main(_) ->
    Forms = parse(?SOURCE),
    plain_forms(Forms),
    tree_forms(Forms),
    constructors(),
    leaves(),
    lists_skeletons(),
    literals(),
    comments_and_attrs(),
    forms_lists(Forms),
    metas(),
    errors(),
    io:format("erl_syntax_trees: ok~n").

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

%% Every node of the erl_parse forms: type, leaf and form tests and accessor results.
plain_forms(Forms) ->
    io:format("== plain~n"),
    lists:foreach(fun(Form) -> walk(Form, 0) end, Forms).

walk(Node, Depth) ->
    Type = erl_syntax:type(Node),
    io:format("~*s~p leaf=~p form=~p literal=~p~n", [
        Depth,
        "",
        Type,
        erl_syntax:is_leaf(Node),
        erl_syntax:is_form(Node),
        erl_syntax:is_literal(Node)
    ]),
    [io:format("~*s  ~p: ~0p~n", [Depth, "", F, attempt(fun() -> erl_syntax:F(Node) end)]) || F <- accessors(Type)],
    [[walk(Child, Depth + 2) || Child <- Group] || Group <- erl_syntax:subtrees(Node)],
    ok.

%% Tree forms rebuilt with make_tree/2 from their subtrees: printed, reverted and compared with the originals.
tree_forms(Forms) ->
    io:format("== trees~n"),
    Trees = [rebuild(Form) || Form <- Forms],
    [io:format("~p~n", [Tree]) || Tree <- lists:sublist(Trees, 6, 6)],
    lists:foreach(
        fun({Form, Tree}) ->
            Reverted = erl_syntax:revert(Tree),
            io:format("~p same=~p~n", [erl_syntax:type(Tree), Reverted =:= Form]),
            Reverted =:= Form orelse io:format("  ~p~n  ~p~n", [Form, Reverted])
        end,
        lists:zip(Forms, Trees)
    ),
    [walk(Tree, 0) || Tree <- lists:sublist(Trees, 11, 3)],
    io:format("updated ~p~n", [erl_syntax:update_tree(hd(Forms), erl_syntax:subtrees(hd(Forms)))]).

rebuild(Node) ->
    case erl_syntax:subtrees(Node) of
        [] ->
            Node;
        Groups ->
            Tree = erl_syntax:make_tree(erl_syntax:type(Node), [[rebuild(C) || C <- G] || G <- Groups]),
            erl_syntax:copy_pos(Node, Tree)
    end.

%% Trees built by every constructor, printed and reverted.
constructors() ->
    io:format("== constructors~n"),
    A = erl_syntax:atom(a),
    V = erl_syntax:variable('V'),
    I = erl_syntax:integer(1),
    C = erl_syntax:clause([V], none, [A]),
    Built = [
        erl_syntax:annotated_type(V, A),
        erl_syntax:application(A, [I]),
        erl_syntax:application(A, A, [I]),
        erl_syntax:application(none, A, []),
        erl_syntax:arity_qualifier(A, I),
        erl_syntax:attribute(A),
        erl_syntax:attribute(A, [I]),
        erl_syntax:binary([erl_syntax:binary_field(I)]),
        erl_syntax:binary_comp(I, [erl_syntax:binary_generator(V, A)]),
        erl_syntax:binary_field(I, I, [A]),
        erl_syntax:binary_field(I, none, [A]),
        erl_syntax:bitstring_type(I, I),
        erl_syntax:block_expr([A]),
        erl_syntax:case_expr(A, [C]),
        erl_syntax:catch_expr(A),
        erl_syntax:class_qualifier(A, V),
        erl_syntax:class_qualifier(A, V, V),
        erl_syntax:clause([I], [A]),
        erl_syntax:clause([], [[A, A], [V]], []),
        erl_syntax:clause([], [A, V], []),
        erl_syntax:conjunction([A]),
        erl_syntax:constrained_function_type(A, [A]),
        erl_syntax:constraint(A, [V]),
        erl_syntax:disjunction([A]),
        erl_syntax:else_expr([C]),
        erl_syntax:eof_marker(),
        erl_syntax:error_marker({1, m, e}),
        erl_syntax:float(1.5),
        erl_syntax:form_list([A]),
        erl_syntax:fun_expr([C]),
        erl_syntax:fun_type(),
        erl_syntax:function(A, [C]),
        erl_syntax:function_type([A], A),
        erl_syntax:function_type(A),
        erl_syntax:generator(V, A),
        erl_syntax:if_expr([C]),
        erl_syntax:implicit_fun(A),
        erl_syntax:implicit_fun(A, I),
        erl_syntax:implicit_fun(A, none),
        erl_syntax:implicit_fun(A, A, I),
        erl_syntax:implicit_fun(none, A, I),
        erl_syntax:infix_expr(I, erl_syntax:operator('+'), I),
        erl_syntax:integer_range_type(I, I),
        erl_syntax:list_comp(A, [erl_syntax:generator(V, A)]),
        erl_syntax:macro(A),
        erl_syntax:macro(A, [I]),
        erl_syntax:map_comp(A, [erl_syntax:map_generator(V, A)]),
        erl_syntax:map_expr([erl_syntax:map_field_assoc(A, I)]),
        erl_syntax:map_expr(V, [erl_syntax:map_field_exact(A, I)]),
        erl_syntax:map_type(),
        erl_syntax:map_type([erl_syntax:map_type_assoc(A, A), erl_syntax:map_type_exact(A, A)]),
        erl_syntax:match_expr(V, A),
        erl_syntax:maybe_expr([A]),
        erl_syntax:maybe_expr([A], erl_syntax:else_expr([C])),
        erl_syntax:maybe_match_expr(V, A),
        erl_syntax:module_qualifier(A, A),
        erl_syntax:named_fun_expr(V, [C]),
        erl_syntax:operator("++"),
        erl_syntax:parentheses(A),
        erl_syntax:prefix_expr(erl_syntax:operator('-'), I),
        erl_syntax:receive_expr([C]),
        erl_syntax:receive_expr([C], I, [A]),
        erl_syntax:receive_expr([C], none, [A]),
        erl_syntax:record_access(V, A, A),
        erl_syntax:record_access(V, erl_syntax:list([A, A]), A),
        erl_syntax:record_expr(A, [erl_syntax:record_field(A)]),
        erl_syntax:record_expr(V, A, [erl_syntax:record_field(A, I)]),
        erl_syntax:record_expr([A, A], []),
        erl_syntax:record_expr(erl_syntax:atom('_'), []),
        erl_syntax:record_index_expr(A, A),
        erl_syntax:record_type(A, [erl_syntax:record_type_field(A, A)]),
        erl_syntax:size_qualifier(I, I),
        erl_syntax:strict_binary_generator(V, A),
        erl_syntax:strict_generator(V, A),
        erl_syntax:strict_map_generator(V, A),
        erl_syntax:text("text"),
        erl_syntax:try_expr([A], [C]),
        erl_syntax:try_expr([A], [C], [C]),
        erl_syntax:try_expr([A], [C], [C], [A]),
        erl_syntax:try_after_expr([A], [A]),
        erl_syntax:tuple_type(),
        erl_syntax:tuple_type([A]),
        erl_syntax:type_application(A, [I]),
        erl_syntax:type_application(A, A, [I]),
        erl_syntax:type_application(none, A, [I]),
        erl_syntax:type_union([A, I]),
        erl_syntax:typed_record_field(erl_syntax:record_field(A), A),
        erl_syntax:user_type_application(A, [I]),
        erl_syntax:warning_marker({1, m, w}),
        erl_syntax:zip_generator([erl_syntax:generator(V, A), erl_syntax:generator(V, A)])
    ],
    lists:foreach(fun constructed/1, Built).

constructed(Tree) ->
    Type = erl_syntax:type(Tree),
    io:format("~p~n  ~0p~n", [Type, Tree]),
    [io:format("  ~p: ~0p~n", [F, attempt(fun() -> erl_syntax:F(Tree) end)]) || F <- accessors(Type)],
    io:format("  subtrees: ~0p~n  revert: ~0p~n", [
        erl_syntax:subtrees(Tree), attempt(fun() -> erl_syntax:revert(Tree) end)
    ]).

%% Leaf constructors, their values, literals and tests.
leaves() ->
    io:format("== leaves~n"),
    Show = fun(Label, Value) -> io:format("~s: ~0p~n", [Label, Value]) end,
    Show("atoms", [erl_syntax:atom(a), erl_syntax:atom("b c"), erl_syntax:atom_name(erl_syntax:atom('x y'))]),
    Show("atom_literal", [
        erl_syntax:atom_literal(erl_syntax:atom('a b')),
        erl_syntax:atom_literal(erl_syntax:atom('ä')),
        erl_syntax:atom_literal(erl_syntax:atom('ä'), utf8),
        erl_syntax:atom_literal(erl_syntax:atom('ä'), unicode),
        erl_syntax:atom_literal({atom, 1, ok})
    ]),
    Show("is_atom", [
        erl_syntax:is_atom(erl_syntax:atom(a), a), erl_syntax:is_atom({atom, 1, a}, b), erl_syntax:is_atom(x, a)
    ]),
    Show("chars", [
        erl_syntax:char($a),
        erl_syntax:char_value({char, 1, $b}),
        erl_syntax:char_literal(erl_syntax:char($\n)),
        erl_syntax:char_literal(erl_syntax:char(1024)),
        erl_syntax:char_literal(erl_syntax:char(1024), utf8),
        erl_syntax:is_char(erl_syntax:char($a), $a),
        erl_syntax:is_char({char, 1, $a}, $b)
    ]),
    Show("strings", [
        erl_syntax:string("ab"),
        erl_syntax:string_value({string, 1, "cd"}),
        erl_syntax:string_literal(erl_syntax:string("a\"b\n")),
        erl_syntax:string_literal(erl_syntax:string([1024])),
        erl_syntax:string_literal(erl_syntax:string([1024]), unicode),
        erl_syntax:is_string(erl_syntax:string("x"), "x"),
        erl_syntax:is_string({string, 1, "x"}, "y")
    ]),
    Show("integers", [
        erl_syntax:integer(-5),
        erl_syntax:integer_value({integer, 2, 7}),
        erl_syntax:integer_literal(erl_syntax:integer(123456789012345678901234567890)),
        erl_syntax:is_integer(erl_syntax:integer(3), 3),
        erl_syntax:is_integer({integer, 1, 3}, 4)
    ]),
    Show("floats", [
        erl_syntax:float(2.5),
        erl_syntax:float_value({float, 1, 0.5}),
        erl_syntax:float_literal(erl_syntax:float(1.0e10))
    ]),
    Show("variables", [
        erl_syntax:variable("X"),
        erl_syntax:variable_name({var, 1, 'Y'}),
        erl_syntax:variable_literal(erl_syntax:variable('Z')),
        erl_syntax:underscore(),
        erl_syntax:type({var, 1, '_'})
    ]),
    Show("operators", [
        erl_syntax:operator_name(erl_syntax:operator("+")), erl_syntax:operator_literal(erl_syntax:operator('div'))
    ]),
    Show("text", [erl_syntax:text_string(erl_syntax:text("t"))]),
    Show("comment", [
        erl_syntax:comment(["% a", "% b"]),
        erl_syntax:comment(2, ["x"]),
        erl_syntax:comment_text(erl_syntax:comment(["% c"])),
        erl_syntax:comment_padding(erl_syntax:comment(3, ["% d"]))
    ]),
    Show("markers", [
        erl_syntax:error_marker_info({error, {1, m, x}}),
        erl_syntax:warning_marker_info(erl_syntax:warning_marker(w)),
        erl_syntax:revert(erl_syntax:error_marker(e)),
        erl_syntax:revert(erl_syntax:eof_marker())
    ]),
    Show("tuples", [
        erl_syntax:tuple_size(erl_syntax:tuple([erl_syntax:atom(a)])),
        erl_syntax:tuple_size({tuple, 1, [{atom, 1, a}, {atom, 1, b}]})
    ]),
    Show("arities", [
        erl_syntax:function_arity(
            {function, 1, f, 2, [{clause, 1, [{var, 1, 'A'}, {var, 1, 'B'}], [], [{atom, 1, ok}]}]}
        ),
        erl_syntax:fun_expr_arity(erl_syntax:fun_expr([erl_syntax:clause([erl_syntax:underscore()], none, [])])),
        erl_syntax:named_fun_expr_arity({named_fun, 1, 'F', [{clause, 1, [], [], [{atom, 1, ok}]}]})
    ]),
    Show("trees", [
        erl_syntax:tree(t),
        erl_syntax:tree(t, d),
        erl_syntax:data(erl_syntax:tree(t, d)),
        erl_syntax:is_tree(erl_syntax:tree(t)),
        erl_syntax:is_tree({atom, 1, a})
    ]).

%% List skeletons in both representations.
lists_skeletons() ->
    io:format("== lists~n"),
    [A, B, C] = [erl_syntax:atom(X) || X <- [a, b, c]],
    Plain = {cons, 1, {atom, 1, a}, {cons, 1, {atom, 1, b}, {var, 1, 'T'}}},
    Nested = erl_syntax:list([A], erl_syntax:list([B], erl_syntax:list([C]))),
    Lists = [
        erl_syntax:list([]),
        erl_syntax:list([A, B]),
        erl_syntax:list([A], B),
        erl_syntax:nil(),
        Plain,
        {nil, 1},
        {cons, 1, {atom, 1, a}, {nil, 1}},
        Nested,
        erl_syntax:list([A], erl_syntax:nil())
    ],
    lists:foreach(
        fun(L) ->
            io:format("~0p~n", [L]),
            io:format("  skeleton=~p proper=~p~n", [erl_syntax:is_list_skeleton(L), erl_syntax:is_proper_list(L)]),
            io:format("  prefix=~0p~n  suffix=~0p~n", [
                attempt(fun() -> erl_syntax:list_prefix(L) end), attempt(fun() -> erl_syntax:list_suffix(L) end)
            ]),
            io:format("  head=~0p~n  tail=~0p~n", [
                attempt(fun() -> erl_syntax:list_head(L) end), attempt(fun() -> erl_syntax:list_tail(L) end)
            ]),
            io:format("  elements=~0p~n  length=~0p~n", [
                attempt(fun() -> erl_syntax:list_elements(L) end), attempt(fun() -> erl_syntax:list_length(L) end)
            ]),
            io:format("  normalize=~0p~n  compact=~0p~n", [erl_syntax:normalize_list(L), erl_syntax:compact_list(L)]),
            io:format("  revert=~0p~n", [erl_syntax:revert(L)])
        end,
        Lists
    ),
    io:format("cons ~0p~n", [[erl_syntax:cons(A, B), erl_syntax:cons(A, erl_syntax:nil()), erl_syntax:cons(A, Nested)]]),
    io:format("cons plain ~0p~n", [erl_syntax:cons(A, Plain)]),
    Located = erl_syntax:set_pos(erl_syntax:list([A]), erl_anno:set_end_location({3, 4}, erl_anno:new({3, 1}))),
    io:format("end ~0p~n", [erl_syntax:revert(Located)]).

%% abstract/1, concrete/1 and is_literal/1.
literals() ->
    io:format("== literals~n"),
    Terms = [a, 1, -2, 1.5, [], "abc", [1, 2], [a | b], {a, [b]}, #{a => 1}, <<1, 2>>, <<5:3>>, [1, a], [300]],
    lists:foreach(
        fun(T) ->
            Abs = erl_syntax:abstract(T),
            io:format("~0p~n  ~0p~n  literal=~p concrete=~0p~n", [
                T, Abs, erl_syntax:is_literal(Abs), erl_syntax:concrete(Abs)
            ])
        end,
        Terms
    ),
    Plain = [
        {map, 1, [{map_field_assoc, 1, {atom, 1, a}, {integer, 1, 1}}]},
        {map, 1, [{map_field_exact, 1, {atom, 1, a}, {integer, 1, 1}}]}
    ],
    io:format("literal maps ~p~n", [[erl_syntax:is_literal(P) || P <- Plain]]),
    io:format("concrete ~0p~n", [
        [
            sorted_map(V)
         || V <- [
                erl_syntax:concrete({bin, 1, [{bin_element, 1, {string, 1, "ab"}, default, [utf8]}]}),
                erl_syntax:concrete(erl_syntax:arity_qualifier(erl_syntax:atom(f), erl_syntax:integer(1))),
                erl_syntax:concrete(
                    {map, 1, {map, 1, [{map_field_assoc, 1, {atom, 1, a}, {integer, 1, 1}}]}, [
                        {map_field_assoc, 1, {atom, 1, b}, {integer, 1, 2}}
                    ]}
                ),
                erl_syntax:concrete({char, 1, $x})
            ]
        ]
    ]),
    io:format("binary literal ~p~n", [
        [
            erl_syntax:is_literal({bin, 1, [{bin_element, 1, {string, 1, "ab"}, default, [utf8]}]}),
            erl_syntax:is_literal({bin, 1, [{bin_element, 1, {integer, 1, 1}, {integer, 1, 8}, default}]}),
            erl_syntax:is_literal({bin, 1, [{bin_element, 1, {var, 1, 'X'}, default, default}]})
        ]
    ]).

%% Positions, comments, annotations and attributes on plain nodes, wrappers and trees.
comments_and_attrs() ->
    io:format("== attrs~n"),
    Plain = {atom, 5, a},
    Tree = erl_syntax:atom(b),
    Com = erl_syntax:comment(["% x"]),
    Steps = [
        {get_pos, [
            erl_syntax:get_pos(Plain),
            erl_syntax:get_pos(Tree),
            erl_syntax:get_pos({error, {7, m, e}}),
            erl_syntax:get_pos({warning, {8, m, w}})
        ]},
        {set_pos, [erl_syntax:set_pos(Plain, 9), erl_syntax:set_pos(Tree, 9)]},
        {copy_pos, erl_syntax:copy_pos(Plain, Tree)},
        {pre, erl_syntax:set_precomments(Plain, [Com])},
        {add_pre, erl_syntax:add_precomments([Com], erl_syntax:add_precomments([Com], Tree))},
        {post, erl_syntax:set_postcomments(Tree, [Com])},
        {add_post, erl_syntax:add_postcomments([Com], erl_syntax:set_precomments(Tree, [Com]))},
        {get_comments, [
            erl_syntax:get_precomments(Plain), erl_syntax:get_postcomments(erl_syntax:set_postcomments(Plain, [Com]))
        ]},
        {has, [
            erl_syntax:has_comments(Plain),
            erl_syntax:has_comments(Tree),
            erl_syntax:has_comments(erl_syntax:set_precomments(Tree, [])),
            erl_syntax:has_comments(erl_syntax:set_precomments(Tree, [Com]))
        ]},
        {remove, [
            erl_syntax:remove_comments(Plain), erl_syntax:remove_comments(erl_syntax:set_precomments(Plain, [Com]))
        ]},
        {copy_comments, erl_syntax:copy_comments(erl_syntax:set_postcomments(Tree, [Com]), Plain)},
        {join,
            erl_syntax:join_comments(
                erl_syntax:set_postcomments(erl_syntax:set_precomments(Tree, [Com]), [Com]),
                erl_syntax:set_precomments(Plain, [Tree])
            )},
        {ann, [
            erl_syntax:get_ann(Plain),
            erl_syntax:set_ann(Plain, [x]),
            erl_syntax:add_ann(y, erl_syntax:set_ann(Tree, [x])),
            erl_syntax:get_ann(erl_syntax:add_ann(z, Plain))
        ]},
        {copy_ann, erl_syntax:copy_ann(erl_syntax:set_ann(Tree, [q]), Plain)},
        {attrs, [erl_syntax:get_attrs(Plain), erl_syntax:get_attrs(erl_syntax:set_ann(Tree, [a]))]},
        {set_attrs, erl_syntax:set_attrs(Plain, erl_syntax:get_attrs(erl_syntax:add_ann(k, Tree)))},
        {copy_attrs, erl_syntax:copy_attrs(erl_syntax:set_pos(Tree, 4), Plain)},
        {wrapped_revert, [
            erl_syntax:revert(erl_syntax:set_pos(Plain, 11)), erl_syntax:revert(erl_syntax:set_ann({error, x}, [a]))
        ]},
        {wrapped_type, [
            erl_syntax:type(erl_syntax:set_ann(Plain, [])), erl_syntax:atom_value(erl_syntax:set_ann(Plain, []))
        ]}
    ],
    [io:format("~p: ~0p~n", [Name, Value]) || {Name, Value} <- Steps],
    ok.

%% form_list/1, flatten_form_list/1 and revert_forms/1.
forms_lists(Forms) ->
    io:format("== form lists~n"),
    [F1, F2, F3 | _] = Forms,
    Nested = erl_syntax:form_list([
        F1, erl_syntax:form_list([F2, erl_syntax:form_list([]), F3]), erl_syntax:comment(["% c"])
    ]),
    io:format("~0p~n", [erl_syntax:form_list_elements(erl_syntax:flatten_form_list(Nested))]),
    io:format("~0p~n", [erl_syntax:revert_forms(Nested)]),
    io:format("~0p~n", [erl_syntax:revert_forms([F1, rebuild(F2)])]),
    io:format("~0p~n", [attempt(fun() -> erl_syntax:revert_forms([F1, erl_syntax:macro(erl_syntax:atom(m))]) end)]),
    io:format("~0p~n", [attempt(fun() -> erl_syntax:revert_forms(erl_syntax:atom(a)) end)]),
    Attribute = erl_syntax:attribute(erl_syntax:atom(export), [
        erl_syntax:list([erl_syntax:arity_qualifier(erl_syntax:atom(f), erl_syntax:integer(0))])
    ]),
    io:format("~0p~n", [erl_syntax:revert(Attribute)]),
    Record = erl_syntax:attribute(erl_syntax:atom(record), [
        erl_syntax:atom(r), erl_syntax:tuple([erl_syntax:record_field(erl_syntax:atom(a))])
    ]),
    io:format("~0p~n", [erl_syntax:revert(Record)]),
    Bad = erl_syntax:attribute(erl_syntax:variable('M'), [erl_syntax:atom(a)]),
    io:format("~0p~n", [erl_syntax:revert(Bad)]),
    Wild = erl_syntax:attribute(erl_syntax:atom(module), [
        erl_syntax:atom(m), erl_syntax:list([erl_syntax:variable('P')])
    ]),
    io:format("~0p~n", [erl_syntax:revert(Wild)]),
    io:format("~0p~n", [erl_syntax:attribute_arguments({attribute, 1, module, {m, ['P']}})]).

%% meta/1 of trees, plain nodes, comments and meta variables.
metas() ->
    io:format("== meta~n"),
    Nodes = [
        erl_syntax:atom(a),
        {integer, 1, 2},
        erl_syntax:list([erl_syntax:atom(a)], erl_syntax:variable('T')),
        erl_syntax:tuple([erl_syntax:string("s"), erl_syntax:char($c), erl_syntax:float(1.0)]),
        erl_syntax:set_precomments(erl_syntax:atom(b), [erl_syntax:comment(["% c"])]),
        erl_syntax:set_postcomments(erl_syntax:nil(), [erl_syntax:comment(["% d"])]),
        erl_syntax:add_ann(meta_var, erl_syntax:variable('X')),
        erl_syntax:variable('Y'),
        erl_syntax:operator('+'),
        erl_syntax:text("t"),
        erl_syntax:underscore(),
        erl_syntax:eof_marker(),
        erl_syntax:error_marker({1, m, e}),
        erl_syntax:warning_marker(w),
        erl_syntax:comment(["% e"]),
        {call, 1, {atom, 1, f}, [{var, 1, 'A'}]}
    ],
    [io:format("~0p~n", [erl_syntax:meta(N)]) || N <- Nodes],
    ok.

%% OTP's errors for malformed nodes and records of another type.
errors() ->
    io:format("== errors~n"),
    Calls = [
        fun() -> erl_syntax:type(foo) end,
        fun() -> erl_syntax:type({bar, 1}) end,
        fun() -> erl_syntax:data({atom, 1, a}) end,
        fun() -> erl_syntax:case_expr_argument(erl_syntax:atom(a)) end,
        fun() -> erl_syntax:case_expr_argument({atom, 1, a}) end,
        fun() -> erl_syntax:comment_text(erl_syntax:atom(a)) end,
        fun() ->
            try
                erl_syntax:abstract(self())
            catch
                error:{badarg, Pid} when Pid =:= self() -> badarg_self
            end
        end,
        fun() -> erl_syntax:concrete(erl_syntax:variable('X')) end,
        fun() -> erl_syntax:make_tree(atom, []) end,
        fun() -> erl_syntax:list([], erl_syntax:atom(a)) end,
        fun() -> erl_syntax:list_head(erl_syntax:nil()) end,
        fun() -> erl_syntax:list_elements(erl_syntax:atom(a)) end,
        fun() -> erl_syntax:function_arity(erl_syntax:function(erl_syntax:atom(f), [])) end,
        fun() -> erl_syntax:attribute_arguments({attribute, 1, import, x}) end,
        fun() ->
            erl_syntax:revert(erl_syntax:attribute(erl_syntax:atom(export), [erl_syntax:list([erl_syntax:atom(f)])]))
        end,
        fun() ->
            erl_syntax:is_literal(
                erl_syntax:binary([
                    erl_syntax:binary_field(erl_syntax:integer(1), [erl_syntax:atom(a), erl_syntax:atom(b)])
                ])
            )
        end,
        fun() -> erl_syntax:subtrees(erl_syntax:tree(unknown, [])) end,
        fun() -> erl_syntax:revert(erl_syntax:tree(unknown, [])) end,
        fun() -> erl_syntax:clause_guard({clause, 1, [], x, []}) end
    ],
    [io:format("~0p~n", [attempt(F)]) || F <- Calls],
    ok.

%% A map as its sorted pairs, since map printing order differs; other terms as they are.
sorted_map(Map) when is_map(Map) -> {map, lists:sort(maps:to_list(Map))};
sorted_map(Term) -> Term.

%% A call's value, or its exception class and reason without the stack trace.
attempt(F) ->
    try F() of
        V -> {value, V}
    catch
        C:R -> {C, R}
    end.

%% The accessors of each node type, as subtrees/1 and the documentation group them.
accessors(Type) ->
    case Type of
        annotated_type -> [annotated_type_name, annotated_type_body];
        application -> [application_operator, application_arguments];
        arity_qualifier -> [arity_qualifier_body, arity_qualifier_argument];
        atom -> [atom_value, atom_name, atom_literal];
        attribute -> [attribute_name, attribute_arguments];
        binary -> [binary_fields];
        binary_comp -> [binary_comp_template, binary_comp_body];
        binary_field -> [binary_field_body, binary_field_types, binary_field_size];
        binary_generator -> [binary_generator_pattern, binary_generator_body];
        bitstring_type -> [bitstring_type_m, bitstring_type_n];
        block_expr -> [block_expr_body];
        case_expr -> [case_expr_argument, case_expr_clauses];
        catch_expr -> [catch_expr_body];
        char -> [char_value, char_literal];
        class_qualifier -> [class_qualifier_argument, class_qualifier_body, class_qualifier_stacktrace];
        clause -> [clause_patterns, clause_guard, clause_body];
        conjunction -> [conjunction_body];
        constrained_function_type -> [constrained_function_type_body, constrained_function_type_argument];
        constraint -> [constraint_argument, constraint_body];
        disjunction -> [disjunction_body];
        else_expr -> [else_expr_clauses];
        error_marker -> [error_marker_info];
        float -> [float_value, float_literal];
        form_list -> [form_list_elements];
        fun_expr -> [fun_expr_clauses, fun_expr_arity];
        function -> [function_name, function_clauses, function_arity];
        function_type -> [function_type_arguments, function_type_return];
        generator -> [generator_pattern, generator_body];
        if_expr -> [if_expr_clauses];
        implicit_fun -> [implicit_fun_name];
        infix_expr -> [infix_expr_left, infix_expr_operator, infix_expr_right];
        integer -> [integer_value, integer_literal];
        integer_range_type -> [integer_range_type_low, integer_range_type_high];
        list -> [list_prefix, list_suffix];
        list_comp -> [list_comp_template, list_comp_body];
        macro -> [macro_name, macro_arguments];
        map_comp -> [map_comp_template, map_comp_body];
        map_expr -> [map_expr_argument, map_expr_fields];
        map_field_assoc -> [map_field_assoc_name, map_field_assoc_value];
        map_field_exact -> [map_field_exact_name, map_field_exact_value];
        map_generator -> [map_generator_pattern, map_generator_body];
        map_type -> [map_type_fields];
        map_type_assoc -> [map_type_assoc_name, map_type_assoc_value];
        map_type_exact -> [map_type_exact_name, map_type_exact_value];
        match_expr -> [match_expr_pattern, match_expr_body];
        maybe_expr -> [maybe_expr_body, maybe_expr_else];
        maybe_match_expr -> [maybe_match_expr_pattern, maybe_match_expr_body];
        module_qualifier -> [module_qualifier_argument, module_qualifier_body];
        named_fun_expr -> [named_fun_expr_name, named_fun_expr_clauses, named_fun_expr_arity];
        operator -> [operator_name, operator_literal];
        parentheses -> [parentheses_body];
        prefix_expr -> [prefix_expr_operator, prefix_expr_argument];
        receive_expr -> [receive_expr_clauses, receive_expr_timeout, receive_expr_action];
        record_access -> [record_access_argument, record_access_type, record_access_field];
        record_expr -> [record_expr_argument, record_expr_type, record_expr_fields];
        record_field -> [record_field_name, record_field_value];
        record_index_expr -> [record_index_expr_type, record_index_expr_field];
        record_type -> [record_type_name, record_type_fields];
        record_type_field -> [record_type_field_name, record_type_field_type];
        size_qualifier -> [size_qualifier_body, size_qualifier_argument];
        strict_binary_generator -> [strict_binary_generator_pattern, strict_binary_generator_body];
        strict_generator -> [strict_generator_pattern, strict_generator_body];
        strict_map_generator -> [strict_map_generator_pattern, strict_map_generator_body];
        string -> [string_value, string_literal];
        text -> [text_string];
        try_expr -> [try_expr_body, try_expr_clauses, try_expr_handlers, try_expr_after];
        tuple -> [tuple_elements, tuple_size];
        tuple_type -> [tuple_type_elements];
        type_application -> [type_application_name, type_application_arguments];
        type_union -> [type_union_types];
        typed_record_field -> [typed_record_field_body, typed_record_field_type];
        user_type_application -> [user_type_application_name, user_type_application_arguments];
        variable -> [variable_name, variable_literal];
        warning_marker -> [warning_marker_info];
        zip_generator -> [zip_generator_body];
        _ -> []
    end.
