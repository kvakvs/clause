%% erl_pp over parsed forms and expressions (plan 21 step LIB34): forms of a snippet module, expressions at several
%% indentations and line widths, guards, hooks, the options, legalize_vars/1 and forms erl_pp cannot print.
-module(erl_pp_forms).
-export([main/1, hook/4, ehook/5]).

main(_) ->
    [show("form", erl_pp:form(F)) || F <- forms(source())],
    options(),
    expressions(),
    entry_points(),
    io:format("erl_pp_forms: ok~n").

show(Label, Chars) -> io:format("~s:~n~ts~n", [Label, lists:flatten(Chars)]).

forms(Text) -> forms(Text, 1, []).

forms(Text, Line, Acc) ->
    case erl_scan:tokens([], Text, Line) of
        {done, {ok, Tokens, End}, Rest} ->
            {ok, Form} = erl_parse:parse_form(Tokens),
            forms(Rest, End, [Form | Acc]);
        {done, {eof, _}, _} ->
            lists:reverse(Acc);
        {more, _} ->
            lists:reverse(Acc)
    end.

exprs(Text) ->
    {ok, Tokens, _} = erl_scan:string(Text),
    {ok, Es} = erl_parse:parse_exprs(Tokens),
    Es.

source() ->
    "-module(snippet).\n"
    "-export([f/1, g/2, h/0]).\n"
    "-export_type([t/0]).\n"
    "-import(lists, [map/2, foldl/3]).\n"
    "-compile({nowarn_unused_function, [{k, 1}]}).\n"
    "-record(r, {a = 1 :: integer(), b, c = \"text\" :: string() | atom()}).\n"
    "-type t() :: {ok, [integer()]} | {error, term()} | #{atom() => binary(), k := 1..10}.\n"
    "-opaque o(A) :: [A] | nonempty_list(A) | fun((A) -> A) | [A, ...] | fun(() -> ok) | fun((...) -> any()).\n"
    "-type bits() :: <<>> | <<_:8>> | <<_:_*4>> | <<_:3, _:_*2>> | #r{a :: 1} | tuple() | {} | map().\n"
    "-spec f(X) -> X when X :: integer(); (atom()) -> atom().\n"
    "-spec g(A :: list(), B) -> ok when B :: module:type(A), is_subtype(A, term()).\n"
    "-callback init(Args :: term()) -> {ok, State :: term()} | ignore.\n"
    "-my_attr({some, [attribute, {value, 1}]}).\n"
    "f(X) when is_integer(X), X > 0; is_atom(X) -> X;\n"
    "f(X) -> case X of {a, B} -> B; [H | T] when H =:= 1 -> T; _ -> other end.\n"
    "g(A, B) ->\n"
    "    C = [Y * 2 || Y <- A, Y > B, <<Z:4>> <= <<1, 2>>, K := V <- #{a => 1}],\n"
    "    D = << <<X:8/little-unit:1>> || X <- C >>,\n"
    "    M = #{K => V || K := V <- #{1 => 2}},\n"
    "    R = #r{a = 2, b = C},\n"
    "    R2 = R#r{c = D},\n"
    "    {R#r.a, R2, #r.b, M#{k => 1, j := 2}, catch throw(x), -A, not B, bnot 3, A ++ B -- C}.\n"
    "h() ->\n"
    "    try\n"
    "        receive {msg, M} -> M; stop -> exit(normal) after 1000 -> timeout end\n"
    "    of\n"
    "        timeout -> begin x, y end;\n"
    "        Other -> if Other > 1 -> big; true -> small end\n"
    "    catch\n"
    "        throw:T -> T;\n"
    "        error:R:S -> {R, S};\n"
    "        C:R -> {C, R}\n"
    "    after\n"
    "        ok\n"
    "    end,\n"
    "    F = fun(0) -> zero; (N) when N > 0 -> fun lists:reverse/1 end,\n"
    "    G = fun Fact(0) -> 1; Fact(N) -> N * Fact(N - 1) end,\n"
    "    E = fun h/0, M = fun erlang:abs/1, Mod = lists, Fn = sort, Ar = 1, X = fun Mod:Fn/Ar,\n"
    "    maybe {ok, A} ?= F(1), {ok, B} ?= G(2), A + B else {error, Er} -> Er end,\n"
    "    maybe ok end,\n"
    "    (A andalso B) orelse C,\n"
    "    lists:foldl(fun(Xx, Acc) -> [Xx | Acc] end, [], [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17]),\n"
    "    erlang:element(1, {a}), erlang:float(1), float(2), {E, M, X},\n"
    "    \"a very long string that will certainly not fit on one single line of seventy-two characters at all\",\n"
    "    [$a, $\\n, $\\\\, $', 1.5e-10, 16#ff, -0.0, 'Quoted Atom', [] | tail],\n"
    "    <<\"bin\", 1:4, X/binary, (A + 1):8/integer-signed-big>>,\n"
    "    [Q || Q <- [1, 2] && Q2 <- [3, 4]],\n"
    "    #{a := 1} = #{a => 1}.\n"
    "-record(empty, {}).\n"
    "-file(\"src/other.erl\", 12).\n".

