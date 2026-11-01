#include "../builtins/erlang/dist_messages.hpp"
#include "../builtins/erlang/term_decoding.hpp"
#include "../builtins/erlang/term_encoding.hpp"
#include "../process/exits.hpp"
#include "../terms/identities.hpp"
#include "signal_messages.hpp"
#include <array>
#include <string>
#include <utility>

// Spawning on other nodes (plan 30 step DIST5b2, docs/distribution.md#spawning), as OTP's dist.c spawns: a request
// is SPAWN_REQUEST on the node's connection and waits here for its SPAWN_REPLY, which activates the link and monitor
// it asked for; another node's request is served by a child that gets its group leader, link and monitor, and whose
// SPAWN_REPLY is queued before it can run.
namespace clause::runtime::detail {
namespace {
// The control message operations of spawns (erl_dist_protocol) and SPAWN_REPLY's flags (dist.h).
constexpr std::int64_t DEMONITOR_P = 20;
constexpr std::int64_t SPAWN_REPLY = 31;
constexpr std::int64_t SPAWNED_LINK = 1;
constexpr std::int64_t SPAWNED_MONITOR = 2;

std::string encoded(ProcessContext &builder, const Term &term) { return builtins::encode_external(builder, term); }

Term decoded(ProcessContext &builder, const std::string &bytes) { return builtins::decode_external(builder, bytes); }

// The error of a child that could not be published, as the executor's other spawns report it.
TermError creation_error(abi::v1::Status status) {
    return status == abi::v1::Status::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}

// The packet of a control message {Operation, Parts...} built in `builder`'s heap, without a payload.
template <std::size_t N>
std::vector<std::byte> control_packet(ProcessContext &builder, std::int64_t operation,
                                      const std::array<Term, N> &parts) {
    TermFactory factory(builder);
    std::array<Term, N + 1> elements{};
    elements[0] = built(factory.integer(operation));
    std::ranges::copy(parts, elements.begin() + 1);
    return builtins::dist_packet(builder, built(factory.tuple(elements)), std::nullopt);
}

// The spawn reply message {Tag, ReqId, ok | error, Result} built in `builder`'s heap.
Term reply_message(ProcessContext &builder, const PendingSpawn &pending, const Term &reference, const Term &result) {
    TermFactory factory(builder);
    const auto status = atom(builder, result.is_atom() ? "error"_atom : "ok"_atom);
    const std::array parts{built(pending.tag->copy_to(builder)), reference, status, result};
    return built(factory.tuple(parts));
}
} // namespace

TermResult<Term> Executor::launch_requested(ProcessContext &controller, std::unique_ptr<ProcessContext> built,
                                            const Term &pid, const RemoteSpawnRequest &request) noexcept {
    auto &child = *built;
    const std::scoped_lock lock(mutex_);
    if (const auto published = controller.runtime().publish_context(std::move(built)); !published) {
        return std::unexpected(creation_error(published.error()));
    }
    retire_arrays();
    try {
        serve_request(controller, child, pid, request);
        return pid;
    } catch (const std::bad_alloc &) {
        destroy(child);
        return std::unexpected(TermError::out_of_memory);
    }
}

void Executor::serve_request(ProcessContext &controller, ProcessContext &child, const Term &pid,
                             const RemoteSpawnRequest &request) {
    builtins::lead(child, request.leader, controller);
    auto &connection = *connections_.find(request.node);
    const auto self = pid_of(child);
    std::int64_t flags = 0;
    if (request.link) {
        connection.links[self].push_back({.pid = encoded(controller, request.requester)});
        child.signals().link(request.node);
        flags |= SPAWNED_LINK;
    }
    if (request.monitor) {
        // The requester monitors the child with the request id, as monitor/2 of the child would.
        const auto key = built(TermFactory(controller).make_reference()).reference_value_unchecked();
        auto reference = encoded(controller, request.reference);
        connection.processes.keys.emplace(reference, key);
        connection.processes.watchers.emplace(key, RemoteWatcher{.target = self,
                                                                 .name = 0,
                                                                 .watcher = encoded(controller, request.requester),
                                                                 .reference = std::move(reference)});
        child.signals().watch(key, {.pid = request.node, .name = 0});
        flags |= SPAWNED_MONITOR;
    }
    const std::array parts{request.reference, request.requester, built(TermFactory(controller).integer(flags)), pid};
    queue_packet(controller, connection, control_packet(controller, SPAWN_REPLY, parts));
    schedule(child).started = true;
    push(child);
}

SpawnSent Executor::request_spawn(ProcessContext &requester, const Term &node, const ReferenceIdentity &reference,
                                  const std::function<std::vector<std::byte>()> &packet, PendingSpawn pending) {
    const std::scoped_lock lock(mutex_);
    const auto found = output_connection(requester, node, true);
    if (!found) {
        return SpawnSent::noconnection;
    }
    auto &connection = **found;
    if (connection.state == Connection::State::up && (connection.flags & dflags::SPAWN) == 0) {
        return SpawnSent::notsup;
    }
    // Arguments the external format cannot hold here (a local fun) raise notsup before anything is kept.
    auto bytes = packet();
    pending.node = node.word();
    pending.requester = pid_of(requester);
    spawn_requests_.insert_or_assign(reference, std::move(pending));
    queue_packet(requester, connection, std::move(bytes));
    return SpawnSent::queued;
}

void Executor::spawn_replied(ProcessContext &controller, Word node, const std::array<Term, 4> &reply) {
    const auto &[reference, parent, flags, result] = reply;
    const std::scoped_lock lock(mutex_);
    // A request id of an earlier incarnation of this node, or a reply to no pending request, does nothing.
    const auto found =
        reference.is_reference() ? spawn_requests_.find(reference.reference_value_unchecked()) : spawn_requests_.end();
    if (found == spawn_requests_.end() || found->second.node != node || found->second.requester != parent.word()) {
        return;
    }
    auto pending = std::move(found->second);
    spawn_requests_.erase(found);
    const auto bits = builtins::small(flags.word()).value_or(0);
    const SpawnedChild child{.node = node,
                             .reference = reference,
                             .pid = result,
                             .link = result.is_external_pid() && (bits & SPAWNED_LINK) != 0,
                             .monitor = result.is_external_pid() && (bits & SPAWNED_MONITOR) != 0};
    if (pending.orphaned) {
        orphan_child(controller, pending, parent, child);
    } else if (auto *requester = process(pending.requester)) {
        adopt_child(controller, *requester, pending, child);
    }
}

void Executor::orphan_child(ProcessContext &controller, const PendingSpawn &pending, const Term &parent,
                            const SpawnedChild &child) {
    // The requester ended or abandoned the request: the child loses its monitor (only for an ended requester, as
    // OTP's) and its link.
    auto &connection = *connections_.find(child.node);
    if (child.monitor && !pending.abandoned) {
        const std::array parts{parent, child.pid, child.reference};
        queue_packet(controller, connection, control_packet(controller, DEMONITOR_P, parts));
    }
    if (child.link) {
        const auto reason = decoded(controller, *pending.orphaned);
        queue_packet(controller, connection, link_exit_packet(controller, connection, {parent, child.pid, reason}));
    }
}

void Executor::adopt_child(ProcessContext &controller, ProcessContext &requester, const PendingSpawn &pending,
                           const SpawnedChild &child) {
    auto &connection = *connections_.find(child.node);
    const auto id = child.reference.reference_value_unchecked();
    if (child.link) {
        connection.links[pending.requester].push_back({.pid = encoded(controller, child.pid)});
        requester.signals().link(child.node);
    }
    if (child.monitor) {
        connection.processes.targets.emplace(id, RemoteTarget{.watcher = pending.requester,
                                                              .pid = encoded(controller, child.pid),
                                                              .name = 0,
                                                              .tag = pending.monitor_options.tag});
        requester.signals().monitor(id, child.node, 0);
        if (pending.monitor_options.alias) {
            add_alias(id, pending.requester, *pending.monitor_options.alias);
        }
    }
    if (child.pid.is_external_pid() ? pending.success_message : pending.error_message) {
        post(requester, buffered(controller, reply_message(controller, pending, child.reference, child.pid)));
    }
}

bool Executor::abandon_spawn(ProcessContext &requester, const ReferenceIdentity &reference) {
    const std::scoped_lock lock(mutex_);
    const auto found = spawn_requests_.find(reference);
    if (found == spawn_requests_.end() || found->second.requester != pid_of(requester) || found->second.orphaned) {
        return false;
    }
    if (!found->second.link && !found->second.monitor) {
        spawn_requests_.erase(found);
    } else {
        found->second.orphaned = encoded(requester, atom(requester, "abandoned"_atom));
        found->second.abandoned = true;
    }
    return true;
}

void Executor::fail_spawn_requests(ProcessContext &builder, Word node) {
    const auto reason = atom(builder, "noconnection"_atom);
    for (auto entry = spawn_requests_.begin(); entry != spawn_requests_.end();) {
        if (entry->second.node != node) {
            ++entry;
            continue;
        }
        auto pending = std::move(entry->second);
        const auto reference = built(TermFactory(builder).reference(entry->first));
        entry = spawn_requests_.erase(entry);
        auto *requester = pending.orphaned ? nullptr : process(pending.requester);
        if (requester != nullptr && pending.error_message) {
            post(*requester, buffered(builder, reply_message(builder, pending, reference, reason)));
        }
    }
}

void Executor::orphan_spawn_requests(ProcessContext &process) {
    const auto self = pid_of(process);
    std::optional<std::string> reason;
    for (auto entry = spawn_requests_.begin(); entry != spawn_requests_.end();) {
        auto &pending = entry->second;
        if (pending.requester != self || pending.orphaned) {
            ++entry;
            continue;
        }
        if (!pending.link && !pending.monitor) {
            entry = spawn_requests_.erase(entry);
            continue;
        }
        if (!reason) {
            reason = orphan_reason(process);
        }
        pending.orphaned = reason;
        ++entry;
    }
}

std::string Executor::orphan_reason(ProcessContext &process) {
    try {
        return encoded(process, built(exit_reason(process)));
    } catch (const builtins::BuiltinFailure &) {
        // A reason the external format cannot hold here (a local fun) arrives as notsup (docs/differences.md).
        return encoded(process, atom(process, "notsup"_atom));
    }
}
} // namespace clause::runtime::detail
