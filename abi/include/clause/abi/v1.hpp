#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace clause::runtime {
class ProcessContext;
}

namespace clause::abi::v1 {
// Identify this project's generated-code contract; no external language compatibility is promised.
// Revision 7 adds fun descriptors to modules; revision 9 starts every context with abi::v1::FrameState (OPT0.1);
// revision 10 passes the leading X registers as code parameters (OPT0.1A); revision 11 fixes the words of false and
// true (OPT0.2A); revision 12 adds the inline heap fields to FrameState (OPT0.3); revision 13 fixes more atoms and adds
// the module atom cache and FrameState::runtime (OPT0.8); revision 14 adds module literal areas (OPT0.4); revision 15
// adds FrameState::self (OPT0.2D); revision 16 records the Code convention in module descriptors; revision 17 passes
// six X registers as Code parameters; revision 18 adds sub-binary cells (OPT1.4B); v1 names the unchanged term encoding.
inline constexpr std::uint32_t version = 18;
// Decode the shared immediate-term representation without preprocessor constants.
inline constexpr unsigned primary_mask = 0x3;
inline constexpr unsigned small_integer_tag = 0xf;
inline constexpr unsigned small_integer_bits = 4;
// Atom words are the atom number shifted past the six atom tag bits (OPT0.2A). Every runtime enters these atoms
// first, with the numbers of their positions, so generated code and services use their words without a lookup
// (false and true since OPT0.2A, the others since OPT0.8).
inline constexpr unsigned atom_tag_bits = 6;
inline constexpr unsigned atom_tag = 0x0b;
inline constexpr std::array<std::string_view, 5> fixed_atom_spellings{"false", "true", "undefined", "ok", "error"};

constexpr std::size_t fixed_atom(const std::size_t number) noexcept { return (number << atom_tag_bits) | atom_tag; }

inline constexpr std::size_t false_atom = fixed_atom(0);
inline constexpr std::size_t true_atom = fixed_atom(1);
inline constexpr std::size_t undefined_atom = fixed_atom(2);

// Match the runtime target's unsigned pointer width, independently of the compiler host target.
using TermWord = std::uintptr_t;
// Borrow a real runtime context without exposing its implementation to generated-code consumers.
using Context = runtime::ProcessContext;
// Use the native free-function convention; arity is resolved separately and zero arguments permit null.
using GeneratedFunction = TermWord(Context *, const TermWord *);
// X registers every code transfer passes as parameters after the context (OPT0.1A): arguments on entry, the result
// when a body resumes after a call. The others, and these whenever a service runs, live in the register array.
// Six since revision 17: with preserve_none every one is a register on x86-64 and AArch64 (OPT0.1A follow-up).
inline constexpr std::size_t register_arguments = 6;
// Generated continuation code: entered with the process context and x0..x5, it leaves only by tail transfers. Where
// the compiler supports it (Clang on x86-64 and AArch64), Code keeps no callee-saved registers: it never returns
// normally, so saving them on every entry is waste (preserve_none, OPT0.1A follow-up). C++ code defining a Code
// function marks it CLAUSE_CODE_CONVENTION.
#if defined(__clang__) && (defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64))
#define CLAUSE_CODE_CONVENTION __attribute__((preserve_none))
inline constexpr bool code_preserve_none = true;
#else
#define CLAUSE_CODE_CONVENTION
inline constexpr bool code_preserve_none = false;
#endif
using Code = void CLAUSE_CODE_CONVENTION(void *, TermWord, TermWord, TermWord, TermWord, TermWord, TermWord);

static_assert(sizeof(TermWord) == 4 || sizeof(TermWord) == 8);
static_assert(alignof(TermWord) == sizeof(TermWord));
} // namespace clause::abi::v1
