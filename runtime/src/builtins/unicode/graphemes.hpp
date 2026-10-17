#pragma once
#include <cstddef>
#include <string_view>

// Grapheme clusters over code points (plan 21 step LIB5G, docs/builtins.md#unicode-util): the rules of Clause's
// unicode_util:gc/1 (OTP's order of UAX #29) on a flat sequence, for the builtins that need cluster boundaries.
namespace clause::runtime::builtins::unicode {
// The number of code points of the cluster starting at `at` (at least 1, when at < codes.size()).
std::size_t grapheme_length(std::u32string_view codes, std::size_t at);
} // namespace clause::runtime::builtins::unicode
