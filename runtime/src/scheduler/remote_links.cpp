#include "../builtins/erlang/dist_messages.hpp"
#include "../builtins/erlang/term_decoding.hpp"
#include "../builtins/erlang/term_encoding.hpp"
#include "../terms/identities.hpp"
#include "../terms/service_errors.hpp"
#include "signal_messages.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <utility>

// Links and exit signals between nodes (plan 30 step DIST5a, docs/distribution.md#links-and-exit-signals), as OTP's
// dist.c and signal queue keep them: link/1 sends LINK, unlink/1 sends UNLINK_ID and keeps the link, dropping its
// exits, until UNLINK_ID_ACK; a linked process's end sends EXIT and exit/2 EXIT2 (their payload forms with
// EXIT_PAYLOAD); a lost connection exits the links with noconnection. Exit signals reach their targets as port events,
// so they wait while a target runs on another worker.
namespace clause::runtime::detail {
namespace {
// The control message operations of links and exit signals (erl_dist_protocol).
constexpr std::int64_t LINK = 1;
constexpr std::int64_t EXIT = 3;
constexpr std::int64_t EXIT2 = 8;
constexpr std::int64_t PAYLOAD_EXIT = 24;
constexpr std::int64_t PAYLOAD_EXIT2 = 26;
constexpr std::int64_t UNLINK_ID = 35;
constexpr std::int64_t UNLINK_ID_ACK = 36;

std::string encoded(ProcessContext &builder, const Term &term) { return builtins::encode_external(builder, term); }

Term decoded(ProcessContext &builder, const std::string &bytes) { return builtins::decode_external(builder, bytes); }

// The control message {Operation, Parts...} built in `builder`'s heap.
template <std::size_t N>
Term control(ProcessContext &builder, std::int64_t operation, const std::array<Term, N> &parts) {
    TermFactory factory(builder);
    std::array<Term, N + 1> elements{};
    elements[0] = built(factory.integer(operation));
    std::ranges::copy(parts, elements.begin() + 1);
    return built(factory.tuple(elements));
}

// The packet of a control message without a payload.
std::vector<std::byte> packet_of(ProcessContext &builder, const Term &control) {
    return builtins::dist_packet(builder, control, std::nullopt);
}

// The packet of an exit signal from `from` to `to`: a link's EXIT or exit/2's EXIT2, the reason as the payload for a
// node with EXIT_PAYLOAD (also while pending, whose flags are none yet, as monitor exits are sent).
std::vector<std::byte> exit_packet(ProcessContext &builder, std::uint64_t flags, bool linked,
                                   const std::array<Term, 3> &parts) {
    if ((flags & dflags::EXIT_PAYLOAD) != 0) {
        const auto operation = linked ? PAYLOAD_EXIT : PAYLOAD_EXIT2;
        return builtins::dist_packet(builder, control(builder, operation, std::array{parts[0], parts[1]}), parts[2]);
    }
    return packet_of(builder, control(builder, linked ? EXIT : EXIT2, parts));
}

// The link of the process of `pid` to another node's pid in the external format, or null.
RemoteLink *find_link(RemoteLinks &links, Word pid, const std::string &other) {
    const auto found = links.find(pid);
    if (found == links.end()) {
        return nullptr;
    }
    const auto link = std::ranges::find(found->second, other, &RemoteLink::pid);
    return link == found->second.end() ? nullptr : &*link;
}

// The node of another node's pid or port, an atom word.
Word node_of(const Term &identity) { return ExternalAccess::read(identity).value().node; }

// The caller's pid as a term of its heap.
Term self_of(ProcessContext &process) { return built(TermFactory(process).pid(process.identity())); }
} // namespace

bool Executor::link_remote(ProcessContext &process, const Term &pid) {
    const std::scoped_lock lock(mutex_);
    const auto node = built(Term::from_word(node_of(pid), process));
    // A pid of an earlier incarnation of this node names no process: noproc, a message while trapping exits.
    if (is_this_node(node)) {
        if (!process.signals().trap_exit()) {
            return false;
        }
        signal_self(process, pid, atom(process, "noproc"_atom));
        return true;
    }
    const auto found = output_connection(process, node, true);
    if (!found) {
        // A node that cannot be reached exits the link at once, as OTP's.
        signal_self(process, pid, atom(process, "noconnection"_atom));
        return true;
    }
    auto &connection = **found;
    const auto self = pid_of(process);
    auto other = encoded(process, pid);
    if (auto *link = find_link(connection.links, self, other)) {
        if (link->unlinking == 0) {
            return true;
        }
        // Linked again while the unlink waits for its acknowledgement: the link is back.
        link->unlinking = 0;
    } else {
        connection.links[self].push_back({.pid = std::move(other)});
        process.signals().link(node.word());
    }
    queue_packet(process, connection, packet_of(process, control(process, LINK, std::array{self_of(process), pid})));
    return true;
}

void Executor::unlink_remote(ProcessContext &process, const Term &pid) {
    const std::scoped_lock lock(mutex_);
    auto *connection = connections_.find(node_of(pid));
    auto *link = connection != nullptr ? find_link(connection->links, pid_of(process), encoded(process, pid)) : nullptr;
    if (link == nullptr || link->unlinking != 0) {
        return;
    }
    link->unlinking = ++unlink_ids_;
    const auto id = built(TermFactory(process).integer(static_cast<std::int64_t>(link->unlinking)));
    queue_packet(process, *connection,
                 packet_of(process, control(process, UNLINK_ID, std::array{id, self_of(process), pid})));
}

void Executor::exit_remote(ProcessContext &sender, const Term &pid, const Term &reason) {
    const std::scoped_lock lock(mutex_);
    const auto found = output_connection(sender, built(Term::from_word(node_of(pid), sender)), true);
    if (found) {
        queue_packet(sender, **found, exit_packet(sender, (*found)->flags, false, {self_of(sender), pid, reason}));
    }
}

void Executor::signal_self(ProcessContext &process, const Term &from, const Term &reason) {
    const Running running(process);
    signal(process, from.word(), reason, SignalKind::link);
    drain();
}

void Executor::remote_link_signal(ProcessContext &controller, Word node, const RemoteLinkSignal &signal) {
    const std::scoped_lock lock(mutex_);
    const Running running(controller);
    auto &connection = *connections_.find(node);
    switch (signal.kind) {
    case RemoteLinkSignal::Kind::link:
        take_link(controller, connection, node, signal);
        break;
    case RemoteLinkSignal::Kind::unlink:
        take_unlink(controller, connection, node, signal);
        break;
    case RemoteLinkSignal::Kind::unlink_ack:
        take_unlink_ack(controller, connection, node, signal);
        break;
    case RemoteLinkSignal::Kind::link_exit:
        take_link_exit(controller, connection, node, signal);
        break;
    case RemoteLinkSignal::Kind::exit:
        if (auto *target = signal.to.is_pid() ? process(signal.to.word()) : nullptr) {
            post_remote_exit(controller, *target, signal.from, signal.reason, false);
        }
        break;
    }
    drain();
}

void Executor::take_link(ProcessContext &controller, Connection &connection, Word node,
                         const RemoteLinkSignal &signal) {
    auto *target = signal.to.is_pid() ? process(signal.to.word()) : nullptr;
    if (target == nullptr) {
        // No process here, or a pid of an earlier incarnation: the link exits noproc at once.
        const std::array parts{signal.to, signal.from, atom(controller, "noproc"_atom)};
        queue_packet(controller, connection, exit_packet(controller, connection.flags, true, parts));
        return;
    }
    auto other = encoded(controller, signal.from);
    // An existing link stays as it is, also one being unlinked.
    if (find_link(connection.links, signal.to.word(), other) == nullptr) {
        connection.links[signal.to.word()].push_back({.pid = std::move(other)});
        target->signals().link(node);
    }
}

void Executor::take_unlink(ProcessContext &controller, Connection &connection, Word node,
                           const RemoteLinkSignal &signal) {
    const auto pid = signal.to.word();
    const auto other = encoded(controller, signal.from);
    if (const auto *link = find_link(connection.links, pid, other); link != nullptr && link->unlinking == 0) {
        forget_link(connection, pid, other, node);
    }
    // Acknowledged also for no link or no process, as OTP's.
    const auto id = built(TermFactory(controller).integer(static_cast<std::int64_t>(signal.id)));
    const std::array parts{id, signal.to, signal.from};
    queue_packet(controller, connection, packet_of(controller, control(controller, UNLINK_ID_ACK, parts)));
}

void Executor::take_unlink_ack(ProcessContext &controller, Connection &connection, Word node,
                               const RemoteLinkSignal &signal) {
    const auto pid = signal.to.word();
    const auto other = encoded(controller, signal.from);
    if (const auto *link = find_link(connection.links, pid, other); link != nullptr && link->unlinking == signal.id) {
        forget_link(connection, pid, other, node);
    }
}

void Executor::take_link_exit(ProcessContext &controller, Connection &connection, Word node,
                              const RemoteLinkSignal &signal) {
    const auto pid = signal.to.word();
    const auto other = encoded(controller, signal.from);
    const auto *link = find_link(connection.links, pid, other);
    auto *target = process(pid);
    if (link == nullptr || target == nullptr) {
        return;
    }
    // The exit ends the link; one being unlinked brings no signal.
    const bool active = link->unlinking == 0;
    forget_link(connection, pid, other, node);
    if (active) {
        post_remote_exit(controller, *target, signal.from, signal.reason, true);
    }
}

void Executor::forget_link(Connection &connection, Word pid, const std::string &other, Word node) {
    const auto found = connection.links.find(pid);
    std::erase_if(found->second, [&](const RemoteLink &link) { return link.pid == other; });
    if (!found->second.empty()) {
        return;
    }
    connection.links.erase(found);
    if (auto *linked = process(pid)) {
        linked->signals().unlink(node);
    }
}

void Executor::post_remote_exit(ProcessContext &builder, ProcessContext &target, const Term &from, const Term &reason,
                                bool linked) {
    const std::array parts{from, reason};
    post(target, PortEvent::remote(buffered(builder, built(TermFactory(builder).tuple(parts))), linked));
}

void Executor::remote_signal(ProcessContext &target, const MessageBuffer &event, bool linked) {
    const auto copy = event.copy_to(target);
    if (!copy) {
        target.generated_calls().fail_service(term_status(copy.error()));
        retire(target);
        return;
    }
    const auto from = copy->tuple_element_unchecked(0);
    signal(target, from.word(), copy->tuple_element_unchecked(1), linked ? SignalKind::link : SignalKind::exit);
}

void Executor::end_remote_links(ProcessContext &process, Word node, const Term &reason) {
    auto *entry = connections_.find(node);
    const auto found = entry != nullptr ? entry->links.find(pid_of(process)) : RemoteLinks::iterator{};
    if (entry == nullptr || found == entry->links.end()) {
        return;
    }
    auto &connection = *entry;
    const auto links = std::move(found->second);
    connection.links.erase(found);
    const auto self = self_of(process);
    for (const auto &link : links) {
        if (link.unlinking != 0) {
            continue;
        }
        const auto to = decoded(process, link.pid);
        try {
            queue_packet(process, connection, exit_packet(process, connection.flags, true, {self, to, reason}));
        } catch (const builtins::BuiltinFailure &) {
            // A reason the external format cannot hold here (a local fun) arrives as notsup (docs/differences.md).
            const std::array parts{self, to, atom(process, "notsup"_atom)};
            queue_packet(process, connection, exit_packet(process, connection.flags, true, parts));
        }
    }
}

void Executor::drop_remote_links(ProcessContext &builder, Word node, const RemoteLinks &links) {
    const auto reason = atom(builder, "noconnection"_atom);
    for (const auto &[pid, entries] : links) {
        auto *target = process(pid);
        if (target == nullptr) {
            continue;
        }
        target->signals().unlink(node);
        for (const auto &link : entries) {
            if (link.unlinking == 0) {
                post_remote_exit(builder, *target, decoded(builder, link.pid), reason, true);
            }
        }
    }
}

std::vector<Term> Executor::remote_links(ProcessContext &linked, Word node, ProcessContext &builder) const {
    const auto pid = pid_of(linked);
    std::vector<Term> pids;
    const auto *connection = connections_.find(node);
    if (connection == nullptr || !connection->links.contains(pid)) {
        return pids;
    }
    for (const auto &link : connection->links.at(pid)) {
        if (link.unlinking == 0) {
            pids.push_back(decoded(builder, link.pid));
        }
    }
    return pids;
}
} // namespace clause::runtime::detail
