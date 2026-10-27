%% erl_prettypr (plan 21 LIB77E3b): the text of every node type at several paper and ribbon widths, comments,
%% hooks with their context, the empty_lines and encoding options, long strings, floats, markers, best/1,2 and
%% layout/1,2 documents.
-module(erl_prettypr_format).
-export([main/1]).

-define(SOURCE, [
    "-module(corpus).",
    "-export([f/1, g/2, h/0]).",
    "-import(lists, [map/2]).",
    "-record(r, {a, b = 1, c :: integer(), d = 2 :: atom() | [term()]}).",
    "-type t(A) :: [A] | {A, binary()} | #{atom() => A, b := t(A)} | 1..9 | fun((A) -> ok).",
    "-type u() :: fun() | fun((...) -> any()) | <<_:8, _:_*4>> | tuple() | map() | #r{a :: x} | m:n(1).",
    "-type v() :: [] | nonempty_list(integer()) | list(atom()) | <<>> | <<_:3>> | -1 | 'quoted atom'.",
    "-opaque o() :: {}.",
    "-spec f(X) -> X when X :: integer(); (atom()) -> atom().",
    "-spec m:g(a, b) -> ok.",
    "-callback c(term()) -> ok.",
    "-export_type([t/1, u/0]).",
    "-optional_callbacks([c/1]).",
    "-custom({1, \"two\", [3], 4.5}).",
    "-file(\"corpus.erl\", 1).",
    "-compile(export_all).",
    "f(X) when X > 0, is_integer(X); X =:= a -> X + 1; f(_) -> -1.",
    "g(A, B) -> case A of {x, Y} -> Y; _ -> catch B end.",
    "h() -> [1, 2 | T] = [1, 2, 3], {T, <<1:8, \"ab\"/binary, X:4/little-unit:8>>, X}.",
    "i(L) -> [X || X <- L, X > 1] ++ [Y || <<Y>> <= <<1>>] ++ [Z || Z <:- L] ++ [W || W && V <- L, V].",
    "j(B) -> << <<X>> || X <- B >>, #{K => V || K := V <- #{}}, #{a => 1, b := 2}, M#{a := 1}.",
    "k(R) -> {R#r.a, R#r{b = 2}, #r{a = 1, _ = 2}, #r.b, fun k/1, fun m:k/1, fun(X) -> X end, fun F(0) -> 0 end}.",
    "l() -> receive {a, X} -> X; _ -> b after 100 -> c end, receive Y -> Y end, begin 1, 2 end.",
    "m() -> try a() of 1 -> b; _ -> c catch throw:x -> d; error:E:S -> {E, S}; Z -> Z after e end.",
    "n() -> if true -> 1; false -> 2 end, maybe {ok, X} ?= a(), X else {error, E} -> E end, maybe 1 end.",
    "o() -> not true, - 1, + 2, 'A', $a, $\\n, \"s\", 1.5, 1.0e10, 0.1, [], {}, a:b(c), F = fun() -> ok end, F().",
    "p(X) -> X#{a => 1}, #{} = X, <<>>, <<1, 2>>, [a | b], (1 + 2) * 3, 1 - (2 - 3), -(-1), a andalso b orelse c.",
    "q() -> try ok catch _ -> error end, try ok after done end, (catch 1) + 2, 'quoted atom', [$a | \"bc\"].",
    "r(VeryLongVariableNameNumberOne, VeryLongVariableNameNumberTwo) -> some_module:some_function(VeryLongVariableNameNumberOne, VeryLongVariableNameNumberTwo, [another_element, yet_another_element, {tuple, element}]).",
    "s() -> \"a long string that will certainly need to be split across several lines when the ribbon is narrow enough\\n\\tsecond part\".",
    "t(<<A:8/integer-unit:1, B/binary>>) -> {A, B, 16#ff, 2#101, 1.0e-5, 123.456e3}."
]).

main(_) ->
    Forms = parse(?SOURCE),
    io:format("== default~n"),
    [print(erl_prettypr:format(Form)) || Form <- Forms],
    io:format("== narrow~n"),
    [print(erl_prettypr:format(Form, [{paper, 40}, {ribbon, 30}])) || Form <- Forms],
    io:format("== wide~n"),
    [print(erl_prettypr:format(Form, [{paper, 200}, {ribbon, 150}])) || Form <- lists:nthtail(16, Forms)],
    io:format("== trees~n"),
    [print(erl_prettypr:format(tree(Form))) || Form <- lists:nthtail(16, Forms)],
    comments(),
    hooks(),
    options(),
    built_nodes(),
    documents(Forms),
    io:format("erl_prettypr_format: ok~n").

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

