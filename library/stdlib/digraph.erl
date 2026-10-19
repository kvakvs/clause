%% Clause's digraph (plan 21 step LIB12, docs/builtins.md#digraph): OTP 29's #digraph{} record over three ETS tables,
%% a set of {Vertex, Label}, a set of {Edge, From, To, Label} and a bag of {{out, From}, Edge} and {{in, To}, Edge}
%% that also holds the '$vid' and '$eid' counters of new vertex and edge names. The updates and reads are Erlang over
%% the ets builtins; the path searches are runtime builtins reading the tables directly.
-module(digraph).
-export([
    add_edge/3,
    add_edge/4,
    add_edge/5,
    add_vertex/1,
    add_vertex/2,
    add_vertex/3,
    del_edge/2,
    del_edges/2,
    del_path/3,
    del_vertex/2,
    del_vertices/2,
    delete/1,
    edge/2,
    edges/1,
    edges/2,
    get_cycle/2,
    get_path/3,
    get_short_cycle/2,
    get_short_path/3,
    in_degree/2,
    in_edges/2,
    in_neighbours/2,
    info/1,
    new/0,
    new/1,
    no_edges/1,
    no_vertices/1,
    out_degree/2,
    out_edges/2,
    out_neighbours/2,
    sink_vertices/1,
    source_vertices/1,
    vertex/2,
    vertices/1
]).
-include("digraph.types.hrl").

new() ->
    new([]).

%% new(Type): the last protection option and the first cyclicity option count, as in OTP.
new(Type) ->
    case options(Type, protected, undefined) of
        {Access, Cyclic} ->
            Vertices = ets:new(vertices, [set, Access]),
            Edges = ets:new(edges, [set, Access]),
            Neighbours = ets:new(neighbours, [bag, Access]),
            ets:insert(Neighbours, [{'$vid', 0}, {'$eid', 0}]),
            #digraph{vtab = Vertices, etab = Edges, ntab = Neighbours, cyclic = Cyclic =/= false};
        error ->
            erlang:error(badarg)
    end.

options([acyclic | Rest], Access, undefined) ->
    options(Rest, Access, false);
options([cyclic | Rest], Access, undefined) ->
    options(Rest, Access, true);
options([Cyclicity | Rest], Access, Cyclic) when Cyclicity =:= acyclic; Cyclicity =:= cyclic ->
    options(Rest, Access, Cyclic);
options([Access | Rest], _, Cyclic) when Access =:= protected; Access =:= private ->
    options(Rest, Access, Cyclic);
options([], Access, Cyclic) ->
    {Access, Cyclic};
options(_, _, _) ->
    error.

