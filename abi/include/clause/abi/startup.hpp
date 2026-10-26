#pragma once
#include "modules.hpp"

namespace clause::abi::v1 {
// Exit statuses fixed by docs/executables.md; halt/1 statuses pass through unchanged.
inline constexpr int exit_uncaught = 1;
inline constexpr int exit_runtime_failure = 70;
inline constexpr int exit_escript_uncaught = 127;

// Startup option bits; unknown bits are rejected as an ABI mismatch.
inline constexpr std::uint32_t startup_escript = 1;
// The entry is FUNCTION/0: it runs without the program arguments.
inline constexpr std::uint32_t startup_no_arguments = 2;

// A runtime extension archive's entry (clause_runtime_crypto): returns its runtime-owned extension table. Generated
// code only stores the pointer, so referencing it is what links the archive. Added for the crypto runtime archive.
using ExtensionEntry = const void *(*)() noexcept;

struct StartupDescriptor {
    // Reject a startup object built for another ABI revision or term width before reading further fields.
    std::uint32_t abi_version;
    std::uint32_t term_bits;
    // Borrow every module descriptor of the batch in registration order.
    const ModuleDescriptor *const *modules;
    std::size_t module_count;
    // Borrow exact UTF-8 module/function spellings of the entry: arity 1 (the argument list), or 0 with
    // startup_no_arguments.
    const char *entry_module;
    std::size_t entry_module_size;
    const char *entry_function;
    std::size_t entry_function_size;
    // Combine startup_* bits, such as escript exit rules.
    std::uint32_t flags;
    // Borrow the entries of the runtime extensions linked into the program, whose builtins startup registers.
    const ExtensionEntry *extensions;
    std::size_t extension_count;
};

static_assert(std::is_standard_layout_v<StartupDescriptor>);
static_assert(offsetof(StartupDescriptor, modules) == 8);
static_assert(offsetof(StartupDescriptor, flags) == 8 + 6 * sizeof(TermWord));
static_assert(offsetof(StartupDescriptor, extensions) == 8 + 7 * sizeof(TermWord));
} // namespace clause::abi::v1

// Run a whole program: start the runtime, register modules, call the entry with argv and return the exit status.
// Called by the generated native `main`; it contains every exception and never returns to Erlang code.
int CLAUSE_main_v1(int argc, char **argv, const void *startup) noexcept;

// The extension entry of the crypto runtime archive (clause_runtime_crypto, plan 21 step LIB79): its builtins and
// OpenSSL. Defined only in that archive.
const void *CLAUSE_crypto_extension_v1() noexcept;
// The crypto archive's entry for programs built with clau --fips (plan 21 step LIB79F): the same builtins, after a
// start hook loads the FIPS provider beside the executable and keeps FIPS mode on.
const void *CLAUSE_crypto_fips_extension_v1() noexcept;

// Stop the program through erlang:halt/0,1: records the halt (or badarg) in the checked channel and never succeeds.
std::uint8_t CLAUSE_halt_v1(void *context, clause::abi::v1::TermWord status) noexcept;
