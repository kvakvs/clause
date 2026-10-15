#include "../runtime_state.hpp"
#include "executor.hpp"
#include <algorithm>

// What the executor reports about its processes (plan step 41G1, docs/processes.md#introspection): counts, live
// pids and process status.
namespace clause::runtime::detail {
Executor::Counts Executor::counts(ProcessContext &context) noexcept {
    auto &executor = of(context);
    const std::scoped_lock lock(executor.mutex_);
    return {.processes = executor.runtime_.processes.size(),
            .ports = executor.ports_.size(),
            .run_queue = executor.queued(),
            .workers = executor.runtime_.options.schedulers,
            .slices = executor.slices_,
            .memory_words = executor.runtime_.memory->used(),
            .started = executor.epoch_};
}

std::vector<Word> Executor::pids(ProcessContext &context) {
    auto &executor = of(context);
    const std::scoped_lock lock(executor.mutex_);
    std::vector<Word> result;
    result.reserve(executor.runtime_.processes.size());
    for (const auto &[number, process] : executor.runtime_.processes) {
        if (!executor.ending(*process)) {
            result.push_back(pid_word(number));
        }
    }
    std::ranges::sort(result, {}, pid_number);
    return result;
}

bool Executor::ending(ProcessContext &process) const noexcept {
    const auto found = schedules_.find(&process);
    return found != schedules_.end() && found->second.ending;
}

std::string_view Executor::status(const ProcessContext &caller, ProcessContext &target) const noexcept {
    if (&caller == &target) {
        return "running";
    }
    return parked_.contains(&target) ? "waiting" : "runnable";
}
} // namespace clause::runtime::detail
