#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

// Microstate accounting of the workers (plan 21 step MIM5, docs/builtins.md#system-information): the time each worker
// spends running processes, running port tasks, waiting for work and in between, while
// system_flag(microstate_accounting) is on.
namespace clause::runtime::detail {

// The states a worker records, statistics(microstate_accounting)'s emulator, port, sleep and other.
enum class Microstate : std::uint8_t { emulator, port, sleep, other };

inline constexpr std::size_t MICROSTATES = 4;

// Nanoseconds per state, indexed by Microstate.
using MicrostateTimes = std::array<std::int64_t, MICROSTATES>;

// One worker's account: the worker moves between states; other threads switch accounting on and off, zero and read
// it. The worker's own moves with accounting off only note the state.
class WorkerAccount final {
  public:
    // The worker enters `state`; with accounting on, the time since its last move goes to the state it leaves.
    void enter(Microstate state, bool on) noexcept;
    // Accounting starts now.
    void start() noexcept;
    // Accounting stops now, the current state's time counted.
    void stop() noexcept;
    // Every state's time back to zero, counting from now.
    void reset() noexcept;
    // The times so far; with accounting on, the current state's up to now.
    MicrostateTimes read(bool on) noexcept;

  private:
    using Clock = std::chrono::steady_clock;

    // Charge the time since since_ to the current state (under mutex_).
    void charge(Clock::time_point now) noexcept;

    // Guards since_ and times_ between the worker and the threads that switch or read the account.
    std::mutex mutex_;
    // The state the worker is in, noted also with accounting off so that switching it on charges the right one.
    std::atomic<Microstate> state_{Microstate::other};
    // When the time of the current state started to count.
    Clock::time_point since_{};
    // The counted nanoseconds per state.
    MicrostateTimes times_{};
};

} // namespace clause::runtime::detail
