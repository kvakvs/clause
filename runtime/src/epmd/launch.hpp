#pragma once
#include <string>

// Starting an epmd program for --epmd external (plan 30 step DIST3d, docs/distribution.md#epmd).
namespace clause::runtime::epmd {
// Start `program` (UTF-8 path) with -daemon, with standard I/O on the null device and no inherited handles; whether
// it started. POSIX waits for the daemon's first process, which exits once it forked; Windows starts it detached, as a
// new process group without console, and does not wait.
bool start_daemon(const std::string &program);
} // namespace clause::runtime::epmd
