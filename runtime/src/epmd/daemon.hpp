#pragma once
#include <cstdint>

// clause-epmd -daemon (plan 30 step DIST3c, docs/distribution.md#epmd): detaching from the caller as OTP's
// epmd -daemon does.
namespace clause::runtime::epmd {
// What the process that called detach() does next.
enum class Detached : std::uint8_t {
    // Serve: this is the detached process.
    serve,
    // Exit with status 0: the detached process was started.
    started,
    // Exit with status 1: detaching failed.
    failed,
};

// POSIX: fork, setsid and fork again, standard I/O on /dev/null; the parents exit. Windows: start this program again
// with its arguments and -detached, as a new process group without console or inherited handles.
Detached detach();

// Put standard input, output and error on the null device; the detached Windows process (-detached) starts so.
void null_standard_io();
} // namespace clause::runtime::epmd
