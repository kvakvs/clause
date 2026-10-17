#pragma once
#include <clause/runtime/process_context.hpp>
#include <clause/runtime/terms.hpp>
#include <cstdint>

namespace clause::runtime::builtins {
// OTP's make_hash2, the hash behind phash2: equal for exactly equal terms in every program run. Shared with the
// persistent term builtins (step 41M2).
std::uint64_t term_hash(ProcessContext &context, const Term &term);

// OTP's make_hash, the hash behind erlang:phash/2, by which dict and sets (version 1) place keys in their buckets
// (plan 21 step LIB2).
std::uint32_t make_hash(ProcessContext &context, const Term &term);
} // namespace clause::runtime::builtins
