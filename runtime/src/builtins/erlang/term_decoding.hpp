#pragma once
#include "../support.hpp"
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

// binary_to_term/1 of bytes held outside the heap (plan 21 step LIB8B: records:get_definition/2 decodes the field
// list a compiled RecordDescriptor holds).
namespace clause::runtime::builtins {
// The term `bytes` encode, built in the process heap; badarg for malformed bytes.
Term decode_external(ProcessContext &context, std::string_view bytes);
// The term at the start of `bytes` and the bytes it used, built in the process heap; none for malformed bytes (plan
// 30 step DIST4b3a: a packet's control message and payload follow each other).
std::optional<std::pair<Term, std::size_t>> decode_external_prefix(ProcessContext &context, std::string_view bytes);
} // namespace clause::runtime::builtins
