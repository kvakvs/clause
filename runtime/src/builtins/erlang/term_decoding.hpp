#pragma once
#include "../support.hpp"
#include <string_view>

// binary_to_term/1 of bytes held outside the heap (plan 21 step LIB8B: records:get_definition/2 decodes the field
// list a compiled RecordDescriptor holds).
namespace clause::runtime::builtins {
// The term `bytes` encode, built in the process heap; badarg for malformed bytes.
Term decode_external(ProcessContext &context, std::string_view bytes);
} // namespace clause::runtime::builtins
