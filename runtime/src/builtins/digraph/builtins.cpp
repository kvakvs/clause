#include "../lists/list_walk.hpp"
#include "view.hpp"
#include <array>

// The digraph and digraph_utils builtins (plan 21 step LIB12, docs/builtins.md#digraph): digraph's path searches and
// digraph_utils' traversals, orderings, components and tree tests, OTP's walks (module graph's, over the digraph's
// tables) composed as digraph_utils composes them.
namespace clause::runtime::builtins::digraph {
namespace {
using graph::Direction;
using graph::Terms;
using graph::OtherArg;
using graph::VertexArg;
using StartsArg = Named<struct StartsRole>;

Term list(ProcessContext &context, const Vertices &items) { return Terms(context).list(items); }

Term boolean(ProcessContext &context, bool value) { return Terms(context).boolean(value); }

Term path_term(ProcessContext &context, const std::optional<Vertices> &path) {
    return path ? list(context, *path) : boolean(context, false);
}

Term lists_term(ProcessContext &context, const std::vector<Vertices> &groups) {
    Vertices items;
    items.reserve(groups.size());
    for (const auto &group : groups) {
        items.push_back(list(context, group));
    }
    return list(context, items);
}

Vertices appended(const std::vector<Vertices> &groups) {
    Vertices all;
    for (const auto &group : groups) {
        all.insert(all.end(), group.begin(), group.end());
    }
    return all;
}

// get_path(G, V1, V2): a path of one edge or more, depth first.
Term get_path(ProcessContext &context, const DigraphArg &digraph, const VertexArg &from, const OtherArg &to) {
    return path_term(context, graph::one_path(DigraphView(context, digraph), {from.term, to.term}, 1));
}

// get_cycle(G, V): a cycle of two edges or more through V, else [V] for a loop, else false.
Term get_cycle(ProcessContext &context, const DigraphArg &digraph, const VertexArg &vertex) {
    const DigraphView view(context, digraph);
    if (const auto cycle = graph::one_path(view, {vertex.term, vertex.term}, 2)) {
        return list(context, *cycle);
    }
    return view.reflexive(vertex.term) ? list(context, {vertex.term}) : boolean(context, false);
}

Term get_short_path(ProcessContext &context, const DigraphArg &digraph, const VertexArg &from, const OtherArg &to) {
    return path_term(context, graph::short_path(DigraphView(context, digraph), {from.term, to.term}));
}

Term get_short_cycle(ProcessContext &context, const DigraphArg &digraph, const VertexArg &vertex) {
    return path_term(context, graph::short_path(DigraphView(context, digraph), {vertex.term, vertex.term}));
}

Term components(ProcessContext &context, const DigraphArg &digraph) {
    const DigraphView view(context, digraph);
    return lists_term(context, graph::revpreorders(view, Direction::both, view.vertices(), true));
}

Term strong_components(ProcessContext &context, const DigraphArg &digraph) {
    return lists_term(context, graph::strong_components(DigraphView(context, digraph)));
}

Term cyclic_strong_components(ProcessContext &context, const DigraphArg &digraph) {
    return lists_term(context, graph::cyclic_strong_components(DigraphView(context, digraph)));
}

// reachable/2, reachable_neighbours/2, reaching/2, reaching_neighbours/2 of (Vertices, G): the reversed preorders
// appended; function_clause unless Vertices is a list.
template <Direction Way, bool First>
Term reach(ProcessContext &context, const StartsArg &starts, const DigraphArg &digraph) {
    if (!starts.term.is_list()) {
        lists::no_clause();
    }
    const DigraphView view(context, digraph);
    return list(context, appended(graph::revpreorders(view, Way, graph::elements(starts.term), First)));
}

// The vertices with loops, in vertex order.
Vertices loops(const DigraphView &view) {
    Vertices found;
    for (const auto &vertex : view.vertices()) {
        if (view.reflexive(vertex)) {
            found.push_back(vertex);
        }
    }
    return found;
}

// topsort's order: the reverse postorder of every vertex, when following edges backwards from it reaches each vertex
// in a tree of its own (no cycle).
std::optional<Vertices> sorted(const DigraphView &view) {
    const auto vertices = view.vertices();
    auto order = graph::reverse_postorder(view, vertices);
    if (graph::revpreorders(view, Direction::in, order, true).size() != vertices.size()) {
        return std::nullopt;
    }
    return order;
}

Term topsort(ProcessContext &context, const DigraphArg &digraph) {
    return path_term(context, sorted(DigraphView(context, digraph)));
}

Term is_acyclic(ProcessContext &context, const DigraphArg &digraph) {
    const DigraphView view(context, digraph);
    return boolean(context, loops(view).empty() && sorted(view).has_value());
}

Term loop_vertices(ProcessContext &context, const DigraphArg &digraph) {
    return list(context, loops(DigraphView(context, digraph)));
}

// The root of an arborescence: one edge fewer than vertices, one vertex without incoming edges, every other with
// exactly one.
std::optional<Term> root_of(const DigraphView &view) {
    if (view.edge_count() + 1 != view.vertex_count()) {
        return std::nullopt;
    }
    std::optional<Term> root;
    for (const auto &vertex : view.vertices()) {
        const auto in = view.in_degree(vertex);
        if (in == 0 && !root) {
            root = vertex;
        } else if (in != 1) {
            return std::nullopt;
        }
    }
    return root;
}

Term arborescence_root(ProcessContext &context, const DigraphArg &digraph) {
    const Terms terms(context);
    const auto root = root_of(DigraphView(context, digraph));
    if (!root) {
        return terms.atom("no");
    }
    return terms.tuple(std::array{terms.atom("yes"), *root});
}

Term is_arborescence(ProcessContext &context, const DigraphArg &digraph) {
    return boolean(context, root_of(DigraphView(context, digraph)).has_value());
}

// is_tree(G): one edge fewer than vertices and one component.
Term is_tree(ProcessContext &context, const DigraphArg &digraph) {
    const DigraphView view(context, digraph);
    const auto sized = view.edge_count() + 1 == view.vertex_count();
    return boolean(context, sized && graph::revpreorders(view, Direction::both, view.vertices(), true).size() == 1);
}

// preorder(G): one depth-first walk from the roots, the vertices in the order first reached.
Term preorder(ProcessContext &context, const DigraphArg &digraph) {
    const DigraphView view(context, digraph);
    Vertices order;
    graph::Seen seen;
    auto pending = graph::roots(view);
    std::ranges::reverse(pending);
    while (!pending.empty()) {
        const auto vertex = pending.back();
        pending.pop_back();
        if (seen.insert(vertex).second) {
            order.push_back(vertex);
            const auto next = view.out_neighbours(vertex);
            pending.insert(pending.end(), next.rbegin(), next.rend());
        }
    }
    return list(context, order);
}

// postorder(G): from the roots, each vertex once its descendants are done.
Term postorder(ProcessContext &context, const DigraphArg &digraph) {
    const DigraphView view(context, digraph);
    auto order = graph::reverse_postorder(view, graph::roots(view));
    std::ranges::reverse(order);
    return list(context, order);
}

constexpr std::array DIGRAPH_BUILTINS{
    typed_entry<get_path>("digraph", "get_path"),
    typed_entry<get_cycle>("digraph", "get_cycle"),
    typed_entry<get_short_path>("digraph", "get_short_path"),
    typed_entry<get_short_cycle>("digraph", "get_short_cycle"),
    typed_entry<components>("digraph_utils", "components"),
    typed_entry<strong_components>("digraph_utils", "strong_components"),
    typed_entry<cyclic_strong_components>("digraph_utils", "cyclic_strong_components"),
    typed_entry<reach<Direction::out, true>>("digraph_utils", "reachable"),
    typed_entry<reach<Direction::out, false>>("digraph_utils", "reachable_neighbours"),
    typed_entry<reach<Direction::in, true>>("digraph_utils", "reaching"),
    typed_entry<reach<Direction::in, false>>("digraph_utils", "reaching_neighbours"),
    typed_entry<topsort>("digraph_utils", "topsort"),
    typed_entry<is_acyclic>("digraph_utils", "is_acyclic"),
    typed_entry<loop_vertices>("digraph_utils", "loop_vertices"),
    typed_entry<arborescence_root>("digraph_utils", "arborescence_root"),
    typed_entry<is_arborescence>("digraph_utils", "is_arborescence"),
    typed_entry<is_tree>("digraph_utils", "is_tree"),
    typed_entry<preorder>("digraph_utils", "preorder"),
    typed_entry<postorder>("digraph_utils", "postorder"),
};
} // namespace
} // namespace clause::runtime::builtins::digraph

namespace clause::runtime {
std::span<const BuiltinEntry> digraph_builtins() noexcept { return builtins::digraph::DIGRAPH_BUILTINS; }
} // namespace clause::runtime
