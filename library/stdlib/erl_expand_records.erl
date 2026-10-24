%% Clause's erl_expand_records (plan 21 step LIB34, docs/library.md): module/2 written from OTP's documentation and
%% observed output. Tuple records become tuple operations: constructions tuples with the defaults filled in, patterns
%% tuple patterns, field access a case (or element/2 behind an is_record/3 test in guards), updates setelement/3
%% chains (or a case rebuilding the tuple with the dialyzer option), is_record/2 is_record/3, record_info/2 its value.
%% Native records stay, imported names gaining their module. Calls of imported functions and of BIFs get their
%% module. New variables are named rec0, rec1, ... in the order OTP makes them, and is_record/3 tests of a clause's
%% guard become patterns when the guard cannot hold without them.
-module(erl_expand_records).
-export([module/2]).

%% The state of one module's expansion: the variable counter, how each local name F/A is called (local or
%% {imported, M}), the native records (a local definition or {imported, M}), the tuple record definitions with
%% normalised fields and as written, the dialyzer, expand_inits and strict_record_tests options, the guard tests
%% still to add before a guard ends and the tests added already, the module name and whether a guard is expanded.
-record(st, {
    vcount = 0,
    calls = #{},
    natives = #{},
    records = #{},
    raw_records = [],
    dialyzer = false,
    expand_inits = false,
    strict = true,
    check_needed = [],
    checked = [],
    module = '',
    in_guard = false
}).

module(Forms, Options0) ->
    Options = Options0 ++ lists:flatten([C || {attribute, _, compile, C} <- Forms]),
    St = #st{
        dialyzer = lists:member(dialyzer, Options),
        expand_inits = lists:member(expand_inits, Options),
        calls = call_types(Forms),
        natives = native_records(Forms),
        strict = strict_tests(Options, true)
    },
    {Expanded, _} = forms(Forms, St),
    Expanded.

