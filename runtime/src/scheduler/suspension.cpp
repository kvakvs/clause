#include "../runtime_state.hpp"
#include "executor.hpp"
#include <algorithm>

// suspend_process/2 and resume_process/1 (plan 21 step LIB9B, docs/processes.md#suspending-processes): each suspend
// is one hold of the target, so a suspended process is queued again only when its last hold goes; the suspender
// records its suspends, which its end undoes.
namespace clause::runtime::detail {
std::optional<bool> Executor::suspend(ProcessContext &caller, Word pid, SuspendRequest request) {
    auto &executor = of(caller);
    const std::scoped_lock lock(executor.mutex_);
    auto *target = find(caller, pid);
    if (target == nullptr || executor.ending(*target)) {
        return std::nullopt;
    }
    auto &holder = executor.schedule(caller);
    if (request.unless_suspending && std::ranges::contains(holder.suspending, pid)) {
        return false;
    }
    if (!request.asynchronous && executor.busy(target)) {
        // The caller runs again once the target's slice ended, and suspends it then.
        executor.block(caller, *target);
    }
    holder.suspending.push_back(pid);
    auto &state = executor.schedule(*target);
    ++state.suspends;
    ++state.holds;
    executor.dequeue(*target);
    return true;
}

std::optional<bool> Executor::resume(ProcessContext &caller, Word pid) {
    auto &executor = of(caller);
    const std::scoped_lock lock(executor.mutex_);
    auto *target = find(caller, pid);
    if (target == nullptr || executor.ending(*target)) {
        return std::nullopt;
    }
    auto &suspending = executor.schedule(caller).suspending;
    const auto found = std::ranges::find(suspending, pid);
    if (found == suspending.end()) {
        return false;
    }
    suspending.erase(found);
    executor.unsuspend(*target);
    return true;
}

void Executor::unsuspend(ProcessContext &target) {
    auto &state = schedule(target);
    if (state.suspends == 0 || state.holds == 0) {
        return;
    }
    --state.suspends;
    --state.holds;
    if (state.holds == 0 && std::exchange(state.ready, false)) {
        push(target);
    }
}

void Executor::release_suspends(ProcessContext &process) {
    for (const auto pid : std::exchange(schedule(process).suspending, {})) {
        if (auto *target = find(process, pid); target != nullptr && !ending(*target)) {
            unsuspend(*target);
        }
    }
}

void Executor::dequeue(ProcessContext &process) {
    bool queued = false;
    const std::scoped_lock queues(queues_mutex_);
    for (const auto &queue : queues_) {
        queued = queue->processes.erase(&process) || queued;
        if (queue->handed.load(std::memory_order_relaxed) == &process) {
            queue->handed.store(nullptr, std::memory_order_relaxed);
            queued = true;
        }
    }
    if (queued) {
        schedule(process).ready = true;
    }
}
} // namespace clause::runtime::detail
