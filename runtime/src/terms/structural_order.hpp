#pragma once
#include <clause/abi/v1.hpp>
#include <clause/runtime/terms.hpp>
#include <optional>
#include <type_traits>

namespace clause::runtime::detail {
// The order of two words when the words alone decide it (cheap term access): two small integers by value. Equal
// words are equal terms too, but callers keep their validation for those.
inline std::optional<int> small_order(Word left, Word right) noexcept {
    constexpr Word tag = abi::v1::small_integer_tag;
    if ((left & tag) != tag || (right & tag) != tag) {
        return std::nullopt;
    }
    // Signed at the word's own width: a 32-bit word widened unsigned would order negatives last.
    const auto a = static_cast<std::make_signed_t<Word>>(left);
    const auto b = static_cast<std::make_signed_t<Word>>(right);
    return a < b ? -1 : (a > b ? 1 : 0);
}

// Compare admitted immutable graphs iteratively with no work cap, as in OTP; exact mode retains atom identity across
// host runtimes.
TermResult<int> structural_order(const Term &left, const Term &right, bool exact = false) noexcept;
} // namespace clause::runtime::detail