options() ->
    [F | _] = Forms = forms(source()),
    G = lists:keyfind(g, 3, [X || X <- Forms, element(1, X) =:= function]),
    show("module latin1", erl_pp:form(F, [{encoding, latin1}])),
    show("narrow", erl_pp:form(G, [{linewidth, 40}])),
    show("indent 2", erl_pp:form(G, [{indent, 2}, {linewidth, 50}])),
    show("wide", erl_pp:form(G, [{linewidth, 200}])),
    Types = [X || {attribute, _, type, _} = X <- Forms],
    [show("quoted", erl_pp:form(T, [{quote_singleton_atom_types, true}])) || T <- Types],
    show("unicode string", erl_pp:expr({string, 1, [104, 233, 8364]}, [{encoding, unicode}])),
    show("latin1 string", erl_pp:expr({string, 1, [104, 233, 8364]}, [{encoding, latin1}])),
    show("hook default", erl_pp:expr({tuple, 1, [{atom, 1, a}, {mystery, 1}]}, [])),
    show("hook fun", erl_pp:expr({tuple, 1, [{atom, 1, a}, {mystery, 1}]}, fun ?MODULE:hook/4)),
    show(
        "hook fun list",
        erl_pp:expr({tuple, 1, [{mystery, 1}, {mystery, 2}]}, [{hook, fun ?MODULE:hook/4}])
    ),
    show("hook mfa", erl_pp:expr({tuple, 1, [{mystery, 1}]}, {?MODULE, ehook, [extra]})).

%% A hook printing the unknown form and where it was asked to.
hook(Expr, I, Prec, _Options) -> io_lib:format("<~w at ~w, ~w>", [Expr, I, Prec]).

ehook(Expr, I, Prec, {M, F, _}, Extra) ->
    io_lib:format("<~w at ~w, ~w by ~w:~w ~w>", [Expr, I, Prec, M, F, Extra]).

expressions() ->
    Es = exprs(
        "X = {a, [1, 2, 3], \"long string text here\", #{key => value, other => [x, y, z]}}, "
        "case X of {a, L, _, _} when length(L) > 2 -> lists:sum(L); _ -> 0 end, "
        "fun(A, B) -> A + B end."
    ),
    [show(io_lib:format("expr at ~w", [I]), erl_pp:expr(E, I, [])) || E <- Es, I <- [0, 4, 30, -1]],
    show("exprs", erl_pp:exprs(Es)),
    show("exprs 8", erl_pp:exprs(Es, 8, [])),
    show("exprs none", erl_pp:exprs(Es, none)),
    [Op] = exprs("(1 + 2) * 3 - (4 - 5) - 6 / (7 div 8)."),
    [show(io_lib:format("prec ~w", [P]), erl_pp:expr(Op, 0, P, [])) || P <- [0, 500, 700, 900]],
    show("expr/1", erl_pp:expr(Op)).

entry_points() ->
    [{function, _, _, _, [{clause, _, _, Guard, _}]} = Function] = forms(
        "f(X) when X > 1, is_atom(X); X =:= y -> X.\n"
    ),
    show("guard", erl_pp:guard(Guard)),
    show("guard opts", erl_pp:guard(Guard, [{linewidth, 10}])),
    show("guard old", erl_pp:guard([{atom, 1, true}])),
    show("guard empty", erl_pp:guard([])),
    show("function", erl_pp:function(Function)),
    show("function opts", erl_pp:function(Function, [{indent, 8}])),
    [Attribute | _] = forms("-export([a/1]).\n"),
    show("attribute", erl_pp:attribute(Attribute)),
    show("attribute opts", erl_pp:attribute(Attribute, [{encoding, utf8}])),
    show("error form", erl_pp:form({error, {1, erl_parse, "bad"}})),
    show("warning form", erl_pp:form({warning, {1, erl_lint, "warn"}})),
    show("eof form", erl_pp:form({eof, 3})),
    show("value", erl_pp:expr({value, 1, {term, [1, "s"]}})),
    show("fun info", erl_pp:expr({'fun', 1, {function, f, 1}, [{info, 1}]})),
    show(
        "fun info clauses",
        erl_pp:expr({'fun', 1, {clauses, [{clause, 1, [], [], [{atom, 1, ok}]}]}, extra})
    ),
    show("var integer", erl_pp:expr({var, 1, 7})),
    show("invalid", erl_pp:expr({oops, 1, 2})),
    show("module params", erl_pp:attribute({attribute, 1, module, {m, ['A', 'B']}})),
    show("package", erl_pp:attribute({attribute, 1, module, [a, b, c]})),
    show("optional", erl_pp:attribute({attribute, 1, optional_callbacks, [{f, 1}]})),
    show("optional bad", erl_pp:attribute({attribute, 1, optional_callbacks, bad})),
    Generated =
        {function, 1, g, 1, [
            {clause, 1, [{var, 1, rec0}, {var, 1, 'X'}], [], [
                {tuple, 1, [{var, 1, rec0}, {var, 1, 'REC0'}, {var, 1, rec1}]}
            ]}
        ]},
    show("legalize", erl_pp:function(erl_pp:legalize_vars(Generated))),
    io:format("legalize bad: ~p~n", [
        catch_error(fun() -> erl_pp:legalize_vars({attribute, 1, a, b}) end)
    ]).

catch_error(Fun) ->
    try Fun() of
        V -> {value, V}
    catch
        error:Reason -> {error, Reason}
    end.
