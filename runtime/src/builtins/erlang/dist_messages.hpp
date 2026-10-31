#pragma once
#include "../../scheduler/connections.hpp"
#include "../support.hpp"
#include <cstddef>
#include <span>

// Messages across nodes (plan 30 step DIST4b3a, docs/distribution.md#messages): sends to another node's processes as
// OTP's pass-through packets on its connection, and the packets a connection's input handler puts, delivered.
namespace clause::runtime::builtins {
// A send to another node's process: another node's pid or alias, or {Name, Node} of another node; the message; and
// whether an idle connection may be made (false: noconnect).
struct RemoteMessage {
    Term destination;
    Term message;
    bool connect = true;
};

// The connection a packet came on: its node (an atom word) and its controller's pid word.
struct PacketSource {
    Word node = 0;
    Word controller = 0;
};

// Send a message to another node's process from the caller.
detail::DistSend send_remote(ProcessContext &context, const RemoteMessage &send);
// Take a packet of `source`: deliver its message, or kill the controller and write OTP's report when the packet
// cannot be read. A killed controller may make the caller's builtin run again first, so nothing else is changed
// before.
void take_packet(ProcessContext &context, const PacketSource &source, std::span<const std::byte> packet);
} // namespace clause::runtime::builtins
