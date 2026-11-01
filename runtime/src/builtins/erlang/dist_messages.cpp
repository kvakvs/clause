#include "dist_messages.hpp"
#include "../../process/exits.hpp"
#include "../../scheduler/executor.hpp"
#include "../../terms/identities.hpp"
#include "dist_spawns.hpp"
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
// messages are delivered, process monitor signals (DIST4b3b1) and link signals (DIST5a) go to the executor, group
// leaders are set (DIST5b1), spawns served and answered (DIST5b2), and unreadable packets kill the controller after
// OTP's report.
namespace clause::runtime::builtins {
namespace {
using detail::DistSend;
using detail::Executor;

// The control message operations Clause sends (erl_dist_protocol).
constexpr std::int64_t SEND = 2;
constexpr std::int64_t REG_SEND = 6;
constexpr std::int64_t GROUP_LEADER = 7;
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

// The process monitor signal (DIST4b3b1), group leader (DIST5b1), spawn (DIST5b2) or link signal (DIST5a) of an
// operation acted on, or none. The link signals come last.
enum class Signal : std::uint8_t {
    none,
    monitor,
    demonitor,
    monitor_exit,
    group_leader,
    spawn_request,
    spawn_reply,
    link,
    unlink,
    unlink_ack,
    link_exit,
    exit
};

// The shape of one of OTP's control messages: its operation, its tuple's arity, whether a payload follows, the
// receiver of a message and its element, its signal, and the element of an exit's reason when no payload brings it.
struct Shape {
    std::int64_t operation = 0;
    std::size_t arity = 0;
    bool payload = false;
    Receiver receiver = Receiver::none;
    std::size_t element = 0;
    Signal signal = Signal::none;
    std::size_t reason = 0;
};

// Every operation of OTP's protocol for the offered flags; the sends and their _TT forms (a trace token last) deliver.
constexpr std::array SHAPES{
    Shape{.operation = 1, .arity = 3, .signal = Signal::link},                                   // LINK
    Shape{.operation = 2, .arity = 3, .payload = true, .receiver = Receiver::pid, .element = 2}, // SEND
    Shape{.operation = 3, .arity = 4, .signal = Signal::link_exit, .reason = 3},                 // EXIT
    Shape{.operation = 5, .arity = 1},                                                           // NODE_LINK
    Shape{.operation = REG_SEND, .arity = 4, .payload = true, .receiver = Receiver::name, .element = 3},
    Shape{.operation = GROUP_LEADER, .arity = 3, .signal = Signal::group_leader},
    Shape{.operation = 8, .arity = 4, .signal = Signal::exit, .reason = 3},                        // EXIT2
    Shape{.operation = 12, .arity = 4, .payload = true, .receiver = Receiver::pid, .element = 2},  // SEND_TT
    Shape{.operation = 13, .arity = 5, .signal = Signal::link_exit, .reason = 4},                  // EXIT_TT
    Shape{.operation = 16, .arity = 5, .payload = true, .receiver = Receiver::name, .element = 3}, // REG_SEND_TT
    Shape{.operation = 18, .arity = 5, .signal = Signal::exit, .reason = 4},                       // EXIT2_TT
    Shape{.operation = 19, .arity = 4, .signal = Signal::monitor},                                 // MONITOR_P
    Shape{.operation = 20, .arity = 4, .signal = Signal::demonitor},                               // DEMONITOR_P
    Shape{.operation = 21, .arity = 5, .signal = Signal::monitor_exit},                            // MONITOR_P_EXIT
    Shape{.operation = SEND_SENDER, .arity = 3, .payload = true, .receiver = Receiver::pid, .element = 2},
    Shape{.operation = 23, .arity = 4, .payload = true, .receiver = Receiver::pid, .element = 2}, // SEND_SENDER_TT
    Shape{.operation = 24, .arity = 3, .payload = true, .signal = Signal::link_exit},             // PAYLOAD_EXIT
    Shape{.operation = 25, .arity = 4, .payload = true, .signal = Signal::link_exit},             // PAYLOAD_EXIT_TT
    Shape{.operation = 26, .arity = 3, .payload = true, .signal = Signal::exit},                  // PAYLOAD_EXIT2
    Shape{.operation = 27, .arity = 4, .payload = true, .signal = Signal::exit},                  // PAYLOAD_EXIT2_TT
    Shape{.operation = 28, .arity = 4, .payload = true, .signal = Signal::monitor_exit},  // PAYLOAD_MONITOR_P_EXIT
    Shape{.operation = 29, .arity = 6, .payload = true, .signal = Signal::spawn_request}, // SPAWN_REQUEST
    Shape{.operation = 30, .arity = 7, .payload = true, .signal = Signal::spawn_request}, // SPAWN_REQUEST_TT
    Shape{.operation = 31, .arity = 5, .signal = Signal::spawn_reply},                    // SPAWN_REPLY
    Shape{.operation = 32, .arity = 6, .signal = Signal::spawn_reply},                    // SPAWN_REPLY_TT
    Shape{.operation = ALIAS_SEND, .arity = 3, .payload = true, .receiver = Receiver::alias, .element = 2},
    Shape{.operation = 34, .arity = 4, .payload = true, .receiver = Receiver::alias, .element = 2}, // ALIAS_SEND_TT
    Shape{.operation = 35, .arity = 4, .signal = Signal::unlink},                                   // UNLINK_ID
    Shape{.operation = 36, .arity = 4, .signal = Signal::unlink_ack},                               // UNLINK_ID_ACK
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

// Whether `term` is a pid of any node (GROUP_LEADER names two, DIST5b1).
bool any_pid(const Term &term) { return term.is_pid() || term.is_external_pid(); }

// Whether `term` is a reference of any node.
bool any_reference(const Term &term) { return term.is_reference() || term.is_external_reference(); }

// Whether `term` is a pid of the other node `node`, or of an earlier incarnation of this node.
bool pid_of_node(ProcessContext &context, const Term &term, Word node) {
    return term.is_external_pid() && node_of(context, term).word() == node;
}

bool pid_of_old_self(ProcessContext &context, const Term &term) {
    return term.is_external_pid() && detail::is_this_node(node_of(context, term));
}

bool old_self_reference(ProcessContext &context, const Term &term) {
    return term.is_external_reference() && detail::is_this_node(node_of(context, term));
}

// Whether a signal is a link signal (DIST5a), and whether it is UNLINK_ID or UNLINK_ID_ACK, whose id comes before
// the sender and the receiver.
bool links(Signal signal) { return signal >= Signal::link; }

bool unlinks(Signal signal) { return signal == Signal::unlink || signal == Signal::unlink_ack; }

// Whether the id of UNLINK_ID or UNLINK_ID_ACK is one OTP's dist.c accepts: not negative, UNLINK_ID's not 0.
bool unlink_id(const Shape &shape, const Term &control) {
    const auto id = small(control.tuple_element_unchecked(1).word());
    return id && *id >= 0 && (*id != 0 || shape.signal == Signal::unlink_ack);
}

// Whether a link signal from `node` names what OTP's dist.c accepts: that node's pid as the sender and a pid here as
// the receiver, LINK and EXIT2 also a pid of an earlier incarnation; an unlink a valid id.
bool link_formed(ProcessContext &context, const Shape &shape, const Term &control, Word node) {
    const std::size_t first = unlinks(shape.signal) ? 2 : 1;
    const auto to = control.tuple_element_unchecked(first + 1);
    if (!pid_of_node(context, control.tuple_element_unchecked(first), node) ||
        (unlinks(shape.signal) && !unlink_id(shape, control))) {
        return false;
    }
    const bool earlier = shape.signal == Signal::link || shape.signal == Signal::exit;
    return to.is_pid() || (earlier && pid_of_old_self(context, to));
}

// Whether a monitor signal from `node` names what OTP's dist.c accepts: MONITOR_P and DEMONITOR_P a watcher of that
// node and a pid or name here (or of an earlier incarnation), MONITOR_P_EXIT a pid or name there and a pid here.
bool monitor_formed(ProcessContext &context, const Shape &shape, const Term &control, Word node) {
    const auto element = [&](std::size_t index) { return control.tuple_element_unchecked(index); };
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

// Whether a SPAWN_REQUEST's {Module, Function, Arity} has OTP's shape.
bool mfa_formed(const Term &mfa) {
    return mfa.is_tuple() && mfa.tuple_size_unchecked() == 3 && mfa.tuple_element_unchecked(0).is_atom() &&
           mfa.tuple_element_unchecked(1).is_atom() && small(mfa.tuple_element_unchecked(2).word());
}

// Whether a SPAWN_REPLY names a request and receiver here, its flags and a result.
bool reply_formed(ProcessContext &context, const Term &control) {
    const auto element = [&](std::size_t index) { return control.tuple_element_unchecked(index); };
    const auto flags = small(element(3).word());
    const bool here = (element(2).is_pid() || pid_of_old_self(context, element(2))) &&
                      (element(1).is_reference() || old_self_reference(context, element(1)));
    return here && flags && *flags >= 0 && *flags < (std::int64_t{1} << 27) &&
           (element(4).is_external_pid() || element(4).is_atom());
}

// Whether a spawn signal names what OTP's dist.c accepts (DIST5b2): SPAWN_REQUEST another node's reference and pid,
// a group leader pid and {Module, Function, Arity}; SPAWN_REPLY a reference and pid here (or of an earlier
// incarnation), flags below 2^27 and another node's pid or an error atom.
bool spawn_formed(ProcessContext &context, const Shape &shape, const Term &control) {
    const auto element = [&](std::size_t index) { return control.tuple_element_unchecked(index); };
    if (shape.signal == Signal::spawn_reply) {
        return reply_formed(context, control);
    }
    return element(1).is_external_reference() && element(2).is_external_pid() && any_pid(element(3)) &&
           mfa_formed(element(4));
}

// Whether a signal from `node` names what OTP's dist.c accepts, as monitor_formed and link_formed check.
bool well_formed(ProcessContext &context, const Shape &shape, const Term &control, Word node) {
    if (shape.signal == Signal::none) {
        return true;
    }
    if (shape.signal == Signal::group_leader) {
        return any_pid(control.tuple_element_unchecked(1)) && any_pid(control.tuple_element_unchecked(2));
    }
    if (shape.signal == Signal::spawn_request || shape.signal == Signal::spawn_reply) {
        return spawn_formed(context, shape, control);
    }
    return links(shape.signal) ? link_formed(context, shape, control, node)
                               : monitor_formed(context, shape, control, node);
}

// Whether an operation's payload is read: a message to deliver, or the exit reason of a monitor or a link.
bool reads_payload(const Shape &shape) {
    return shape.receiver != Receiver::none || (shape.payload && shape.signal != Signal::none);
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
    const auto signal = reading.shape->signal;
    if (signal == Signal::monitor) {
        executor.watch_remote(context, node, {.watcher = element(1), .watched = element(2), .reference = element(3)});
    } else if (signal == Signal::demonitor) {
        executor.unwatch_remote(context, node, {.watcher = element(1), .watched = element(2), .reference = element(3)});
    } else if (signal == Signal::monitor_exit && element(2).is_pid()) {
        const auto reason = reading.shape->payload ? reading.message : element(4);
        executor.remote_down(context, node, {.reference = element(3), .watcher = element(2), .reason = reason});
    }
}

// The executor's kind of a link signal.
detail::RemoteLinkSignal::Kind link_kind(Signal signal) {
    using Kind = detail::RemoteLinkSignal::Kind;
    // The link signals come last in Signal, in the order of the executor's kinds.
    constexpr std::array KINDS{Kind::link, Kind::unlink, Kind::unlink_ack, Kind::link_exit, Kind::exit};
    return KINDS.at(static_cast<std::size_t>(signal) - static_cast<std::size_t>(Signal::link));
}

// GROUP_LEADER {7, Leader, Member}: a member here takes the leader once it does not run elsewhere; another node's
// member is ignored, as OTP's (DIST5b1).
void signal_group_leader(ProcessContext &context, const Reading &reading) {
    const auto member = reading.control.tuple_element_unchecked(2);
    if (member.is_pid()) {
        Executor::inspect(context, member.word(), [&](ProcessContext &target, Executor &) {
            lead(target, reading.control.tuple_element_unchecked(1), context);
            return true;
        });
    }
}

// Hand a link signal to the executor (DIST5a): the sender, the receiver, an unlink's id, and an exit's reason from
// the payload or the control message.
void signal_link(ProcessContext &context, Word node, const Reading &reading) {
    const auto &shape = *reading.shape;
    const auto element = [&](std::size_t index) { return reading.control.tuple_element_unchecked(index); };
    const std::size_t first = unlinks(shape.signal) ? 2 : 1;
    detail::RemoteLinkSignal signal{
        .kind = link_kind(shape.signal), .from = element(first), .to = element(first + 1), .id = 0, .reason = {}};
    if (unlinks(shape.signal)) {
        signal.id = static_cast<std::uint64_t>(small(element(1).word()).value_or(0));
    }
    if (shape.payload) {
        signal.reason = reading.message;
    } else if (shape.reason != 0) {
        signal.reason = element(shape.reason);
    }
    Executor::of(context).remote_link_signal(context, node, signal);
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

void lead(ProcessContext &member, const Term &leader, ProcessContext &builder) {
    if (leader.is_pid()) {
        member.signals().set_group_leader(leader.word());
    } else {
        member.signals().set_remote_group_leader(need(detail::MessageBuffer::copy_of(leader, builder)));
    }
}

void send_group_leader(ProcessContext &context, const Term &leader, const Term &pid) {
    TermFactory factory(context);
    const auto control = need(factory.tuple(std::array{need(factory.integer(GROUP_LEADER)), leader, pid}));
    Executor::of(context).dist_output(context, node_of(context, pid), dist_packet(context, control, std::nullopt),
                                      true);
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
        } else if (links(reading.shape->signal)) {
            signal_link(context, source.node, reading);
        } else if (reading.shape->signal == Signal::group_leader) {
            signal_group_leader(context, reading);
        } else if (reading.shape->signal == Signal::spawn_request) {
            take_spawn_request(context, reading.control, source.node, reading.message);
        } else if (reading.shape->signal == Signal::spawn_reply) {
            take_spawn_reply(context, source.node, reading.control);
        } else {
            signal_monitor(context, source.node, reading);
        }
        return;
    }
    Executor::of(context).exit(context, source.controller, atom(context, "kill"_atom), false);
    report_fault(context, source.node, reading);
}
} // namespace clause::runtime::builtins