print(Text) -> io:format("~ts~n", [Text]).

%% A form rebuilt as a tree with make_tree/2.
tree(Node) ->
    case erl_syntax:subtrees(Node) of
        [] ->
            Node;
        Groups ->
            Tree = erl_syntax:make_tree(erl_syntax:type(Node), [[tree(C) || C <- G] || G <- Groups]),
            erl_syntax:copy_pos(Node, Tree)
    end.

%% Pre- and postcomments, padding, standalone comments and comments in form lists.
comments() ->
    io:format("== comments~n"),
    A = erl_syntax:atom(a),
    Pre = erl_syntax:comment(["% before", "% again"]),
    Post = erl_syntax:comment(4, ["% after"]),
    Call = erl_syntax:application(erl_syntax:atom(f), [erl_syntax:set_precomments(A, [Pre]), erl_syntax:integer(1)]),
    Fun = erl_syntax:function(erl_syntax:atom(g), [
        erl_syntax:clause([], none, [erl_syntax:set_postcomments(Call, [Post])])
    ]),
    print(erl_prettypr:format(erl_syntax:set_precomments(Fun, [erl_syntax:comment(["%% function g"])]))),
    print(erl_prettypr:format(erl_syntax:comment(["% standalone", "% two lines"]))),
    print(erl_prettypr:format(erl_syntax:comment(3, ["% padded"]))),
    print(erl_prettypr:format(erl_syntax:comment([]))),
    List = erl_syntax:form_list([
        erl_syntax:attribute(erl_syntax:atom(module), [erl_syntax:atom(m)]),
        erl_syntax:comment(["% between"]),
        Fun,
        erl_syntax:eof_marker()
    ]),
    print(erl_prettypr:format(List)).

%% A hook that brackets annotated nodes and reads and changes the context it is given.
hooks() ->
    io:format("== hooks~n"),
    Hook = fun(Node, Ctxt, Continue) ->
        Info = [
            erl_prettypr:get_ctxt_precedence(Ctxt),
            erl_prettypr:get_ctxt_paperwidth(Ctxt),
            erl_prettypr:get_ctxt_linewidth(Ctxt),
            erl_prettypr:get_ctxt_user(Ctxt),
            is_function(erl_prettypr:get_ctxt_hook(Ctxt), 3)
        ],
        Ctxt1 = erl_prettypr:set_ctxt_user(erl_prettypr:set_ctxt_precedence(Ctxt, 0), seen),
        Inner = Continue(Node, erl_prettypr:set_ctxt_linewidth(erl_prettypr:set_ctxt_paperwidth(Ctxt1, 70), 50)),
        Label = lists:flatten(io_lib:format("~w", [Info])),
        prettypr:beside(prettypr:text("<" ++ Label ++ ">"), Inner)
    end,
    A = erl_syntax:add_ann(marked, erl_syntax:atom(a)),
    Sum = erl_syntax:infix_expr(
        A,
        erl_syntax:operator('*'),
        erl_syntax:infix_expr(erl_syntax:integer(1), erl_syntax:operator('+'), erl_syntax:integer(2))
    ),
    print(erl_prettypr:format(Sum, [{hook, Hook}, {user, me}])),
    print(erl_prettypr:format(erl_syntax:add_ann(top, Sum), [{hook, Hook}, {paper, 60}, {ribbon, 40}])),
    print(erl_prettypr:format(Sum)).

%% The empty_lines and encoding options, long strings and floats at several widths.
options() ->
    io:format("== options~n"),
    Source = "f() ->\n    a(),\n\n    b(),\n    c().\n",
    {ok, Tokens, _} = erl_scan:string(Source, 1),
    {ok, Form} = erl_parse:parse_form(Tokens),
    print(erl_prettypr:format(Form)),
    print(erl_prettypr:format(Form, [{empty_lines, sets:from_list([3])}])),
    print(erl_prettypr:format(Form, [{empty_lines, sets:from_list([2, 3, 4])}])),
    Unicode = erl_syntax:tuple([erl_syntax:atom('ä'), erl_syntax:string([1024, $a]), erl_syntax:char(1024)]),
    print(erl_prettypr:format(Unicode)),
    print(erl_prettypr:format(Unicode, [{encoding, utf8}])),
    print(erl_prettypr:format(Unicode, [{encoding, unicode}])),
    Long = erl_syntax:string("one two three four five six seven eight nine ten eleven twelve thirteen\\x{41}\\101"),
    [print(erl_prettypr:format(Long, [{ribbon, R}])) || R <- [10, 20, 40, 100]],
    Floats = [erl_syntax:float(F) || F <- [0.0, 1.5, 1.0e10, 1.0e-10, 123.456, 0.1, 1.0e100, -2.5e-3, 3.0e20]],
    print(erl_prettypr:format(erl_syntax:list(Floats))).

