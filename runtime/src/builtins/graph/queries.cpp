#include "../collections/collections.hpp"
#include "graph.hpp"
#include <array>

// The graph builtins (plan 21 step LIB7B, docs/builtins.md#graph): vertex and edge queries in OTP's orders (folds
// over the maps prepend, so their lists come in reverse map order), traversals and paths.
namespace clause::runtime::builtins::graph {
namespace {
Term list(ProcessContext &context, const Vertices &items) { return Terms(context).list(items); }

Term boolean(ProcessContext &context, bool value) { return Terms(context).boolean(value); }

Term count(ProcessContext &context, std::size_t value) {
    return Terms(context).integer(static_cast<std::int64_t>(value));
}

Term no_vertices(ProcessContext &context, const GraphArg &graph) {
    return count(context, Graph(graph).labels().map_size_unchecked());
}

Term vertices(ProcessContext &context, const GraphArg &graph) { return list(context, Graph(graph).vertices()); }

Term vertices_with_labels(ProcessContext &context, const GraphArg &graph) {
    const Terms terms(context);
    Vertices pairs;
    for (const auto &[vertex, label] : need(Graph(graph).labels().map_entries())) {
        const std::array pair{vertex, label};
        pairs.push_back(terms.tuple(pair));
    }
    return terms.list(pairs);
}

Term has_vertex(ProcessContext &context, const GraphArg &graph, const VertexArg &vertex) {
    return boolean(context, need(Graph(graph).labels().map_contains(vertex.term)));
}

// vertex(G, V): its label; {badkey, V} for a vertex the graph lacks.
Term vertex2(ProcessContext & /*context*/, const GraphArg &graph, const VertexArg &vertex) {
    const auto label = need(Graph(graph).labels().map_find(vertex.term));
    if (!label) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badkey, .payload = vertex.term.word()};
    }
    return *label;
}

Term vertex3(ProcessContext & /*context*/, const GraphArg &graph, const VertexArg &vertex, const OtherArg &fallback) {
    return need(Graph(graph).labels().map_find(vertex.term)).value_or(fallback.term);
}

// source_vertices/1 and sink_vertices/1: vertices without incoming or outgoing edges, last vertex first.
template <bool Source> Term ends_of(ProcessContext &context, const GraphArg &graph_arg) {
    const Graph graph(graph_arg);
    Vertices found;
    for (const auto &vertex : graph.vertices()) {
        if ((Source ? graph.in_edges(vertex) : graph.out_edges(vertex)).empty()) {
            found.insert(found.begin(), vertex);
        }
    }
    return list(context, found);
}

Term no_edges(ProcessContext &context, const GraphArg &graph) {
    std::size_t total = 0;
    for (const auto &[vertex, edges] : need(Graph(graph).out_map().map_entries())) {
        total += elements(edges).size();
    }
    return count(context, total);
}

// edges(G): every edge, the edges of the last vertex first.
Term edges1(ProcessContext &context, const GraphArg &graph) {
    Vertices all;
    for (const auto &[vertex, edges] : need(Graph(graph).out_map().map_entries())) {
        const auto items = elements(edges);
        all.insert(all.begin(), items.begin(), items.end());
    }
    return list(context, all);
}

// edges(G, V): its outgoing edges, then its incoming ones.
Term edges2(ProcessContext &context, const GraphArg &graph_arg, const VertexArg &vertex) {
    const Graph graph(graph_arg);
    auto all = graph.out_edges(vertex.term);
    const auto in = graph.in_edges(vertex.term);
    all.insert(all.end(), in.begin(), in.end());
    return list(context, all);
}

// The edges from V1 to V2, any label.
Vertices between(const Graph &graph, const Term &from, const Term &to) {
    Vertices found;
    for (const auto &edge : graph.out_edges(from)) {
        if (exact(edge.tuple_element_unchecked(0), from) && exact(edge.tuple_element_unchecked(1), to)) {
            found.push_back(edge);
        }
    }
    return found;
}

