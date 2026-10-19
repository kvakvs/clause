#pragma once
#include "../support.hpp"
#include <string>

// term_to_binary/1 of a term into bytes held outside the heap (plan 21 step LIB53: dets stores objects as their
// external format).
namespace clause::runtime::builtins {
// The external format of `term` with term_to_binary/1's defaults.
std::string encode_external(ProcessContext &context, const Term &term);
} // namespace clause::runtime::builtins
