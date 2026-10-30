#pragma once
#include "registry.hpp"
#include <cstdint>
#include <string>
#include <string_view>

// The epmd requests (ALIVE2, PORT_PLEASE2, NAMES, DUMP, KILL, STOP) with OTP's replies and command checks
// (docs/distribution.md#epmd). Created for plan 30 step DIST3a; the server only moves the bytes.
namespace clause::runtime::epmd {
// What the connection does once the reply is written.
enum class After : std::uint8_t {
    // Close the connection.
    close,
    // Wait for another request: a refused registration keeps its connection, as with OTP's epmd.
    keep_reading,
    // The connection now holds the registration of Outcome::name_ and ends it when it closes.
    registered,
    // Stop the epmd.
    kill,
};

// The answer to one request.
struct Outcome {
    // Written before After applies; empty for a request closed without reply.
    std::string reply_;
    After after_ = After::close;
    // The registered name (After::registered), or the node a STOP request ends.
    std::string name_;
};

// The epmd's settings that requests see.
struct Settings {
    // The port the epmd listens on, which NAMES and DUMP replies start with.
    std::uint16_t port_ = 0;
    // -relaxed_command_check: KILL with registered names, and STOP.
    bool relaxed_ = false;
};

// The connection a request came on.
struct Requester {
    // Whether the peer is on this host; others may only ask PORT_PLEASE2 and NAMES.
    bool local_ = false;
    // The connection's descriptor, kept for DUMP.
    std::uint64_t descriptor_ = 0;
};

// Answer one request packet (without its 2-byte length) of a connection that holds no registration.
Outcome handle_request(Registry &registry, const Settings &settings, const Requester &requester,
                       std::string_view packet);
} // namespace clause::runtime::epmd
