#pragma once
#include <cstdint>

// Node mode's -detached (plan 30 step DIST10a, docs/executables.md#node-mode): the program goes on in the background
// on the null device and its caller returns at once, as OTP's erlexec starts a detached emulator.
namespace clause::runtime::detail {
// What the program that called detach() does next.
enum class Detached : std::uint8_t {
    // Run the node: this is the detached program.
    run,
    // Exit with status 0: the detached copy was started.
    started,
    // Fail: detaching was not possible.
    failed,
};

// POSIX: fork, setsid and fork again, standard I/O on /dev/null; the parents exit 0. Windows: start this program
// again with the same command line, without console or inherited handles, marked so that the copy runs instead of
// detaching again.
Detached detach();
} // namespace clause::runtime::detail
