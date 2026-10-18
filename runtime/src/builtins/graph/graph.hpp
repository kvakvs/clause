#pragma once
#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include <map>
#include <optional>
#include <set>
#include <vector>

// OTP 29's functional graph module (plan 21 step LIB7B, docs/builtins.md#graph): the queries and traversals over
// OTP's #graph{vs, in_es, out_es, cyclic, next_vid} record, whose maps hold vertex labels and each vertex's edges as
// ordsets of {From, To, Label}; the updates are Erlang in library/stdlib/graph.erl.
namespace clause::runtime::builtins::graph {
using string::Terms;
using Vertices = std::vector<Term>;

// Orders terms by exact equality (=:=), as map keys and sets compare them.
struct ExactLess {
    bool operator()(const Term &left, const Term &right) const;
};

// The vertices a traversal has seen.
using Seen = std::set<Term, ExactLess>;

// The roles of the graph builtins' arguments (typed.hpp's Named).
struct GraphRole;
struct VertexRole;
struct OtherRole;
using GraphArg = Named<GraphRole>;
using VertexArg = Named<VertexRole>;
// A second vertex, or a list of vertices.
using OtherArg = Named<OtherRole>;

// One #graph{} record; function_clause for anything else, as OTP's clause heads raise.
class Graph final {
  public:
    explicit Graph(const GraphArg &graph);

    // The vertices in map order (maps:keys/1).
    Vertices vertices() const;

    // The edges of a vertex: in_es or out_es entries, [] for a vertex without any.
    Vertices in_edges(const Term &vertex) const { return edges_of(in_es_, vertex); }

    Vertices out_edges(const Term &vertex) const { return edges_of(out_es_, vertex); }

    // The other ends of those edges, in edge order.
    Vertices in_neighbours(const Term &vertex) const { return ends(in_edges(vertex), 0); }

    Vertices out_neighbours(const Term &vertex) const { return ends(out_edges(vertex), 1); }

    // Whether a vertex has an edge to itself.
    bool reflexive(const Term &vertex) const;

    Term labels() const { return vs_; }

    Term in_map() const { return in_es_; }

    Term out_map() const { return out_es_; }

    bool cyclic() const { return cyclic_; }

  private:
    static Vertices edges_of(const Term &map, const Term &vertex);
    static Vertices ends(const Vertices &edges, std::size_t element);

    // The vertex label map and the edge maps of the record, and whether it may have cycles.
    Term vs_;
    Term in_es_;
    Term out_es_;
    bool cyclic_ = true;
};

// Whether two terms are exactly equal.
bool exact(const Term &left, const Term &right);

// The proper list `list` as a vector; function_clause unless it is one.
Vertices elements(const Term &list);

// The neighbour functions of OTP's traversals: in/3, out/3 and inout/3.
enum class Direction : std::uint8_t { in, out, both };
// The next vertices to visit from `vertex`, in front of `rest`.
Vertices next_of(const Graph &graph, Direction direction, const Term &vertex, const Vertices &rest);

// revpreorders/4: one reversed preorder per traversal from each vertex of `starts` not seen yet; with `first` the
// start vertex itself counts as reached, else only its neighbours (the *_via_neighbours functions).
std::vector<Vertices> revpreorders(const Graph &graph, Direction direction, const Vertices &starts, bool first);
// posttraverse/4 from `starts`: every reached vertex, the last finished first (reverse_postorder/2).
Vertices reverse_postorder(const Graph &graph, const Vertices &starts);
// strong_components/1 and cyclic_strong_components/1.
std::vector<Vertices> strong_components(const Graph &graph);
std::vector<Vertices> cyclic_strong_components(const Graph &graph);
// roots/1: the vertices without incoming edges, then the first vertex of each cyclic strong component.
Vertices roots(const Graph &graph);

// The two ends of a path search: where it starts and the vertex it looks for.
struct Ends {
    Term from;
    Term to;
};

// one_path/8: depth first to `ends.to`; a sink met fewer than `prune` levels deep is skipped.
std::optional<Vertices> one_path(const Graph &graph, const Ends &ends, std::size_t prune);
// has_path/3.
bool has_path(const Graph &graph, const Ends &ends);
// get_short_path/3: breadth first over the edges.
std::optional<Vertices> short_path(const Graph &graph, const Ends &ends);
} // namespace clause::runtime::builtins::graph
