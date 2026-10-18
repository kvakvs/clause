%% Clause's graph (plan 21 step LIB7B, docs/builtins.md#graph): OTP 29's functional graphs over OTP's #graph{}
%% record. The queries and traversals are runtime builtins with clause_builtin_placeholder bodies; the updates,
%% which build new maps, and the functions taking funs are Erlang over them.
-module(graph).
-export([
    add_edge/3,
    add_edge/4,
    add_vertex/1,
    add_vertex/2,
    add_vertex/3,
    arborescence_root/1,
    components/1,
    condensation/1,
    cyclic_strong_components/1,
    del_edge/2,
    del_edges/2,
    del_edges/3,
    del_path/3,
    del_vertex/2,
    del_vertices/2,
    edges/1,
    edges/2,
    edges/3,
    fold_vertices/3,
    get_cycle/2,
    get_path/3,
    get_short_cycle/2,
    get_short_path/3,
    has_edge/2,
    has_edge/3,
    has_path/3,
    has_vertex/2,
    in_degree/2,
    in_edges/2,
    in_neighbours/2,
    info/1,
    is_acyclic/1,
    is_arborescence/1,
    is_tree/1,
    loop_vertices/1,
    new/0,
    new/1,
    no_edges/1,
    no_vertices/1,
    out_degree/2,
    out_edges/2,
    out_neighbours/2,
    postorder/1,
    postorder/2,
    preorder/1,
    preorder/2,
    reachable/2,
    reachable_via_neighbours/2,
    reaching/2,
    reaching_via_neighbours/2,
    reverse_postorder/1,
    reverse_postorder/2,
    roots/1,
    sink_vertices/1,
    source_vertices/1,
    strong_components/1,
    subgraph/2,
    subgraph/3,
    topsort/1,
    vertex/2,
    vertex/3,
    vertices/1,
    vertices_with_labels/1
]).
-include("graph.types.hrl").

arborescence_root(_) -> clause_builtin_placeholder.
components(_) -> clause_builtin_placeholder.
cyclic_strong_components(_) -> clause_builtin_placeholder.
edges(_) -> clause_builtin_placeholder.
edges(_, _) -> clause_builtin_placeholder.
edges(_, _, _) -> clause_builtin_placeholder.
get_cycle(_, _) -> clause_builtin_placeholder.
get_path(_, _, _) -> clause_builtin_placeholder.
get_short_cycle(_, _) -> clause_builtin_placeholder.
get_short_path(_, _, _) -> clause_builtin_placeholder.
has_edge(_, _) -> clause_builtin_placeholder.
has_edge(_, _, _) -> clause_builtin_placeholder.
has_path(_, _, _) -> clause_builtin_placeholder.
has_vertex(_, _) -> clause_builtin_placeholder.
in_degree(_, _) -> clause_builtin_placeholder.
in_edges(_, _) -> clause_builtin_placeholder.
in_neighbours(_, _) -> clause_builtin_placeholder.
info(_) -> clause_builtin_placeholder.
is_acyclic(_) -> clause_builtin_placeholder.
is_arborescence(_) -> clause_builtin_placeholder.
is_tree(_) -> clause_builtin_placeholder.
loop_vertices(_) -> clause_builtin_placeholder.
no_edges(_) -> clause_builtin_placeholder.
no_vertices(_) -> clause_builtin_placeholder.
out_degree(_, _) -> clause_builtin_placeholder.
out_edges(_, _) -> clause_builtin_placeholder.
out_neighbours(_, _) -> clause_builtin_placeholder.
postorder(_) -> clause_builtin_placeholder.
postorder(_, _) -> clause_builtin_placeholder.
preorder(_) -> clause_builtin_placeholder.
preorder(_, _) -> clause_builtin_placeholder.
reachable(_, _) -> clause_builtin_placeholder.
reachable_via_neighbours(_, _) -> clause_builtin_placeholder.
reaching(_, _) -> clause_builtin_placeholder.
reaching_via_neighbours(_, _) -> clause_builtin_placeholder.
reverse_postorder(_) -> clause_builtin_placeholder.
reverse_postorder(_, _) -> clause_builtin_placeholder.
roots(_) -> clause_builtin_placeholder.
sink_vertices(_) -> clause_builtin_placeholder.
source_vertices(_) -> clause_builtin_placeholder.
strong_components(_) -> clause_builtin_placeholder.
topsort(_) -> clause_builtin_placeholder.
vertex(_, _) -> clause_builtin_placeholder.
vertex(_, _, _) -> clause_builtin_placeholder.
vertices(_) -> clause_builtin_placeholder.
vertices_with_labels(_) -> clause_builtin_placeholder.