delete(G) ->
    ets:delete(G#digraph.vtab),
    ets:delete(G#digraph.etab),
    ets:delete(G#digraph.ntab).

info(G) ->
    Cyclicity =
        case G#digraph.cyclic of
            true -> cyclic;
            false -> acyclic
        end,
    Protection = ets:info(G#digraph.vtab, protection),
    Memory =
        ets:info(G#digraph.vtab, memory) + ets:info(G#digraph.etab, memory) +
            ets:info(G#digraph.ntab, memory),
    [{cyclicity, Cyclicity}, {memory, Memory}, {protection, Protection}].

add_vertex(G) ->
    insert_vertex(G, new_name(G, '$vid', '$v'), []).

add_vertex(G, V) ->
    insert_vertex(G, V, []).

add_vertex(G, V, Label) ->
    insert_vertex(G, V, Label).

insert_vertex(G, V, Label) ->
    ets:insert(G#digraph.vtab, {V, Label}),
    V.

%% The next ['$v' | N] or ['$e' | N] name, counted in the neighbour table.
new_name(G, Counter, Tag) ->
    Neighbours = G#digraph.ntab,
    [{Counter, N}] = ets:lookup(Neighbours, Counter),
    true = ets:delete(Neighbours, Counter),
    true = ets:insert(Neighbours, {Counter, N + 1}),
    [Tag | N].

del_vertex(G, V) ->
    del_neighbour_edges(G, ets:lookup(G#digraph.ntab, {in, V})),
    del_neighbour_edges(G, ets:lookup(G#digraph.ntab, {out, V})),
    ets:delete(G#digraph.vtab, V).

del_vertices(G, [V | Vs]) ->
    del_vertex(G, V),
    del_vertices(G, Vs);
del_vertices(#digraph{}, []) ->
    true.

del_neighbour_edges(G, [{_, E} | Rest]) ->
    case ets:lookup(G#digraph.etab, E) of
        [{E, From, To, _}] -> remove_edge(G, E, From, To);
        [] -> ok
    end,
    del_neighbour_edges(G, Rest);
del_neighbour_edges(#digraph{}, []) ->
    true.

vertex(G, V) ->
    case ets:lookup(G#digraph.vtab, V) of
        [] -> false;
        [Vertex] -> Vertex
    end.

no_vertices(G) ->
    ets:info(G#digraph.vtab, size).

vertices(G) ->
    ets:select(G#digraph.vtab, [{{'$1', '_'}, [], ['$1']}]).

%% The vertices without incoming (outgoing) edges, in the reverse of vertices/1's order, as OTP's fold gives them.
source_vertices(G) ->
    unconnected(G, in).

sink_vertices(G) ->
    unconnected(G, out).

unconnected(G, Side) ->
    lists:foldl(
        fun(V, Found) ->
            case ets:member(G#digraph.ntab, {Side, V}) of
                true -> Found;
                false -> [V | Found]
            end
        end,
        [],
        vertices(G)
    ).

in_degree(G, V) ->
    length(ets:lookup(G#digraph.ntab, {in, V})).

out_degree(G, V) ->
    length(ets:lookup(G#digraph.ntab, {out, V})).

%% The other ends of a vertex's edges, the last edge first, as OTP collects them.
in_neighbours(G, V) ->
    ends(G, ets:lookup(G#digraph.ntab, {in, V}), 2, []).

out_neighbours(G, V) ->
    ends(G, ets:lookup(G#digraph.ntab, {out, V}), 3, []).

ends(G, [{_, E} | Rest], Position, Found) ->
    ends(G, Rest, Position, [ets:lookup_element(G#digraph.etab, E, Position) | Found]);
ends(_, [], _, Found) ->
    Found.

in_edges(G, V) ->
    [E || {{in, _}, E} <- ets:lookup(G#digraph.ntab, {in, V})].

out_edges(G, V) ->
    [E || {{out, _}, E} <- ets:lookup(G#digraph.ntab, {out, V})].

add_edge(G, From, To) ->
    add_new_edge(G, new_name(G, '$eid', '$e'), From, To, []).

add_edge(G, From, To, Label) ->
    add_new_edge(G, new_name(G, '$eid', '$e'), From, To, Label).

add_edge(G, E, From, To, Label) ->
    add_new_edge(G, E, From, To, Label).

%% Both ends must be vertices, an edge of that name must not join other vertices, and an acyclic graph must stay so.
add_new_edge(G, E, From, To, Label) ->
    case {ets:member(G#digraph.vtab, From), ets:member(G#digraph.vtab, To)} of
        {false, _} -> {error, {bad_vertex, From}};
        {_, false} -> {error, {bad_vertex, To}};
        _ -> checked_edge(G, E, From, To, Label)
    end.

checked_edge(G, E, From, To, Label) ->
    case ets:lookup(G#digraph.etab, E) of
        [{E, OldFrom, OldTo, _}] when OldFrom =/= From; OldTo =/= To ->
            {error, {bad_edge, [From, To]}};
        _ when G#digraph.cyclic =:= false ->
            acyclic_edge(G, E, From, To, Label);
        _ ->
            insert_edge(G, E, From, To, Label)
    end.

acyclic_edge(_G, _E, From, To, _Label) when From =:= To ->
    {error, {bad_edge, [From, To]}};
acyclic_edge(G, E, From, To, Label) ->
    case digraph:get_path(G, To, From) of
        false -> insert_edge(G, E, From, To, Label);
        Path -> {error, {bad_edge, Path}}
    end.

insert_edge(G, E, From, To, Label) ->
    ets:insert(G#digraph.ntab, [{{out, From}, E}, {{in, To}, E}]),
    ets:insert(G#digraph.etab, {E, From, To, Label}),
    E.

del_edge(G, E) ->
    del_edges(G, [E]).

del_edges(G, [E | Es]) ->
    case ets:lookup(G#digraph.etab, E) of
        [{E, From, To, _}] -> remove_edge(G, E, From, To);
        [] -> ok
    end,
    del_edges(G, Es);
del_edges(#digraph{}, []) ->
    true.

remove_edge(G, E, From, To) ->
    ets:select_delete(G#digraph.ntab, [{{{in, To}, E}, [], [true]}, {{{out, From}, E}, [], [true]}]),
    ets:delete(G#digraph.etab, E).

%% del_path(G, V1, V2): every edge of each path found is removed until none is left.
del_path(G, From, To) ->
    case digraph:get_path(G, From, To) of
        false ->
            true;
        Path ->
            remove_path(G, Path),
            del_path(G, From, To)
    end.

%% Remove every edge from each vertex of a path to the next.
remove_path(G, [From, To | Rest]) ->
    lists:foreach(
        fun(E) ->
            case ets:lookup(G#digraph.etab, E) of
                [{E, From, To, _}] -> remove_edge(G, E, From, To);
                _ -> ok
            end
        end,
        out_edges(G, From)
    ),
    remove_path(G, [To | Rest]);
remove_path(_, _) ->
    true.

no_edges(G) ->
    ets:info(G#digraph.etab, size).

edges(G) ->
    ets:select(G#digraph.etab, [{{'$1', '_', '_', '_'}, [], ['$1']}]).

edges(G, V) ->
    ets:select(G#digraph.ntab, [{{{out, V}, '$1'}, [], ['$1']}, {{{in, V}, '$1'}, [], ['$1']}]).

edge(G, E) ->
    case ets:lookup(G#digraph.etab, E) of
        [] -> false;
        [Edge] -> Edge
    end.

%% The path searches: runtime builtins over the tables (depth first with OTP's prune counter, breadth first).
get_path(_, _, _) -> clause_builtin_placeholder.
get_cycle(_, _) -> clause_builtin_placeholder.
get_short_path(_, _, _) -> clause_builtin_placeholder.
get_short_cycle(_, _) -> clause_builtin_placeholder.
