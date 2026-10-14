#include "../typed.hpp"
#include <array>
#include <pcre2.h>
#include <terms.hpp>

// The re module's PCRE2 queries (plan step 41ZA, docs/builtins.md#re-module): the runtime links the PCRE2 release
// OTP 29 bundles (docs/compile.md#pcre2), so its version string is OTP's.
namespace clause::runtime::builtins {
namespace {
// re:version(): the PCRE2 version and release date as a binary, as OTP answers.
TermResult<Term> version(ProcessContext &context) {
    std::array<char, 64> text{};
    const auto size = pcre2_config(PCRE2_CONFIG_VERSION, text.data());
    if (size <= 1) {
        bad_argument();
    }
    // The size counts the terminating zero.
    return TermFactory(context).binary(std::as_bytes(std::span(text.data(), static_cast<std::size_t>(size) - 1)));
}

constexpr std::array RE_BUILTINS{
    typed_entry<version>("re", "version"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> re_builtins() noexcept { return builtins::RE_BUILTINS; }
} // namespace clause::runtime
