#include "graph.hpp"
#include <algorithm>
#include <deque>

// The path searches of graph (plan 21 step LIB7B): has_path/3, get_path/3 and get_cycle/2 by OTP's one_path depth
// first search with its prune counter, get_short_path/3 and get_short_cycle/2 breadth first over the edges.
namespace clause::runtime::builtins::graph {
namespace {
// A list of vertices one_path/8 walks: its elements and the next one.
struct Pending {
    Vertices vertices;
    std::size_t next = 0;
    // The path to the vertex these were the neighbours of, most recent first.
    Vertices path;
};

bool contains(const Vertices &vertices, const Term &vertex) {
    return std::ranges::any_of(vertices, [&](const Term &other) { return exact(other, vertex); });
}

// The path found by one_path/8: the start, then each vertex of `path` (most recent first), then `sink`.
Vertices found_path(const Vertices &path, const Term &sink) {
    Vertices result(path.rbegin(), path.rend());
    result.push_back(sink);
    return result;
}

// One step of one_path/8 on a vertex that is not the sink: descend into an unvisited one.
void descend(const View &graph, std::vector<Pending> &stack, Vertices &visited, const Term &vertex) {
    if (contains(visited, vertex)) {
        return;
    }
    visited.push_back(vertex);
    auto path = stack.back().path;
    path.insert(path.begin(), vertex);
    stack.push_back({graph.out_neighbours(vertex), 0, std::move(path)});
}
} // namespace

std::optional<Vertices> one_path(const View &graph, const Ends &ends, std::size_t prune) {
    const auto &sink = ends.to;
    std::vector<Pending> stack{{graph.out_neighbours(ends.from), 0, {ends.from}}};
    Vertices visited{ends.from};
    while (!stack.empty()) {
        auto &top = stack.back();
        if (top.next == top.vertices.size()) {
            stack.pop_back();
            continue;
        }
        const auto vertex = top.vertices[top.next++];
        if (exact(vertex, sink)) {
            if (stack.size() >= prune) {
                return found_path(top.path, sink);
            }
            continue;
        }
        descend(graph, stack, visited, vertex);
    }
    return std::nullopt;
}

bool has_path(const View &graph, const Ends &ends) {
    const auto &to = ends.to;
    Seen seen;
    Vertices pending{ends.from};
    while (!pending.empty()) {
        const auto vertex = pending.back();
        pending.pop_back();
        if (exact(vertex, to)) {
            return true;
        }
        if (seen.insert(vertex).second) {
            auto next = graph.out_neighbours(vertex);
            pending.insert(pending.end(), next.rbegin(), next.rend());
        }
    }
    return false;
}

std::optional<Vertices> short_path(const View &graph, const Ends &ends) {
    const auto &source = ends.from;
    const auto &sink = ends.to;
    // Each reached vertex and the vertex it was reached from; the source has none.
    std::map<Term, std::optional<Term>, ExactLess> parents{{source, std::nullopt}};
    std::deque<std::pair<Term, Term>> queue;
    const auto enqueue = [&](const Term &vertex) {
        for (const auto &edge : graph.out_ends(vertex)) {
            queue.push_back(edge);
        }
    };
    enqueue(source);
    while (!queue.empty()) {
        const auto [from, to] = queue.front();
        queue.pop_front();
        if (exact(to, sink)) {
            Vertices path{to};
            for (std::optional<Term> at = from; at; at = parents.at(*at)) {
                path.push_back(*at);
            }
            std::ranges::reverse(path);
            return path;
        }
        if (parents.emplace(to, from).second) {
            enqueue(to);
        }
    }
    return std::nullopt;
}
} // namespace clause::runtime::builtins::graph
