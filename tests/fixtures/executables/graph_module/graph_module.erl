-module(graph_module).
-export([main/1]).

%% OTP 29's functional graph module (plan 21 step LIB7B): construction, vertex and edge queries and updates, paths
%% and cycles, the traversals and orderings, components, condensation and subgraphs, acyclic graphs and the errors.
%% Vertices are small integers and atoms below 32 per graph, so map iteration order is the same everywhere.
main(_) ->
    basics(),
    edges(),
    deletion(),
    paths(),
    traversals(),
    components(),
    trees(),
    acyclic(),
    errors(),
    io:format("graph_module: ok~n").

sample() ->
    G0 = graph:new(),
    G1 = lists:foldl(fun(V, G) -> graph:add_vertex(G, V) end, G0, [1, 2, 3, 4, 5, 6]),
    G2 = graph:add_vertex(G1, 7, seven),
    lists:foldl(
        fun({A, B}, G) -> graph:add_edge(G, A, B) end,
        G2,
        [{1, 2}, {2, 3}, {3, 1}, {3, 4}, {4, 5}, {5, 4}, {6, 6}, {6, 7}]
    ).

basics() ->
    G = sample(),
    {V8, G8} = graph:add_vertex(graph:new()),
    {V9, G9} = graph:add_vertex(G8),
    show(basics, [
        graph:info(G),
        graph:info(graph:new([acyclic])),
        graph:info(graph:new([cyclic, acyclic])),
        graph:no_vertices(G),
        graph:no_edges(G),
        lists:sort(graph:vertices(G)),
        lists:sort(graph:vertices_with_labels(G)),
        graph:vertex(G, 7),
        graph:vertex(G, 1),
        graph:vertex(G, 99, default),
        graph:has_vertex(G, 3),
        graph:has_vertex(G, 99),
        lists:sort(graph:source_vertices(G)),
        lists:sort(graph:sink_vertices(G)),
        lists:sort(graph:source_vertices(graph:add_vertex(G, lonely))),
        {V8, V9, lists:sort(graph:vertices(G9))},
        graph:vertex(graph:add_vertex(G, 7, relabeled), 7),
        graph:fold_vertices(G, fun(V, L, Acc) -> [{V, L} | Acc] end, []) =:=
            lists:reverse(lists:sort(graph:vertices_with_labels(G))) orelse
            lists:sort(graph:fold_vertices(G, fun(V, L, Acc) -> [{V, L} | Acc] end, []))
    ]).

edges() ->
    G = graph:add_edge(graph:add_edge(sample(), 1, 2, heavy), 1, 2, light),
    show(edges, [
        lists:sort(graph:edges(G)),
        lists:sort(graph:edges(G, 1)),
        lists:sort(graph:edges(G, 1, 2)),
        graph:edges(G, 2, 1),
        graph:edges(G, 99, 1),
        graph:has_edge(G, {1, 2, heavy}),
        graph:has_edge(G, {1, 2, medium}),
        graph:has_edge(G, 1, 2),
        graph:has_edge(G, 2, 1),
        graph:has_edge(G, 99, 1),
        graph:in_degree(G, 2),
        graph:out_degree(G, 1),
        graph:in_degree(G, 99),
        lists:sort(graph:in_edges(G, 4)),
        lists:sort(graph:out_edges(G, 1)),
        lists:sort(graph:in_neighbours(G, 4)),
        lists:sort(graph:out_neighbours(G, 6)),
        graph:out_neighbours(G, 99),
        lists:sort(graph:loop_vertices(G))
    ]).

deletion() ->
    G = sample(),
    show(deletion, [
        lists:sort(graph:edges(graph:del_edge(G, {1, 2, []}))),
        lists:sort(graph:edges(graph:del_edge(G, {1, 2, nolabel}))),
        lists:sort(graph:edges(graph:del_edges(G, [{1, 2, []}, {6, 6, []}]))),
        lists:sort(graph:edges(graph:del_edges(graph:add_edge(G, 1, 2, x), 1, 2))),
        lists:sort(graph:edges(graph:del_vertex(G, 3))),
        lists:sort(graph:vertices(graph:del_vertex(G, 3))),
        lists:sort(graph:vertices(graph:del_vertices(G, [1, 2, 99]))),
        lists:sort(graph:edges(graph:del_path(G, 1, 4))),
        lists:sort(graph:edges(graph:del_path(G, 4, 1)))
    ]).

