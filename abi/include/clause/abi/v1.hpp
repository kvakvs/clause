#pragma once
#include <cstdint>

namespace clause::runtime {
class ProcessContext;
}

namespace clause::abi::v1 {
// Identify this project's generated-code contract; no external language compatibility is promised.
// Revision 7 adds fun descriptors to modules; revision 9 starts every context with abi::v1::FrameState (OPT0.1);
// v1 names the unchanged term encoding.
inline constexpr std::uint32_t version = 9;
// Decode the shared immediate-term representation without preprocessor constants.
inline constexpr unsigned primary_mask = 0x3;
inline constexpr unsigned small_integer_tag = 0xf;
inline constexpr unsigned small_integer_bits = 4;

// Match the runtime target's unsigned pointer width, independently of the compiler host target.
using TermWord = std::uintptr_t;
// Borrow a real runtime context without exposing its implementation to generated-code consumers.
using Context = runtime::ProcessContext;
// Use the native free-function convention; arity is resolved separately and zero arguments permit null.
using GeneratedFunction = TermWord(Context *, const TermWord *);
// Generated continuation code: entered with the process context, it leaves only by tail transfers.
using Code = void(void *);

static_assert(sizeof(TermWord) == 4 || sizeof(TermWord) == 8);
static_assert(alignof(TermWord) == sizeof(TermWord));
} // namespace clause::abi::v1
