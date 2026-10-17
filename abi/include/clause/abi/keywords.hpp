#pragma once
#include <algorithm>
#include <array>
#include <string_view>

// Erlang's reserved words, shared by the compiler and the runtime: erl_scan:reserved_word/1's words plus the keywords
// of the features OTP 29 enables by default (maybe_expr: maybe, else). The lexer starts with them as its keywords, and
// the compiler's and runtime's atom printers quote atoms spelled like them, as io_lib does.
namespace clause::abi::v1 {
inline constexpr std::array<std::string_view, 29> RESERVED_WORDS{
    "after", "and",   "andalso", "band",   "begin",   "bnot", "bor", "bsl",  "bsr", "bxor",
    "case",  "catch", "cond",    "div",    "else",    "end",  "fun", "if",   "let", "maybe",
    "not",   "of",    "or",      "orelse", "receive", "rem",  "try", "when", "xor"};
// Kept sorted for the binary search in reserved_word.
static_assert(std::ranges::is_sorted(RESERVED_WORDS));

// Whether a spelling (ASCII words in UTF-8 or UTF-32 code units) is one of RESERVED_WORDS.
template <typename Char> constexpr bool reserved_word(std::basic_string_view<Char> spelling) {
    const auto less = [](const auto &a, const auto &b) {
        return std::ranges::lexicographical_compare(
            a, b, [](auto x, auto y) { return static_cast<char32_t>(x) < static_cast<char32_t>(y); });
    };
    return std::ranges::binary_search(RESERVED_WORDS, spelling, less);
}
} // namespace clause::abi::v1
