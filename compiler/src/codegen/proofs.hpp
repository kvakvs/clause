#pragma once
#include "../semantic/types/domain.hpp"
#include <cstdint>
#include <optional>

// What inferred facts prove about a value's representation (docs/specialization.md#proofs). Only facts of
// caller-joined inputs and expression facts are read; bottom (unreachable) and term() prove nothing.
namespace clause::codegen {
// The small integers a fact proves a value to be one of, within the target's immediate payload.
struct SmallRange {
    std::int64_t low;
    std::int64_t high;
};

// A list fact: unknown, any proper list (a cons cell or []), or surely a cons cell.
enum class ListShape : std::uint8_t { unknown, list, cons };

// A value's fact, and how many leading list cells of it were already taken: the value is that list's tail then.
struct Known {
    semantic::types::Id fact;
    std::size_t taken = 0;
};

// Read-only questions about facts of one inference graph for a target word width.
class Proofs {
  public:
    Proofs(const semantic::types::Graph &graph, unsigned bits) noexcept : graph_(graph), bits_(bits) {}

    // The small integers `fact` holds when all of them fit an immediate; none otherwise.
    std::optional<SmallRange> small(semantic::types::Id fact) const;
    // Whether `fact` holds only floats (wider float loops, OPT1.11 remainder).
    bool floating(semantic::types::Id fact) const;
    // The arity shared by every tuple `known` holds, when it holds only tuples.
    std::optional<std::size_t> arity(const Known &known) const;
    // Whether `known` holds only proper lists, or only cons cells.
    ListShape list(const Known &known) const;
    // The fact of element `index` of an exact tuple fact, of a list's head, or of a list's tail.
    std::optional<Known> element(const Known &known, std::size_t index) const;
    std::optional<Known> head(const Known &known) const;
    std::optional<Known> tail(const Known &known) const;

  private:
    // The range of one member (a singleton or a range), or none.
    std::optional<SmallRange> member_range(const semantic::types::Node &node) const;
    // A positional list's remaining fact once `taken` cells are gone: its tail fact when all elements are taken.
    Known normalized(const Known &known) const;
    // The list shape of one member fact.
    ListShape member_list(const semantic::types::Node &node) const;

    // Borrow the inference graph that owns every fact asked about, and the target word width.
    const semantic::types::Graph &graph_;
    unsigned bits_;
};
} // namespace clause::codegen
