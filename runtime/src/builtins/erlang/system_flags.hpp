#pragma once
#include <cstdint>

// The program-wide settings of erlang:system_flag/2 (plan 21 step LIB9A) other runtime parts read.
namespace clause::runtime::builtins {
// system_flag(trace_control_word, N)'s word, which match specifications read with get_tcw (plan 21 step LIB11).
std::int64_t system_trace_control_word() noexcept;
} // namespace clause::runtime::builtins
