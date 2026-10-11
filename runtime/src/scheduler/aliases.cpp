#include "executor.hpp"

// Process aliases (plan step 41G2, docs/processes.md#aliases): references a process accepts messages through, made
// by alias/0,1 or a monitor's {alias, Mode}, until unalias/1, a first message or the monitor's end deactivates them.
namespace clause::runtime::detail {
void Executor::alias(ProcessContext &owner, const ReferenceIdentity &reference, AliasMode mode) {
    auto &executor = of(owner);
    const std::scoped_lock lock(executor.mutex_);
    executor.add_alias(reference, pid_of(owner), mode);
}

bool Executor::unalias(ProcessContext &owner, const ReferenceIdentity &reference) noexcept {
    auto &executor = of(owner);
    const std::scoped_lock lock(executor.mutex_);
    const auto found = executor.aliases_.find(reference);
    if (found == executor.aliases_.end() || found->second.first != pid_of(owner)) {
        return false;
    }
    executor.aliases_.erase(found);
    return true;
}

TermResult<void> Executor::send_alias(ProcessContext &sender, const ReferenceIdentity &reference, const Term &message) {
    auto &executor = of(sender);
    const std::scoped_lock lock(executor.mutex_);
    const auto found = executor.aliases_.find(reference);
    auto *receiver = found != executor.aliases_.end() ? executor.process(found->second.first) : nullptr;
    if (!receiver) {
        return {};
    }
    // Wait for a busy owner before the alias changes: the send runs again from the start.
    if (executor.busy(receiver)) {
        executor.block(sender, *receiver);
    }
    const auto [owner, mode] = found->second;
    const auto copy = message.copy_to(receiver->heap());
    if (!copy) {
        return std::unexpected(copy.error());
    }
    if (mode == AliasMode::reply || mode == AliasMode::reply_demonitor) {
        executor.aliases_.erase(found);
    }
    if (mode == AliasMode::reply_demonitor) {
        executor.end_monitor(owner, reference);
    }
    receiver->mailbox().deliver(copy->word());
    return executor.wake(*receiver);
}

void Executor::end_monitor(Word owner, const ReferenceIdentity &reference) noexcept {
    auto *watcher = process(owner);
    const auto monitored = watcher ? watcher->signals().demonitor(reference) : std::nullopt;
    if (auto *target = monitored ? process(*monitored) : nullptr) {
        target->signals().unwatch(reference);
    }
}

void Executor::add_alias(const ReferenceIdentity &reference, Word owner, AliasMode mode) {
    aliases_.insert_or_assign(reference, std::pair{owner, mode});
}

void Executor::forget_alias(const ReferenceIdentity &reference, bool always) noexcept {
    const auto found = aliases_.find(reference);
    if (found == aliases_.end()) {
        return;
    }
    const auto mode = found->second.second;
    if (always || mode == AliasMode::demonitor || mode == AliasMode::reply_demonitor) {
        aliases_.erase(found);
    }
}

void Executor::drop_aliases(Word pid) noexcept {
    std::erase_if(aliases_, [&](const auto &entry) { return entry.second.first == pid; });
}
} // namespace clause::runtime::detail
