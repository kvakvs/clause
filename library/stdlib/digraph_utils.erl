%% Clause's digraph_utils (plan 21 step LIB12, docs/builtins.md#digraph): the traversals, orderings, components and
%% tree tests are runtime builtins reading a digraph's tables directly (OTP's walks, shared with module graph);
%% subgraph/2,3 and condensation/1, which build new digraphs, are Erlang over digraph.
-module(digraph_utils).
-export([
    arborescence_root/1,
    components/1,
    condensation/1,
    cyclic_strong_components/1,
    is_acyclic/1,
    is_arborescence/1,
    is_tree/1,
    loop_vertices/1,
    postorder/1,
    preorder/1,
    reachable/2,
    reachable_neighbours/2,
    reaching/2,
    reaching_neighbours/2,
    strong_components/1,
    subgraph/2,
    subgraph/3,
    topsort/1
]).

arborescence_root(_) -> clause_builtin_placeholder.
components(_) -> clause_builtin_placeholder.
cyclic_strong_components(_) -> clause_builtin_placeholder.
is_acyclic(_) -> clause_builtin_placeholder.
is_arborescence(_) -> clause_builtin_placeholder.
is_tree(_) -> clause_builtin_placeholder.
loop_vertices(_) -> clause_builtin_placeholder.
postorder(_) -> clause_builtin_placeholder.
preorder(_) -> clause_builtin_placeholder.
reachable(_, _) -> clause_builtin_placeholder.
reachable_neighbours(_, _) -> clause_builtin_placeholder.
reaching(_, _) -> clause_builtin_placeholder.
reaching_neighbours(_, _) -> clause_builtin_placeholder.
strong_components(_) -> clause_builtin_placeholder.
topsort(_) -> clause_builtin_placeholder.

%% subgraph(G, Vertices [, Options]): a new digraph of the given vertices of G and the edges of G between them;
%% options {type, inherit | DigraphType} and {keep_labels, boolean()}; badarg for any other.
subgraph(G, Vertices) ->
    subgraph(G, Vertices, []).

subgraph(G, Vertices, Options) ->
    try
        build_subgraph(G, Vertices, Options)
    catch
        throw:badarg -> erlang:error(badarg)
    end.

build_subgraph(G, Vertices, Options) ->
    {Type, Keep} = subgraph_options(Options, inherit, true),
    try digraph:new(graph_type(G, Type)) of
        Sub ->
            lists:foreach(fun(V) -> copy_vertex(G, Sub, V, Keep) end, Vertices),
            lists:foreach(
                fun(V) ->
                    lists:foreach(fun(E) -> copy_edge(G, Sub, E, Keep) end, digraph:out_edges(G, V))
                end,
                digraph:vertices(Sub)
            ),
            Sub
    catch
        error:badarg -> throw(badarg)
    end.

subgraph_options([{type, Type} | Rest], _, Keep) when Type =:= inherit; is_list(Type) ->
    subgraph_options(Rest, Type, Keep);
subgraph_options([{keep_labels, Keep} | Rest], Type, _) when is_boolean(Keep) ->
    subgraph_options(Rest, Type, Keep);
subgraph_options([], Type, Keep) ->
    {Type, Keep};
subgraph_options(_, _, _) ->
    throw(badarg).

%% The type of a new graph: inherit is G's cyclicity and protection.
graph_type(G, inherit) ->
    Info = digraph:info(G),
    {cyclicity, Cyclicity} = lists:keyfind(cyclicity, 1, Info),
    {protection, Protection} = lists:keyfind(protection, 1, Info),
    [Cyclicity, Protection];
graph_type(_, Type) ->
    Type.

copy_vertex(G, Sub, V, Keep) ->
    case digraph:vertex(G, V) of
        false -> ok;
        {_, _} when not Keep -> digraph:add_vertex(Sub, V);
        {_, Label} -> digraph:add_vertex(Sub, V, Label)
    end.

copy_edge(G, Sub, E, Keep) ->
    {_, From, To, Label} = digraph:edge(G, E),
    case digraph:vertex(Sub, To) of
        false -> ok;
        _ when not Keep -> digraph:add_edge(Sub, E, From, To, []);
        _ -> digraph:add_edge(Sub, E, From, To, Label)
    end.

%% condensation(G): a digraph of G's strong components, an edge from one component to another wherever G has an
%% edge between their vertices.
condensation(G) ->
    Components = digraph_utils:strong_components(G),
    Numbers = ets:new(condensation, []),
    ByNumber = ets:new(condensation, []),
    lists:foldl(
        fun(Component, N) ->
            lists:foreach(fun(V) -> true = ets:insert(Numbers, {V, N}) end, Component),
            true = ets:insert(ByNumber, {N, Component}),
            N + 1
        end,
        1,
        Components
    ),
    Condensed = build_subgraph(G, [], []),
    lists:foreach(
        fun(Component) -> condense(Component, G, Condensed, Numbers, ByNumber) end, Components
    ),
    ets:delete(Numbers),
    ets:delete(ByNumber),
    Condensed.

%% One component's vertex, and an edge to each other component its vertices have edges to.
condense(Component, G, Condensed, Numbers, ByNumber) ->
    Reached = ets:new(condense, []),
    lists:foreach(
        fun(V) ->
            lists:foreach(
                fun(Neighbour) ->
                    [{_, N}] = ets:lookup(Numbers, Neighbour),
                    ets:insert(Reached, {N})
                end,
                digraph:out_neighbours(G, V)
            )
        end,
        Component
    ),
    digraph:add_vertex(Condensed, Component),
    connect(ets:first(Reached), Reached, Component, Condensed, ByNumber),
    ets:delete(Reached).

connect('$end_of_table', _Reached, _Component, _Condensed, _ByNumber) ->
    ok;
connect(N, Reached, Component, Condensed, ByNumber) ->
    [{_, Other}] = ets:lookup(ByNumber, N),
    digraph:add_vertex(Condensed, Other),
    _ = [digraph:add_edge(Condensed, Component, Other) || Other =/= Component],
    connect(ets:next(Reached, N), Reached, Component, Condensed, ByNumber).
