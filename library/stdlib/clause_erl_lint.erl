%% erl_lint's checks of expressions (plan 21 step LIB32, docs/library.md): what erl_eval needs while OTP's erl_lint is
%% not provided (LIB34). exprs/2 finds the first error erl_lint reports for a shell command (unbound and unsafe
%% variables, illegal patterns and guards, undefined records, stack trace variables, map and binary pattern rules),
%% used_vars/2 the variables each fun takes from its surroundings, and is_guard_test/1, is_guard_expr/1 are erl_lint's.
-module(clause_erl_lint).
-export([exprs/2, used_vars/2, is_guard_test/1, is_guard_expr/1]).

%% The scope of a used_vars/2 walk: the variables of the surroundings (outer), those bound inside the fun (local), the
%% outer ones used so far, and the {Used, Inner} tree of the funs met.
-record(uv, {outer = #{}, local = #{}, used = #{}, funs = #{}}).

%% exprs(Exprs, Bindings): ok, or {error, {Location, erl_lint, Error}} for the first error erl_lint:exprs_opt/3 reports.
exprs(Exprs, Bindings) ->
    Vt = maps:from_list([{V, bound} || {V, _} <- Bindings]),
    try body(Exprs, Vt) of
        _ -> ok
    catch
        throw:{lint, Anno, Error} -> {error, {erl_anno:location(Anno), erl_lint, Error}}
    end.

%% Variable table: each variable bound, unsafe ({unsafe, {Construct, Location}}) or a stack trace variable inside a
%% catch clause's guard (stacktrace).
body(Es, Vt) -> lists:foldl(fun expr/2, Vt, Es).

%% Variables of a list of expressions evaluated side by side: each sees the same table, their bindings join.
side_by_side(Es, Vt) ->
    lists:foldl(fun(E, Acc) -> maps:merge(Acc, new_vars(expr(E, Vt), Vt)) end, Vt, Es).

lint_error(Anno, Error) -> throw({lint, Anno, Error}).

new_vars(Vt1, Vt0) -> maps:without(maps:keys(Vt0), Vt1).

%% New variables of a construct marked unsafe after it.
unsafe(New, Construct, Anno) ->
    maps:map(fun(_, _) -> {unsafe, {Construct, erl_anno:location(Anno)}} end, New).

use(V, Anno, Vt) ->
    case Vt of
        #{V := bound} -> Vt;
        #{V := {unsafe, What}} -> lint_error(Anno, {unsafe_var, V, What});
        #{V := stacktrace} -> lint_error(Anno, {stacktrace_guard, V});
        #{} -> lint_error(Anno, {unbound_var, V})
    end.

expr({var, Anno, V}, Vt) ->
    use(V, Anno, Vt);
expr({Leaf, _, _}, Vt) when
    Leaf =:= char; Leaf =:= integer; Leaf =:= float; Leaf =:= atom; Leaf =:= string
->
    Vt;
expr({nil, _}, Vt) ->
    Vt;
expr({cons, _, H, T}, Vt) ->
    side_by_side([H, T], Vt);
expr({tuple, _, Es}, Vt) ->
    side_by_side(Es, Vt);
expr({map, _, Fields}, Vt) ->
    [lint_error(A, illegal_map_construction) || {map_field_exact, A, _, _} <- Fields],
    side_by_side(field_exprs(Fields), Vt);
expr({map, _, Src, Fields}, Vt) ->
    side_by_side([Src | field_exprs(Fields)], Vt);
expr({bin, _, Segments}, Vt) ->
    side_by_side(lists:append([[V | sizes(S)] || {bin_element, _, V, S, _} <- Segments]), Vt);
expr({block, _, Es}, Vt) ->
    body(Es, Vt);
expr({'if', Anno, Cs}, Vt) ->
    branches(Cs, Vt, {'if', Anno});
expr({'case', Anno, E, Cs}, Vt) ->
    branches(Cs, expr(E, Vt), {'case', Anno});
expr({'receive', Anno, Cs}, Vt) ->
    branches(Cs, Vt, {'receive', Anno});
expr({'receive', Anno, Cs, Timeout, After}, Vt) ->
    Vt1 = expr(Timeout, Vt),
    join([body(After, Vt1) | [clause(C, Vt) || C <- Cs]], Vt1, {'receive', Anno});
expr({'try', Anno, B, Cases, Catches, After}, Vt) ->
    try_expr(Anno, B, Cases, Catches, After, Vt);
expr({'catch', Anno, E}, Vt) ->
    maps:merge(Vt, unsafe(new_vars(expr(E, Vt), Vt), 'catch', Anno));
expr({match, _, P, E}, Vt) ->
    pattern(P, expr(E, Vt));
expr({maybe_match, _, P, E}, Vt) ->
    pattern(P, expr(E, Vt));
expr({'maybe', Anno, Es}, Vt) ->
    maps:merge(Vt, unsafe(new_vars(body(Es, Vt), Vt), 'maybe', Anno));
expr({'maybe', Anno, Es, {'else', _, Cs}}, Vt) ->
    Inside = unsafe(new_vars(body(Es, Vt), Vt), 'maybe', Anno),
    Else = [new_vars(clause(C, maps:merge(Vt, Inside)), Vt) || C <- Cs],
    lists:foldl(
        fun(New, Acc) -> maps:merge(Acc, unsafe(New, 'maybe', Anno)) end,
        maps:merge(Vt, Inside),
        Else
    );
expr({op, Anno, Op, L, R}, Vt) when Op =:= 'andalso'; Op =:= 'orelse' ->
    Vt1 = expr(L, Vt),
    maps:merge(Vt1, unsafe(new_vars(expr(R, Vt1), Vt1), Op, Anno));
expr({op, _, _, L, R}, Vt) ->
    side_by_side([L, R], Vt);
expr({op, _, _, E}, Vt) ->
    expr(E, Vt);
expr({'fun', _, {clauses, Cs}}, Vt) ->
    [fun_clause(C, Vt) || C <- Cs],
    Vt;
expr({'fun', _, {function, M, F, A}}, Vt) ->
    side_by_side([M, F, A], Vt);
expr({named_fun, _, Name, Cs}, Vt) ->
    [fun_clause(C, Vt#{Name => bound}) || C <- Cs],
    Vt;
expr({call, _, {remote, _, M, F}, As} = Call, Vt) ->
    Vt1 = side_by_side([M, F | As], Vt),
    record_test(Call),
    Vt1;
expr({call, _, F, As} = Call, Vt) ->
    Vt1 = side_by_side([F | As], Vt),
    record_test(Call),
    Vt1;
expr({Comprehension, _, Template, Qs}, Vt) when
    Comprehension =:= lc; Comprehension =:= bc; Comprehension =:= mc
->
    template(Template, qualifiers(Qs, Vt)),
    Vt;
expr(Record, Vt) when
    element(1, Record) =:= record;
    element(1, Record) =:= record_field;
    element(1, Record) =:= record_index
->
    record(Record, Vt);
expr(_, Vt) ->
    Vt.

field_exprs(Fields) -> lists:append([[K, V] || {_, _, K, V} <- Fields]).

sizes(default) -> [];
sizes(Size) -> [Size].

%% is_record(E, Name) names a record, which a command cannot define.
record_test({call, _, {atom, _, is_record}, [_, {atom, A, Name}]}) ->
    lint_error(A, {undefined_record, Name});
record_test({call, _, {remote, _, {atom, _, erlang}, {atom, _, is_record}}, [_, {atom, A, Name}]}) ->
    lint_error(A, {undefined_record, Name});
record_test(_) ->
    ok.

%% Tuple records are undefined in a command; native record forms ({Module, Name} or []) check their parts.
record({record, Anno, Name, _}, _Vt) when is_atom(Name) ->
    lint_error(Anno, {undefined_record, Name});
record({record, _, _, Fields}, Vt) ->
    side_by_side(record_values(Fields), Vt);
record({record, Anno, Src, Name, Fields}, Vt) ->
    Vt1 = expr(Src, Vt),
    is_atom(Name) andalso lint_error(Anno, {undefined_record, Name}),
    maps:merge(Vt1, new_vars(side_by_side(record_values(Fields), Vt), Vt));
record({record_field, Anno, Src, Name, _}, Vt) ->
    Vt1 = expr(Src, Vt),
    is_atom(Name) andalso lint_error(Anno, {undefined_record, Name}),
    Vt1;
record({record_index, Anno, Name, _}, _Vt) ->
    lint_error(Anno, {undefined_record, Name});
record(_, Vt) ->
    Vt.

record_values(Fields) -> [V || {record_field, _, _, V} <- Fields].

%% Clauses of case, if and receive: variables bound in every clause stay bound, the others become unsafe.
branches(Cs, Vt, What) ->
    join([clause(C, Vt) || C <- Cs], Vt, What).

join(Results, Vt, {Construct, Anno}) ->
    News = [new_vars(R, Vt) || R <- Results],
    All = lists:foldl(fun maps:merge/2, #{}, News),
    Everywhere = maps:filter(
        fun(V, _) -> lists:all(fun(New) -> is_map_key(V, New) end, News) end, All
    ),
    maps:merge(
        maps:merge(Vt, unsafe(All, Construct, Anno)), maps:map(fun(_, _) -> bound end, Everywhere)
    ).

clause({clause, _, Head, Guard, Body}, Vt) ->
    Vt1 = lists:foldl(fun pattern/2, Vt, Head),
    guard(Guard, Vt1),
    body(Body, Vt1).

%% A fun clause: head variables are new, shadowing the surroundings; nothing leaves the fun.
fun_clause({clause, _, Head, Guard, Body}, Vt) ->
    Vt1 = lists:foldl(fun fresh_pattern/2, Vt, Head),
    guard(Guard, Vt1),
    body(Body, Vt1).

%% try: the body's variables reach the success clauses only; everything bound inside is unsafe afterwards.
try_expr(Anno, B, Cases, Catches, After, Vt) ->
    VtBody = body(B, Vt),
    Inside = unsafe(new_vars(VtBody, Vt), 'try', Anno),
    Unsafe = maps:merge(Vt, Inside),
    Success = [new_vars(clause(C, VtBody), VtBody) || C <- Cases],
    Caught = [new_vars(catch_clause(C, Unsafe), Unsafe) || C <- Catches],
    Clauses = lists:foldl(
        fun(New, Acc) -> maps:merge(Acc, unsafe(New, 'try', Anno)) end, Unsafe, Success ++ Caught
    ),
    maps:merge(Clauses, unsafe(new_vars(body(After, Clauses), Clauses), 'try', Anno)).

%% Class:Reason:Stacktrace -> ...: the stack trace variable must be new and stays out of the guard.
catch_clause({clause, _, [{tuple, _, [Class, Reason, {var, SA, S}]}], Guard, Body}, Vt) when
    S =/= '_'
->
    is_map_key(S, Vt) andalso lint_error(SA, {stacktrace_bound, S}),
    Vt1 = pattern(Reason, pattern(Class, Vt)),
    guard(Guard, Vt1#{S => stacktrace}),
    body(Body, Vt1#{S => bound});
catch_clause(Clause, Vt) ->
    clause(Clause, Vt).

%% Each guard test uses only bound variables and is a guard test.
guard([G | _] = Disjunction, Vt) when is_list(G) ->
    [guard(Conjunction, Vt) || Conjunction <- Disjunction],
    ok;
guard(Tests, Vt) ->
    [guard_test(Test, Vt) || Test <- Tests],
    ok.

guard_test(Test, Vt) ->
    expr(Test, Vt),
    is_guard_test(Test) orelse lint_error(element(2, Test), illegal_guard_expr).

%% Qualifiers of a comprehension in order; generator patterns bind new variables.
qualifiers(Qs, Vt) -> lists:foldl(fun qualifier/2, Vt, Qs).

qualifier({zip, _, Generators}, Vt) ->
    lists:foldl(fun(G, Acc) -> maps:merge(Acc, new_vars(qualifier(G, Vt), Vt)) end, Vt, Generators);
qualifier({Generate, _, {map_field_exact, _, K, V}, E}, Vt) when
    Generate =:= m_generate; Generate =:= m_generate_strict
->
    fresh_pattern(V, fresh_pattern(K, expr(E, Vt)));
qualifier({Generate, _, P, E}, Vt) when
    Generate =:= generate;
    Generate =:= generate_strict;
    Generate =:= b_generate;
    Generate =:= b_generate_strict
->
    fresh_pattern(P, expr(E, Vt));
qualifier(Filter, Vt) ->
    expr(Filter, Vt).

template({map_field_assoc, _, K, V}, Vt) -> side_by_side([K, V], Vt);
template(Es, Vt) when is_list(Es) -> side_by_side(Es, Vt);
template(E, Vt) -> expr(E, Vt).

pattern(P, Vt) -> pattern(P, Vt, match).

fresh_pattern(P, Vt) -> pattern(P, Vt, fresh).

%% A pattern's variables: in match mode a bound variable is compared and an unsafe one an error; in fresh mode
%% (fun heads, generators) every variable is new.
pattern({var, _, '_'}, Vt, _Mode) ->
    Vt;
pattern({var, _, V}, Vt, fresh) ->
    Vt#{V => bound};
pattern({var, Anno, V}, Vt, match) ->
    case Vt of
        #{V := {unsafe, What}} -> lint_error(Anno, {unsafe_var, V, What});
        #{V := _} -> Vt;
        #{} -> Vt#{V => bound}
    end;
pattern({Leaf, _, _}, Vt, _Mode) when
    Leaf =:= char; Leaf =:= integer; Leaf =:= float; Leaf =:= atom; Leaf =:= string
->
    Vt;
pattern({nil, _}, Vt, _Mode) ->
    Vt;
pattern({cons, _, H, T}, Vt, Mode) ->
    pattern(T, pattern(H, Vt, Mode), Mode);
pattern({tuple, _, Ps}, Vt, Mode) ->
    lists:foldl(fun(P, Acc) -> pattern(P, Acc, Mode) end, Vt, Ps);
pattern({match, _, P1, P2}, Vt, Mode) ->
    pattern(P2, pattern(P1, Vt, Mode), Mode);
pattern({map, _, Fields}, Vt, Mode) ->
    lists:foldl(fun(Field, Acc) -> map_field_pattern(Field, Acc, Vt, Mode) end, Vt, Fields);
pattern({bin, _, Segments}, Vt, Mode) ->
    bin_pattern(Segments, Vt, Mode);
pattern({op, _, '++', L, R}, Vt, Mode) ->
    is_literal_list(L) orelse lint_error(element(2, L), illegal_pattern),
    pattern(R, Vt, Mode);
pattern({op, Anno, _, _} = Op, Vt, _Mode) ->
    constant_pattern(Op, Anno, Vt);
pattern({op, Anno, _, _, _} = Op, Vt, _Mode) ->
    constant_pattern(Op, Anno, Vt);
pattern({record, Anno, Name, _}, _Vt, _Mode) when is_atom(Name) ->
    lint_error(Anno, {undefined_record, Name});
pattern({record, _, _, Fields}, Vt, Mode) ->
    lists:foldl(fun(P, Acc) -> pattern(P, Acc, Mode) end, Vt, record_values(Fields));
pattern(P, _Vt, _Mode) ->
    lint_error(element(2, P), illegal_pattern).

%% Keys of map patterns are expressions over the variables bound before the pattern.
map_field_pattern({map_field_exact, _, K, V}, Acc, Vt, Mode) ->
    expr(K, Vt),
    pattern(V, Acc, Mode);
map_field_pattern({map_field_assoc, A, _, _}, _Acc, _Vt, _Mode) ->
    lint_error(A, illegal_map_assoc_in_pattern).

%% Segment sizes use variables bound before them; only the last segment may be a binary of no size.
bin_pattern([{bin_element, A, V, Size, Types} | Rest], Vt, Mode) ->
    Vt1 = pattern(V, Vt, Mode),
    Size =:= default orelse expr(Size, Vt1),
    Rest =/= [] andalso Size =:= default andalso is_binary_type(Types) andalso
        lint_error(A, unsized_binary_not_at_end),
    bin_pattern(Rest, Vt1, Mode);
bin_pattern([], Vt, _Mode) ->
    Vt.

is_binary_type(Types) when is_list(Types) ->
    lists:any(fun(T) -> lists:member(T, [binary, bytes, bitstring, bits]) end, Types);
is_binary_type(_) ->
    false.

is_literal_list({nil, _}) ->
    true;
is_literal_list({string, _, _}) ->
    true;
is_literal_list({cons, _, {Leaf, _, _}, T}) when Leaf =:= char; Leaf =:= integer ->
    is_literal_list(T);
is_literal_list(_) ->
    false.

%% erl_lint's is_pattern_expr/1: arithmetic over numbers that erl_eval:partial_eval/1 folds to a number.
constant_pattern(Op, Anno, Vt) ->
    Folds =
        is_arith(Op) andalso
            case erl_eval:partial_eval(Op) of
                {Leaf, _, _} when Leaf =:= integer; Leaf =:= char; Leaf =:= float; Leaf =:= atom ->
                    true;
                _ ->
                    false
            end,
    Folds orelse lint_error(Anno, illegal_pattern),
    Vt.

is_arith({Leaf, _, _}) when Leaf =:= char; Leaf =:= integer; Leaf =:= float; Leaf =:= atom -> true;
is_arith({op, _, Op, A}) ->
    erl_internal:arith_op(Op, 1) andalso is_arith(A);
is_arith({op, _, Op, A1, A2}) ->
    erl_internal:arith_op(Op, 2) andalso is_arith(A1) andalso is_arith(A2);
is_arith(_) ->
    false.

%% used_vars(Exprs, Bindings): #{Clauses => {Used, Inner}} for each fun of Exprs, Used being the sorted variables of
%% Bindings it uses and Inner the same tree for the funs inside it (erl_lint:used_vars/2).
used_vars(Exprs, Bindings) ->
    Outer = maps:from_list([{V, true} || {V, _} <- Bindings]),
    (walk(Exprs, #uv{outer = Outer}))#uv.funs.

%% A variable is local when bound inside the fun, used when it comes from the surroundings, else bound here.
walk({var, _, '_'}, S) ->
    S;
walk({var, _, V}, #uv{outer = Outer, local = Local, used = Used} = S) ->
    if
        is_map_key(V, Local) -> S;
        is_map_key(V, Outer) -> S#uv{used = Used#{V => true}};
        true -> S#uv{local = Local#{V => true}}
    end;
walk({'fun', _, {clauses, Cs}}, S) ->
    fun_met(Cs, S);
walk({named_fun, _, _, Cs}, S) ->
    fun_met(Cs, S);
walk({Comprehension, _, Template, Qs}, S) when
    Comprehension =:= lc; Comprehension =:= bc; Comprehension =:= mc
->
    #uv{used = Used, funs = Funs} = walk(Template, lists:foldl(fun walk_qualifier/2, S, Qs)),
    S#uv{used = Used, funs = Funs};
walk(T, S) when is_tuple(T) ->
    walk(tuple_to_list(T), S);
walk([H | T], S) ->
    walk(T, walk(H, S));
walk(_, S) ->
    S.

%% A generator's pattern variables are new inside the comprehension.
walk_qualifier({Generate, _, P, E}, S) when
    Generate =:= generate;
    Generate =:= generate_strict;
    Generate =:= b_generate;
    Generate =:= b_generate_strict;
    Generate =:= m_generate;
    Generate =:= m_generate_strict
->
    #uv{outer = Outer, local = Local} = S1 = walk(E, S),
    Bound = bound_vars(P),
    walk(P, S1#uv{outer = maps:without(maps:keys(Bound), Outer), local = maps:merge(Local, Bound)});
walk_qualifier({zip, _, Generators}, S) ->
    lists:foldl(fun walk_qualifier/2, S, Generators);
walk_qualifier(Filter, S) ->
    walk(Filter, S).

%% A fun inside the walk: its own tree, and the variables it uses from beyond the enclosing fun.
fun_met(Cs, #uv{outer = Outer, local = Local, used = Used, funs = Funs} = S) ->
    {Inner, InnerFuns} = fun_used(Cs, maps:merge(Outer, Local)),
    Beyond = [V || V <- Inner, is_map_key(V, Outer), not is_map_key(V, Local)],
    S#uv{
        used = maps:merge(Used, maps:from_keys(Beyond, true)),
        funs = Funs#{Cs => {Inner, InnerFuns}}
    }.

fun_used(Cs, Enclosing) ->
    Scopes = [fun_clause_used(C, Enclosing) || C <- Cs],
    Used = lists:foldl(fun(#uv{used = U}, Acc) -> maps:merge(Acc, U) end, #{}, Scopes),
    Funs = lists:foldl(fun(#uv{funs = F}, Acc) -> maps:merge(Acc, F) end, #{}, Scopes),
    {lists:sort(maps:keys(Used)), Funs}.

fun_clause_used({clause, _, Head, Guard, Body}, Enclosing) ->
    Bound = bound_vars(Head),
    S = #uv{outer = maps:without(maps:keys(Bound), Enclosing), local = Bound},
    walk(Body, walk(Guard, walk(Head, S))).

%% The variables a pattern binds: map keys and segment sizes are uses, not bindings.
bound_vars(P) -> bound_vars(P, #{}).

bound_vars({var, _, '_'}, Acc) -> Acc;
bound_vars({var, _, V}, Acc) -> Acc#{V => true};
bound_vars({map_field_exact, _, _K, V}, Acc) -> bound_vars(V, Acc);
bound_vars({bin_element, _, V, _Size, _}, Acc) -> bound_vars(V, Acc);
bound_vars(T, Acc) when is_tuple(T) -> bound_vars(tuple_to_list(T), Acc);
bound_vars([H | T], Acc) -> bound_vars(T, bound_vars(H, Acc));
bound_vars(_, Acc) -> Acc.

%% is_guard_test(Expr): a legal guard test (erl_lint:is_guard_test/1, no record definitions).
is_guard_test({call, A, {atom, Ar, record}, [E, R]}) ->
    is_guard_expr({call, A, {atom, Ar, is_record}, [E, R]});
is_guard_test({call, A, {remote, _, {atom, _, erlang}, {atom, Af, is_record}}, [E, R]}) ->
    is_guard_test({call, A, {atom, Af, is_record}, [E, R]});
is_guard_test({call, _, {atom, _, Test}, As} = Call) ->
    case erl_internal:type_test(Test, length(As)) of
        true when Test =:= is_record, length(As) =:= 2 ->
            case As of
                [_, {atom, _, _}] -> all_guard_exprs(As);
                _ -> false
            end;
        true ->
            all_guard_exprs(As);
        false ->
            is_guard_expr(Call)
    end;
is_guard_test(G) ->
    is_guard_expr(G).

%% is_guard_expr(Expr): an expression guards may contain.
is_guard_expr({Leaf, _, _}) when
    Leaf =:= var; Leaf =:= char; Leaf =:= integer; Leaf =:= float; Leaf =:= atom; Leaf =:= string
->
    true;
is_guard_expr({nil, _}) ->
    true;
is_guard_expr({cons, _, H, T}) ->
    all_guard_exprs([H, T]);
is_guard_expr({tuple, _, Es}) ->
    all_guard_exprs(Es);
is_guard_expr({map, _, Fields}) ->
    guard_map_fields(Fields);
is_guard_expr({map, _, Src, Fields}) ->
    is_guard_expr(Src) andalso guard_map_fields(Fields);
is_guard_expr({record_index, _, _, Field}) ->
    is_guard_expr(Field);
is_guard_expr({record_field, _, Rec, _, Field}) ->
    all_guard_exprs([Rec, Field]);
is_guard_expr({record, _, _, Inits}) ->
    lists:all(
        fun
            ({record_field, _, _, V}) -> is_guard_expr(V);
            (_) -> false
        end,
        Inits
    );
is_guard_expr({bin, _, Segments}) ->
    lists:all(
        fun({bin_element, _, E, Size, _}) ->
            is_guard_expr(E) andalso (Size =:= default orelse is_guard_expr(Size))
        end,
        Segments
    );
is_guard_expr({call, _, {atom, _, F}, As}) ->
    erl_internal:guard_bif(F, length(As)) andalso all_guard_exprs(As);
is_guard_expr({call, _, {remote, _, {atom, _, erlang}, {atom, _, F}}, As}) ->
    (erl_internal:guard_bif(F, length(As)) orelse guard_op(F, length(As))) andalso
        all_guard_exprs(As);
is_guard_expr({call, A, {tuple, At, [{atom, Am, erlang}, {atom, Af, F}]}, As}) ->
    is_guard_expr({call, A, {remote, At, {atom, Am, erlang}, {atom, Af, F}}, As});
is_guard_expr({op, _, Op, A}) ->
    guard_op(Op, 1) andalso is_guard_expr(A);
is_guard_expr({op, _, Op, A1, A2}) when Op =:= 'andalso'; Op =:= 'orelse' ->
    all_guard_exprs([A1, A2]);
is_guard_expr({op, _, Op, A1, A2}) ->
    guard_op(Op, 2) andalso all_guard_exprs([A1, A2]);
is_guard_expr(_) ->
    false.

all_guard_exprs(Es) -> lists:all(fun is_guard_expr/1, Es).

guard_map_fields([{Tag, _, K, V} | Fs]) when Tag =:= map_field_assoc; Tag =:= map_field_exact ->
    is_guard_expr(K) andalso is_guard_expr(V) andalso guard_map_fields(Fs);
guard_map_fields([]) ->
    true;
guard_map_fields(_) ->
    false.

%% Arithmetic, boolean and comparison operators are allowed in guards; list and send operators are not.
guard_op(Op, Arity) ->
    try erl_internal:op_type(Op, Arity) of
        Type -> Type =:= arith orelse Type =:= bool orelse Type =:= comp
    catch
        error:_ -> false
    end.
