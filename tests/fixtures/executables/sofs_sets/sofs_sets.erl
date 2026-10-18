% Clause-owned sofs coverage (LIB7A); generate oracle output with OTP 29.
-module(sofs_sets).
-export([main/1]).

% Exercise each export but the digraph conversions (LIB12), keeping fun identities out of output.
main(_) ->
    constructors(),
    algebra(),
    relations(),
    products(),
    selectors(),
    families(),
    callbacks(),
    external_callbacks(),
    untyped(),
    errors(),
    io:format("sofs_sets: ok~n").

% Emit stable terms with explicit labels so individual semantic changes are easy to locate.
show(Label, Values) -> io:format("~s: ~w~n", [Label, Values]).

% Hide deliberately invalid arguments from compile-time evaluation.
id(X) -> X.

% Record only observable exception class and reason, independent of stack traces.
catching(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

% Cover explicit labels, inferred nested types, opaque atomic elements and both record kinds.
constructors() ->
    show("constructors/basic", [
        sofs:empty_set(),
        sofs:set([c, a, b, a]),
        sofs:set([1.0, 1, 2, 2.0]),
        sofs:set([1, 1.0, 2.0, 2]),
        sofs:set([[b, a], {x, y}, [b, a]], [opaque]),
        sofs:set([b, a, b], [label]),
        sofs:relation([]),
        sofs:relation([{c}, {a}, {c}]),
        sofs:relation([{b, 2, x}, {a, 1, y}], 3),
        sofs:relation([{a, 1}, {b, 2}], [{key, value}]),
        sofs:a_function([{a, x}, {b, y}, {a, x}]),
        sofs:a_function([{[b, a], [y, x]}], [{[key], [value]}]),
        sofs:family([{a, [b, a, b]}, {b, []}, {a, [a, b]}]),
        sofs:family([{{x}, [{b, a}, {a, b}]}], [{{key}, [{left, right}]}])
    ]),
    show("constructors/inference", [
        sofs:from_term(a),
        sofs:from_term({a, [c, b, c]}),
        sofs:from_term([[], [a, a], [b, a]]),
        sofs:from_term([[], [[]]]),
        sofs:from_term([{[], {a, b}}, {[b, a], {c, d}}]),
        sofs:from_term([{{a}, [b, a, b], {{c}, {d}}}]),
        sofs:from_term([{[b, a], {c, d}}], [{[key], {left, right}}]),
        sofs:from_term([[a], [{b, c}]], [[opaque]]),
        sofs:from_term([], ['_']),
        sofs:from_term([1, 1.0]),
        sofs:from_term([1.0, 1]),
        sofs:from_term({1.0, [1, 1.0]}, {number, [number]}),
        sofs:relation([{{a}, [b, a]}], ['_']),
        sofs:a_function([{{a}, [b, a]}], ['_']),
        sofs:family([{{a}, [b, a]}], ['_'])
    ]),
    A = sofs:set([b, a]),
    O = sofs:from_term({a, [c, b]}),
    show("constructors/records", [
        sofs:from_external([b, a, b], [atom]),
        sofs:from_external({b, a}, {key, value}),
        sofs:from_external(id(x), id('_')),
        sofs:from_sets([A, sofs:empty_set(), A]),
        sofs:from_sets([sofs:from_term({a}), sofs:from_term({b})]),
        sofs:from_sets({A, O, sofs:empty_set()}),
        sofs:from_sets({}),
        sofs:to_external(A),
        sofs:type(O),
        sofs:to_sets(A),
        sofs:to_sets(O),
        sofs:no_elements(A),
        sofs:no_elements(O)
    ]),
    show("constructors/types", [
        {T, sofs:is_type(T)}
     || T <- [atom, opaque, '_', [], ['_'], [['_']], {}, {a, b}, {a, '_'}, {a, [b]}, [a, b], 1]
    ]).

% Exercise each binary operation, nested folds, queries and representative retention.
algebra() ->
    A = sofs:set([a, c, e]),
    B = sofs:set([b, c, d]),
    SS = sofs:from_term([[a, b], [b, c], [b]]),
    show("algebra/binary", [
        sofs:union(A, B),
        sofs:intersection(A, B),
        sofs:difference(A, B),
        sofs:symdiff(A, B),
        sofs:symmetric_partition(A, B),
        sofs:union(SS),
        sofs:intersection(SS),
        sofs:canonical_relation(SS),
        sofs:partition(SS),
        sofs:constant_function(A, sofs:from_term({x})),
        sofs:constant_function(A, sofs:set([x, y]))
    ]),
    show("algebra/queries", [
        sofs:is_equal(A, sofs:set([e, c, a])),
        sofs:is_equal(sofs:from_term(1), sofs:from_term(1.0)),
        sofs:is_subset(sofs:set([c]), A),
        sofs:is_subset(B, A),
        sofs:is_sofs_set(A),
        sofs:is_sofs_set(sofs:from_term(x)),
        sofs:is_sofs_set(id(x)),
        sofs:is_set(A),
        sofs:is_set(sofs:from_term(x)),
        sofs:is_empty_set(sofs:set([])),
        sofs:is_empty_set(sofs:from_term({}, opaque)),
        sofs:is_disjoint(A, B),
        sofs:is_disjoint(A, sofs:set([x]))
    ]),
    X = sofs:set([0, 2.0]),
    Y = sofs:set([1, 2]),
    show("algebra/numbers", [
        sofs:union(X, Y),
        sofs:union(Y, X),
        sofs:intersection(X, Y),
        sofs:symmetric_partition(X, Y),
        sofs:symdiff(X, Y),
        sofs:from_sets([sofs:from_term(1), sofs:from_term(1.0)]),
        sofs:union(sofs:from_external([[0, 2.0], [1, 2]], [[atom]]))
    ]).

% Binary relation operations include non-functional inputs and empty typed relations.
relations() ->
    R = sofs:relation([{a, b}, {a, c}, {b, c}, {c, c}]),
    F = sofs:relation([{a, b}, {b, c}]),
    show("relations/views", [
        sofs:domain(R),
        sofs:range(R),
        sofs:field(R),
        sofs:converse(R),
        sofs:relation_to_family(R),
        sofs:rel2fam(R),
        sofs:image(R, sofs:set([a])),
        sofs:inverse_image(R, sofs:set([c])),
        sofs:strict_relation(R),
        sofs:weak_relation(F),
        sofs:extension(F, sofs:set([a, b, d]), sofs:from_term(x)),
        sofs:is_a_function(R),
        sofs:is_a_function(F),
        sofs:composite(F, sofs:a_function([{b, x}, {c, y}, {z, w}])),
        sofs:inverse(F),
        sofs:restriction(R, sofs:set([a])),
        sofs:drestriction(R, sofs:set([a]))
    ]),
    N = sofs:relation([{0.0, a}, {0, b}, {1, 2}, {2, 2.0}]),
    show("relations/numbers", [
        sofs:domain(N),
        sofs:range(N),
        sofs:converse(N),
        sofs:relation_to_family(N),
        sofs:weak_relation(sofs:relation([{2.0, 0}, {2, 1}])),
        sofs:weak_relation(sofs:relation([{2.0, 1}, {2, 3}]))
    ]).

% Cartesian, relative, multiple and coordinate joins exercise tuple arities beyond two.
products() ->
    A = sofs:set([a, b]),
    B = sofs:set([1, 2]),
    R = sofs:relation([{a, x}, {a, y}, {b, z}]),
    S = sofs:relation([{a, 1}, {b, 2}]),
    show("products/all", [
        sofs:product(A, B),
        sofs:product({A}),
        sofs:product({A, B}),
        sofs:product({A, B, sofs:set([x])}),
        sofs:product({A, sofs:set([]), B}),
        sofs:product(A, sofs:set([])),
        sofs:relative_product(R, sofs:relation([{x, p}, {z, q}])),
        sofs:relative_product1(R, S),
        sofs:relative_product([R, S]),
        sofs:relative_product({R}),
        sofs:relative_product({R, S}, sofs:from_term([{{x, 1}, done}, {{z, 2}, last}])),
        sofs:multiple_relative_product({R, S}, sofs:relation([{a, a}, {a, b}, {b, b}])),
        sofs:join(sofs:relation([{a, b, c}, {d, e, f}]), 2, sofs:relation([{1, b}, {2, e}]), 2),
        sofs:join(R, 1, S, 1)
    ]).

% Every integer SetFun form uses native workers and preserves the relation's complete type.
selectors() ->
    R = sofs:relation([{a, 1, x}, {a, 2, y}, {b, 1, z}]),
    K = sofs:set([1]),
    show("selectors/integer", [
        sofs:projection(1, R),
        sofs:projection(2, R),
        sofs:substitution(3, R),
        sofs:partition(1, R),
        sofs:partition(2, R),
        sofs:partition_family(2, R),
        sofs:restriction(2, R, K),
        sofs:drestriction(2, R, K),
        sofs:partition(2, R, K),
        sofs:projection(1, sofs:relation([])),
        sofs:partition_family(2, sofs:relation([]))
    ]).

% Families retain keys with empty values in pointwise intersection and subtraction.
families() ->
    F = sofs:family([{a, [1, 2]}, {b, [2, 3]}, {c, []}]),
    G = sofs:family([{b, [3, 4]}, {c, [5]}, {d, []}]),
    Nested = sofs:from_term([{a, [[1, 2], [2, 3]]}, {b, [[3]]}]),
    R = sofs:from_term([{a, [{1, 2}, {2, 3}]}, {b, []}]),
    show("families/algebra", [
        sofs:family_to_relation(F),
        sofs:fam2rel(F),
        sofs:union_of_family(F),
        sofs:intersection_of_family(F),
        sofs:family_union(F, G),
        sofs:family_intersection(F, G),
        sofs:family_difference(F, G),
        sofs:family_union(Nested),
        sofs:family_intersection(Nested),
        sofs:family_domain(R),
        sofs:family_range(R),
        sofs:family_field(R),
        sofs:family([{a, [1]}, {a, [1.0]}]),
        sofs:family_union(sofs:family([{b, [2.0]}]), sofs:family([{a, []}, {b, [2]}]))
    ]).

% Wrapped functions return records; predicates receive wrapped elements or complete family value sets.
callbacks() ->
    S = sofs:from_term([[1], [1, 2], [2, 3]]),
    F = fun(Set) -> sofs:from_term(sofs:no_elements(Set)) end,
    K = sofs:set([2]),
    show("callbacks/sets", [
        sofs:specification(fun(Set) -> sofs:no_elements(Set) > 1 end, S),
        sofs:substitution(F, S),
        sofs:projection(F, S),
        sofs:partition(F, S),
        sofs:partition_family(F, S),
        sofs:restriction(F, S, K),
        sofs:drestriction(F, S, K),
        sofs:partition(F, S, K),
        sofs:substitution(fun(X) -> X end, sofs:set([a, b])),
        sofs:projection(fun(_) -> sofs:empty_set() end, sofs:set([a]))
    ]),
    Family = sofs:family([{a, [1, 2]}, {b, []}, {c, [3]}]),
    show("callbacks/families", [
        sofs:family_specification(fun(Set) -> not sofs:is_empty_set(Set) end, Family),
        sofs:family_projection(F, Family),
        sofs:family_projection(fun(X) -> X end, Family),
        sofs:family_projection(fun sofs:union/1, sofs:from_term([{a, [[1], [2]]}])),
        sofs:family_projection(id(bad), sofs:family([]))
    ]).

% External functions are also called on type tuples and numbered tuples before data values.
external_callbacks() ->
    R = sofs:relation([{a, 1, x}, {b, 1, y}, {c, 2, z}]),
    F = {external, fun({_, Key, _}) -> Key end},
    K = sofs:set([1]),
    show("external/all", [
        sofs:specification({external, fun({_, N, _}) -> N > 1 end}, R),
        sofs:substitution(F, R),
        sofs:projection(F, R),
        sofs:partition(F, R),
        sofs:partition_family(F, R),
        sofs:restriction(F, R, K),
        sofs:drestriction(F, R, K),
        sofs:partition(F, R, K),
        sofs:projection({external, fun({A, B, C}) -> {C, A, B} end}, R),
        sofs:family_specification(
            {external, fun(Vs) -> Vs =/= [] end}, sofs:family([{a, []}, {b, [1]}])
        ),
        sofs:projection({external, fun({A, _}) -> A end}, sofs:relation([]))
    ]),
    DataDependent =
        {external, fun({A, _}) ->
            case A of
                a -> z;
                b -> a;
                _ -> A
            end
        end},
    Reordered = sofs:relation([{a, 1}, {b, 2}, {c, 3}]),
    show("external/order_probe", [
        sofs:partition_family(DataDependent, Reordered),
        sofs:restriction(DataDependent, Reordered, sofs:set([a, z])),
        sofs:drestriction(DataDependent, Reordered, sofs:set([a, z])),
        sofs:partition(DataDependent, Reordered, sofs:set([a, z]))
    ]),
    DataError =
        {external, fun({A, _}) ->
            case A of
                a -> erlang:error(id(data_error));
                _ -> A
            end
        end},
    ProbeError =
        {external, fun({A, _}) ->
            case A of
                1 -> erlang:error(id(probe_error));
                _ -> A
            end
        end},
    show("external/error_boundaries", [
        catching(fun() -> sofs:substitution(DataError, Reordered) end),
        catching(fun() -> sofs:restriction(DataError, Reordered, sofs:set([a])) end),
        catching(fun() -> sofs:restriction(DataError, Reordered, sofs:set([], [different])) end),
        catching(fun() -> sofs:partition_family(ProbeError, Reordered) end)
    ]).

% Untyped sets short circuit different paths without invoking the callback.
untyped() ->
    E = sofs:empty_set(),
    R = sofs:relation([]),
    Never = fun(_) -> erlang:error(unexpected_callback) end,
    show("untyped/sets", [
        sofs:union(E, sofs:set([a])),
        sofs:intersection(E, sofs:set([a])),
        sofs:difference(E, sofs:set([a])),
        sofs:symmetric_partition(E, sofs:set([a])),
        sofs:product(E, sofs:set([a])),
        sofs:union(E),
        sofs:canonical_relation(E),
        sofs:domain(E),
        sofs:range(E),
        sofs:field(E),
        sofs:converse(E),
        sofs:relation_to_family(E),
        sofs:family_to_relation(E),
        sofs:relative_product1(E, R),
        sofs:relative_product([E, R]),
        sofs:projection(0, E),
        sofs:substitution(-1, E),
        sofs:partition(100, E),
        sofs:partition_family(0, E),
        sofs:restriction(0, E, sofs:set([a])),
        sofs:partition(0, E, sofs:set([a])),
        sofs:substitution(Never, R),
        sofs:partition_family(Never, E),
        sofs:restriction(Never, sofs:set([a]), E),
        sofs:drestriction(Never, sofs:set([a]), E),
        sofs:partition(Never, sofs:set([a]), E),
        sofs:projection({external, Never}, E),
        sofs:family_projection(Never, E),
        sofs:family_domain(sofs:from_term([{a, []}])),
        sofs:family_union(E, sofs:family([{a, [b]}])),
        sofs:extension(E, sofs:set([a]), sofs:from_term(b))
    ]).

% Constructor failures and semantic errors must preserve OTP's reason atoms.
errors() ->
    show("errors/arguments", [catching(F) || F <- bad_arguments()]),
    show("errors/types", [catching(F) || F <- bad_types()]),
    show("errors/functions", [catching(F) || F <- bad_functions()]),
    show("errors/records", [catching(F) || F <- wrong_records()]).

% Invalid external shapes and selector positions.
bad_arguments() ->
    [
        fun() -> sofs:from_term(id({})) end,
        fun() -> sofs:from_term(id([a, {b}])) end,
        fun() -> sofs:from_term(id([a | b])) end,
        fun() -> sofs:from_term([], id({atom, '_'})) end,
        fun() -> sofs:set(id(x)) end,
        fun() -> sofs:set([a], id(atom)) end,
        fun() -> sofs:relation(id([{}])) end,
        fun() -> sofs:relation([], id(0)) end,
        fun() -> sofs:relation(id([{a}, {a, b}]), 1) end,
        fun() -> sofs:family(id([{a, b}])) end,
        fun() -> sofs:from_sets(id([a])) end,
        fun() -> sofs:product(id({})) end,
        fun() -> sofs:intersection(sofs:empty_set()) end,
        fun() -> sofs:union(sofs:set(id([a]))) end,
        fun() -> sofs:to_sets(sofs:from_term(a)) end,
        fun() -> sofs:no_elements(sofs:from_term(a)) end,
        fun() -> sofs:projection(id(3), sofs:relation([])) end,
        fun() -> sofs:partition(id(0), sofs:relation([])) end,
        fun() -> sofs:intersection_of_family(sofs:family([])) end,
        fun() -> sofs:family_intersection(sofs:from_term(id([{a, []}]), [{atom, [[atom]]}])) end,
        fun() -> sofs:relative_product(id([])) end,
        fun() -> sofs:multiple_relative_product(id({}), sofs:relation([])) end,
        fun() -> sofs:join(sofs:relation([]), id(0), sofs:empty_set(), 1) end
    ].

% Incompatible labels, tuple arities and nested types.
bad_types() ->
    [
        fun() -> sofs:union(sofs:set([], id([x])), sofs:set([], id([y]))) end,
        fun() -> sofs:intersection(sofs:set([], id([x])), sofs:set([], id([y]))) end,
        fun() -> sofs:difference(sofs:set([], id([x])), sofs:set([], id([y]))) end,
        fun() -> sofs:is_equal(sofs:set([]), sofs:from_term(id(a))) end,
        fun() -> sofs:is_subset(sofs:set([], id([x])), sofs:set([], id([y]))) end,
        fun() -> sofs:field(sofs:relation([], id([{x, y}]))) end,
        fun() -> sofs:weak_relation(sofs:relation([], id([{x, y}]))) end,
        fun() -> sofs:from_sets(id([sofs:from_term({a}), sofs:from_term({a, b})])) end,
        fun() -> sofs:image(sofs:relation([], id([{x, y}])), sofs:set([], id([z]))) end,
        fun() ->
            sofs:relative_product1(sofs:relation([], id([{x, y}])), sofs:relation([], id([{z, y}])))
        end,
        fun() -> sofs:restriction(id(1), sofs:relation([], [{x, y}]), sofs:set([], id([z]))) end,
        fun() ->
            sofs:family_union(sofs:family([], id([{x, [y]}])), sofs:family([], id([{z, [y]}])))
        end
    ].

% Duplicate domains, non-bijective inverses and invalid callback returns.
bad_functions() ->
    [
        fun() -> sofs:a_function(id([{a, b}, {a, c}])) end,
        fun() -> sofs:a_function(id([{a, b}, {a, c}]), [{atom, atom}]) end,
        fun() -> sofs:family(id([{a, [b]}, {a, [c]}])) end,
        fun() -> sofs:inverse(sofs:relation(id([{a, b}, {c, b}]))) end,
        fun() -> sofs:composite(sofs:relation(id([{a, b}])), sofs:relation(id([{c, d}]))) end,
        fun() -> sofs:specification(fun(_) -> id('maybe') end, sofs:set([a])) end,
        fun() -> sofs:substitution(fun(_) -> id(x) end, sofs:set([a])) end,
        fun() -> sofs:substitution(fun(_) -> throw(id(marker)) end, sofs:set([a])) end,
        fun() -> sofs:family_specification(fun(_) -> id('maybe') end, sofs:family([{a, []}])) end,
        fun() ->
            sofs:family_projection(id({external, fun(X) -> X end}), sofs:family([{a, []}]))
        end,
        fun() -> sofs:projection({external, fun(_) -> id(0) end}, sofs:relation([{a, b}])) end,
        fun() ->
            sofs:projection({external, fun(_) -> erlang:error(id(probe)) end}, sofs:relation([]))
        end,
        fun() -> sofs:projection({external, fun({A, _}) -> A end}, sofs:from_term(id([[a]]))) end,
        fun() ->
            sofs:restriction(
                {external, fun({A, _}) -> A end}, sofs:relation([]), sofs:set([], id([other]))
            )
        end,
        fun() ->
            sofs:substitution(
                fun(S) ->
                    case sofs:to_external(S) of
                        a -> sofs:from_term(a);
                        b -> sofs:set([b])
                    end
                end,
                sofs:set([a, b])
            )
        end
    ].

% Public record guards reject unrelated terms with function_clause rather than badarg.
wrong_records() ->
    [
        fun() -> sofs:union(id(x), id(y)) end,
        fun() -> sofs:intersection(id(x), sofs:set([])) end,
        fun() -> sofs:to_external(id(x)) end,
        fun() -> sofs:type(id(x)) end,
        fun() -> sofs:is_set(id(x)) end,
        fun() -> sofs:is_empty_set(id(x)) end,
        fun() -> sofs:domain(id(x)) end,
        fun() -> sofs:range(sofs:from_term(id(x))) end,
        fun() -> sofs:relation_to_family(id(x)) end,
        fun() -> sofs:family_to_relation(id(x)) end,
        fun() -> sofs:family_union(id(x), id(y)) end,
        fun() -> sofs:specification(fun(_) -> true end, id(x)) end,
        fun() -> sofs:substitution(id(1), id(x)) end,
        fun() -> sofs:partition_family(id(1), id(x)) end,
        fun() -> sofs:restriction(id(1), id(x), sofs:set([])) end,
        fun() -> sofs:family_projection(fun(X) -> X end, id(x)) end
    ].
