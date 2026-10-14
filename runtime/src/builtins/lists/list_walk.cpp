#include "list_walk.hpp"
#include "../../terms/structural_order.hpp"
#include <limits>

namespace clause::runtime::builtins::lists {
std::optional<std::size_t> Count::natural() const {
    if (!term.is_integer()) {
        return std::nullopt;
    }
    if (const auto value = small(term.word())) {
        return *value < 0 ? std::nullopt : std::optional{static_cast<std::size_t>(*value)};
    }
    const auto digits = need(term.integer_decimal());
    return digits.starts_with('-') ? std::nullopt : std::optional{std::numeric_limits<std::size_t>::max()};
}

std::vector<Term> proper(const Term &list) {
    std::vector<Term> elements;
    auto rest = list;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        elements.push_back(need(rest.head()));
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return elements;
}

bool equal(const Term &left, const Term &right) { return need(detail::structural_order(left, right)) == 0; }

bool before(const Term &left, const Term &right) { return need(detail::structural_order(left, right)) < 0; }
} // namespace clause::runtime::builtins::lists
