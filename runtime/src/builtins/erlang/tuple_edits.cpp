#include "../../terms/structural_order.hpp"
#include "../typed.hpp"
#include <array>

// erlang's tuple edits and the integer range test (plan 21 step LIB9A, docs/builtins.md#erlang-module):
// append_element/2, insert_element/3, delete_element/2 and is_integer/3 when called rather than inlined.
namespace clause::runtime::builtins {
namespace {
// The roles of the arguments (typed.hpp's Named).
struct TupleRole;
struct ValueRole;
struct LowRole;
struct HighRole;
using TupleArg = Named<TupleRole>;
using ValueArg = Named<ValueRole>;
using LowArg = Named<LowRole>;
using HighArg = Named<HighRole>;

// The elements of a tuple argument; badarg for anything else.
std::vector<Term> elements_of(const TupleArg &tuple) {
    if (!tuple.term.is_tuple()) {
        bad_argument();
    }
    std::vector<Term> elements;
    const auto size = tuple.term.tuple_size_unchecked();
    elements.reserve(size + 1);
    for (std::size_t i = 0; i < size; ++i) {
        elements.push_back(tuple.term.tuple_element_unchecked(i));
    }
    return elements;
}

// A one-based index in 1..limit; badarg otherwise.
std::size_t index_of(const Term &index, std::size_t limit) {
    const auto value = small(index.word());
    if (!value || *value < 1 || static_cast<std::uint64_t>(*value) > limit) {
        bad_argument();
    }
    return static_cast<std::size_t>(*value) - 1;
}

// append_element(Tuple, Term): Tuple with Term added last.
Term append_element(ProcessContext &context, const TupleArg &tuple, const ValueArg &value) {
    auto elements = elements_of(tuple);
    elements.push_back(value.term);
    return need(TermFactory(context).tuple(elements));
}

// insert_element(Index, Tuple, Term): Term inserted at Index (1..size + 1).
Term insert_element(ProcessContext &context, const Term &index, const TupleArg &tuple, const ValueArg &value) {
    auto elements = elements_of(tuple);
    const auto at = index_of(index, elements.size() + 1);
    elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(at), value.term);
    return need(TermFactory(context).tuple(elements));
}

// delete_element(Index, Tuple): Tuple without the element at Index (1..size).
Term delete_element(ProcessContext &context, const Term &index, const TupleArg &tuple) {
    auto elements = elements_of(tuple);
    const auto at = index_of(index, elements.size());
    elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(at));
    return need(TermFactory(context).tuple(elements));
}

// is_integer(Term, LowerBound, UpperBound): an integer within the bounds; badarg unless both bounds are integers.
Term is_integer3(ProcessContext &context, const ValueArg &value, const LowArg &low, const HighArg &high) {
    if (!low.term.is_integer() || !high.term.is_integer()) {
        bad_argument();
    }
    const bool within = value.term.is_integer() && need(detail::structural_order(low.term, value.term)) <= 0 &&
                        need(detail::structural_order(value.term, high.term)) <= 0;
    return need(TermFactory(context).boolean(within));
}

constexpr std::array TUPLE_EDIT_BUILTINS{
    typed_entry<append_element>("erlang", "append_element"),
    typed_entry<insert_element>("erlang", "insert_element"),
    typed_entry<delete_element>("erlang", "delete_element"),
    typed_entry<is_integer3>("erlang", "is_integer"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> tuple_edit_builtins() noexcept { return builtins::TUPLE_EDIT_BUILTINS; }
} // namespace clause::runtime
