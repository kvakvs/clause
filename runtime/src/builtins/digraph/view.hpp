#pragma once
#include "../ets/ets.hpp"
#include "../graph/graph.hpp"
#include <memory>
#include <utility>
#include <vector>

// A digraph's tables as a graph the traversals of module graph walk (plan 21 step LIB12, docs/builtins.md#digraph):
// OTP's {digraph, Vertices, Edges, Neighbours, Cyclic} record, read in place as digraph's own functions read it, so
// vertices come in the vertex table's walk order and neighbours last edge first.
namespace clause::runtime::builtins::digraph {
using graph::Vertices;

// The digraph argument of a builtin, a role of its own.
using DigraphArg = Named<struct DigraphRole>;

class DigraphView final : public graph::View {
  public:
    // The tables of a #digraph{} record the caller may read; {badrecord, Term} for any other term, as OTP's record
    // access raises, and badarg for tables it may not read.
    DigraphView(ProcessContext &context, const DigraphArg &digraph);

    Vertices vertices() const override;

    Vertices in_neighbours(const Term &vertex) const override { return neighbours(vertex, "in", 2); }

    Vertices out_neighbours(const Term &vertex) const override { return neighbours(vertex, "out", 3); }

    std::vector<std::pair<Term, Term>> out_ends(const Term &vertex) const override;

    std::size_t in_degree(const Term &vertex) const override { return edges(vertex, "in"_atom).size(); }

    // The sizes of the vertex and edge tables (no_vertices/1, no_edges/1).
    std::size_t vertex_count() const;
    std::size_t edge_count() const;

  private:
    // The edges of a vertex's {in, V} or {out, V} entries, in the bag's order.
    Vertices edges(const Term &vertex, AtomName side) const;
    // The `position`th element (2 the source, 3 the target) of each of those edges, the last edge first.
    Vertices neighbours(const Term &vertex, std::string_view side, std::size_t position) const;
    // One element of a stored edge; badarg when the edge is gone, as ets:lookup_element/3 raises.
    Term edge_element(const Term &edge, std::size_t position) const;

    ProcessContext &context_;
    std::shared_ptr<ets::Table> vertices_;
    std::shared_ptr<ets::Table> edges_;
    std::shared_ptr<ets::Table> neighbours_;
};
} // namespace clause::runtime::builtins::digraph
