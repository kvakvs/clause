%% Plan 22 B.0: the inference cost shape of a compiler pass written in Erlang. A free-variable
%% pass over a small invented tree language threads a state record of 18 fields through mutually
%% recursive walkers, rebuilds every node (so results are unions of many record shapes), collects
%% variables in ordsets and turns nested functions into top-level ones. The input tree arrives as
%% an unknown term and a builder turns it into node records first.
-module(walkers).
-export([main/1, convert/1]).

-import(lists, [foldl/3, foldr/3, keyfind/3, member/2]).
-import(ordsets, [add_element/2, intersection/2, subtract/2, union/1, union/2]).

%% Atoms of the tree.
-record(var, {name}).
-record(lit, {value}).
-record(local_ref, {name, arity}).
-record(remote_ref, {module, name, arity}).

%% Bodies and expressions.
-record(bind, {vars, value, body}).
-record(fix, {defs}).
-record(lambda, {tag = [], params, body}).
-record(values, {items}).
-record(exit_with, {items = [], merge}).
-record(return, {anno = #{}, value}).
-record(jump, {target, items = []}).
-record(block, {target, params = [], first, rest, results = []}).
-record(choose, {body, results = []}).
-record(attempt, {protected, params, body, error_params, handler, results = []}).
-record(guarded, {body, results = []}).
-record(call, {anno = [], op, args, results = []}).
-record(primitive, {anno = [], op, args, results = []}).
-record(op, {anno = #{}, target = none, name, args = []}).
-record(checked, {op}).
-record(test, {name, args}).
-record(opaque, {value}).
-record(sequence, {first, then}).

%% Matching.
-record(either, {first, otherwise}).
-record(dispatch, {var, kinds}).
-record(kind_case, {kind, cases}).
-record(value_case, {pattern, body}).
-record(guards, {cases}).
-record(guard_case, {test, body}).

%% Patterns.
-record(p_cons, {head, tail}).
-record(p_tuple, {items, keep = []}).
-record(p_map, {base = #lit{value = #{}}, pairs}).
-record(p_pair, {key, value}).
-record(p_bits, {segment}).
-record(p_segment, {size, unit = 8, type = integer, value, next}).
-record(p_int_segment, {size, value, next}).
-record(p_bits_end, {}).
-record(p_record, {id, fields}).
-record(p_field, {key, value}).

%% Output functions.
-record(function, {anno = #{}, params, blocks, count}).
-record(code, {ops, last}).

%% The pass state: most fields only travel along, a few count, collect and remember.
-record(pass, {
    module,
    function,
    params = [],
    next_var = 0,
    next_fun = 0,
    scopes = [],
    functions = [],
    free = #{},
    defaults,
    warnings = [],
    debug = false,
    options = [],
    depth = 0,
    labels = #{},
    inlined = [],
    literals = #{},
    exports = [],
    source = none
}).

main(_) ->
    Tree =
        {bind, [x], {local, helper, [1, {var, y}]},
            {fix, [{helper, [a, b], {ret, {var, a}}}],
                {match,
                    {either,
                        {dispatch, x, [
                            {kind_case, tuple, [{value_case, {tuple, [p, q]}, {ret, {var, p}}}]}
                        ]},
                        {guards, [{guard_case, {test, is_atom, [{var, x}]}, {ret, {var, y}}}]}}}}},
    Functions = convert(binary_to_term(term_to_binary(Tree))),
    io:format("~p~n", [[Name || #function{anno = #{name := Name}} <- Functions]]),
    io:format("walkers: ok~n").

%% Builds the tree, finds its free variables and returns the top-level functions it produced.
convert(Term) ->
    St0 = #pass{module = walkers, function = {main, 0}, next_var = 100, defaults = #{}},
    {Body0, St1} = build_body(Term, St0),
    {Body1, _, St2} = walk_body(Body0, return, St1),
    [make_function([], main, [], Body1, St2) | St2#pass.functions].

%%% Builder: node records from tagged tuples.

build_body({bind, Vs, A, B}, St0) ->
    {E, St1} = build_expr(A, St0),
    {Body, St2} = build_body(B, St1),
    {#bind{vars = build_vars(Vs), value = E, body = Body}, St2};
build_body({fix, Defs, B}, St0) ->
    {Fs, St1} = build_defs(Defs, St0),
    {Body, St2} = build_body(B, St1),
    {#bind{vars = [], value = #fix{defs = Fs}, body = Body}, St2};
build_body({values, As}, St) ->
    {#values{items = build_atoms(As)}, St};
build_body({exit, As}, St) ->
    {#exit_with{items = build_atoms(As)}, St};
build_body({ret, A}, St) ->
    {#return{value = build_atom(A)}, St};
build_body({jump, L, As}, St) ->
    {#jump{target = L, items = build_atoms(As)}, St};
build_body({block, L, Vs, F, R}, St0) ->
    {First, St1} = build_body(F, St0),
    {Rest, St2} = build_body(R, St1),
    {#block{target = L, params = build_vars(Vs), first = First, rest = Rest}, St2};
build_body({match, M}, St0) ->
    {Match, St1} = build_match(M, St0),
    {#choose{body = Match}, St1};
build_body({attempt, A, Vs, B, Evs, H}, St0) ->
    {Protected, St1} = build_body(A, St0),
    {Body, St2} = build_body(B, St1),
    {Handler, St3} = build_body(H, St2),
    Attempt = #attempt{
        protected = Protected,
        params = build_vars(Vs),
        body = Body,
        error_params = build_vars(Evs),
        handler = Handler
    },
    {Attempt, St3};
build_body(Other, St0) ->
    {E, St1} = build_expr(Other, St0),
    {Atom, Pre, St2} = as_atom(E, St1),
    {prefix(Pre, #return{value = Atom}), St2}.

build_expr({test, Name, As}, St) ->
    {#test{name = Name, args = build_atoms(As)}, St};
build_expr({bind, Vs, A, B}, St0) ->
    {E, St1} = build_expr(A, St0),
    {Body, St2} = build_expr(B, St1),
    {#bind{vars = build_vars(Vs), value = E, body = Body}, St2};
build_expr({local, F, As}, St) ->
    Op = #local_ref{name = #lit{value = F}, arity = length(As)},
    {#call{op = Op, args = build_atoms(As)}, St};
build_expr({remote, M, F, As}, St) ->
    Op = #remote_ref{module = build_atom(M), name = build_atom(F), arity = length(As)},
    {#call{anno = [{line, 1}], op = Op, args = build_atoms(As)}, St};
build_expr({primitive, Op, As}, St) ->
    {#primitive{op = Op, args = build_atoms(As)}, St};
build_expr({match, M}, St0) ->
    {Match, St1} = build_match(M, St0),
    {#choose{body = Match}, St1};
build_expr({guarded, B}, St0) ->
    {Body, St1} = build_body(B, St0),
    {#guarded{body = Body}, St1};
build_expr({lambda, Vs, B}, St0) ->
    {Body, St1} = build_body(B, St0),
    {#lambda{tag = [{id, {0, 0, anonymous}}], params = build_vars(Vs), body = Body}, St1};
build_expr({fun_ref, F, A}, St) ->
    {#local_ref{name = #lit{value = F}, arity = A}, St};
build_expr({op, Name, As}, St) ->
    {#op{name = Name, args = build_atoms(As)}, St};
build_expr({checked, Name, As}, St) ->
    {#checked{op = #op{name = Name, args = build_atoms(As)}}, St};
build_expr({opaque, V}, St) ->
    {#opaque{value = V}, St};
build_expr(Other, St) ->
    {build_atom(Other), St}.

build_match({either, F, O}, St0) ->
    {First, St1} = build_match(F, St0),
    {Otherwise, St2} = build_match(O, St1),
    {#either{first = First, otherwise = Otherwise}, St2};
build_match({dispatch, V, Ks}, St0) ->
    {Kinds, St1} = build_matches(Ks, St0),
    {#dispatch{var = #var{name = V}, kinds = Kinds}, St1};
build_match({kind_case, K, Cs}, St0) ->
    {Cases, St1} = build_matches(Cs, St0),
    {#kind_case{kind = K, cases = Cases}, St1};
build_match({value_case, P, B}, St0) ->
    {Body, St1} = build_match(B, St0),
    {#value_case{pattern = build_pattern(P), body = Body}, St1};
build_match({guards, Gs}, St0) ->
    {Cases, St1} = build_matches(Gs, St0),
    {#guards{cases = Cases}, St1};
build_match({guard_case, G, B}, St0) ->
    {Test, St1} = build_expr(G, St0),
    {Body, St2} = build_match(B, St1),
    {#guard_case{test = Test, body = Body}, St2};
build_match(Other, St) ->
    build_body(Other, St).

build_matches(Ms, St) ->
    lists:mapfoldl(fun build_match/2, St, Ms).

build_pattern({cons, H, T}) ->
    #p_cons{head = build_pattern(H), tail = build_pattern(T)};
build_pattern({bits, S}) ->
    #p_bits{segment = build_pattern(S)};
build_pattern({segment, Size, V, N}) ->
    #p_segment{size = build_atom(Size), value = build_pattern(V), next = build_pattern(N)};
build_pattern({int_segment, Size, V, N}) ->
    #p_int_segment{size = build_atom(Size), value = V, next = build_pattern(N)};
build_pattern(bits_end) ->
    #p_bits_end{};
build_pattern({tuple, Es}) ->
    #p_tuple{items = build_vars(Es)};
build_pattern({map, Ps}) ->
    #p_map{pairs = [#p_pair{key = build_atom(K), value = build_pattern(V)} || {K, V} <- Ps]};
build_pattern({record, Id, Fs}) ->
    #p_record{id = Id, fields = [#p_field{key = K, value = build_pattern(V)} || {K, V} <- Fs]};
build_pattern(Other) ->
    build_atom(Other).

build_defs(Defs, St) ->
    lists:mapfoldl(
        fun({Name, Vs, B}, St0) ->
            {Body, St1} = build_body(B, St0),
            {{Name, #lambda{params = build_vars(Vs), body = Body}}, St1}
        end,
        St,
        Defs
    ).

build_vars(Vs) -> [#var{name = V} || V <- Vs].

build_atoms(As) -> [build_atom(A) || A <- As].

build_atom({var, V}) -> #var{name = V};
build_atom(V) -> #lit{value = V}.

prefix([{set, Vs, Value} | Pre], Body) ->
    #bind{vars = Vs, value = Value, body = prefix(Pre, Body)};
prefix([], Body) ->
    Body.

as_atom(#lit{} = Lit, St) ->
    {Lit, [], St};
as_atom(#var{} = Var, St) ->
    {Var, [], St};
as_atom(E, St0) ->
    {V, St1} = fresh_var(St0),
    {V, [{set, [V], E}], St1}.

%%% Free variables: each walker returns the rebuilt node, the ordset of variables it uses and
%%% the state.

%% The variables a function body uses, without collecting the functions it contains.
used_vars(Body, St) ->
    {_, Used, _} = walk_body(Body, return, St#pass{functions = ignore}),
    Used.

walk_body(#bind{vars = [], value = #fix{} = Fix, body = B}, Exit, St0) ->
    St1 = fix_functions(Fix, St0),
    walk_body(B, Exit, St1);
walk_body(#bind{vars = [], value = #lit{}, body = B}, Exit, St) ->
    walk_body(B, Exit, St);
walk_body(#bind{vars = [], value = #var{}, body = B}, Exit, St) ->
    walk_body(B, Exit, St);
walk_body(#bind{vars = Vs, value = E0, body = B0}, Exit, St0) ->
    {E1, Eu, St1} = walk_expr(E0, {exit, Vs}, St0),
    {B1, Bu, St2} = walk_body(B0, Exit, St1),
    Used = union(Eu, subtract(Bu, vars_of(Vs))),
    {#sequence{first = E1, then = B1}, Used, St2};
walk_body(#values{items = As}, {exit, _}, St) ->
    {#exit_with{items = As}, vars_of(As), St};
walk_body(#exit_with{items = As} = Exit, {exit, _}, St) ->
    {Exit, vars_of(As), St};
walk_body(#return{value = V} = Return, return, St) ->
    {Return, atom_vars(V), St};
walk_body(#jump{items = As} = Jump, _, St) ->
    {Jump, vars_of(As), St};
walk_body(#block{} = E, return, St) ->
    walk_expr(E, return, St);
walk_body(#choose{} = E, return, St) ->
    walk_expr(E, return, St);
walk_body(#attempt{} = E, return, St) ->
    walk_expr(E, return, St);
walk_body(E, return, St0) ->
    {Atom, Pre, St1} = as_atom(E, St0),
    walk_body(prefix(Pre, #return{value = Atom}), return, St1);
walk_body(E, {exit, [_]} = Exit, St0) ->
    {Atom, Pre, St1} = as_atom(E, St0),
    walk_body(prefix(Pre, #exit_with{items = [Atom]}), Exit, St1);
walk_body(E, {exit, Rs} = Exit, St0) ->
    {Vs, St1} = fresh_vars(length(Rs), St0),
    walk_body(#bind{vars = Vs, value = E, body = #exit_with{items = Vs}}, Exit, St1).

%% Gives the functions of a fix the free variables of all of them as extra parameters.
fix_functions(#fix{defs = Fs}, St0) ->
    Free = foldl(
        fun({_, #lambda{params = Vs, body = B}}, Acc) ->
            union(subtract(used_vars(B, St0), vars_of(Vs)), Acc)
        end,
        [],
        Fs
    ),
    FreeVars = [#var{name = V} || V <- Free],
    St1 = foldl(
        fun({Name, #lambda{params = Vs}}, St) -> remember_free(Name, length(Vs), FreeVars, St) end,
        St0,
        Fs
    ),
    lift_functions(Fs, FreeVars, St1).

lift_functions(_, _, #pass{functions = ignore} = St) ->
    St;
lift_functions(Fs, FreeVars, St0) ->
    foldl(
        fun({Name, #lambda{tag = Tag, params = Vs, body = B0}}, St1) ->
            {B1, _, St2} = walk_body(B0, return, St1#pass{depth = St1#pass.depth + 1}),
            Function = make_function(Tag, Name, Vs ++ FreeVars, B1, St2),
            St2#pass{functions = [Function | St2#pass.functions], depth = St1#pass.depth}
        end,
        St0,
        Fs
    ).

walk_expr(#test{args = As} = Test, {exit, []}, St) ->
    {Test, vars_of(As), St};
walk_expr(#bind{vars = Vs, value = E0, body = B0}, {exit, _} = Exit, St0) ->
    {E1, Eu, St1} = walk_expr(E0, {exit, Vs}, St0),
    {B1, Bu, St2} = walk_expr(B0, Exit, St1),
    Used = union(Eu, subtract(Bu, vars_of(Vs))),
    {#sequence{first = E1, then = B1}, Used, St2};
walk_expr(
    #call{op = #local_ref{name = #lit{value = F}, arity = A} = Op, args = As0} = Call,
    {exit, Rs0},
    St0
) ->
    {Rs, St1} = result_vars(Rs0, St0),
    As1 = As0 ++ free_of(F, A, St1),
    {
        Call#call{op = Op#local_ref{arity = length(As1)}, args = As1, results = Rs},
        vars_of(As1),
        St1
    };
walk_expr(#call{op = Op, args = As} = Call, {exit, Rs0}, St0) ->
    {[R | Rs], St1} = result_vars(Rs0, St0),
    Used = union(op_vars(Op), vars_of(As)),
    {unused_results(Rs, Call#call{results = [R]}), Used, St1};
walk_expr(#primitive{args = As} = Primitive, {exit, Rs}, St0) ->
    {Results, St1} = primitive_results(Primitive, Rs, St0),
    {Primitive#primitive{results = Results}, vars_of(As), St1};
walk_expr(#choose{body = B0} = Choose, Exit, St0) ->
    {B1, Bu, St1} = walk_match(B0, Exit, St0),
    {Choose#choose{body = B1, results = exit_results(Exit)}, Bu, St1};
walk_expr(
    #attempt{protected = A0, params = Vs, body = B0, error_params = Evs, handler = H0} = Attempt,
    Exit,
    St0
) ->
    {Avs, St1} = fresh_vars(length(Vs), St0),
    {A1, Au, St2} = walk_body(A0, {exit, Avs}, St1),
    {B1, Bu, St3} = walk_body(B0, Exit, St2),
    {H1, Hu, St4} = walk_body(H0, Exit, St3),
    Used = union([Au, subtract(Bu, vars_of(Vs)), subtract(Hu, vars_of(Evs))]),
    {
        Attempt#attempt{protected = A1, body = B1, handler = H1, results = exit_results(Exit)},
        Used,
        St4
    };
walk_expr(#guarded{body = B0} = Guarded, {exit, Rs0}, St0) ->
    {R, St1} = fresh_var(St0),
    {B1, Bu, St2} = walk_body(B0, {exit, [R]}, St1),
    {Extra, St3} = fresh_vars(1 - length(Rs0), St2),
    {Guarded#guarded{body = B1, results = Rs0 ++ Extra}, Bu, St3};
walk_expr(#lambda{tag = Tag, params = Vs, body = B0}, {exit, Rs}, St0) ->
    {B1, Bu, St1} = walk_body(B0, return, St0),
    Free = subtract(Bu, vars_of(Vs)),
    FreeVars = [#var{name = V} || V <- Free],
    {Name, St2} = lambda_name(Tag, St1),
    Function = make_function(Tag, Name, Vs ++ FreeVars, B1, St2),
    Ref = #local_ref{name = #lit{value = Name}, arity = length(Vs) + length(Free)},
    {Closure, St3} = make_closure(Rs, Ref, FreeVars, St2),
    {Closure, Free, add_function(Function, St3)};
walk_expr(#local_ref{name = #lit{value = Name}, arity = A} = Ref, {exit, Rs}, St0) ->
    Free = vars_of(free_of(Name, A, St0)),
    FreeVars = [#var{name = V} || V <- Free],
    {Closure, St1} = make_closure(Rs, Ref#local_ref{arity = A + length(Free)}, FreeVars, St0),
    {Closure, Free, St1};
walk_expr(#block{params = Vs, first = F0, rest = R0} = Block, Exit, St0) ->
    {F1, Fu, St1} = walk_body(F0, Exit, St0),
    {R1, Ru, St2} = walk_body(R0, Exit, St1),
    Used = subtract(union(Fu, Ru), vars_of(Vs)),
    St3 = St2#pass{labels = (St2#pass.labels)#{Block#block.target => length(Vs)}},
    {Block#block{first = F1, rest = R1, results = exit_results(Exit)}, Used, St3};
walk_expr(#op{target = none, args = As} = Op, {exit, [Target]}, St) ->
    {Op#op{target = Target}, vars_of(As), St};
walk_expr(#op{target = none, args = As} = Op, {exit, Rs0}, St0) ->
    {[Target | Rest], St1} = result_vars(Rs0, St0),
    {unused_results(Rest, Op#op{target = Target}), vars_of(As), St1};
walk_expr(#checked{op = Op0}, {exit, _} = Exit, St0) ->
    {Op, Used, St1} = walk_expr(Op0, Exit, St0),
    {#checked{op = Op}, Used, St1};
walk_expr(#opaque{} = Opaque, _, St) ->
    {Opaque, [], St};
walk_expr(Atom, {exit, [Target]}, St) ->
    {#op{name = copy, target = Target, args = [Atom]}, atom_vars(Atom), St}.

walk_match(#either{first = F0, otherwise = O0} = Either, Exit, St0) ->
    {F1, Fu, St1} = walk_match(F0, Exit, St0),
    {O1, Ou, St2} = walk_match(O0, Exit, St1),
    {Either#either{first = F1, otherwise = O1}, union(Fu, Ou), St2};
walk_match(#dispatch{var = #var{name = V}, kinds = Ks0} = Dispatch, Exit, St0) ->
    {Ks1, Used, St1} = walk_matches(Ks0, Exit, St0),
    {Dispatch#dispatch{kinds = Ks1}, add_element(V, Used), St1};
walk_match(#kind_case{cases = Cs0} = KindCase, Exit, St0) ->
    {Cs1, Used, St1} = walk_matches(Cs0, Exit, St0),
    {KindCase#kind_case{cases = Cs1}, Used, St1};
walk_match(#value_case{pattern = P0, body = B0} = ValueCase, Exit, St0) ->
    {Uses, Binds} = pattern_vars(P0),
    {B1, Bu, St1} = walk_match(B0, Exit, St0),
    P1 = drop_unused(P0, Bu, Binds),
    {ValueCase#value_case{pattern = P1, body = B1}, union(Uses, subtract(Bu, Binds)), St1};
walk_match(#guards{cases = Gs0} = Guards, Exit, St0) ->
    {Gs1, Used, St1} = walk_matches(Gs0, Exit, St0),
    {Guards#guards{cases = Gs1}, Used, St1};
walk_match(#guard_case{test = T0, body = B0} = GuardCase, Exit, St0) ->
    {T1, Tu, St1} = walk_expr(T0, {exit, []}, St0),
    {B1, Bu, St2} = walk_match(B0, Exit, St1),
    {GuardCase#guard_case{test = T1, body = B1}, union(Tu, Bu), St2};
walk_match(B, Exit, St) ->
    walk_body(B, Exit, St).

walk_matches(Ms0, Exit, St0) ->
    foldr(
        fun(M0, {Ms, Used, St1}) ->
            {M1, Mu, St2} = walk_match(M0, Exit, St1),
            {[M1 | Ms], union(Mu, Used), St2}
        end,
        {[], [], St0},
        Ms0
    ).

%%% Patterns: the variables a pattern uses (sizes, map keys) and the variables it binds.

pattern_vars(#var{name = N}) ->
    {[], [N]};
pattern_vars(#lit{}) ->
    {[], []};
pattern_vars(#p_cons{head = H, tail = T}) ->
    patterns_vars([H, T]);
pattern_vars(#p_bits{segment = S}) ->
    pattern_vars(S);
pattern_vars(#p_segment{size = Size, value = V, next = N}) ->
    {Uses, Binds} = patterns_vars([V, N]),
    {union(Uses, atom_vars(Size)), Binds};
pattern_vars(#p_int_segment{size = Size, next = N}) ->
    {[], Binds} = pattern_vars(N),
    {atom_vars(Size), Binds};
pattern_vars(#p_bits_end{}) ->
    {[], []};
pattern_vars(#p_tuple{items = Es}) ->
    patterns_vars(Es);
pattern_vars(#p_map{pairs = Ps}) ->
    patterns_vars(Ps);
pattern_vars(#p_pair{key = K, value = V}) ->
    {Uses, Binds} = pattern_vars(V),
    {union(Uses, atom_vars(K)), Binds};
pattern_vars(#p_record{fields = Fs}) ->
    patterns_vars(Fs);
pattern_vars(#p_field{value = V}) ->
    pattern_vars(V).

patterns_vars(Ps) ->
    foldl(
        fun(P, {Uses0, Binds0}) ->
            {Uses, Binds} = pattern_vars(P),
            {union(Uses0, Uses), union(Binds0, Binds)}
        end,
        {[], []},
        Ps
    ).

%% Tuple items the body never reads become literals, unless the pattern keeps them.
drop_unused(#p_tuple{items = Es, keep = Keep} = P, Used, Binds) ->
    Live = intersection(union(Used, Keep), Binds),
    P#p_tuple{items = [item_if_live(E, Live) || E <- Es]};
drop_unused(P, _, _) ->
    P.

item_if_live(#var{name = N} = Var, Live) ->
    case member(N, Live) of
        true -> Var;
        false -> #lit{value = unused}
    end.

%%% Helpers.

vars_of(Atoms) ->
    foldl(fun(A, Vs) -> union(atom_vars(A), Vs) end, [], Atoms).

atom_vars(#var{name = N}) -> [N];
atom_vars(#lit{}) -> [].

op_vars(#remote_ref{module = M, name = N}) -> vars_of([M, N]);
op_vars(Atom) -> atom_vars(Atom).

exit_results({exit, Rs}) -> Rs;
exit_results(return) -> [].

result_vars([], St) -> fresh_vars(1, St);
result_vars([_ | _] = Rs, St) -> {Rs, St}.

primitive_results(#primitive{op = Op, args = As}, Rs, St0) ->
    Count =
        case {Op, length(As)} of
            {peek_message, 0} -> 2;
            {_, _} -> 1
        end,
    {Extra, St1} = fresh_vars(Count - length(Rs), St0),
    {Rs ++ Extra, St1}.

unused_results([R | Rs], Body) ->
    Copy = #op{name = copy, target = R, args = [#lit{value = unused}]},
    unused_results(Rs, #sequence{first = Copy, then = Body});
unused_results([], Body) ->
    Body.

make_closure(Rs, Ref, FreeVars, St0) ->
    {[Target], St1} = result_vars(Rs, St0),
    {#op{name = make_fun, target = Target, args = [Ref | FreeVars]}, St1}.

lambda_name(Tag, St) ->
    case keyfind(id, 1, Tag) of
        {id, {_, _, Name}} -> {Name, St};
        false -> fresh_fun_name(St)
    end.

add_function(_, #pass{functions = ignore} = St) ->
    St;
add_function(#function{anno = #{name := Name}} = F, #pass{functions = Fs} = St) ->
    case [Name || #function{anno = #{name := Other}} <- Fs, Other =:= Name] of
        [] -> St#pass{functions = [F | Fs]};
        [_ | _] -> St
    end.

free_of(F, A, #pass{free = Free}) ->
    case Free of
        #{{F, A} := Vars} -> Vars;
        #{} -> []
    end.

remember_free(F, A, Vars, #pass{free = Free} = St) ->
    St#pass{free = Free#{{F, A} => Vars}}.

fresh_fun_name(#pass{function = {F, A}, next_fun = C} = St) ->
    Name = list_to_atom(lists:concat(["-", F, "/", A, "-fun-", C, "-"])),
    {Name, St#pass{next_fun = C + 1}}.

fresh_var(#pass{next_var = C} = St) ->
    {#var{name = C}, St#pass{next_var = C + 1}}.

fresh_vars(N, St) when N > 0 ->
    {V, St1} = fresh_var(St),
    {Vs, St2} = fresh_vars(N - 1, St1),
    {[V | Vs], St2};
fresh_vars(_, St) ->
    {[], St}.

make_function(Anno, Name, Params, Body, #pass{module = M, next_var = Count}) ->
    Last = #return{value = #lit{value = element(1, Body)}},
    Code = #code{ops = [#op{name = copy, target = #var{name = Count}, args = Params}], last = Last},
    #function{
        anno = #{name => Name, info => {M, Name, length(Params)}, tag => Anno},
        params = Params,
        blocks = #{0 => Code},
        count = Count
    }.