%% An empty graph; the options cyclic (the default) and acyclic, the last one winning.
new() -> new([]).

new(Options) when is_list(Options) -> with_options(Options, #graph{}).

with_options([cyclic | Options], G) -> with_options(Options, G#graph{cyclic = true});
with_options([acyclic | Options], G) -> with_options(Options, G#graph{cyclic = false});
with_options([], G) -> G;
with_options(_, _) -> erlang:error(badarg).

%% A new vertex with the next free integer identifier, and the graph holding it.
add_vertex(#graph{next_vid = V} = G) -> {V, add_vertex(G#graph{next_vid = V + 1}, V)}.

add_vertex(G, V) -> add_vertex(G, V, []).

%% The vertex added, or its label replaced.
add_vertex(G, V, Label) ->
    #graph{vs = Vs} = G,
    G#graph{vs = Vs#{V => Label}}.

%% The vertex removed with every edge from or to it.
del_vertex(G, V) ->
    #graph{vs = Vs, in_es = InMap, out_es = OutMap} = G,
    Incoming = maps:get(V, InMap, []),
    Outgoing = maps:get(V, OutMap, []),
    NewOut = lists:foldl(
        fun({From, _, _} = E, M) -> drop_edge(From, E, M) end, maps:remove(V, OutMap), Incoming
    ),
    NewIn = lists:foldl(
        fun({_, To, _} = E, M) -> drop_edge(To, E, M) end, maps:remove(V, InMap), Outgoing
    ),
    G#graph{vs = maps:remove(V, Vs), in_es = NewIn, out_es = NewOut}.

del_vertices(G, [V | Vs]) -> del_vertices(del_vertex(G, V), Vs);
del_vertices(G, []) -> G.

add_edge(G, From, To) -> add_edge(G, From, To, []).

%% The edge {From, To, Label} added; an acyclic graph refuses an edge that closes a cycle.
add_edge(#graph{cyclic = true} = G, From, To, Label) ->
    insert_edge(G, From, To, Label);
add_edge(#graph{cyclic = false}, From, To, _Label) when From =:= To ->
    erlang:error({bad_edge, {From, To}});
add_edge(#graph{cyclic = false} = G, From, To, Label) ->
    case graph:get_path(G, To, From) of
        false -> insert_edge(G, From, To, Label);
        _ -> erlang:error({bad_edge, {From, To}})
    end.

insert_edge(#graph{vs = Vs} = G, From, To, Label) ->
    maps:is_key(From, Vs) orelse erlang:error({bad_vertex, From}),
    maps:is_key(To, Vs) orelse erlang:error({bad_vertex, To}),
    #graph{in_es = InMap, out_es = OutMap} = G,
    Edge = {From, To, Label},
    G#graph{in_es = keep_edge(To, Edge, InMap), out_es = keep_edge(From, Edge, OutMap)}.

% A vertex's edge set with one more edge.
keep_edge(V, Edge, Map) -> Map#{V => ordsets:add_element(Edge, maps:get(V, Map, []))}.

% A vertex's edge set without an edge; a vertex without any edge set keeps none.
drop_edge(V, Edge, Map) ->
    case maps:find(V, Map) of
        error -> Map;
        {ok, Edges} -> Map#{V := Edges -- [Edge]}
    end.

%% The edge removed.
del_edge(G, {From, To, _} = Edge) ->
    #graph{in_es = InMap, out_es = OutMap} = G,
    G#graph{in_es = drop_edge(To, Edge, InMap), out_es = drop_edge(From, Edge, OutMap)}.

del_edges(G, Edges) when is_list(Edges) ->
    lists:foldl(fun(E, Acc) -> del_edge(Acc, E) end, G, Edges).

%% Every edge from V1 to V2 removed.
del_edges(G, V1, V2) -> del_edges(G, graph:edges(G, V1, V2)).

%% Paths from V1 to V2 removed, one edge group of each found path at a time, until none is left.
del_path(G, V1, V2) ->
    case graph:get_path(G, V1, V2) of
        false -> G;
        Path -> del_path(drop_path(G, Path), V1, V2)
    end.

drop_path(G, [V1, V2 | Vs]) -> drop_path(del_edges(G, V1, V2), [V2 | Vs]);
drop_path(G, _) -> G.

%% Fun(Vertex, Label, Acc) over the vertices in map order.
fold_vertices(#graph{vs = Vs}, Fun, Acc) -> maps:fold(Fun, Acc, Vs).

%% The subgraph of the vertices of Vs the graph has, and the edges between them.
subgraph(G, Vs) -> subgraph(G, Vs, []).

subgraph(G, Vs, Options) ->
    try sub_options(Options, inherit, true, G, Vs) of
        SubGraph -> SubGraph
    catch
        throw:badarg -> erlang:error(badarg)
    end.

% {type, inherit | Options} and {keep_labels, Boolean}; anything else is badarg.
sub_options([{type, Type} | Options], _, Keep, G, Vs) when Type =:= inherit; is_list(Type) ->
    sub_options(Options, Type, Keep, G, Vs);
sub_options([{keep_labels, Keep} | Options], Type, _, G, Vs) when is_boolean(Keep) ->
    sub_options(Options, Type, Keep, G, Vs);
sub_options([], inherit, Keep, G, Vs) ->
    [{cyclicity, Cyclicity}] = graph:info(G),
    sub_build(G, Vs, [Cyclicity], Keep);
sub_options([], Type, Keep, G, Vs) ->
    sub_build(G, Vs, Type, Keep);
sub_options(_, _, _, _, _) ->
    throw(badarg).

sub_build(G, Vs, Type, Keep) ->
    Empty =
        try
            new(Type)
        catch
            error:badarg -> throw(badarg)
        end,
    WithVertices = lists:foldl(fun(V, S) -> sub_vertex(G, V, S, Keep) end, Empty, Vs),
    lists:foldl(
        fun(V, S) ->
            lists:foldl(fun(E, S1) -> sub_edge(E, S1, Keep) end, S, graph:out_edges(G, V))
        end,
        WithVertices,
        graph:vertices(WithVertices)
    ).

sub_vertex(G, V, S, Keep) ->
    case graph:has_vertex(G, V) of
        false -> S;
        true when Keep -> add_vertex(S, V, graph:vertex(G, V));
        true -> add_vertex(S, V)
    end.

sub_edge({V1, V2, Label}, S, Keep) ->
    case graph:has_vertex(S, V2) of
        false -> S;
        true when Keep -> add_edge(S, V1, V2, Label);
        true -> add_edge(S, V1, V2)
    end.

%% The graph of the strong components: a vertex per component (its vertex list) and an edge between two components
%% when an edge joins their vertices.
condensation(G) ->
    Components = graph:strong_components(G),
    {_, Index, ByIndex} = lists:foldl(fun number_component/2, {1, #{}, #{}}, Components),
    Empty = sub_options([], inherit, true, G, []),
    lists:foldl(fun(C, S) -> condense(C, G, S, Index, ByIndex) end, Empty, Components).

number_component(Component, {N, Index, ByIndex}) ->
    {N + 1, lists:foldl(fun(V, M) -> M#{V => N} end, Index, Component), ByIndex#{N => Component}}.

condense(Component, G, S, Index, ByIndex) ->
    Targets = lists:foldl(
        fun(V, T) ->
            lists:foldl(
                fun(N, T1) -> T1#{maps:get(N, Index) => true} end, T, graph:out_neighbours(G, V)
            )
        end,
        #{},
        Component
    ),
    maps:fold(
        fun(I, true, S0) ->
            Target = maps:get(I, ByIndex),
            S1 = add_vertex(S0, Target),
            case Target =/= Component of
                true -> add_edge(S1, Component, Target);
                false -> S1
            end
        end,
        add_vertex(S, Component),
        Targets
    ).
