#include "../builtins/erlang/dist_messages.hpp"
#include "../builtins/erlang/term_decoding.hpp"
#include "../builtins/erlang/term_encoding.hpp"
#include "../terms/identities.hpp"
#include "signal_messages.hpp"
#include <array>
#include <expected>
#include <string>
#include <utility>

// Process monitors between nodes (plan 30 step DIST4b3b1, docs/distribution.md#process-monitors), as OTP's dist.c
// keeps them: a monitor of another node's process is MONITOR_P on the node's connection, ended by DEMONITOR_P or by
// that node's MONITOR_P_EXIT, and by 'DOWN' with noconnection when the connection goes down. The connection keeps the
// other side's identities in the external format; each side's Signals entry names the node's atom in place of a pid.
namespace clause::runtime::detail {
namespace {
// The control message operations of process monitors (erl_dist_protocol).
constexpr std::int64_t MONITOR_P = 19;
constexpr std::int64_t DEMONITOR_P = 20;
constexpr std::int64_t MONITOR_P_EXIT = 21;
constexpr std::int64_t PAYLOAD_MONITOR_P_EXIT = 28;

std::string encoded(ProcessContext &builder, const Term &term) { return builtins::encode_external(builder, term); }

Term decoded(ProcessContext &builder, const std::string &bytes) { return builtins::decode_external(builder, bytes); }

// The control message {Operation, First, Second, Reference} built in `builder`'s heap.
Term control(ProcessContext &builder, std::int64_t operation, const std::array<Term, 3> &parts) {
    TermFactory factory(builder);
    return built(factory.tuple(std::array{built(factory.integer(operation)), parts[0], parts[1], parts[2]}));
}

// What a monitor of another node's process names in its 'DOWN' message: the pid, or {Name, Node}.
Term target_item(ProcessContext &builder, Word node, const RemoteTarget &target) {
    if (target.name == 0) {
        return decoded(builder, target.pid);
    }
    const std::array parts{built(Term::from_word(target.name, builder)), built(Term::from_word(node, builder))};
    return built(TermFactory(builder).tuple(parts));
}

// The node of another node's pid, an atom word.
Word node_of(const Term &pid) { return ExternalAccess::read(pid).value().node; }
} // namespace

Term Executor::monitor_remote(ProcessContext &watcher, const Term &item, const MonitorOptions &options) {
    const std::scoped_lock lock(mutex_);
    TermFactory factory(watcher);
    const auto reference = built(factory.make_reference());
    const auto identity = reference.reference_value_unchecked();
    if (options.alias) {
        add_alias(identity, pid_of(watcher), *options.alias);
    }
    const bool by_name = item.is_tuple();
    const auto node = by_name ? item.tuple_element_unchecked(1) : built(Term::from_word(node_of(item), watcher));
    // A pid of an earlier incarnation of this node names no process.
    std::expected<Connection *, DistSend> found = std::unexpected(DistSend::dropped);
    if (!is_this_node(node)) {
        found = output_connection(watcher, node, true);
    }
    if (!found) {
        const Running running(watcher);
        const auto reason = atom(watcher, is_this_node(node) ? "noproc"_atom : "noconnection"_atom);
        deliver(watcher, remote_down_message(watcher, identity, options.tag.get(), item, reason));
        forget_alias(identity, false);
        return reference;
    }
    auto &connection = **found;
    const auto name = by_name ? item.tuple_element_unchecked(0) : item;
    connection.processes.targets.emplace(identity, RemoteTarget{.watcher = pid_of(watcher),
                                                                .pid = by_name ? std::string{} : encoded(watcher, item),
                                                                .name = by_name ? name.word() : 0,
                                                                .tag = options.tag});
    watcher.signals().monitor(identity, node.word(), by_name ? name.word() : 0);
    const auto self = built(factory.pid(watcher.identity()));
    queue_monitor_signal(watcher, connection, control(watcher, MONITOR_P, {self, name, reference}));
    return reference;
}

void Executor::demonitor_remote(ProcessContext &watcher, Word node, const ReferenceIdentity &reference) {
    auto *connection = connections_.find(node);
    auto &targets = connection->processes.targets;
    const auto found = targets.find(reference);
    if (found == targets.end()) {
        return;
    }
    const auto target = std::move(found->second);
    targets.erase(found);
    const auto watched = target.name != 0 ? built(Term::from_word(target.name, watcher)) : decoded(watcher, target.pid);
    const std::array parts{built(Term::from_word(target.watcher, watcher)), watched,
                           built(TermFactory(watcher).reference(reference))};
    queue_monitor_signal(watcher, *connection, control(watcher, DEMONITOR_P, parts));
}

void Executor::remote_exit(ProcessContext &process, Word node, const ReferenceIdentity &key, const Term &reason) {
    auto *connection = connections_.find(node);
    auto &monitors = connection->processes;
    const auto found = monitors.watchers.find(key);
    if (found == monitors.watchers.end()) {
        return;
    }
    const auto watcher = std::move(found->second);
    monitors.watchers.erase(found);
    monitors.keys.erase(watcher.reference);
    const auto watched = built(Term::from_word(watcher.name != 0 ? watcher.name : watcher.target, process));
    const std::array parts{watched, decoded(process, watcher.watcher), decoded(process, watcher.reference)};
    try {
        monitor_exit(process, *connection, parts, reason);
    } catch (const builtins::BuiltinFailure &) {
        // A reason the external format cannot hold here (a local fun) arrives as notsup (docs/differences.md).
        monitor_exit(process, *connection, parts, atom(process, "notsup"_atom));
    }
}

void Executor::monitor_exit(ProcessContext &builder, Connection &connection, const std::array<Term, 3> &parts,
                            const Term &reason) {
    // A node with EXIT_PAYLOAD gets the reason as the payload, as OTP's erts_dsig_send_m_exit sends it.
    if ((connection.flags & dflags::EXIT_PAYLOAD) != 0) {
        queue_monitor_signal(builder, connection, control(builder, PAYLOAD_MONITOR_P_EXIT, parts), reason);
        return;
    }
    TermFactory factory(builder);
    const std::array message{built(factory.integer(MONITOR_P_EXIT)), parts[0], parts[1], parts[2], reason};
    queue_monitor_signal(builder, connection, built(factory.tuple(message)));
}

void Executor::watch_remote(ProcessContext &controller, Word node, const RemoteMonitor &watch) {
    const std::scoped_lock lock(mutex_);
    auto &connection = *connections_.find(node);
    const auto name = watch.watched.is_atom() ? watch.watched.word() : Word{0};
    const auto pid = name != 0 ? lookup(controller, name) : (watch.watched.is_pid() ? watch.watched.word() : 0);
    auto *target = pid != 0 ? find(controller, pid) : nullptr;
    if (target == nullptr) {
        monitor_exit(controller, connection, {watch.watched, watch.watcher, watch.reference},
                     atom(controller, "noproc"_atom));
        return;
    }
    const auto key = built(TermFactory(controller).make_reference()).reference_value_unchecked();
    auto reference = encoded(controller, watch.reference);
    connection.processes.keys.emplace(reference, key);
    connection.processes.watchers.emplace(key, RemoteWatcher{.target = pid,
                                                             .name = name,
                                                             .watcher = encoded(controller, watch.watcher),
                                                             .reference = std::move(reference)});
    target->signals().watch(key, {.pid = node, .name = name});
}

void Executor::unwatch_remote(ProcessContext &controller, Word node, const RemoteMonitor &watch) {
    const std::scoped_lock lock(mutex_);
    auto &monitors = connections_.find(node)->processes;
    const auto key = monitors.keys.find(encoded(controller, watch.reference));
    if (key == monitors.keys.end()) {
        return;
    }
    const auto found = monitors.watchers.find(key->second);
    const auto &watcher = found->second;
    // A pid of an earlier incarnation of this node, or another process than the monitor's, ends nothing.
    if (watch.watched.word() != (watcher.name != 0 ? watcher.name : watcher.target)) {
        return;
    }
    if (auto *target = process(watcher.target)) {
        target->signals().unwatch(found->first);
    }
    monitors.watchers.erase(found);
    monitors.keys.erase(key);
}

void Executor::remote_down(ProcessContext &controller, Word node, const RemoteDown &down) {
    const std::scoped_lock lock(mutex_);
    auto &targets = connections_.find(node)->processes.targets;
    // Another node's reference is no monitor of this node.
    const auto found =
        down.reference.is_reference() ? targets.find(down.reference.reference_value_unchecked()) : targets.end();
    if (found == targets.end() || found->second.watcher != down.watcher.word()) {
        return;
    }
    const auto reference = found->first;
    const auto target = std::move(found->second);
    targets.erase(found);
    auto *watcher = process(target.watcher);
    if (watcher == nullptr) {
        return;
    }
    watcher->signals().demonitor(reference);
    forget_alias(reference, false);
    const auto item = target_item(controller, node, target);
    post(*watcher,
         buffered(controller, built(remote_down_message(controller, reference, target.tag.get(), item, down.reason))));
}

void Executor::end_process_monitors(ProcessContext &builder, Word node, const ProcessMonitors &monitors) {
    for (const auto &[reference, target] : monitors.targets) {
        auto *watcher = process(target.watcher);
        if (watcher == nullptr) {
            continue;
        }
        watcher->signals().demonitor(reference);
        forget_alias(reference, false);
        const auto down = remote_down_message(builder, reference, target.tag.get(), target_item(builder, node, target),
                                              atom(builder, "noconnection"_atom));
        post(*watcher, buffered(builder, built(down)));
    }
    for (const auto &[key, watcher] : monitors.watchers) {
        if (auto *target = process(watcher.target)) {
            target->signals().unwatch(key);
        }
    }
}

void Executor::queue_monitor_signal(ProcessContext &builder, Connection &connection, const Term &control,
                                    const std::optional<Term> &payload) {
    // A node without OTP's monitor flags gets no monitor signals: its monitors watch only the connection.
    constexpr auto MONITORS = dflags::DIST_MONITOR | dflags::DIST_MONITOR_NAME;
    if (connection.state == Connection::State::up && (connection.flags & MONITORS) != MONITORS) {
        return;
    }
    queue_packet(builder, connection, builtins::dist_packet(builder, control, payload));
}

Term Executor::remote_item(ProcessContext &builder, Word node, const ReferenceIdentity &reference, bool watcher) const {
    const auto &monitors = connections_.find(node)->processes;
    if (watcher) {
        return decoded(builder, monitors.watchers.at(reference).watcher);
    }
    return target_item(builder, node, monitors.targets.at(reference));
}
} // namespace clause::runtime::detail
