#include "microstates.hpp"
#include "executor.hpp"

// Microstate accounting (plan 21 step MIM5): each worker's account and the executor's switch over them.
namespace clause::runtime::detail {

void WorkerAccount::enter(const Microstate state, const bool on) noexcept {
    if (!on) {
        state_.store(state, std::memory_order_relaxed);
        return;
    }
    const std::scoped_lock lock(mutex_);
    charge(Clock::now());
    state_.store(state, std::memory_order_relaxed);
}

void WorkerAccount::start() noexcept {
    const std::scoped_lock lock(mutex_);
    since_ = Clock::now();
}

void WorkerAccount::stop() noexcept {
    const std::scoped_lock lock(mutex_);
    charge(Clock::now());
}

void WorkerAccount::reset() noexcept {
    const std::scoped_lock lock(mutex_);
    times_ = {};
    since_ = Clock::now();
}

MicrostateTimes WorkerAccount::read(const bool on) noexcept {
    const std::scoped_lock lock(mutex_);
    auto times = times_;
    if (on) {
        const auto running = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - since_);
        times.at(static_cast<std::size_t>(state_.load(std::memory_order_relaxed))) += running.count();
    }
    return times;
}

void WorkerAccount::charge(const Clock::time_point now) noexcept {
    const auto spent = std::chrono::duration_cast<std::chrono::nanoseconds>(now - since_);
    times_.at(static_cast<std::size_t>(state_.load(std::memory_order_relaxed))) += spent.count();
    since_ = now;
}

namespace {
// Apply `step` to every worker's account.
template <typename Queues, typename Step> void each_account(const Queues &queues, const Step &step) {
    for (const auto &queue : queues) {
        step(queue->account);
    }
}
} // namespace

bool Executor::microstate_accounting(ProcessContext &context, const MicrostateRequest request) {
    auto &executor = of(context);
    const std::scoped_lock lock(executor.mutex_);
    const bool was_on = executor.accounting_.load(std::memory_order_relaxed);
    if (request == MicrostateRequest::reset) {
        each_account(executor.queues_, [](WorkerAccount &account) { account.reset(); });
        return was_on;
    }
    const bool on = request == MicrostateRequest::enable;
    if (on == was_on) {
        return was_on;
    }
    if (on) {
        // Every account counts from now before the workers start charging.
        each_account(executor.queues_, [](WorkerAccount &account) { account.start(); });
        executor.accounting_.store(true, std::memory_order_relaxed);
    } else {
        executor.accounting_.store(false, std::memory_order_relaxed);
        each_account(executor.queues_, [](WorkerAccount &account) { account.stop(); });
    }
    return was_on;
}

std::vector<MicrostateTimes> Executor::microstates(ProcessContext &context) {
    auto &executor = of(context);
    const std::scoped_lock lock(executor.mutex_);
    const bool on = executor.accounting_.load(std::memory_order_relaxed);
    std::vector<MicrostateTimes> times;
    times.reserve(executor.queues_.size());
    for (const auto &queue : executor.queues_) {
        times.push_back(queue->account.read(on));
    }
    return times;
}

} // namespace clause::runtime::detail
