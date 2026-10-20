#include "executor.hpp"
#include <algorithm>
#include <new>

// start_timer/send_after timers (plan step 41F, docs/processes.md#timers): armed in the receive-timeout wheel, their
// messages kept in a MessageBuffer and copied into the receiver's heap when they fire, as a send copies.
namespace clause::runtime::detail {
namespace {
// Ticks one arming may reach ahead: half of what the six wheel levels cover.
constexpr std::uint64_t WHEEL_REACH = std::uint64_t{1} << 35U;
} // namespace

void Executor::start_timer(ProcessContext &caller, TimerStart request) {
    auto &executor = of(caller);
    const std::scoped_lock lock(executor.mutex_);
    auto owned = std::make_unique<MessageTimer>();
    owned->reference = request.reference;
    owned->destination = request.destination;
    owned->message = std::move(request.message);
    owned->deadline = executor.tick_of(request.deadline);
    auto &timer = *owned;
    executor.message_timers_.emplace(request.reference, std::move(owned));
    executor.arm(timer);
}

std::optional<std::int64_t> Executor::timer_left(ProcessContext &caller, std::uint64_t reference,
                                                 bool cancel) noexcept {
    auto &executor = of(caller);
    const std::scoped_lock lock(executor.mutex_);
    const auto found = executor.message_timers_.find(reference);
    if (found == executor.message_timers_.end()) {
        return std::nullopt;
    }
    const auto now = executor.tick_of(std::chrono::steady_clock::now());
    const auto left = found->second->deadline > now ? found->second->deadline - now : 0;
    if (cancel) {
        executor.wheel_.cancel(found->second->timer);
        executor.message_timers_.erase(found);
    }
    return static_cast<std::int64_t>(left);
}

void Executor::arm(MessageTimer &timer) {
    timer.timer.owner = &timer;
    timer.timer.message = true;
    const auto earliest = wheel_.next();
    wheel_.arm(timer.timer, std::min(timer.deadline, wheel_.now() + WHEEL_REACH));
    // The timer thread sleeps until the earliest slot; an earlier one makes it plan again.
    if (!earliest || timer.timer.tick < *earliest) {
        timer_signal_.notify_one();
    }
}

void Executor::fire(MessageTimer &timer) {
    if (timer.deadline > wheel_.now()) {
        arm(timer);
        return;
    }
    const auto found = message_timers_.find(timer.reference);
    const auto owned = std::move(found->second);
    message_timers_.erase(found);
    auto destination = owned->destination;
    if (TermTag{destination}.get_kind() == TermKind::atom) {
        const auto name = names_.find(destination);
        destination = name == names_.end() ? 0 : name->second;
    }
    if (auto *receiver = destination ? process(destination) : nullptr) {
        post(*receiver, owned->message);
    }
}

void Executor::post(ProcessContext &receiver, const std::shared_ptr<const MessageBuffer> &message) {
    auto posted = Arrivals::Owned(Arrivals::Message::make(0));
    posted->buffer = message;
    post(receiver, std::move(posted));
}

void Executor::post(ProcessContext &receiver, Arrivals::Owned &&message) {
    if (schedule(receiver).ending) {
        return;
    }
    // The receiver takes it into its heap when it next waits for a message (arrival buffers).
    if (receiver.mailbox().arrivals().post(std::move(message)) && receiver.schedule().parked) {
        if (const auto woken = wake(receiver); !woken) {
            throw std::bad_alloc();
        }
    }
}

void Executor::drop_timers(Word pid) noexcept {
    std::erase_if(message_timers_, [&](auto &entry) {
        if (entry.second->destination != pid) {
            return false;
        }
        wheel_.cancel(entry.second->timer);
        return true;
    });
}
} // namespace clause::runtime::detail
