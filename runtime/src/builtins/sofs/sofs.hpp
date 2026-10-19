#pragma once
#include "../collections/collections.hpp"
#include "../string/string.hpp"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <span>
#include <vector>

// Shared data operations for Clause's sofs implementation (LIB7A); callbacks remain in Erlang.
namespace clause::runtime::builtins::sofs {
using collections::compare;
using collections::same;
using lists::no_clause;
using string::Terms;
using Items = std::vector<Term>;
struct LeftRole;
struct RightRole;
struct IndexRole;
struct OtherIndexRole;
struct ValueRole;
using Left = Named<LeftRole>;
using Right = Named<RightRole>;
using Index = Named<IndexRole>;
using OtherIndex = Named<OtherIndexRole>;
using Value = Named<ValueRole>;

// A decoded observable sofs record, without validating its user-supplied contents.
struct Set {
    // External elements or ordered value.
    Term data_;
    // Element type for Set, complete type for OrdSet.
    Term type_;
    // Whether this is an unordered Set record.
    bool unordered_;
};

// A normalized value together with its inferred or explicit type.
struct Normalized {
    // Canonical external representation.
    Term data_;
    // Refined type after processing the value.
    Term type_;
};
// The four binary operations over ordered element lists.
enum class Operation : std::uint8_t { unite, intersect, subtract, symmetric };

// Test an atom without interning a second copy.
bool atom_is(const Term &term, std::string_view name);
// Read a tuple component by zero-based index, rejecting malformed input with badarg.
Term at(const Term &term, std::size_t index);
// Build a small tuple from explicit components.
Term tuple(const Terms &terms, std::initializer_list<Term> values);
// Read a proper list, rejecting malformed input with badarg.
Items elements(const Term &term);
// Sort by Erlang value order, retaining the first representative of equal values.
Items unique(Items values);
// Decode a record; no matching public clause raises function_clause.
Set record(const Term &term);
// Require the unordered record kind accepted by collection operations.
Set unordered(const Term &term);
// Whether a term has either observable sofs record tag and arity.
bool is_record(const Term &term);
// Build a Set record with an element type.
Term make_set(const Terms &terms, const Items &values, const Value &type);
// Build a record over already constructed external data.
Term wrap(const Terms &terms, const Set &set);
// Build an empty untyped set.
Term empty(const Terms &terms);
// Complete public type of a decoded record.
Term full_type(const Terms &terms, const Set &set);
// A singleton type list, or none for a different shape.
std::optional<Term> list_type(const Term &term);
// Validate a public type, including wildcards only as set element types.
bool valid_type(const Term &term);
// Merge compatible types; none denotes a type mismatch.
std::optional<Term> unify(const Terms &terms, const Left &left, const Right &right);
// Merge types and raise the public type_mismatch error on failure.
Term unified(const Terms &terms, const Left &left, const Right &right);
// Require compatible types without choosing a more specific one.
void match(const Terms &terms, const Left &left, const Right &right);
// Raise a named Erlang error reason.
[[noreturn]] void error(const Terms &terms, AtomName reason);
// Normalize external data using evolving and original element type constraints.
Normalized normalize(const Terms &terms, const Value &value, const Left &type, const Right &original);
// Normalize an unordered set, preserving explicit atomic leaf values.
Term setify(const Terms &terms, const Left &data, const Right &type);
// Apply an ordered list operation, preserving the left representative on equality.
Items combine(const Items &left, const Items &right, Operation operation);
// Combine compatible unordered records.
Term binary_set(const Terms &terms, const Left &left, const Right &right, Operation operation);
// Read a binary relation type, treating an untyped set as a wildcard pair.
std::array<Term, 2> relation_type(const Terms &terms, const Set &set);
// Read a family type as domain and member type, rejecting other shapes.
std::array<Term, 2> family_type(const Set &set);
// Check that no two pairs share their domain, raising bad_function otherwise.
void check_function(const Terms &terms, const Items &pairs);
// Group sorted pairs by their first component, preserving that component's first representative.
Items group(const Terms &terms, Items pairs);
// Group consecutive keys, preserving order unless member sorting is explicitly requested.
Items group_adjacent(const Terms &terms, const Items &pairs, bool sort_members);
// Project either component of a binary relation.
Term component(const Terms &terms, const Term &relation, std::size_t index);
// Exchange the two components of a binary relation.
Term converse(const Terms &terms, const Term &relation);
// Join two binary relations on their domain.
Term relative_product1(const Terms &terms, const Left &left, const Right &right);
// Compose binary relations on range and domain.
Term relative_product(const Terms &terms, const Left &left, const Right &right);
// Convert a relation to a family.
Term relation_to_family(const Terms &terms, const Term &relation);
// Convert a family to a relation.
Term family_to_relation(const Terms &terms, const Term &family);
// Canonical membership relation for a set of sets.
Term canonical(const Terms &terms, const Term &sets);
// Integer selectors: projection, substitution, partition, partition_family.
Term select(const Terms &terms, const Index &index, const Left &set, std::string_view operation);
// Integer selectors: restriction, drestriction, partition/3.
Term restrict(const Terms &terms, const Index &index, const Left &set, const Right &keys, std::string_view operation);
// Construct a product over a nonempty collection of binary relations sharing a domain.
Term relative_many(const Terms &terms, const Term &relations);
// Pointwise binary family operations.
Term family_binary(const Terms &terms, const Left &left, const Right &right, Operation operation);
} // namespace clause::runtime::builtins::sofs
