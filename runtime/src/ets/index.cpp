#include "index.hpp"
#include "../memory/heap_object.hpp"
#include "../terms/structural_order.hpp"
#include <clause/abi/term.hpp>

// Key and object comparisons of ETS indexes (plan 21 step LIB10A).
namespace clause::runtime::detail::ets {
namespace {
// Whether a word points at a heap object; other words are equal terms exactly when the words are.
bool compound(Word value) noexcept {
    const auto primary = value & static_cast<Word>(abi::v1::primary_mask);
    return primary == abi::v1::list_tag || primary == abi::v1::boxed_tag;
}

// The exact structural comparison of two words; a malformed term compares unequal.
bool exactly_equal(Word left, Word right) noexcept {
    if (left == right) {
        return true;
    }
    if (!compound(left) || !compound(right)) {
        return false;
    }
    const auto order = structural_order(TermAccess::make(left), TermAccess::make(right), true);
    return order && *order == 0;
}
} // namespace

Word key_of(Word object, std::size_t keypos) noexcept {
    return TermAccess::make(object).tuple_element_unchecked(keypos - 1).word();
}

bool same_key(Word left, Word right) noexcept { return exactly_equal(left, right); }

bool same_object(Word left, Word right) noexcept { return exactly_equal(left, right); }

int key_order(Word left, Word right) noexcept {
    if (left == right) {
        return 0;
    }
    if (const auto order = small_order(left, right)) {
        return *order;
    }
    return structural_order(TermAccess::make(left), TermAccess::make(right), false).value_or(0);
}
} // namespace clause::runtime::detail::ets