%% Nodes built by the constructors, including those erl_parse has no form for.
built_nodes() ->
    io:format("== nodes~n"),
    A = erl_syntax:atom(a),
    V = erl_syntax:variable('V'),
    I = erl_syntax:integer(1),
    C = erl_syntax:clause([V], none, [A]),
    Nodes = [
        erl_syntax:text("raw text"),
        erl_syntax:macro(erl_syntax:variable('MOD')),
        erl_syntax:macro(erl_syntax:atom(m), [I, A]),
        erl_syntax:parentheses(erl_syntax:infix_expr(I, erl_syntax:operator('+'), I)),
        erl_syntax:error_marker({3, erl_parse, ["syntax error before: ", "x"]}),
        erl_syntax:error_marker({0, erl_parse, "bad"}),
        erl_syntax:error_marker(something_else),
        erl_syntax:warning_marker({2, erl_lint, {unused_var, 'X'}}),
        erl_syntax:eof_marker(),
        erl_syntax:clause([V], [A], [I]),
        erl_syntax:size_qualifier(V, I),
        erl_syntax:class_qualifier(A, V),
        erl_syntax:class_qualifier(A, V, erl_syntax:variable('S')),
        erl_syntax:record_field(A),
        erl_syntax:typed_record_field(
            erl_syntax:record_field(A, I), erl_syntax:type_application(erl_syntax:atom(integer), [])
        ),
        erl_syntax:conjunction([A, V]),
        erl_syntax:disjunction([A, V]),
        erl_syntax:arity_qualifier(A, I),
        erl_syntax:module_qualifier(A, A),
        erl_syntax:if_expr([C]),
        erl_syntax:named_fun_expr(erl_syntax:variable('F'), [C]),
        erl_syntax:try_expr([A], [C], [C], [A]),
        erl_syntax:try_after_expr([A], [A]),
        erl_syntax:receive_expr([], I, [A]),
        erl_syntax:record_access(V, erl_syntax:list([A, A]), A),
        erl_syntax:record_expr([A, A], [erl_syntax:record_field(A, I)]),
        erl_syntax:attribute(erl_syntax:atom('if'), [A]),
        erl_syntax:attribute(erl_syntax:atom(native_record), [A, erl_syntax:tuple([erl_syntax:record_field(A)])]),
        erl_syntax:attribute(erl_syntax:atom(endif)),
        erl_syntax:annotated_type(V, erl_syntax:type_application(erl_syntax:atom(atom), [])),
        erl_syntax:constraint(erl_syntax:atom(is_subtype), [V, A]),
        erl_syntax:constraint(erl_syntax:atom(other), [V, A]),
        erl_syntax:map_type_assoc(A, I),
        erl_syntax:list([A, V, I], erl_syntax:list([A], erl_syntax:nil())),
        erl_syntax:function_type(any_arity, A),
        erl_syntax:tuple_type([A, I]),
        erl_syntax:binary_field(V, [
            erl_syntax:atom(little), erl_syntax:size_qualifier(erl_syntax:atom(unit), erl_syntax:integer(8))
        ])
    ],
    [print(erl_prettypr:format(N)) || N <- Nodes],
    % Markers are formatted by their module's format_error/1, so the program links erl_lint and erl_parse.
    print(erl_lint:format_error({unused_var, 'X'})),
    print(erl_parse:format_error(["syntax error before: ", "x"])).

%% best/1,2 and layout/1,2 as documents.
documents(Forms) ->
    io:format("== documents~n"),
    Form = lists:nth(17, Forms),
    io:format("~p~n", [erl_prettypr:layout(Form)]),
    io:format("~p~n", [erl_prettypr:best(Form)]),
    io:format("~p~n", [erl_prettypr:best(Form, [{paper, 30}, {ribbon, 20}])]),
    io:format("~p~n", [erl_prettypr:layout(erl_syntax:atom(a), [{paper, 10}])]).