Term edges3(ProcessContext &context, const GraphArg &graph, const VertexArg &from, const OtherArg &to) {
    return list(context, between(Graph(graph), from.term, to.term));
}

// has_edge(G, {V1, V2, Label}): whether the edge is among V1's, compared with ==.
Term has_edge2(ProcessContext &context, const GraphArg &graph_arg, const VertexArg &edge) {
    const Graph graph(graph_arg);
    if (!edge.term.is_tuple() || edge.term.tuple_size_unchecked() != 3) {
        lists::no_clause();
    }
    const auto edges = graph.out_edges(edge.term.tuple_element_unchecked(0));
    const bool found =
        std::ranges::any_of(edges, [&](const Term &other) { return collections::compare(other, edge.term) == 0; });
    return boolean(context, found);
}

Term has_edge3(ProcessContext &context, const GraphArg &graph, const VertexArg &from, const OtherArg &to) {
    return boolean(context, !between(Graph(graph), from.term, to.term).empty());
}

template <bool In> Term degree(ProcessContext &context, const GraphArg &graph, const VertexArg &vertex) {
    return count(context, (In ? Graph(graph).in_edges(vertex.term) : Graph(graph).out_edges(vertex.term)).size());
}

template <bool In> Term edges_at(ProcessContext &context, const GraphArg &graph, const VertexArg &vertex) {
    return list(context, In ? Graph(graph).in_edges(vertex.term) : Graph(graph).out_edges(vertex.term));
}

template <bool In> Term neighbours(ProcessContext &context, const GraphArg &graph, const VertexArg &vertex) {
    return list(context, In ? Graph(graph).in_neighbours(vertex.term) : Graph(graph).out_neighbours(vertex.term));
}

Term loop_vertices(ProcessContext &context, const GraphArg &graph_arg) {
    const Graph graph(graph_arg);
    Vertices found;
    for (const auto &vertex : graph.vertices()) {
        if (graph.reflexive(vertex)) {
            found.push_back(vertex);
        }
    }
    return list(context, found);
}

Term info(ProcessContext &context, const GraphArg &graph) {
    const Terms terms(context);
    const std::array pair{terms.atom("cyclicity"), terms.atom(Graph(graph).cyclic() ? "cyclic" : "acyclic")};
    const std::array items{terms.tuple(pair)};
    return terms.list(items);
}

constexpr std::array GRAPH_QUERY_BUILTINS{
    typed_entry<no_vertices>("graph", "no_vertices"),
    typed_entry<vertices>("graph", "vertices"),
    typed_entry<vertices_with_labels>("graph", "vertices_with_labels"),
    typed_entry<has_vertex>("graph", "has_vertex"),
    typed_entry<vertex2>("graph", "vertex"),
    typed_entry<vertex3>("graph", "vertex"),
    typed_entry<ends_of<true>>("graph", "source_vertices"),
    typed_entry<ends_of<false>>("graph", "sink_vertices"),
    typed_entry<no_edges>("graph", "no_edges"),
    typed_entry<edges1>("graph", "edges"),
    typed_entry<edges2>("graph", "edges"),
    typed_entry<edges3>("graph", "edges"),
    typed_entry<has_edge2>("graph", "has_edge"),
    typed_entry<has_edge3>("graph", "has_edge"),
    typed_entry<degree<true>>("graph", "in_degree"),
    typed_entry<degree<false>>("graph", "out_degree"),
    typed_entry<edges_at<true>>("graph", "in_edges"),
    typed_entry<edges_at<false>>("graph", "out_edges"),
    typed_entry<neighbours<true>>("graph", "in_neighbours"),
    typed_entry<neighbours<false>>("graph", "out_neighbours"),
    typed_entry<loop_vertices>("graph", "loop_vertices"),
    typed_entry<info>("graph", "info"),
};

} // namespace
} // namespace clause::runtime::builtins::graph

namespace clause::runtime {
std::span<const BuiltinEntry> graph_query_builtins() noexcept { return builtins::graph::GRAPH_QUERY_BUILTINS; }
} // namespace clause::runtime
