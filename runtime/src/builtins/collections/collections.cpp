#include "collections.hpp"
#include "../../terms/floats.hpp"
#include "../../terms/structural_order.hpp"
#include <clause/abi/immediate_services.hpp>

// Shared helpers of the collection modules (plan 21 step LIB1).
namespace clause::runtime::builtins::collections {
int compare(const Term &left, const Term &right) {
    const auto order = need(detail::structural_order(left, right));
    return order < 0 ? -1 : (order > 0 ? 1 : 0);
}

bool same(const Term &left, const Term &right) { return need(left.exactly_equal(right)); }

Term append(ProcessContext &context, const List &list, const Term &tail) {
    std::vector<Term> elements;
    auto rest = list.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        elements.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return need(TermFactory(context).list(elements, tail));
}

Term add(ProcessContext &context, const Term &left, const Term &right) {
    if (!left.is_number() || !right.is_number()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    return need(detail::numeric_service(context, abi::v1::ImmediateOperation::add, left, right));
}

std::optional<std::pair<Term, Term>> pair_of(const Term &term) {
    if (!term.is_tuple() || term.tuple_size_unchecked() != 2) {
        return std::nullopt;
    }
    return std::pair{term.tuple_element_unchecked(0), term.tuple_element_unchecked(1)};
}

std::optional<Term> first_element(const Term &term) {
    if (!term.is_tuple() || term.tuple_size_unchecked() == 0) {
        return std::nullopt;
    }
    return term.tuple_element_unchecked(0);
}
} // namespace clause::runtime::builtins::collections
