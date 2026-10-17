#pragma once
#include "../support.hpp"

// unicode_util's normalization of one grapheme cluster over terms (plan 21 step LIB4, docs/builtins.md#unicode-util):
// nfd/1, nfkd/1, nfc/1 and nfkc/1 return [NormalizedCluster | Rest] in OTP's shapes (a cluster of one code point as
// the integer), [] at the end, {error, Rest} at invalid UTF-8.
namespace clause::runtime::builtins::unicode {
enum class Form : std::uint8_t { nfd, nfkd, nfc, nfkc };

Term normalized_term(ProcessContext &context, const Term &data, Form form);
} // namespace clause::runtime::builtins::unicode
