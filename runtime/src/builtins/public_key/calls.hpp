#pragma once
// Added for public_key's names (plan 21 step LIB80E3): calls of other registered builtins (string, unicode,
// uri_string) from public_key's walks, so that their results and errors stay OTP's.
#include "../support.hpp"
#include <initializer_list>
#include <string_view>

namespace clause::runtime::builtins::public_key {
// A failure a called builtin already recorded; the public_key builtin unwinds and returns 0 with it.
struct Recorded {};

// Module:Function(Args) of a registered builtin; throws Recorded when it fails.
Term call(ProcessContext &context, std::string_view module, std::string_view function,
          std::initializer_list<Term> args);

// A builtin's walk: its result, or 0 after a failure a called builtin recorded.
template <typename Body> Word recorded_or(Body &&body) {
    try {
        return body().word();
    } catch (const Recorded &) {
        return 0;
    }
}
} // namespace clause::runtime::builtins::public_key
