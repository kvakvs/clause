#include "list_walk.hpp"
#include "../../terms/structural_order.hpp"
#include "../../terms/word_order.hpp"
#include "../portions.hpp"
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
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        elements.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return elements;
}

bool equal(const Term &left, const Term &right) { return need(detail::structural_order(left, right)) == 0; }

bool before(const Term &left, const Term &right) { return need(detail::structural_order(left, right)) < 0; }

std::vector<Word> proper_words(Term list) {
    std::vector<Word> words;
    walk(list, std::numeric_limits<std::size_t>::max(), [&](Word head) { words.push_back(head); });
    if (!list.is_nil()) {
        no_clause();
    }
    return words;
}

bool before(ProcessHeap &heap, Word left, Word right) {
    const auto order = detail::trusted_word_order(heap, left, right);
    return order ? *order < 0 : before(detail::TermAccess::make(left), detail::TermAccess::make(right));
}
} // namespace clause::runtime::builtins::lists
