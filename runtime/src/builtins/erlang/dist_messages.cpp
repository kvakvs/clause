#include "dist_messages.hpp"
#include "../../process/exits.hpp"
#include "../../scheduler/executor.hpp"
#include "../../terms/identities.hpp"
#include "term_decoding.hpp"
#include "term_encoding.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/output.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Messages across nodes (plan 30 step DIST4b3a, docs/distribution.md#messages): a send to another node's process
// becomes OTP's pass-through packet (112, then the control message and the payload, each in the external format) on
// the node's connection. The packets a connection's input handler puts are read as OTP 29 reads them (observed):
// messages are delivered, process monitor signals go to the executor (DIST4b3b1), other operations of the right shape
// wait for DIST5, and unreadable packets kill the controller after OTP's report.
namespace clause::runtime::builtins {
namespace {
using detail::DistSend;
using detail::Executor;

// The control message operations Clause sends (erl_dist_protocol).
constexpr std::int64_t SEND = 2;
constexpr std::int64_t REG_SEND = 6;
constexpr std::int64_t SEND_SENDER = 22;
constexpr std::int64_t ALIAS_SEND = 33;
// The first byte of a pass-through packet, and of a term in the external format.
constexpr auto PASS_THROUGH = std::byte{112};
constexpr auto VERSION = std::byte{131};
// The shortest packet OTP reads; a shorter one ends the connection without a report.
constexpr std::size_t SHORTEST_PACKET = 3;

// The node of another node's pid or reference.
Term node_of(ProcessContext &context, const Term &identity) {
    return need(Term::from_word(need(detail::ExternalAccess::read(identity)).node, context));
}

// A send's control message from the caller to `destination`, and the node it goes to. A pid's is SEND without the
// sender until the node's connection is up, as OTP's.
std::pair<Term, Term> control_of(ProcessContext &context, const Term &destination) {
    TermFactory factory(context);
    const auto self = need(factory.pid(context.identity()));
    const auto operation = [&](std::int64_t code) { return need(factory.integer(code)); };
    if (destination.is_external_pid()) {
        const auto node = node_of(context, destination);
        if (!Executor::of(context).connected_creation(node.word())) {
            const std::array parts{operation(SEND), atom(context, std::string_view{}), destination};
            return {need(factory.tuple(parts)), node};
        }
        return {need(factory.tuple(std::array{operation(SEND_SENDER), self, destination})), node};
    }
    if (destination.is_external_reference()) {
        return {need(factory.tuple(std::array{operation(ALIAS_SEND), self, destination})),
                node_of(context, destination)};
    }
    const std::array parts{operation(REG_SEND), self, atom(context, std::string_view{}),
                           destination.tuple_element_unchecked(0)};
    return {need(factory.tuple(parts)), destination.tuple_element_unchecked(1)};
}

// Which element of a control message names the receiver of its message: a pid, a registered name or an alias; none
// for the operations that bring no message to deliver.
enum class Receiver : std::uint8_t { none, pid, name, alias };

// The process monitor signal of an operation the executor acts on (DIST4b3b1), or none.
enum class Signal : std::uint8_t { none, monitor, demonitor, monitor_exit };

// The shape of one of OTP's control messages: its operation, its tuple's arity, whether a payload follows, the
// receiver of a message and its element, and its monitor signal.
struct Shape {
    std::int64_t operation = 0;
    std::size_t arity = 0;
    bool payload = false;
    Receiver receiver = Receiver::none;
    std::size_t element = 0;
    Signal signal = Signal::none;
};

// Every operation of OTP's protocol for the offered flags; the sends and their _TT forms (a trace token last) deliver.
constexpr std::array SHAPES{
    Shape{.operation = 1, .arity = 3},                                                           // LINK
    Shape{.operation = 2, .arity = 3, .payload = true, .receiver = Receiver::pid, .element = 2}, // SEND
    Shape{.operation = 3, .arity = 4},                                                           // EXIT
    Shape{.operation = 4, .arity = 3},                                                           // UNLINK
    Shape{.operation = 5, .arity = 1},                                                           // NODE_LINK
    Shape{.operation = REG_SEND, .arity = 4, .payload = true, .receiver = Receiver::name, .element = 3},
    Shape{.operation = 7, .arity = 3},                                                             // GROUP_LEADER
    Shape{.operation = 8, .arity = 4},                                                             // EXIT2
    Shape{.operation = 12, .arity = 4, .payload = true, .receiver = Receiver::pid, .element = 2},  // SEND_TT
    Shape{.operation = 13, .arity = 5},                                                            // EXIT_TT
    Shape{.operation = 16, .arity = 5, .payload = true, .receiver = Receiver::name, .element = 3}, // REG_SEND_TT
    Shape{.operation = 18, .arity = 5},                                                            // EXIT2_TT
    Shape{.operation = 19, .arity = 4, .signal = Signal::monitor},                                 // MONITOR_P
    Shape{.operation = 20, .arity = 4, .signal = Signal::demonitor},                               // DEMONITOR_P
    Shape{.operation = 21, .arity = 5, .signal = Signal::monitor_exit},                            // MONITOR_P_EXIT
    Shape{.operation = SEND_SENDER, .arity = 3, .payload = true, .receiver = Receiver::pid, .element = 2},
    Shape{.operation = 23, .arity = 4, .payload = true, .receiver = Receiver::pid, .element = 2}, // SEND_SENDER_TT
    Shape{.operation = 24, .arity = 3, .payload = true},                                          // PAYLOAD_EXIT
    Shape{.operation = 25, .arity = 4, .payload = true},                                          // PAYLOAD_EXIT_TT
    Shape{.operation = 26, .arity = 3, .payload = true},                                          // PAYLOAD_EXIT2
    Shape{.operation = 27, .arity = 4, .payload = true},                                          // PAYLOAD_EXIT2_TT
    Shape{.operation = 28, .arity = 4, .payload = true, .signal = Signal::monitor_exit}, // PAYLOAD_MONITOR_P_EXIT
    Shape{.operation = 29, .arity = 6, .payload = true},                                 // SPAWN_REQUEST
    Shape{.operation = 30, .arity = 7, .payload = true},                                 // SPAWN_REQUEST_TT
    Shape{.operation = 31, .arity = 5},                                                  // SPAWN_REPLY
    Shape{.operation = 32, .arity = 6},                                                  // SPAWN_REPLY_TT
    Shape{.operation = ALIAS_SEND, .arity = 3, .payload = true, .receiver = Receiver::alias, .element = 2},
    Shape{.operation = 34, .arity = 4, .payload = true, .receiver = Receiver::alias, .element = 2}, // ALIAS_SEND_TT
    Shape{.operation = 35, .arity = 4},                                                             // UNLINK_ID
    Shape{.operation = 36, .arity = 4},                                                             // UNLINK_ID_ACK
};

// Whether `term` can name the receiver of `kind`, a process or alias of any node.
bool names_receiver(Receiver kind, const Term &term) {
    switch (kind) {
    case Receiver::pid:
        return term.is_pid() || term.is_external_pid();
    case Receiver::name:
        return term.is_atom();
    case Receiver::alias:
        return term.is_reference() || term.is_external_reference();
    case Receiver::none:
        break;
    }
    return true;
}

// Whether `term` is a reference of any node.
bool any_reference(const Term &term) { return term.is_reference() || term.is_external_reference(); }

// Whether `term` is a pid of the other node `node`, or of an earlier incarnation of this node.
bool pid_of_node(ProcessContext &context, const Term &term, Word node) {
    return term.is_external_pid() && node_of(context, term).word() == node;
}

bool pid_of_old_self(ProcessContext &context, const Term &term) {
    return term.is_external_pid() && detail::is_this_node(node_of(context, term));
}

// Whether a monitor signal from `node` names what OTP's dist.c accepts: MONITOR_P and DEMONITOR_P a watcher of that
// node and a pid or name here (or of an earlier incarnation), MONITOR_P_EXIT a pid or name there and a pid here.
bool well_formed(ProcessContext &context, const Shape &shape, const Term &control, Word node) {
    const auto element = [&](std::size_t index) { return control.tuple_element_unchecked(index); };
    if (shape.signal == Signal::none) {
        return true;
    }
    if (!any_reference(element(3))) {
        return false;
    }
    if (shape.signal == Signal::monitor_exit) {
        const auto watcher = element(2);
        return (element(1).is_external_pid() || element(1).is_atom()) &&
               (watcher.is_pid() || pid_of_old_self(context, watcher));
    }
    const auto watched = element(2);
    return pid_of_node(context, element(1), node) &&
           (watched.is_pid() || watched.is_atom() || pid_of_old_self(context, watched));
}

// Whether an operation's payload is read: a message to deliver, or a monitor's exit reason.
bool reads_payload(const Shape &shape) {
    return shape.receiver != Receiver::none || (shape.payload && shape.signal == Signal::monitor_exit);
}

// The shape of a control message, or null for anything OTP calls an invalid distribution message.
const Shape *shape_of(const Term &control) {
    if (!control.is_tuple() || control.tuple_size_unchecked() == 0) {
        return nullptr;
    }
    const auto operation = small(control.tuple_element_unchecked(0).word());
    const auto size = control.tuple_size_unchecked();
    const auto found = std::ranges::find_if(SHAPES, [&](const Shape &candidate) {
        return operation && candidate.operation == *operation && candidate.arity == size;
    });
    if (found == SHAPES.end() || !names_receiver(found->receiver, control.tuple_element_unchecked(found->element))) {
        return nullptr;
    }
    return &*found;
}

// Why a packet cannot be read, as OTP reports it: too short (no report), not OTP's format, a term that does not
// decode, or a control message of no known shape.
enum class Fault : std::uint8_t { none, short_packet, incompatible, corrupted, invalid };

// A packet as read: its control message's shape and its message, or its fault with the control message (invalid) or
// the bytes from the term that failed (corrupted).
struct Reading {
    Fault fault = Fault::none;
    const Shape *shape = nullptr;
    Term control{};
    Term message{};
    std::span<const std::byte> failed{};
};

// The term in the external format at the start of `bytes` and the bytes it used; none when it does not decode.
std::optional<std::pair<Term, std::size_t>> term_at(ProcessContext &context, std::span<const std::byte> bytes) {
    if (bytes.empty() || bytes.front() != VERSION) {
        return std::nullopt;
    }
    return decode_external_prefix(context,
                                  std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

// Read a packet from `node` as OTP does; the payload only of operations that use it.
Reading read_packet(ProcessContext &context, Word node, std::span<const std::byte> packet) {
    if (packet.size() < SHORTEST_PACKET) {
        return {.fault = Fault::short_packet};
    }
    if (packet[0] != PASS_THROUGH || packet[1] != VERSION) {
        return {.fault = Fault::incompatible};
    }
    const auto rest = packet.subspan(1);
    const auto control = term_at(context, rest);
    if (!control) {
        return {.fault = Fault::corrupted, .failed = rest};
    }
    const auto *shape = shape_of(control->first);
    if (shape == nullptr || !well_formed(context, *shape, control->first, node)) {
        return {.fault = Fault::invalid, .control = control->first};
    }
    if (!reads_payload(*shape)) {
        return {.shape = shape, .control = control->first};
    }
    const auto tail = rest.subspan(control->second);
    const auto message = term_at(context, tail);
    if (!message) {
        return {.fault = Fault::corrupted, .failed = tail};
    }
    return {.shape = shape, .control = control->first, .message = message->first};
}

// Deliver a message to the receiver its control message names on this node; another node's receiver, a name no
// process has and a reference that is no alias get nothing, as in OTP.
void deliver(ProcessContext &context, const Reading &reading) {
    const auto receiver = reading.control.tuple_element_unchecked(reading.shape->element);
    TermResult<void> sent;
    if (reading.shape->receiver == Receiver::pid && receiver.is_pid()) {
        sent = Executor::send(context, receiver.word(), reading.message);
    } else if (reading.shape->receiver == Receiver::name) {
        const auto pid = Executor::of(context).whereis(context, receiver.word());
        if (pid != 0 && TermTag{pid}.get_kind() != TermKind::local_port) {
            sent = Executor::send(context, pid, reading.message);
        }
    } else if (reading.shape->receiver == Receiver::alias && receiver.is_reference()) {
        sent = Executor::send_alias(context, receiver.reference_value_unchecked(), reading.message);
    }
    if (!sent) {
        throw BuiltinFailure{.term = sent.error()};
    }
}

// Hand a monitor signal to the executor; a MONITOR_P_EXIT to a pid of an earlier incarnation of this node does
// nothing.
void signal_monitor(ProcessContext &context, Word node, const Reading &reading) {
    const auto element = [&](std::size_t index) { return reading.control.tuple_element_unchecked(index); };
    auto &executor = Executor::of(context);
    switch (reading.shape->signal) {
    case Signal::monitor:
        executor.watch_remote(context, node, {.watcher = element(1), .watched = element(2), .reference = element(3)});
        break;
    case Signal::demonitor:
        executor.unwatch_remote(context, node, {.watcher = element(1), .watched = element(2), .reference = element(3)});
        break;
    case Signal::monitor_exit:
        if (element(2).is_pid()) {
            const auto reason = reading.shape->payload ? reading.message : element(4);
            executor.remote_down(context, node, {.reference = element(3), .watcher = element(2), .reason = reason});
        }
        break;
    case Signal::none:
        break;
    }
}

// A term as OTP's runtime prints it in its reports (erlang:display's form).
std::string shown(const Term &term) { return format_term(term, TermStyle::display).value_or(""); }

// The bytes of a corrupted term as OTP's report dumps them: <<...,B1,B2>>, or >> for none.
std::string dumped(std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return ">>";
    }
    std::string text = "<<...";
    for (const auto byte : bytes) {
        text += "," + std::to_string(std::to_integer<unsigned>(byte));
    }
    return text + ">>";
}

// OTP's report of an unreadable packet from `node`.
void report_fault(ProcessContext &context, Word node, const Reading &reading) {
    const auto channel = std::to_string(detail::node_channel(node));
    switch (reading.fault) {
    case Fault::incompatible:
        detail::report_error(context, "** Got message from incompatible erlang on channel " + channel + "\n");
        break;
    case Fault::invalid:
        detail::report_error(context, "Invalid distribution message: " + shown(reading.control));
        break;
    case Fault::corrupted:
        detail::report_warning(context,
                               shown(need(detail::this_node_atom(context))) + " got a corrupted external term from " +
                                   shown(need(Term::from_word(node, context))) + " on distribution channel " + channel +
                                   "\n" + dumped(reading.failed) + "\nATOM_CACHE_REF translations: none");
        break;
    case Fault::none:
    case Fault::short_packet:
        break;
    }
}
} // namespace

DistSend send_remote(ProcessContext &context, const RemoteMessage &send) {
    const auto [control, node] = control_of(context, send.destination);
    return Executor::of(context).dist_output(context, node, dist_packet(context, control, send.message), send.connect);
}

std::vector<std::byte> dist_packet(ProcessContext &context, const Term &control, const std::optional<Term> &payload) {
    const auto head = encode_external(context, control);
    const auto body = payload ? encode_external(context, *payload) : std::string{};
    std::vector<std::byte> bytes;
    bytes.reserve(1 + head.size() + body.size());
    bytes.push_back(PASS_THROUGH);
    for (const auto *part : {&head, &body}) {
        const auto *data = reinterpret_cast<const std::byte *>(part->data());
        bytes.insert(bytes.end(), data, data + part->size());
    }
    return bytes;
}

void take_packet(ProcessContext &context, const PacketSource &source, std::span<const std::byte> packet) {
    const auto reading = read_packet(context, source.node, packet);
    if (reading.fault == Fault::none) {
        if (reading.shape->receiver != Receiver::none) {
            deliver(context, reading);
        }
        signal_monitor(context, source.node, reading);
        return;
    }
    Executor::of(context).exit(context, source.controller, atom(context, "kill"_atom), false);
    report_fault(context, source.node, reading);
}
} // namespace clause::runtime::builtins
