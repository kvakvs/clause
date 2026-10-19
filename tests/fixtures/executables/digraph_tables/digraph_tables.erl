-module(digraph_tables).
-export([main/1, id/1]).

%% digraph, digraph_utils and sofs' digraph conversions (plan 21 step LIB12) over OTP's three-table digraphs. Vertex
%% lists follow the vertex table's order, which hash tables do not fix, so they are printed sorted; paths and
%% neighbour lists follow edge insertion and are printed as they come.
main(_) ->
    show(build, build()),
    show(edges, edges()),
    show(deletes, deletes()),
    show(acyclic, acyclic()),
    show(paths, paths()),
    show(utils, utils()),
    show(orders, orders()),
    show(trees, trees()),
    show(subgraphs, subgraphs()),
    show(condensation, condensation()),
    show(sofs, sofs_conversions()),
    show(errors, errors()),
    io:format("digraph_tables: ok~n").

id(X) -> X.

show(Label, Value) -> io:format("~p: ~w~n", [Label, Value]).

attempt(F) ->
    try F() of
        Value -> {ok, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

sorted(L) when is_list(L) -> lists:sort(L);
sorted(Other) -> Other.

%% a -> b -> c -> d, a -> c, d -> b (a cycle b c d), e alone, f -> f.
sample(Type) ->
    G = digraph:new(Type),
    [digraph:add_vertex(G, V) || V <- [a, b, c, d, e, f]],
    Edges = [{a, b}, {b, c}, {c, d}, {a, c}, {d, b}, {f, f}],
    [digraph:add_edge(G, X, Y) || {X, Y} <- Edges],
    G.

build() ->
    G = digraph:new(),
    V1 = digraph:add_vertex(G),
    V2 = digraph:add_vertex(G),
    V3 = digraph:add_vertex(G, x),
    V4 = digraph:add_vertex(G, y, label),
    V5 = digraph:add_vertex(G, y, relabel),
    E1 = digraph:add_edge(G, V1, V2),
    E2 = digraph:add_edge(G, V1, x, lab),
    E3 = digraph:add_edge(G, named, x, y, lab2),
    R = [
        V1,
        V2,
        V3,
        V4,
        V5,
        E1,
        E2,
        E3,
        digraph:no_vertices(G),
        digraph:no_edges(G),
        sorted(digraph:vertices(G)),
        sorted(digraph:edges(G)),
        digraph:vertex(G, y),
        digraph:vertex(G, nope),
        digraph:edge(G, named),
        digraph:edge(G, nope),
        [{K, V} || {K, V} <- digraph:info(G), K =/= memory],
        is_integer(proplists:get_value(memory, digraph:info(G))),
        [{K, V} || {K, V} <- digraph:info(digraph:new([acyclic, private])), K =/= memory],
        [{K, V} || {K, V} <- digraph:info(digraph:new([cyclic, acyclic, protected])), K =/= memory],
        element(1, G),
        tuple_size(G),
        digraph:delete(G),
        attempt(fun() -> digraph:no_vertices(G) end)
    ],
    R.

edges() ->
    G = sample([]),
    R = [
        digraph:out_neighbours(G, a),
        digraph:in_neighbours(G, c),
        digraph:out_neighbours(G, f),
        digraph:in_neighbours(G, e),
        length(digraph:out_edges(G, a)),
        length(digraph:in_edges(G, b)),
        digraph:out_degree(G, a),
        digraph:in_degree(G, b),
        digraph:out_degree(G, nope),
        length(digraph:edges(G, b)),
        sorted(digraph:source_vertices(G)),
        sorted(digraph:sink_vertices(G)),
        [element(2, digraph:edge(G, E)) || E <- digraph:in_edges(G, c)],
        digraph:add_edge(G, a, nope),
        digraph:add_edge(G, nope, a),
        digraph:add_edge(G, ['$e' | 0], c, d, []),
        digraph:add_edge(G, ['$e' | 0], a, b, relabelled),
        digraph:edge(G, ['$e' | 0])
    ],
    digraph:delete(G),
    R.

deletes() ->
    G = sample([]),
    R1 = digraph:del_edge(G, ['$e' | 1]),
    N1 = digraph:no_edges(G),
    R2 = digraph:del_edges(G, [['$e' | 2], nope]),
    N2 = digraph:no_edges(G),
    R3 = digraph:del_vertex(G, a),
    R4 = sorted(digraph:vertices(G)),
    R5 = digraph:no_edges(G),
    R6 = digraph:del_vertices(G, [f, nope]),
    R7 = digraph:no_edges(G),
    H = sample([]),
    digraph:add_edge(H, a, b),
    R8 = digraph:del_path(H, a, d),
    R9 = digraph:get_path(H, a, d),
    R10 = sorted(digraph:out_neighbours(H, a)),
    digraph:delete(G),
    digraph:delete(H),
    [R1, N1, R2, N2, R3, R4, R5, R6, R7, R8, R9, R10].

acyclic() ->
    G = digraph:new([acyclic]),
    [digraph:add_vertex(G, V) || V <- [a, b, c]],
    R = [
        digraph:add_edge(G, a, b) =/= false,
        digraph:add_edge(G, b, c) =/= false,
        digraph:add_edge(G, c, a),
        digraph:add_edge(G, a, a),
        digraph:add_edge(G, a, c) =/= false,
        digraph:no_edges(G)
    ],
    digraph:delete(G),
    R.

paths() ->
    G = sample([]),
    R = [
        digraph:get_path(G, a, d),
        digraph:get_path(G, d, a),
        digraph:get_path(G, b, b),
        digraph:get_cycle(G, b),
        digraph:get_cycle(G, f),
        digraph:get_cycle(G, a),
        digraph:get_cycle(G, e),
        digraph:get_short_path(G, a, d),
        digraph:get_short_path(G, a, a),
        digraph:get_short_path(G, c, b),
        digraph:get_short_cycle(G, b),
        digraph:get_short_cycle(G, f),
        digraph:get_short_cycle(G, e),
        digraph:get_path(G, nope, a)
    ],
    digraph:delete(G),
    R.

utils() ->
    G = sample([]),
    R = [
        lists:sort([lists:sort(C) || C <- digraph_utils:components(G)]),
        lists:sort([lists:sort(C) || C <- digraph_utils:strong_components(G)]),
        lists:sort([lists:sort(C) || C <- digraph_utils:cyclic_strong_components(G)]),
        sorted(digraph_utils:reachable([a], G)),
        sorted(digraph_utils:reachable_neighbours([a], G)),
        sorted(digraph_utils:reachable_neighbours([b], G)),
        sorted(digraph_utils:reaching([c], G)),
        sorted(digraph_utils:reaching_neighbours([a], G)),
        sorted(digraph_utils:reaching_neighbours([f], G)),
        sorted(digraph_utils:loop_vertices(G)),
        digraph_utils:topsort(G),
        digraph_utils:is_acyclic(G),
        digraph_utils:reachable([], G),
        sorted(digraph_utils:reachable([e, e], G))
    ],
    digraph:delete(G),
    R.

%% A diamond a -> b, a -> c, b -> d, c -> d: each order is valid where OTP's choice depends on hash order.
orders() ->
    G = digraph:new(),
    [digraph:add_vertex(G, V) || V <- [a, b, c, d]],
    [digraph:add_edge(G, X, Y) || {X, Y} <- [{a, b}, {a, c}, {b, d}, {c, d}]],
    Topsort = digraph_utils:topsort(G),
    Before = fun(X, Y, L) -> index(X, L) < index(Y, L) end,
    Edges = [{X, Y} || {X, Y} <- [{a, b}, {a, c}, {b, d}, {c, d}]],
    Pre = digraph_utils:preorder(G),
    Post = digraph_utils:postorder(G),
    R = [
        lists:all(fun({X, Y}) -> Before(X, Y, Topsort) end, Edges),
        lists:sort(Topsort),
        hd(Pre),
        lists:sort(Pre),
        lists:last(Post),
        lists:sort(Post),
        digraph_utils:is_acyclic(G),
        digraph:get_short_path(G, a, d),
        digraph:get_path(G, a, d)
    ],
    digraph:delete(G),
    R.

index(X, L) -> length(lists:takewhile(fun(Y) -> Y =/= X end, L)).

trees() ->
    Tree = digraph:new(),
    [digraph:add_vertex(Tree, V) || V <- [r, x, y, z]],
    [digraph:add_edge(Tree, X, Y) || {X, Y} <- [{r, x}, {r, y}, {y, z}]],
    Forest = digraph:new(),
    [digraph:add_vertex(Forest, V) || V <- [p, q]],
    Empty = digraph:new(),
    R = [
        digraph_utils:arborescence_root(Tree),
        digraph_utils:is_arborescence(Tree),
        digraph_utils:is_tree(Tree),
        digraph_utils:arborescence_root(Forest),
        digraph_utils:is_tree(Forest),
        digraph_utils:arborescence_root(Empty),
        digraph_utils:is_tree(Empty),
        digraph_utils:components(Empty),
        digraph_utils:topsort(Empty),
        digraph_utils:preorder(Empty),
        digraph_utils:postorder(Empty),
        digraph_utils:is_acyclic(Empty)
    ],
    [digraph:delete(D) || D <- [Tree, Forest, Empty]],
    R.

subgraphs() ->
    G = digraph:new([acyclic]),
    [digraph:add_vertex(G, V, {label, V}) || V <- [a, b, c, d]],
    [digraph:add_edge(G, X, Y, {X, Y}) || {X, Y} <- [{a, b}, {b, c}, {c, d}, {a, d}]],
    S1 = digraph_utils:subgraph(G, [a, b, d, nope]),
    S2 = digraph_utils:subgraph(G, [a, b], [{keep_labels, false}]),
    S3 = digraph_utils:subgraph(G, [b, c], [{type, [cyclic]}]),
    R = [
        sorted(digraph:vertices(S1)),
        digraph:vertex(S1, a),
        lists:sort([{digraph:edge(S1, E)} || E <- digraph:edges(S1)]),
        digraph:vertex(S2, a),
        lists:sort([digraph:edge(S2, E) || E <- digraph:edges(S2)]),
        [{K, V} || {K, V} <- digraph:info(S1), K =/= memory],
        [{K, V} || {K, V} <- digraph:info(S3), K =/= memory],
        attempt(fun() -> digraph_utils:subgraph(G, [a], [bogus]) end),
        attempt(fun() -> digraph_utils:subgraph(G, [a], [{type, bogus}]) end),
        attempt(fun() -> digraph_utils:subgraph(G, [a], [{type, [bogus]}]) end)
    ],
    [digraph:delete(D) || D <- [G, S1, S2, S3]],
    R.

condensation() ->
    G = sample([]),
    C = digraph_utils:condensation(G),
    Vertices = lists:sort([lists:sort(V) || V <- digraph:vertices(C)]),
    Edges = lists:sort([
        {lists:sort(X), lists:sort(Y)}
     || E <- digraph:edges(C), {_, X, Y, _} <- [digraph:edge(C, E)]
    ]),
    R = [Vertices, Edges, digraph_utils:is_acyclic(C) orelse loops],
    digraph:delete(G),
    digraph:delete(C),
    R.

sofs_conversions() ->
    F = sofs:family([{a, [b, c]}, {b, [c]}, {c, []}, {d, [a]}]),
    G = sofs:family_to_digraph(F),
    Back = sofs:digraph_to_family(G),
    Typed = sofs:digraph_to_family(G, [{atom, [atom]}]),
    Empty = sofs:family_to_digraph(sofs:empty_set()),
    Acyclic = sofs:family_to_digraph(F, [acyclic]),
    R = [
        sorted(digraph:vertices(G)),
        sofs:to_external(Back),
        sofs:type(Back),
        sofs:to_external(Typed),
        digraph:no_vertices(Empty),
        digraph:no_edges(Acyclic),
        attempt(fun() -> sofs:family_to_digraph(sofs:family([{a, [b]}, {b, [a]}]), [acyclic]) end),
        attempt(fun() -> sofs:family_to_digraph(sofs:set([a])) end),
        attempt(fun() -> sofs:family_to_digraph(F, [bogus]) end),
        attempt(fun() -> sofs:digraph_to_family(G, id(atom)) end),
        attempt(fun() -> sofs:digraph_to_family(id(not_a_graph)) end),
        attempt(fun() ->
            sofs:digraph_to_family(sofs:family_to_digraph(sofs:family([{1, [1.0, 2]}])))
        end)
    ],
    [digraph:delete(D) || D <- [G, Empty, Acyclic]],
    R.

errors() ->
    G = digraph:new([private]),
    Self = self(),
    Other = spawn(fun() -> Self ! {other, attempt(fun() -> digraph_utils:components(G) end)} end),
    receive
        {other, Result} -> ok
    end,
    Other,
    R = [
        attempt(fun() -> digraph:new(id(bogus)) end),
        attempt(fun() -> digraph:new(id([bogus])) end),
        attempt(fun() -> digraph_utils:components(id(not_a_graph)) end),
        attempt(fun() -> digraph:get_path(id({digraph, x, y, z, true}), a, b) end),
        attempt(fun() -> digraph_utils:reachable(id(a), G) end),
        Result
    ],
    digraph:delete(G),
    R.
