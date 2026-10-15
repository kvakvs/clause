#pragma once
#include <cstddef>
#include <cstdint>

namespace clause::runtime {
class ProcessContext;
}

namespace clause::abi::v1 {
// Identify this project's generated-code contract; no external language compatibility is promised.
// Revision 7 adds fun descriptors to modules; revision 9 starts every context with abi::v1::FrameState (OPT0.1);
// revision 10 passes the leading X registers as code parameters (OPT0.1A); revision 11 fixes the words of false and
// true (OPT0.2A); revision 12 adds the inline heap fields to FrameState (OPT0.3); v1 names the unchanged term encoding.
inline constexpr std::uint32_t version = 12;
// Decode the shared immediate-term representation without preprocessor constants.
inline constexpr unsigned primary_mask = 0x3;
inline constexpr unsigned small_integer_tag = 0xf;
inline constexpr unsigned small_integer_bits = 4;
// Atom words are the atom number shifted past the six atom tag bits (OPT0.2A). Every runtime gives false and true
// the first two numbers, so generated code and services use these words without a lookup.
inline constexpr unsigned atom_tag_bits = 6;
inline constexpr unsigned atom_tag = 0x0b;
inline constexpr std::size_t false_atom = (std::size_t{0} << atom_tag_bits) | atom_tag;
inline constexpr std::size_t true_atom = (std::size_t{1} << atom_tag_bits) | atom_tag;

// Match the runtime target's unsigned pointer width, independently of the compiler host target.
using TermWord = std::uintptr_t;
// Borrow a real runtime context without exposing its implementation to generated-code consumers.
using Context = runtime::ProcessContext;
// Use the native free-function convention; arity is resolved separately and zero arguments permit null.
using GeneratedFunction = TermWord(Context *, const TermWord *);
// X registers every code transfer passes as parameters after the context (OPT0.1A): arguments on entry, the result
// when a body resumes after a call. The others, and these whenever a service runs, live in the register array.
inline constexpr std::size_t register_arguments = 3;
// Generated continuation code: entered with the process context and x0..x2, it leaves only by tail transfers.
using Code = void(void *, TermWord, TermWord, TermWord);

static_assert(sizeof(TermWord) == 4 || sizeof(TermWord) == 8);
static_assert(alignof(TermWord) == sizeof(TermWord));
} // namespace clause::abi::v1
