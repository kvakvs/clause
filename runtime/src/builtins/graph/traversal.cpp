#include "../../terms/structural_order.hpp"
#include "../lists/list_walk.hpp"
#include "graph.hpp"
#include <algorithm>

// The graph record and OTP's depth-first traversals (plan 21 step LIB7B): revpreorders/4, posttraverse/4, strong
// components and roots, ported with explicit stacks so each visits vertices and builds lists in OTP's order.
namespace clause::runtime::builtins::graph {
bool ExactLess::operator()(const Term &left, const Term &right) const {
    return need(detail::structural_order(left, right, true)) < 0;
}

bool exact(const Term &left, const Term &right) { return need(left.exactly_equal(right)); }

Vertices elements(const Term &list) {
    Vertices items;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        lists::no_clause();
    }
    return items;
}

Graph::Graph(const GraphArg &graph) {
    const auto &record = graph.term;
    if (!record.is_tuple() || record.tuple_size_unchecked() != 6 || !record.tuple_element_unchecked(0).is_atom() ||
        record.tuple_element_unchecked(0).atom_spelling_unchecked() != "graph") {
        lists::no_clause();
    }
    vs_ = record.tuple_element_unchecked(1);
    in_es_ = record.tuple_element_unchecked(2);
    out_es_ = record.tuple_element_unchecked(3);
    const auto cyclic = record.tuple_element_unchecked(4);
    cyclic_ = !(cyclic.is_atom() && cyclic.atom_spelling_unchecked() == "false");
    if (!vs_.is_map() || !in_es_.is_map() || !out_es_.is_map()) {
        lists::no_clause();
    }
}

Vertices Graph::vertices() const {
    Vertices keys;
    for (const auto &[key, label] : need(vs_.map_entries())) {
        keys.push_back(key);
    }
    return keys;
}

Vertices Graph::edges_of(const Term &map, const Term &vertex) {
    const auto found = need(map.map_find(vertex));
    return found ? elements(*found) : Vertices{};
}

Vertices Graph::ends(const Vertices &edges, std::size_t element) {
    Vertices vertices;
    vertices.reserve(edges.size());
    for (const auto &edge : edges) {
        vertices.push_back(edge.tuple_element_unchecked(element));
    }
    return vertices;
}

std::vector<std::pair<Term, Term>> Graph::out_ends(const Term &vertex) const {
    std::vector<std::pair<Term, Term>> found;
    for (const auto &edge : out_edges(vertex)) {
        found.emplace_back(edge.tuple_element_unchecked(0), edge.tuple_element_unchecked(1));
    }
    return found;
}

bool View::reflexive(const Term &vertex) const {
    const auto neighbours = out_neighbours(vertex);
    return std::ranges::any_of(neighbours, [&](const Term &other) { return exact(other, vertex); });
}

Vertices next_of(const View &graph, Direction direction, const Term &vertex, const Vertices &rest) {
    Vertices next;
    if (direction != Direction::out) {
        next = graph.in_neighbours(vertex);
    }
    if (direction != Direction::in) {
        const auto out = graph.out_neighbours(vertex);
        next.insert(next.end(), out.begin(), out.end());
    }
    next.insert(next.end(), rest.begin(), rest.end());
    return next;
}

namespace {
// pretraverse_1/6: visit a list of vertices depth first, each unseen one marked and its neighbours visited before the
// rest; the vertices reached, the last reached first.
Vertices pretraverse(const View &graph, Direction direction, Vertices pending, Seen &seen) {
    Vertices reached;
    // The vertices still to visit, the next one last.
    std::ranges::reverse(pending);
    while (!pending.empty()) {
        const auto vertex = pending.back();
        pending.pop_back();
        if (!seen.insert(vertex).second) {
            continue;
        }
        reached.push_back(vertex);
        auto next = next_of(graph, direction, vertex, {});
        pending.insert(pending.end(), next.rbegin(), next.rend());
    }
    std::ranges::reverse(reached);
    return reached;
}
} // namespace

std::vector<Vertices> revpreorders(const View &graph, Direction direction, const Vertices &starts, bool first) {
    Seen seen;
    std::vector<Vertices> found;
    for (const auto &start : starts) {
        if (!first && seen.contains(start)) {
            continue;
        }
        auto reached =
            pretraverse(graph, direction, first ? Vertices{start} : next_of(graph, direction, start, {}), seen);
        if (!reached.empty()) {
            found.push_back(std::move(reached));
        }
    }
    std::ranges::reverse(found);
    return found;
}

namespace {
// One vertex of posttraverse/4 being visited: it and the neighbours it still has to visit.
struct Visit {
    Term vertex;
    Vertices children;
    std::size_t next = 0;
};
} // namespace

Vertices reverse_postorder(const View &graph, const Vertices &starts) {
    Seen seen;
    Vertices finished;
    std::vector<Visit> stack;
    const auto enter = [&](const Term &vertex) {
        if (seen.insert(vertex).second) {
            stack.push_back({vertex, graph.out_neighbours(vertex)});
        }
    };
    for (const auto &start : starts) {
        enter(start);
        while (!stack.empty()) {
            auto &top = stack.back();
            if (top.next < top.children.size()) {
                const auto child = top.children[top.next++];
                enter(child);
                continue;
            }
            finished.push_back(top.vertex);
            stack.pop_back();
        }
    }
    std::ranges::reverse(finished);
    return finished;
}

std::vector<Vertices> strong_components(const View &graph) {
    return revpreorders(graph, Direction::in, reverse_postorder(graph, graph.vertices()), true);
}

std::vector<Vertices> cyclic_strong_components(const View &graph) {
    std::vector<Vertices> cyclic;
    for (auto &component : strong_components(graph)) {
        if (component.size() != 1 || graph.reflexive(component.front())) {
            cyclic.push_back(std::move(component));
        }
    }
    std::ranges::reverse(cyclic);
    return cyclic;
}

Vertices roots(const View &graph) {
    Vertices found;
    for (const auto &vertex : graph.vertices()) {
        if (graph.in_degree(vertex) == 0) {
            found.push_back(vertex);
        }
    }
    for (const auto &component : cyclic_strong_components(graph)) {
        found.push_back(component.front());
    }
    return found;
}
} // namespace clause::runtime::builtins::graph