call_types(Forms) ->
    Local = #{{Name, Arity} => local || {function, _, Name, Arity, _} <- Forms},
    lists:foldl(
        fun
            ({attribute, _, import, {Mod, Fs}}, Acc) when is_atom(Mod) ->
                lists:foldl(fun(FA, A) -> A#{FA => {imported, Mod}} end, Acc, Fs);
            (_, Acc) ->
                Acc
        end,
        Local,
        Forms
    ).

native_records(Forms) ->
    Local = #{Name => {local, Inits} || {attribute, _, native_record, {Name, Inits}} <- Forms},
    lists:foldl(
        fun
            ({attribute, _, import_record, {Mod, Names}}, Acc) when is_atom(Mod) ->
                lists:foldl(fun(N, A) -> A#{N => {imported, Mod}} end, Acc, Names);
            (_, Acc) ->
                Acc
        end,
        Local,
        Forms
    ).

strict_tests([strict_record_tests | Os], _) -> strict_tests(Os, true);
strict_tests([no_strict_record_tests | Os], _) -> strict_tests(Os, false);
strict_tests([_ | Os], Strict) -> strict_tests(Os, Strict);
strict_tests([], Strict) -> Strict.

forms([{attribute, _, record, {Name, Defs}} = Attr | Fs], St0) ->
    St = St0#st{
        records = (St0#st.records)#{Name => normalise_fields(Defs)},
        raw_records = [Attr | St0#st.raw_records]
    },
    {Rest, St1} = forms(Fs, St),
    {[Attr | Rest], St1};
forms([{function, Anno, N, A, Cs0} | Fs], St0) ->
    {Cs, St1} = clauses(Cs0, St0),
    {Rest, St2} = forms(Fs, St1),
    {[{function, Anno, N, A, Cs} | Rest], St2};
forms([{attribute, _, module, M} = Attr | Fs], St0) ->
    {Rest, St1} = forms(Fs, St0#st{module = M}),
    {[Attr | Rest], St1};
forms([F | Fs], St0) ->
    {Rest, St1} = forms(Fs, St0),
    {[F | Rest], St1};
forms([], St) ->
    {[], St}.

clauses([{clause, Anno, H0, G0, B0} | Cs0], St0) ->
    {H1, St1} = patterns(H0, St0),
    {G1, St2} = guard(G0, St1),
    {H, G} = record_tests_as_patterns(H1, G1, St2),
    {B, St3} = exprs(B0, St2),
    {Cs, St4} = clauses(Cs0, St3),
    {[{clause, Anno, H, G, B} | Cs], St4};
clauses([], St) ->
    {[], St}.

%% Patterns

pattern({Leaf, _, _} = P, St) when
    Leaf =:= var; Leaf =:= char; Leaf =:= integer; Leaf =:= float; Leaf =:= atom; Leaf =:= string
->
    {P, St};
pattern({nil, _} = P, St) ->
    {P, St};
pattern({cons, Anno, H0, T0}, St0) ->
    {H, St1} = pattern(H0, St0),
    {T, St2} = pattern(T0, St1),
    {{cons, Anno, H, T}, St2};
pattern({tuple, Anno, Ps0}, St0) ->
    {Ps, St1} = patterns(Ps0, St0),
    {{tuple, Anno, Ps}, St1};
pattern({map, Anno, Ps0}, St0) ->
    {Ps, St1} = patterns(Ps0, St0),
    {{map, Anno, Ps}, St1};
pattern({map_field_exact, Anno, K0, V0}, St0) ->
    {K, St1} = expr(K0, St0),
    {V, St2} = pattern(V0, St1),
    {{map_field_exact, Anno, K, V}, St2};
pattern({record_field, Anno, F, V0}, St0) ->
    {V, St1} = pattern(V0, St0),
    {{record_field, Anno, F, V}, St1};
pattern({record_index, Anno, Name, Field}, St) ->
    {index_expr(Anno, Field, record_fields(Name, Anno, St)), St};
pattern({record, Anno0, Name, Pfs}, St0) when is_atom(Name) ->
    case St0#st.natives of
        #{Name := {local, _}} ->
            {Ps, St1} = patterns(Pfs, St0),
            {{record, Anno0, Name, Ps}, St1};
        #{Name := {imported, M}} ->
            pattern({record, Anno0, {M, Name}, Pfs}, St0);
        #{} ->
            Fields = record_fields(Name, Anno0, St0),
            {Ps, St1} = patterns(field_patterns(Fields, Pfs), St0),
            {{tuple, mark_record(Anno0, St1), [{atom, Anno0, Name} | Ps]}, St1}
    end;
pattern({record, Anno, Id, Ps0}, St0) ->
    %% A native record: [] (any record) or {Module, Name}.
    {Ps, St1} = patterns(Ps0, St0),
    {{record, Anno, Id, Ps}, St1};
pattern({bin, Anno, Es0}, St0) ->
    {Es, St1} = lists:foldr(fun pattern_segment/2, {[], St0}, Es0),
    {{bin, Anno, Es}, St1};
pattern({match, Anno, P1, P2}, St0) ->
    {Right, St1} = pattern(P2, St0),
    {Left, St2} = pattern(P1, St1),
    {{match, Anno, Left, Right}, St2};
pattern({op, Anno, Op, A0}, St0) ->
    {A, St1} = pattern(A0, St0),
    {{op, Anno, Op, A}, St1};
pattern({op, Anno, Op, L0, R0}, St0) ->
    {L, St1} = pattern(L0, St0),
    {R, St2} = pattern(R0, St1),
    {{op, Anno, Op, L, R}, St2}.

patterns([P0 | Ps0], St0) ->
    {P, St1} = pattern(P0, St0),
    {Ps, St2} = patterns(Ps0, St1),
    {[P | Ps], St2};
patterns([], St) ->
    {[], St}.

pattern_segment({bin_element, Anno, E0, Size0, Type}, {Es, St0}) ->
    {E, St1} = pattern(E0, St0),
    {Size, St2} =
        case Size0 of
            default -> {default, St1};
            _ -> expr(Size0, St1)
        end,
    {[{bin_element, Anno, E, Size, Type} | Es], St2}.

%% Guards

guard([G0 | Gs0], St0) ->
    {G, St1} = guard_tests(G0, St0),
    {Gs, St2} = guard(Gs0, St1),
    {[G | Gs], St2};
guard([], St) ->
    {[], St}.

guard_tests(Tests0, St0) ->
    {Tests, St1} = guard_test_list(Tests0, St0),
    {Tests, St1#st{checked = []}}.

guard_test_list([T0 | Ts0], St0) ->
    {T, St1} = guard_test(T0, St0),
    {Ts, St2} = guard_test_list(Ts0, St1),
    {[T | Ts], St2};
guard_test_list([], St) ->
    {[], St}.

%% One guard test expanded in guard context, the record tests its field accesses need joined to it. Old type tests
%% (atom(X), ...) become their is_ forms first.
guard_test(G0, St0) ->
    Outer = St0#st.in_guard,
    {G1, St1} = guard_test_expr(G0, St0#st{in_guard = true}),
    {G, St2} = strict_record_access(G1, St1),
    {G, St2#st{in_guard = Outer}}.

guard_test_expr({call, Anno, {atom, TA, Name}, As}, St) ->
    expr({call, Anno, {atom, TA, normalise_test(Name, length(As))}, As}, St);
guard_test_expr(Test, St) ->
    expr(Test, St).

normalise_test(atom, 1) -> is_atom;
normalise_test(binary, 1) -> is_binary;
normalise_test(float, 1) -> is_float;
normalise_test(function, 1) -> is_function;
normalise_test(integer, 1) -> is_integer;
normalise_test(list, 1) -> is_list;
normalise_test(number, 1) -> is_number;
normalise_test(pid, 1) -> is_pid;
normalise_test(port, 1) -> is_port;
normalise_test(record, 2) -> is_record;
normalise_test(reference, 1) -> is_reference;
normalise_test(tuple, 1) -> is_tuple;
normalise_test(Name, _) -> Name.

%% Record tests

%% is_record(Term, Name): of a native record the module test, of a tuple record is_record/3 (on a new variable
%% bound to Term outside guards, as Term may have effects).
record_test(Anno, Term, Name, St) ->
    Generated = generated(Anno),
    IsRecord = {remote, Generated, {atom, Generated, erlang}, {atom, Generated, is_record}},
    case St#st.natives of
        #{Name := Native} ->
            Mod =
                case Native of
                    {local, _} -> St#st.module;
                    {imported, M} -> M
                end,
            expr({call, Generated, IsRecord, [Term, {atom, Anno, Mod}, {atom, Anno, Name}]}, St);
        #{} when St#st.in_guard ->
            case not_a_tuple(Term) of
                true ->
                    expr({atom, Anno, false}, St);
                false ->
                    Size = length(record_fields(Name, Anno, St)) + 1,
                    expr(
                        {call, Generated, IsRecord, [
                            Term, {atom, Anno, Name}, {integer, Anno, Size}
                        ]},
                        St
                    )
            end;
        #{} ->
            Size = length(record_fields(Name, Anno, St)) + 1,
            {Var, St1} = new_var(Anno, St),
            Test = {call, Generated, IsRecord, [Var, {atom, Anno, Name}, {integer, Anno, Size}]},
            expr({block, Anno, [{match, Anno, Var, Term}, Test]}, St1)
    end.

not_a_tuple({Leaf, _, _}) when
    Leaf =:= atom; Leaf =:= integer; Leaf =:= float; Leaf =:= char; Leaf =:= string; Leaf =:= bin
->
    true;
not_a_tuple({nil, _}) ->
    true;
not_a_tuple({cons, _, _, _}) ->
    true;
not_a_tuple({record_index, _, _, _}) ->
    true;
not_a_tuple({op, _, _, _}) ->
    true;
not_a_tuple({op, _, _, _, _}) ->
    true;
not_a_tuple(_) ->
    false.

%% Expressions

exprs([E0 | Es0], St0) ->
    {E, St1} = expr(E0, St0),
    {Es, St2} = exprs(Es0, St1),
    {[E | Es], St2};
exprs([], St) ->
    {[], St}.

expr_or_exprs(Es, St) when is_list(Es) -> exprs(Es, St);
expr_or_exprs(E, St) -> expr(E, St).

expr({Leaf, _, _} = E, St) when
    Leaf =:= var; Leaf =:= char; Leaf =:= integer; Leaf =:= float; Leaf =:= atom; Leaf =:= string
->
    {E, St};
expr({nil, _} = E, St) ->
    {E, St};
expr({cons, Anno, H0, T0}, St0) ->
    {H, St1} = expr(H0, St0),
    {T, St2} = expr(T0, St1),
    {{cons, Anno, H, T}, St2};
expr({Comprehension, Anno, E0, Qs0}, St0) when
    Comprehension =:= lc; Comprehension =:= bc; Comprehension =:= mc
->
    {Qs, St1} = qualifiers(Qs0, St0),
    {E, St2} =
        case Comprehension of
            bc -> expr(E0, St1);
            _ -> expr_or_exprs(E0, St1)
        end,
    {{Comprehension, Anno, E, Qs}, St2};
expr({tuple, Anno, Es0}, St0) ->
    {Es, St1} = exprs(Es0, St0),
    {{tuple, Anno, Es}, St1};
expr({map, Anno, Es0}, St0) ->
    {Es, St1} = exprs(Es0, St0),
    {{map, Anno, Es}, St1};
expr({map, Anno, Arg0, Es0}, St0) ->
    {Arg, St1} = expr(Arg0, St0),
    {Es, St2} = exprs(Es0, St1),
    {{map, Anno, Arg, Es}, St2};
expr({Field, Anno, K0, V0}, St0) when Field =:= map_field_assoc; Field =:= map_field_exact ->
    {K, St1} = expr(K0, St0),
    {V, St2} = expr(V0, St1),
    {{Field, Anno, K, V}, St2};
expr({record_index, Anno, Name, F}, St) ->
    expr(index_expr(Anno, F, record_fields(Name, Anno, St)), St);
expr({record, _, _, _} = Record, St) ->
    record_construction(Record, St);
expr({record, _, _, _, _} = Record, St) ->
    record_update_expr(Record, St);
expr({record_field, _, _, _, _} = Access, St) ->
    record_access(Access, St);
expr({record_field, Anno, K, E0}, St0) ->
    {E, St1} = expr(E0, St0),
    {{record_field, Anno, K, E}, St1};
expr({bin, Anno, Es0}, St0) ->
    {Es, St1} = lists:foldr(fun expr_segment/2, {[], St0}, Es0),
    {{bin, Anno, Es}, St1};
expr({block, Anno, Es0}, St0) ->
    {Es, St1} = exprs(Es0, St0),
    {{block, Anno, Es}, St1};
expr({'if', Anno, Cs0}, St0) ->
    {Cs, St1} = clauses(Cs0, St0),
    {{'if', Anno, Cs}, St1};
expr({'case', Anno, E0, Cs0}, St0) ->
    {E, St1} = expr(E0, St0),
    {Cs, St2} = clauses(Cs0, St1),
    {{'case', Anno, E, Cs}, St2};
expr({'receive', Anno, Cs0}, St0) ->
    {Cs, St1} = clauses(Cs0, St0),
    {{'receive', Anno, Cs}, St1};
expr({'receive', Anno, Cs0, To0, ToEs0}, St0) ->
    {To, St1} = expr(To0, St0),
    {ToEs, St2} = exprs(ToEs0, St1),
    {Cs, St3} = clauses(Cs0, St2),
    {{'receive', Anno, Cs, To, ToEs}, St3};
expr({'fun', Anno, {function, F, A}} = Fun, St) ->
    local_fun(Anno, F, A, Fun, St);
expr({'fun', _, {function, _, _, _}} = Fun, St) ->
    {Fun, St};
expr({'fun', Anno, {clauses, Cs0}}, St0) ->
    {Cs, St1} = outside_guard(fun(S) -> clauses(Cs0, S) end, St0),
    {{'fun', Anno, {clauses, Cs}}, St1};
expr({named_fun, Anno, Name, Cs0}, St0) ->
    {Cs, St1} = outside_guard(fun(S) -> clauses(Cs0, S) end, St0),
    {{named_fun, Anno, Name, Cs}, St1};
expr({call, _, _, _} = Call, St) ->
    call(Call, St);
expr({'try', Anno, Es0, Scs0, Ccs0, As0}, St0) ->
    {Es, St1} = exprs(Es0, St0),
    {Scs, St2} = clauses(Scs0, St1),
    {Ccs, St3} = clauses(Ccs0, St2),
    {As, St4} = exprs(As0, St3),
    {{'try', Anno, Es, Scs, Ccs, As}, St4};
expr({'catch', Anno, E0}, St0) ->
    {E, St1} = expr(E0, St0),
    {{'catch', Anno, E}, St1};
expr({'maybe', Anno, Es0}, St0) ->
    {Es, St1} = exprs(Es0, St0),
    {{'maybe', Anno, Es}, St1};
expr({'maybe', Anno, Es0, {'else', ElseAnno, Cs0}}, St0) ->
    {Es, St1} = exprs(Es0, St0),
    {Cs, St2} = clauses(Cs0, St1),
    {{'maybe', Anno, Es, {'else', ElseAnno, Cs}}, St2};
expr({Match, Anno, P0, E0}, St0) when Match =:= match; Match =:= maybe_match ->
    {E, St1} = expr(E0, St0),
    {P, St2} = pattern(P0, St1),
    {{Match, Anno, P, E}, St2};
expr({op, Anno, 'not', A0}, St0) ->
    {A, St1} = bool_operand(A0, St0),
    {{op, Anno, 'not', A}, St1};
expr({op, Anno, Op, A0}, St0) ->
    {A, St1} = expr(A0, St0),
    {{op, Anno, Op, A}, St1};
expr({op, Anno, Op, L0, R0}, St0) when Op =:= 'and'; Op =:= 'or' ->
    {L, St1} = bool_operand(L0, St0),
    {R, St2} = bool_operand(R0, St1),
    {{op, Anno, Op, L, R}, St2};
expr({op, Anno, Op, L0, R0}, St0) when Op =:= 'andalso'; Op =:= 'orelse' ->
    %% Tests added for the right operand do not count as made after it.
    {L, St1} = bool_operand(L0, St0),
    {R, St2} = bool_operand(R0, St1),
    {{op, Anno, Op, L, R}, St2#st{checked = St1#st.checked}};
expr({op, Anno, Op, L0, R0}, St0) ->
    {L, St1} = expr(L0, St0),
    {R, St2} = expr(R0, St1),
    {{op, Anno, Op, L, R}, St2};
expr({Line, _, _} = E, St) when Line =:= executable_line; Line =:= debug_line ->
    {E, St};
expr({ssa_check_when, _, _, _, _, _, _} = E, St) ->
    {E, St}.

%% A body expanded outside guard context (a fun inside a guard).
outside_guard(F, St0) ->
    Outer = St0#st.in_guard,
    {Result, St1} = F(St0#st{in_guard = false}),
    {Result, St1#st{in_guard = Outer}}.

expr_segment({bin_element, Anno, E0, Size0, Type}, {Es, St0}) ->
    {E, St1} = expr(E0, St0),
    {Size, St2} =
        case Size0 of
            default -> {default, St1};
            _ -> expr(Size0, St1)
        end,
    {[{bin_element, Anno, E, Size, Type} | Es], St2}.

%% fun F/A: kept for a local function, M:F/A for an imported one or a BIF, else a fun calling F.
local_fun(Anno, F, A, Fun, St0) ->
    case St0#st.calls of
        #{{F, A} := local} ->
            {Fun, St0};
        #{{F, A} := {imported, M}} ->
            {{'fun', Anno, {function, {atom, Anno, M}, {atom, Anno, F}, {integer, Anno, A}}}, St0};
        #{} ->
            case erl_internal:bif(F, A) of
                true ->
                    {
                        {'fun', Anno,
                            {function, {atom, Anno, erlang}, {atom, Anno, F}, {integer, Anno, A}}},
                        St0
                    };
                false ->
                    {Vars, St1} = new_vars(A, Anno, St0),
                    expr(
                        {'fun', Anno,
                            {clauses, [
                                {clause, Anno, Vars, [], [{call, Anno, {atom, Anno, F}, Vars}]}
                            ]}},
                        St1
                    )
            end
    end.

call({call, Anno, {atom, _, is_record}, [A, {atom, _, Name}]}, St) ->
    record_test(Anno, A, Name, St);
call({call, Anno, {remote, _, {atom, _, erlang}, {atom, _, is_record}}, [A, {atom, _, Name}]}, St) ->
    record_test(Anno, A, Name, St);
call({call, Anno, {tuple, _, [{atom, _, erlang}, {atom, _, is_record}]}, [A, {atom, _, Name}]}, St) ->
    record_test(Anno, A, Name, St);
call({call, Anno, {atom, _, is_record}, [_, _, {integer, _, Size}]}, St) when
    is_integer(Size), Size =< 0
->
    {{atom, Anno, false}, St};
call(
    {call, Anno, {remote, _, {atom, _, erlang}, {atom, _, is_record}}, [_, _, {integer, _, Size}]},
    St
) when
    is_integer(Size), Size =< 0
->
    {{atom, Anno, false}, St};
call({call, Anno, {atom, _, record_info}, [_, _] = As0}, St0) ->
    {As, St1} = exprs(As0, St0),
    record_info(Anno, As, St1);
call({call, Anno, {atom, _, N} = Atom, As0}, St0) ->
    {As, St1} = exprs(As0, St0),
    Arity = length(As),
    case St0#st.calls of
        #{{N, Arity} := local} ->
            {{call, Anno, Atom, As}, St1};
        #{{N, Arity} := {imported, M}} ->
            {{call, Anno, {remote, Anno, {atom, Anno, M}, Atom}, As}, St1};
        #{} ->
            case erl_internal:bif(N, Arity) of
                true -> {{call, Anno, {remote, Anno, {atom, Anno, erlang}, Atom}, As}, St1};
                false -> {{call, Anno, Atom, As}, St1}
            end
    end;
call({call, Anno, {remote, AnnoR, M0, F0}, As0}, St0) ->
    {[M, F | As], St1} = exprs([M0, F0 | As0], St0),
    {{call, Anno, {remote, AnnoR, M, F}, As}, St1};
call({call, Anno, F0, As0}, St0) ->
    {[F | As], St1} = exprs([F0 | As0], St0),
    {{call, Anno, F, As}, St1}.

%% An operand of a boolean operator with the record tests its field accesses need.
bool_operand(E0, St0) ->
    {E, St1} = expr(E0, St0),
    strict_record_access(E, St1).

%% The record tests needed and not made yet, joined to E as (Test orelse fail) and ... and E.
strict_record_access(E, #st{check_needed = []} = St) ->
    {E, St};
strict_record_access(E0, #st{check_needed = Needed, checked = Checked0} = St0) ->
    {New, Checked} = lists:foldl(
        fun({Key, _, _} = Test, {Acc, Made}) ->
            case lists:keymember(Key, 1, Made) of
                true -> {Acc, Made};
                false -> {[Test | Acc], [Test | Made]}
            end
        end,
        {[], Checked0},
        Needed
    ),
    E =
        case New of
            [] -> E0;
            _ -> conjunction(New, E0)
        end,
    expr(E, St0#st{check_needed = [], checked = Checked}).

conjunction([], _E) ->
    empty;
conjunction([{_Key, Anno, Test} | Tests], E) ->
    Failing = {op, Anno, 'orelse', Test, {atom, Anno, fail}},
    Joined =
        case conjunction(Tests, none) of
            empty -> Failing;
            Rest -> {op, Anno, 'and', Rest, Failing}
        end,
    case E of
        none ->
            case Joined of
                {op, _, 'and', _, _} -> Joined;
                _ -> {op, Anno, 'and', Joined, {atom, Anno, true}}
            end;
        _ ->
            {op, Anno, 'and', Joined, E}
    end.

qualifiers([{Generate, AnnoG, P0, G0} | Qs0], St0) when
    Generate =:= generate;
    Generate =:= generate_strict;
    Generate =:= b_generate;
    Generate =:= b_generate_strict
->
    {G, St1} = expr(G0, St0),
    {P, St2} = pattern(P0, St1),
    {Qs, St3} = qualifiers(Qs0, St2),
    {[{Generate, AnnoG, P, G} | Qs], St3};
qualifiers([{Generate, AnnoG, {map_field_exact, AnnoF, K0, V0}, G0} | Qs0], St0) when
    Generate =:= m_generate; Generate =:= m_generate_strict
->
    {G, St1} = expr(G0, St0),
    {K, St2} = pattern(K0, St1),
    {V, St3} = pattern(V0, St2),
    {Qs, St4} = qualifiers(Qs0, St3),
    {[{Generate, AnnoG, {map_field_exact, AnnoF, K, V}, G} | Qs], St4};
qualifiers([{zip, AnnoG, Gs0} | Qs0], St0) ->
    {Gs, St1} = qualifiers(Gs0, St0),
    {Qs, St2} = qualifiers(Qs0, St1),
    {[{zip, AnnoG, Gs} | Qs], St2};
qualifiers([F0 | Qs0], #st{calls = Calls, raw_records = Records} = St0) ->
    %% A filter that is a guard test (record/2 included) is expanded as one.
    Overridden = fun(FA) -> is_map_key(FA, Calls) end,
    {F, St1} =
        case clause_erl_lint:is_guard_test(F0, Records, Overridden) of
            true -> guard_test(F0, St0);
            false -> expr(F0, St0)
        end,
    {Qs, St2} = qualifiers(Qs0, St1),
    {[F | Qs], St2};
qualifiers([], St) ->
    {[], St#st{checked = []}}.

%% Records

%% Every field with a default: undefined when the definition gives none.
normalise_fields(Fs) ->
    lists:map(
        fun
            ({record_field, Anno, Field}) ->
                {record_field, Anno, Field, {atom, Anno, undefined}};
            ({typed_record_field, {record_field, Anno, Field}, _Type}) ->
                {record_field, Anno, Field, {atom, Anno, undefined}};
            ({typed_record_field, Field, _Type}) ->
                Field;
            (F) ->
                F
        end,
        Fs
    ).

%% The fields of a tuple record, their defaults copied with annotation Anno.
record_fields(Name, Anno, St) ->
    [
        {record_field, Anno, {atom, Anno, F}, copy(Default, Anno)}
     || {record_field, _, {atom, _, F}, Default} <- maps:get(Name, St#st.records)
    ].

copy(Expr, Anno) -> erl_parse:map_anno(fun(_) -> Anno end, Expr).

find_field(F, [{record_field, _, {atom, _, F}, Value} | _]) -> {ok, Value};
find_field(F, [_ | Fs]) -> find_field(F, Fs);
find_field(_, []) -> error.

index_expr(Anno, {atom, _, F}, Fields) -> {integer, Anno, field_index(F, Fields, 2)}.

field_index(F, [{record_field, _, {atom, _, F}, _} | _], I) -> I;
field_index(F, [_ | Fs], I) -> field_index(F, Fs, I + 1).

%% The patterns of a tuple record's elements: the given ones, else _ = Pattern's, else _.
field_patterns(Fields, Given) ->
    Wildcard = wildcard(Given),
    [
        case find_field(F, Given) of
            {ok, P} -> P;
            error when Wildcard =:= none -> {var, Anno, '_'};
            error -> Wildcard
        end
     || {record_field, Anno, {atom, _, F}, _} <- Fields
    ].

%% The initial values of a tuple record's elements: the given ones, else _ = Value's, else the defaults.
field_inits(Fields, Given) ->
    Wildcard = wildcard(Given),
    [
        case find_field(F, Given) of
            {ok, Init} -> Init;
            error when Wildcard =:= none -> Default;
            error -> Wildcard
        end
     || {record_field, _, {atom, _, F}, Default} <- Fields
    ].

wildcard([{record_field, _, {var, _, '_'}, D} | _]) -> D;
wildcard([_ | Is]) -> wildcard(Is);
wildcard([]) -> none.

record_construction({record, Anno0, {shell_default, N}, Is}, St0) ->
    #{N := {local, Inits}} = St0#st.natives,
    {yes, {record, Anno1, {M, N}, Es0}} = expanded_inits(Anno0, N, Inits, Is, St0),
    {Es, St1} = exprs(Es0, St0),
    {{record, Anno1, {M, N}, Es}, St1};
record_construction({record, Anno, {M, N}, Inits0}, St0) ->
    {Inits, St1} = exprs(Inits0, St0),
    {{record, Anno, {M, N}, Inits}, St1};
record_construction({record, Anno0, Name, Is}, St0) when is_atom(Name) ->
    case St0#st.natives of
        #{Name := {local, Inits}} ->
            case expanded_inits(Anno0, Name, Inits, Is, St0) of
                no ->
                    {Es, St1} = exprs(Is, St0),
                    {{record, Anno0, Name, Es}, St1};
                {yes, Record} ->
                    expr(Record, St0)
            end;
        #{Name := {imported, M}} ->
            expr({record, Anno0, {M, Name}, Is}, St0);
        #{} ->
            Elements = [{atom, Anno0, Name} | field_inits(record_fields(Name, Anno0, St0), Is)],
            expr({tuple, mark_record(Anno0, St0), Elements}, St0)
    end.

record_update_expr({record, _, Arg0, Id, Updates0}, St0) when Id =:= []; tuple_size(Id) =:= 2 ->
    Anno = erl_parse:first_anno(Arg0),
    {Arg, St1} = expr(Arg0, St0),
    {Updates, St2} = exprs(Updates0, St1),
    {{record, Anno, Arg, Id, Updates}, St2};
record_update_expr({record, Anno0, Arg0, Name, Updates0}, St0) when is_atom(Name) ->
    case St0#st.natives of
        #{Name := {local, _}} ->
            Anno = erl_parse:first_anno(Arg0),
            {Arg, St1} = expr(Arg0, St0),
            {Updates, St2} = exprs(Updates0, St1),
            {{record, Anno, Arg, Name, Updates}, St2};
        #{Name := {imported, M}} ->
            expr({record, Anno0, Arg0, {M, Name}, Updates0}, St0);
        #{} ->
            {Update, St1} = record_update(
                Arg0, Name, record_fields(Name, Anno0, St0), Updates0, St0
            ),
            expr(Update, St1)
    end.

record_access({record_field, A, Rec0, Name, F}, St0) when is_atom(Name) ->
    case St0#st.natives of
        #{Name := {local, _}} ->
            {Rec, St1} = expr(Rec0, St0),
            {{record_field, erl_parse:first_anno(Rec), Rec, Name, F}, St1};
        #{Name := {imported, M}} ->
            expr({record_field, A, Rec0, {M, Name}, F}, St0);
        #{} ->
            get_field(erl_parse:first_anno(Rec0), Rec0, F, Name, St0)
    end;
record_access({record_field, _, Rec0, Id, F}, St0) ->
    {Rec, St1} = expr(Rec0, St0),
    {{record_field, erl_parse:first_anno(Rec), Rec, Id, F}, St1}.

%% The fields of a native record construction completed from its definition: badfield for an unknown field,
%% novalue for one without a default (dropped with dialyzer). With neither option the construction stays.
expanded_inits(Anno, Name, Inits, Is, #st{module = M} = St) ->
    case St of
        #st{dialyzer = true} when is_list(Inits) ->
            Fields = [
                F
             || {record_field, _, _, V} = F <- native_inits(Anno, Inits, Is), not is_missing(V)
            ],
            {yes, {record, Anno, {M, Name}, Fields}};
        #st{expand_inits = true} ->
            {yes, {record, Anno, {M, Name}, native_inits(Anno, Inits, Is)}};
        #st{} ->
            no
    end.

is_missing({nil, novalue}) -> true;
is_missing({nil, badfield}) -> true;
is_missing(_) -> false.

native_inits(Anno, Defs0, Is) ->
    Defs = [
        case Def of
            {record_field, _, {atom, _, F}, D} ->
                {record_field, Anno, {atom, Anno, F}, copy(D, Anno)};
            {record_field, _, {atom, _, _}} ->
                Def
        end
     || Def <- untyped(Defs0)
    ],
    Given = [F || {record_field, _, {atom, _, F}, _} <- Is],
    Known =
        [F || {record_field, _, {atom, _, F}, _} <- untyped(Defs0)] ++
            [F || {record_field, _, {atom, _, F}} <- untyped(Defs0)],
    Unknown = ordsets:subtract(ordsets:from_list(Given), ordsets:from_list(Known)),
    [{record_field, Anno, {atom, Anno, F}, {nil, badfield}} || F <- Unknown] ++
        [
            case Def of
                {record_field, A1, {atom, A2, F}, D} ->
                    {record_field, A1, {atom, A2, F}, given_or(F, Is, D)};
                {record_field, A1, {atom, A2, F}} ->
                    {record_field, A1, {atom, A2, F}, given_or(F, Is, {nil, novalue})}
            end
         || Def <- Defs
        ].

untyped(Defs) ->
    [
        case Def of
            {typed_record_field, Field, _} -> Field;
            _ -> Def
        end
     || Def <- Defs
    ].

given_or(F, Is, Default) ->
    case find_field(F, Is) of
        {ok, Init} -> Init;
        error -> Default
    end.

%% Rec#Name.F: in a body a case binding the field (badrecord otherwise), in a guard element/2 with an is_record/3 test
%% to join to the guard test; without strict record tests element/2 alone.
get_field(Anno, Rec, Field, Name, #st{strict = false} = St) ->
    I = index_expr(Anno, Field, record_fields(Name, Anno, St)),
    expr(element_call(Anno, I, Rec), St);
get_field(Anno0, Rec, {atom, _, F}, Name, #st{in_guard = false} = St0) ->
    {Var, St} = new_var(Anno0, St0),
    Fields = record_fields(Name, Anno0, St),
    I = field_index(F, Fields, 2),
    Elements = element_pattern(2, I, Var, length(Fields) + 1, Anno0, [{atom, Anno0, Name}]),
    Anno = generated(Anno0),
    Case =
        {'case', Anno0, Rec, [
            {clause, Anno, [{tuple, mark_record(Anno, St), Elements}], [], [Var]},
            {clause, Anno, [Var], [], [badrecord(Anno, Var)]}
        ]},
    expr(Case, St);
get_field(Anno0, Rec, Field, Name, St0) ->
    Fields = record_fields(Name, Anno0, St0),
    I = index_expr(Anno0, Field, Fields),
    {ExpRec, St1} = expr(Rec, St0),
    Key = {Name, erl_parse:map_anno(fun(_) -> erl_anno:new(0) end, ExpRec)},
    Anno = generated(Anno0),
    IsRecord =
        {call, Anno, {remote, Anno, {atom, Anno, erlang}, {atom, Anno, is_record}}, [
            ExpRec, {atom, Anno, Name}, {integer, Anno, length(Fields) + 1}
        ]},
    St2 = St1#st{check_needed = [{Key, Anno, IsRecord} | St1#st.check_needed]},
    {element_call(Anno0, I, ExpRec), St2}.

element_call(Anno, I, Rec) ->
    {call, Anno, {remote, Anno, {atom, Anno, erlang}, {atom, Anno, element}}, [I, Rec]}.

%% Elements Cur.. Size of a record pattern: Var at index I, _ elsewhere.
element_pattern(Cur, I, Var, Size, Anno, Acc) when Cur =:= I ->
    element_pattern(Cur + 1, I, Var, Size, Anno, [Var | Acc]);
element_pattern(Cur, I, Var, Size, Anno, Acc) when Cur =< Size ->
    element_pattern(Cur + 1, I, Var, Size, Anno, [{var, Anno, '_'} | Acc]);
element_pattern(_, _, _, _, _, Acc) ->
    lists:reverse(Acc).

badrecord(Anno, Term) -> error_call(Anno, {tuple, Anno, [{atom, Anno, badrecord}, Term]}).

error_call(Anno, Reason) ->
    {call, Anno, {remote, Anno, {atom, Anno, erlang}, {atom, Anno, error}}, [Reason]}.

%% Rec#Name{Updates}: the update values that are not simple bound first, the record bound to a new variable, then a
%% setelement/3 chain (or, with dialyzer or no updates, a case rebuilding the tuple).
record_update(Rec, Name, Fields, Updates0, St0) ->
    Anno = element(2, Rec),
    {Pre, Updates, St1} = bound_updates(Updates0, St0, [], []),
    {Var, St2} = new_var(Anno, St1),
    {Update, St} =
        case not St2#st.dialyzer andalso Updates =/= [] of
            true -> {setelement_chain(Var, Name, Fields, Updates), St2};
            false -> rebuild_case(Var, Name, Anno, Fields, Updates, St2)
        end,
    {{block, Anno, Pre ++ [{match, Anno, Var, Rec}, Update]}, St}.

rebuild_case(Rec, Name, Anno, Fields, Updates, St0) ->
    {Ps, News, St1} = rebuilt_fields(Fields, Updates, St0),
    Generated = generated(Anno),
    Marked = mark_record(Anno, St1),
    {
        {'case', Anno, Rec, [
            {clause, Anno, [{tuple, Marked, [{atom, Anno, Name} | Ps]}], [], [
                {tuple, Marked, [{atom, Anno, Name} | News]}
            ]},
            {clause, Generated, [{var, Generated, '_'}], [], [badrecord(Generated, Rec)]}
        ]},
        St1
    }.

rebuilt_fields([{record_field, Anno, {atom, _, F}, _} | Fs], Updates, St0) ->
    {P, St1} = new_var(Anno, St0),
    {Ps, News, St2} = rebuilt_fields(Fs, Updates, St1),
    case find_field(F, Updates) of
        {ok, New} -> {[P | Ps], [New | News], St2};
        error -> {[P | Ps], [P | News], St2}
    end;
rebuilt_fields([], _, St) ->
    {[], [], St}.

setelement_chain(Rec, Name, Fields, Updates0) ->
    Indexed = lists:sort(
        lists:foldl(
            fun({record_field, Anno, Field, Value}, Acc) ->
                {integer, _, Index} = I = index_expr(Anno, Field, Fields),
                [{Index, {I, Anno, Value}} | Acc]
            end,
            [],
            Updates0
        )
    ),
    Updates = [U || {_, U} <- Indexed],
    AnnoR = element(2, hd(Updates)),
    Generated = generated(AnnoR),
    Chain = lists:foldr(
        fun({I, Anno, Value}, Acc) ->
            {call, Anno, {remote, Anno, {atom, Anno, erlang}, {atom, Anno, setelement}}, [
                I, Acc, Value
            ]}
        end,
        Rec,
        Updates
    ),
    {'case', AnnoR, Rec, [
        {clause, AnnoR,
            [
                {tuple, AnnoR, [
                    {atom, AnnoR, Name} | lists:duplicate(length(Fields), {var, AnnoR, '_'})
                ]}
            ],
            [], [
                Chain
            ]},
        {clause, Generated, [{var, Generated, '_'}], [], [badrecord(Generated, Rec)]}
    ]}.

%% Update values that are not variables or literals, bound to new variables before the update.
bound_updates([{record_field, Anno, {atom, _, _} = Name, Value} = Field | Us], St0, Pre, Fs) ->
    case is_simple(Value) of
        true ->
            bound_updates(Us, St0, Pre, [Field | Fs]);
        false ->
            {Var, St} = new_var(Anno, St0),
            bound_updates(Us, St, [{match, Anno, Var, Value} | Pre], [
                {record_field, Anno, Name, Var} | Fs
            ])
    end;
bound_updates([], St, Pre, Fs) ->
    {lists:reverse(Pre), Fs, St}.

is_simple({var, _, _}) ->
    true;
is_simple(Value) ->
    try erl_parse:normalise(Value) of
        _ -> true
    catch
        error:_ -> false
    end.

record_info(Anno, [{atom, _, size}, {atom, _, Name}], St) ->
    {{integer, Anno, 1 + length(record_fields(Name, Anno, St))}, St};
record_info(Anno, [{atom, _, fields}, {atom, _, Name}], St) ->
    Names = [Field || {record_field, _, Field, _} <- record_fields(Name, Anno, St)],
    {lists:foldr(fun(H, T) -> {cons, Anno, H, T} end, {nil, Anno}, Names), St}.

new_vars(N, Anno, St) -> new_vars(N, Anno, St, []).

new_vars(N, Anno, St0, Vs) when N > 0 ->
    {V, St1} = new_var(Anno, St0),
    new_vars(N - 1, Anno, St1, [V | Vs]);
new_vars(0, _Anno, St, Vs) ->
    {Vs, St}.

new_var(Anno, #st{vcount = N} = St) ->
    {{var, Anno, list_to_atom("rec" ++ integer_to_list(N))}, St#st{vcount = N + 1}}.

generated(Anno) -> erl_anno:set_generated(true, Anno).

mark_record(Anno, #st{dialyzer = true}) -> erl_anno:set_record(true, Anno);
mark_record(Anno, #st{}) -> Anno.

%% is_record/3 tests that the whole guard needs become patterns: the variable matched against a tuple of the
%% record's name and size, the test replaced by true (not with dialyzer).
record_tests_as_patterns(H, G, #st{dialyzer = true}) ->
    {H, G};
record_tests_as_patterns(H0, G0, #st{}) ->
    case needed_tests(G0) of
        [] ->
            {H0, G0};
        Tests0 ->
            {H, Tests} = test_patterns(H0, Tests0, []),
            {H, [[remove_tests(T, Tests) || T <- Conj] || Conj <- G0]}
    end.

%% The variables whose is_record/3 test every conjunction of the guard needs.
needed_tests([G | Gs]) ->
    lists:foldl(
        fun(Conj, Acc) -> ordsets:intersection(Acc, conj_tests(Conj, [])) end, conj_tests(G, []), Gs
    );
needed_tests([]) ->
    [].

conj_tests(Tests, Acc) -> lists:foldl(fun needed_test/2, Acc, Tests).

needed_test({op, _, Op, A1, A2}, Acc) when Op =:= 'and'; Op =:= 'andalso' ->
    conj_tests([A1, A2], Acc);
needed_test({op, _, 'orelse', A, {atom, _, fail}}, Acc) ->
    needed_test(A, Acc);
needed_test(
    {call, Anno, {remote, _, {atom, _, erlang}, {atom, _, is_record} = IsRecord}, Args}, Acc
) ->
    needed_test({call, Anno, IsRecord, Args}, Acc);
needed_test(
    {call, _, {atom, _, is_record}, [{var, _, V}, {atom, _, Tag}, {integer, _, Size}]}, Acc
) when
    is_integer(Size), 0 < Size, Size < 100
->
    orddict:store(V, {Tag, Size}, Acc);
needed_test(_, Acc) ->
    Acc.

test_patterns([P0 | Ps], Tests0, Acc) ->
    {P, Tests} = test_pattern(P0, Tests0),
    test_patterns(Ps, Tests, [P | Acc]);
test_patterns([], Tests, Acc) ->
    {lists:reverse(Acc), Tests}.

test_pattern({var, Anno, V} = Var, Tests) ->
    case orddict:find(V, Tests) of
        {ok, {Tag, Size}} ->
            Elements = element_pattern(2, -1, ignore, Size, Anno, [{atom, Anno, Tag}]),
            {
                {match, Anno, {tuple, Anno, Elements}, Var},
                orddict:store(V, {remove, Tag, Size}, Tests)
            };
        _ ->
            {Var, Tests}
    end;
test_pattern({cons, Anno, H0, T0}, Tests0) ->
    {H, Tests1} = test_pattern(H0, Tests0),
    {T, Tests} = test_pattern(T0, Tests1),
    {{cons, Anno, H, T}, Tests};
test_pattern({tuple, Anno, Es0}, Tests0) ->
    {Es, Tests} = test_patterns(Es0, Tests0, []),
    {{tuple, Anno, Es}, Tests};
test_pattern({match, Anno, A0, B0}, Tests0) ->
    {A, Tests1} = test_pattern(A0, Tests0),
    {B, Tests} = test_pattern(B0, Tests1),
    {{match, Anno, A, B}, Tests};
test_pattern(P, Tests) ->
    {P, Tests}.

remove_tests({op, Anno, Op, A1, A2}, Tests) when Op =:= 'and'; Op =:= 'andalso' ->
    {op, Anno, Op, remove_tests(A1, Tests), remove_tests(A2, Tests)};
remove_tests({op, Anno, 'orelse', A1, A2}, Tests) ->
    {op, Anno, 'orelse', remove_tests(A1, Tests), A2};
remove_tests({call, Anno, Callee, [{var, _, V}, {atom, _, Tag}, {integer, _, Size}]} = Test, Tests) ->
    case is_record_callee(Callee) andalso orddict:find(V, Tests) of
        {ok, {remove, Tag, Size}} -> {atom, Anno, true};
        _ -> Test
    end;
remove_tests(Test, _Tests) ->
    Test.

is_record_callee({remote, _, {atom, _, erlang}, {atom, _, is_record}}) -> true;
is_record_callee({atom, _, is_record}) -> true;
is_record_callee(_) -> false.
