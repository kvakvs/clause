#include "graph.hpp"
#include <array>

// The traversal and path builtins of graph (plan 21 step LIB7B, docs/builtins.md#graph): components, orderings,
// reachability, roots, tree tests and the path searches, each returning OTP's lists in OTP's order.
namespace clause::runtime::builtins::graph {
namespace {
Term list(ProcessContext &context, const Vertices &items) { return Terms(context).list(items); }

Term boolean(ProcessContext &context, bool value) { return Terms(context).boolean(value); }

// A path, or false without one.
Term path_term(ProcessContext &context, const std::optional<Vertices> &path) {
    return path ? list(context, *path) : boolean(context, false);
}

// A list of vertex lists.
Term lists_term(ProcessContext &context, const std::vector<Vertices> &groups) {
    Vertices items;
    items.reserve(groups.size());
    for (const auto &group : groups) {
        items.push_back(list(context, group));
    }
    return list(context, items);
}

// The vertices of a list argument; function_clause unless it is a list (OTP's is_list guards).
Vertices starts_of(const OtherArg &starts) {
    if (!starts.term.is_list()) {
        lists::no_clause();
    }
    return elements(starts.term);
}

Term components(ProcessContext &context, const GraphArg &graph_arg) {
    const Graph graph(graph_arg);
    return lists_term(context, revpreorders(graph, Direction::both, graph.vertices(), true));
}

Term strong_components1(ProcessContext &context, const GraphArg &graph) {
    return lists_term(context, strong_components(Graph(graph)));
}

Term cyclic_strong_components1(ProcessContext &context, const GraphArg &graph) {
    return lists_term(context, cyclic_strong_components(Graph(graph)));
}

// reachable/2, reachable_via_neighbours/2, reaching/2, reaching_via_neighbours/2: the reversed preorders appended.
template <Direction Way, bool First>
Term reach(ProcessContext &context, const GraphArg &graph_arg, const OtherArg &starts) {
    const Graph graph(graph_arg);
    Vertices all;
    for (const auto &group : revpreorders(graph, Way, starts_of(starts), First)) {
        all.insert(all.end(), group.begin(), group.end());
    }
    return list(context, all);
}

Term roots1(ProcessContext &context, const GraphArg &graph) { return list(context, roots(Graph(graph))); }

Term is_acyclic(ProcessContext &context, const GraphArg &graph) {
    return boolean(context, cyclic_strong_components(Graph(graph)).empty());
}

// preorder(G, Vs): one depth-first walk over the start list, the vertices in the order first reached.
Term preorder2(ProcessContext &context, const GraphArg &graph_arg, const OtherArg &starts) {
    const Graph graph(graph_arg);
    Vertices order;
    Seen seen;
    // The vertices still to visit, the next one last.
    Vertices pending = starts_of(starts);
    std::ranges::reverse(pending);
    while (!pending.empty()) {
        const auto vertex = pending.back();
        pending.pop_back();
        if (seen.insert(vertex).second) {
            order.push_back(vertex);
            const auto next = graph.out_neighbours(vertex);
            pending.insert(pending.end(), next.rbegin(), next.rend());
        }
    }
    return list(context, order);
}

Term preorder1(ProcessContext &context, const GraphArg &graph) {
    return preorder2(context, graph, {list(context, roots(Graph(graph)))});
}

// postorder/2 and reverse_postorder/2.
template <bool Reverse> Term postorder2(ProcessContext &context, const GraphArg &graph, const OtherArg &starts) {
    auto order = reverse_postorder(Graph(graph), starts_of(starts));
    if (!Reverse) {
        std::ranges::reverse(order);
    }
    return list(context, order);
}

// postorder/1, reverse_postorder/1 and topsort/1: from the roots.
template <bool Reverse> Term postorder1(ProcessContext &context, const GraphArg &graph) {
    return postorder2<Reverse>(context, graph, {list(context, roots(Graph(graph)))});
}

std::size_t edge_count(const Graph &graph) {
    std::size_t total = 0;
    for (const auto &vertex : graph.vertices()) {
        total += graph.out_edges(vertex).size();
    }
    return total;
}

// The root of an arborescence: one edge fewer than vertices, one vertex without incoming edges, every other with
// exactly one.
std::optional<Term> root_of(const Graph &graph) {
    const auto vertices = graph.vertices();
    if (edge_count(graph) + 1 != vertices.size()) {
        return std::nullopt;
    }
    std::optional<Term> root;
    for (const auto &vertex : vertices) {
        const auto in = graph.in_edges(vertex).size();
        if (in == 0 && !root) {
            root = vertex;
        } else if (in != 1) {
            return std::nullopt;
        }
    }
    return root;
}

Term arborescence_root(ProcessContext &context, const GraphArg &graph) {
    const Terms terms(context);
    const auto root = root_of(Graph(graph));
    if (!root) {
        return terms.atom("no");
    }
    const std::array result{terms.atom("yes"), *root};
    return terms.tuple(result);
}

Term is_arborescence(ProcessContext &context, const GraphArg &graph) {
    return boolean(context, root_of(Graph(graph)).has_value());
}

// is_tree(G): one edge fewer than vertices and one component.
Term is_tree(ProcessContext &context, const GraphArg &graph_arg) {
    const Graph graph(graph_arg);
    const auto vertices = graph.vertices();
    const bool sized = edge_count(graph) + 1 == vertices.size();
    return boolean(context, sized && revpreorders(graph, Direction::both, vertices, true).size() == 1);
}

Term has_path3(ProcessContext &context, const GraphArg &graph, const VertexArg &from, const OtherArg &to) {
    return boolean(context, has_path(Graph(graph), {from.term, to.term}));
}

Term get_path(ProcessContext &context, const GraphArg &graph, const VertexArg &from, const OtherArg &to) {
    return path_term(context, one_path(Graph(graph), {from.term, to.term}, 1));
}

// get_cycle(G, V): a cycle of two edges or more through V, else [V] for a loop, else false.
Term get_cycle(ProcessContext &context, const GraphArg &graph_arg, const VertexArg &vertex) {
    const Graph graph(graph_arg);
    if (const auto cycle = one_path(graph, {vertex.term, vertex.term}, 2)) {
        return list(context, *cycle);
    }
    return graph.reflexive(vertex.term) ? list(context, {vertex.term}) : boolean(context, false);
}

Term get_short_path(ProcessContext &context, const GraphArg &graph, const VertexArg &from, const OtherArg &to) {
    return path_term(context, short_path(Graph(graph), {from.term, to.term}));
}

Term get_short_cycle(ProcessContext &context, const GraphArg &graph, const VertexArg &vertex) {
    return path_term(context, short_path(Graph(graph), {vertex.term, vertex.term}));
}

constexpr std::array GRAPH_TRAVERSAL_BUILTINS{
    typed_entry<components>("graph", "components"),
    typed_entry<strong_components1>("graph", "strong_components"),
    typed_entry<cyclic_strong_components1>("graph", "cyclic_strong_components"),
    typed_entry<reach<Direction::out, true>>("graph", "reachable"),
    typed_entry<reach<Direction::out, false>>("graph", "reachable_via_neighbours"),
    typed_entry<reach<Direction::in, true>>("graph", "reaching"),
    typed_entry<reach<Direction::in, false>>("graph", "reaching_via_neighbours"),
    typed_entry<roots1>("graph", "roots"),
    typed_entry<is_acyclic>("graph", "is_acyclic"),
    typed_entry<preorder1>("graph", "preorder"),
    typed_entry<preorder2>("graph", "preorder"),
    typed_entry<postorder1<false>>("graph", "postorder"),
    typed_entry<postorder2<false>>("graph", "postorder"),
    typed_entry<postorder1<true>>("graph", "reverse_postorder"),
    typed_entry<postorder2<true>>("graph", "reverse_postorder"),
    typed_entry<postorder1<true>>("graph", "topsort"),
    typed_entry<arborescence_root>("graph", "arborescence_root"),
    typed_entry<is_arborescence>("graph", "is_arborescence"),
    typed_entry<is_tree>("graph", "is_tree"),
    typed_entry<has_path3>("graph", "has_path"),
    typed_entry<get_path>("graph", "get_path"),
    typed_entry<get_cycle>("graph", "get_cycle"),
    typed_entry<get_short_path>("graph", "get_short_path"),
    typed_entry<get_short_cycle>("graph", "get_short_cycle"),
};
} // namespace
} // namespace clause::runtime::builtins::graph

namespace clause::runtime {
std::span<const BuiltinEntry> graph_traversal_builtins() noexcept { return builtins::graph::GRAPH_TRAVERSAL_BUILTINS; }
} // namespace clause::runtime
