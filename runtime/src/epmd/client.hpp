#pragma once
#include <cstdint>

// clause-epmd's questions to a running epmd (plan 30 step DIST3c, docs/distribution.md#epmd): -names and -kill, with
// OTP's epmd messages and exit statuses.
namespace clause::runtime::epmd {
// Print what the epmd on `port` of this host registered, as epmd -names; the exit status.
int print_names(std::uint16_t port);

// Ask the epmd on `port` of this host to stop, as epmd -kill; the exit status.
int kill_epmd(std::uint16_t port);
} // namespace clause::runtime::epmd