paths() ->
    G = sample(),
    show(paths, [
        graph:get_path(G, 1, 5),
        graph:get_path(G, 5, 1),
        graph:get_path(G, 1, 1),
        graph:get_short_path(G, 1, 5),
        graph:get_short_path(G, 5, 1),
        graph:get_short_path(G, 1, 1),
        graph:get_cycle(G, 1),
        graph:get_cycle(G, 6),
        graph:get_cycle(G, 7),
        graph:get_short_cycle(G, 4),
        graph:get_short_cycle(G, 7),
        graph:has_path(G, 1, 5),
        graph:has_path(G, 5, 1),
        graph:has_path(G, 7, 7)
    ]).

traversals() ->
    G = sample(),
    show(traversals, [
        graph:preorder(G),
        graph:postorder(G),
        graph:reverse_postorder(G),
        graph:preorder(G, [4, 1]),
        graph:postorder(G, [6]),
        graph:reverse_postorder(G, [3]),
        graph:topsort(G),
        graph:roots(G),
        lists:sort(graph:reachable(G, [4])),
        lists:sort(graph:reachable_via_neighbours(G, [4])),
        lists:sort(graph:reachable_via_neighbours(G, [7])),
        lists:sort(graph:reaching(G, [3])),
        lists:sort(graph:reaching_via_neighbours(G, [6])),
        lists:sort(graph:reaching_via_neighbours(G, [1]))
    ]).

components() ->
    G = sample(),
    show(components, [
        sorted(graph:components(G)),
        sorted(graph:strong_components(G)),
        sorted(graph:cyclic_strong_components(G)),
        lists:sort(graph:vertices(graph:condensation(G))),
        lists:sort(graph:edges(graph:condensation(G))),
        lists:sort(graph:vertices_with_labels(graph:subgraph(G, [1, 2, 7, 99]))),
        lists:sort(graph:edges(graph:subgraph(G, [1, 2, 3]))),
        lists:sort(graph:vertices_with_labels(graph:subgraph(G, [6, 7], [{keep_labels, false}]))),
        graph:info(graph:subgraph(G, [1], [{type, [acyclic]}])),
        catching(fun() -> graph:subgraph(G, [1], [{type, bad}]) end),
        catching(fun() -> graph:subgraph(G, [1], [bad]) end)
    ]).

trees() ->
    T = lists:foldl(
        fun({A, B}, G) -> graph:add_edge(G, A, B) end,
        lists:foldl(fun(V, G) -> graph:add_vertex(G, V) end, graph:new(), [a, b, c, d]),
        [{a, b}, {a, c}, {c, d}]
    ),
    show(trees, [
        graph:is_tree(T),
        graph:is_arborescence(T),
        graph:arborescence_root(T),
        graph:is_acyclic(T),
        graph:is_tree(sample()),
        graph:arborescence_root(sample()),
        graph:is_acyclic(sample()),
        graph:is_tree(graph:new()),
        graph:arborescence_root(graph:add_vertex(graph:new(), x))
    ]).

acyclic() ->
    G0 = lists:foldl(fun(V, G) -> graph:add_vertex(G, V) end, graph:new([acyclic]), [1, 2, 3]),
    G1 = graph:add_edge(graph:add_edge(G0, 1, 2), 2, 3),
    show(acyclic, [
        lists:sort(graph:edges(G1)),
        catching(fun() -> graph:add_edge(G1, 3, 1) end),
        catching(fun() -> graph:add_edge(G1, 2, 2) end),
        lists:sort(graph:edges(graph:add_edge(G1, 1, 3)))
    ]).

errors() ->
    G = sample(),
    show(errors, [
        catching(fun() -> graph:add_edge(G, 1, 99) end),
        catching(fun() -> graph:add_edge(G, 99, 1) end),
        catching(fun() -> graph:vertex(G, 99) end),
        catching(fun() -> graph:new(id(bad)) end),
        catching(fun() -> graph:new([id(bad)]) end),
        catching(fun() -> graph:del_edges(G, id(x)) end),
        catching(fun() -> graph:reachable(G, id(x)) end),
        catching(fun() -> graph:no_vertices(id(x)) end)
    ]).

sorted(Lists) -> lists:sort([lists:sort(L) || L <- Lists]).

catching(Fun) ->
    try Fun() of
        Value -> Value
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Values) -> io:format("~s: ~w~n", [Label, Values]).

id(X) -> X.
