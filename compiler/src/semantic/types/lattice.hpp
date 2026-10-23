#pragma once
#include "domain.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The fact domain of type inference (docs/semantic.md#inference-domain): the facts inference states about values,
// how two facts join where control flow meets, and how a recursive result widens, within budgets that keep facts
// small. Facts are nodes of a type graph; categories are erlang built-in type applications, so facts print by their
// built-in names (boolean(), non_neg_integer(), string(), ...).
namespace clause::semantic::types {
// The lowest and highest integer of a range, as canonical decimals.
struct IntegerBounds {
    std::string_view low;
    std::string_view high;
};

// The budgets of the fact domain; going past one widens a fact soundly, never fails.
struct FactLimits {
    // Singleton atoms or integers a fact keeps before atoms become atom() and integers their range.
    std::size_t singletons = 8;
    // Members a fact union keeps before it becomes term().
    std::size_t members = 8;
    // Container nesting a fact keeps before an inner fact becomes term().
    std::size_t depth = 4;
    // Tuple elements and map keys a fact keeps before it becomes tuple() or map().
    std::size_t elements = 16;
};

// The numbers a fact holds, for arithmetic on facts.
struct Numbers {
    // Whether it holds integers, from `low` to `high` (canonical decimals; unbounded when missing).
    bool integers = false;
    std::optional<std::string> low;
    std::optional<std::string> high;
    // Whether it holds floats, and whether it holds values that are no numbers.
    bool floats = false;
    bool others = false;
};

class Lattice {
  public:
    // Facts live in `graph`, which owns their nodes.
    explicit Lattice(Graph &graph, FactLimits limits = {}) noexcept : graph_(graph), limits_(limits) {}

    // The least fact that holds both, within the budgets: the result of two clauses or branches.
    Id join(Id left, Id right);
    // The join of a recursive result's previous and next facts that stops it from growing for ever: integer bounds
    // that moved since the previous round, also inside tuples, maps and lists of one shape, become unbounded.
    Id widen(Id previous, Id next);

    // Facts of values. An integer (canonical decimal) and an atom.
    Id integer(std::string_view decimal);
    Id atom(std::string_view name);
    // A built-in category by its name: integer, float, atom, pid, binary, ...
    Id category(std::string_view name);
    // The integers from `bounds.low` to `bounds.high` (low below high).
    Id range(IntegerBounds bounds);
    // A tuple, a proper list of `element` (never empty when `nonempty`), the empty list, a map with exactly the
    // keys of `fields` (keys and values alternate), a fun of `arity` returning `result`, and a bitstring of `base`
    // bits plus any multiple of `unit` bits (unit 0: exactly `base` bits).
    Id tuple(std::vector<Id> elements);
    Id list(Id element, bool nonempty);
    Id nil();
    // A list of known elements at fixed positions followed by `children.back()`, its tail (at least two elements).
    Id positional(std::vector<Id> children);
    // The plain list fact of a positional list: its elements joined in front of its tail.
    Id plain(Id fact);
    // The list fact of `element` cells in front of `tail`.
    Id prepend(Id element, Id tail);
    // The join of `element` cells in front of each member of a union tail.
    Id prepend_members(Id element, std::span<const Id> members);
    // `prepend` in front of a named list category (or another named fact) `node`.
    Id prepend_named(Id element, const Node &node, Id tail);
    // A list of at least one `head` cell ending in a `tail` that is no list: nonempty_improper_list(Head, Tail).
    Id improper(Id head, Id tail);
    Id map(std::vector<Id> fields);
    // Maps whose keys are all of `key` with values of `value`, any of them possibly missing: #{Key => Value}.
    Id association(Id key, Id value);
    Id fun(std::size_t arity, Id result);
    Id bitstring(std::uint64_t base, std::uint64_t unit);
    // A fun of one function type: arguments of the facts `inputs` returning `result`; `exact` when every argument
    // within the inputs enters its clause.
    Id fun(std::vector<Id> inputs, Id result, bool exact = false);
    // A fun of several function types, funs of one arity with inputs; a single one is that fun itself.
    Id overloaded(std::vector<Id> types);
    // The fun of today a fun fact stands for: its arity, any inputs, its function types' joined results; another
    // fact as it is.
    Id joined_fun(Id fact);
    // The function types of a fun fact: the funs of an overloaded fun, else the fact itself.
    std::vector<Id> function_types(Id fact) const;

    // The integers from `low` to `high` as a fact: a singleton, a range or the category of an unbounded interval.
    Id interval(const std::optional<std::string> &low, const std::optional<std::string> &high);
    // The values both facts hold, or more: what narrowing proves (semantic/types/meet). none() only when they share
    // no value.
    Id meet(Id left, Id right);
    // The values of `fact` outside `removed`, or more: a member surely within `removed` is dropped.
    Id subtract(Id fact, Id removed);
    // `subtract` of a number fact member and a number fact.
    Id subtract_numbers(Id member, Id removed);
    // The rank of a fact's family in Erlang term order: numbers 0, atoms 1, ..., bitstrings 10, other facts 11.
    int family(Id fact) const;
    // Whether every value of `inner` is surely a value of `outer`.
    bool within(Id inner, Id outer);
    // The members of a fact: a union's members, or the fact itself.
    std::vector<Id> members(Id fact) const;
    // The numbers `fact` holds, and whether it may hold the atom `name`.
    Numbers numbers(Id fact);
    bool holds_atom(Id fact, std::string_view name);

    // The facts of a graph and its budgets, for the helpers of lattice.cpp.
    Graph &graph() noexcept { return graph_; }

    const FactLimits &limits() const noexcept { return limits_; }

    // Join `members` found `depth` containers deep; past the depth budget the join is term().
    Id join(std::span<const Id> members, std::size_t depth);

  private:
    // An element fact cut to the depth budget: containers below it are term() past depth - 1 levels.
    Id within_depth(Id fact);
    // The fact with containers nested deeper than `levels` replaced by term().
    Id truncate(Id fact, std::size_t levels);

    Graph &graph_;
    FactLimits limits_;
};
} // namespace clause::semantic::types
