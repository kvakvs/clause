#pragma once
#include "../../scheduler/connections.hpp"
#include "../support.hpp"
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// Messages across nodes (plan 30 step DIST4b3a, docs/distribution.md#messages): sends to another node's processes as
// OTP's pass-through packets on its connection, and the packets a connection's input handler puts, delivered or, for
// process monitors, handed to the executor (DIST4b3b1).
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
// The pass-through packet of a control message built in `context`'s heap and its payload, if any (plan 30 step
// DIST4b3b1: the runtime's monitor signals).
std::vector<std::byte> dist_packet(ProcessContext &context, const Term &control, const std::optional<Term> &payload);
// Take a packet of `source`: deliver its message, act on its monitor signal, or kill the controller and write OTP's
// report when the packet cannot be read. A killed controller may make the caller's builtin run again first, so nothing
// else is changed before.
void take_packet(ProcessContext &context, const PacketSource &source, std::span<const std::byte> packet);
// Make `leader`, a pid of any node, the group leader of `member`, another node's pid in a buffer copied from
// `builder`'s heap (plan 30 step DIST5b1); a local one may be this node's standard I/O (DIST7c). The executor's lock
// is held.
void lead(ProcessContext &member, const Term &leader, ProcessContext &builder);
// The group leader `process` reports, as a term of `builder`: `user` (a pid word, 0 when none is registered) for this
// node's standard I/O, else its leader's pid, its own when it leads itself, or another node's (plan 30 step DIST7c).
Term reported_leader(ProcessContext &process, ProcessContext &builder, Word user);
// group_leader(Leader, Pid) of another node's pid: GROUP_LEADER on its connection, auto-connected; nothing when it
// cannot be reached (plan 30 step DIST5b1).
void send_group_leader(ProcessContext &context, const Term &leader, const Term &pid);
} // namespace clause::runtime::builtins
