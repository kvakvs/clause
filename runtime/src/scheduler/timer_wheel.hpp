#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// A hierarchical hashed timer wheel of millisecond ticks (plan step 62B, docs/processes.md#receive-timeouts), as ERTS
// keeps timers: arming and cancelling are constant time, and advancing consults only the slots whose time came.
namespace clause::runtime::detail {
class TimerWheel final {
  public:
    // One timer, embedded in its owner: linked into one slot while armed.
    struct Timer final {
        // The slot neighbours while armed; prev is null for the first timer of a slot.
        Timer *prev = nullptr;
        Timer *next = nullptr;
        // The tick it fires at, whether it is in a slot now, and which one.
        std::uint64_t tick = 0;
        bool armed = false;
        std::uint8_t level = 0;
        std::uint8_t slot = 0;
        // Whatever the owner identifies itself with when the timer fires.
        void *owner = nullptr;
        // Whether the owner is a message timer (start_timer/send_after, step 41F) rather than a parked process.
        bool message = false;
    };

    // Six levels of 64 slots cover 2^36 ticks, beyond OTP's longest receive timeout of 2^32 - 1 ms.
    static constexpr unsigned LEVEL_BITS = 6;
    static constexpr std::size_t LEVELS = 6;
    static constexpr std::size_t SLOTS = std::size_t{1} << LEVEL_BITS;

    // Arm `timer` to fire at `tick`; a tick already reached fires at the next one, so no timer fires early.
    void arm(Timer &timer, std::uint64_t tick) noexcept;
    // Unlink an armed timer; an unarmed one is left alone.
    void cancel(Timer &timer) noexcept;
    // Move the wheel to `tick` and return every timer whose tick is at most `tick`, unlinked, in firing order.
    std::vector<Timer *> advance(std::uint64_t tick);
    // The next tick at which a slot must be consulted (a timer fires, or a higher slot cascades); none when empty.
    std::optional<std::uint64_t> next() const noexcept;
    // Unlink every timer, as when the owners all go away.
    void clear() noexcept;

    bool empty() const noexcept { return count_ == 0; }

    std::uint64_t now() const noexcept { return now_; }

  private:
    // The level and slot a timer firing at `tick` belongs to, seen from now_ (tick >= now_).
    std::pair<std::size_t, std::size_t> position(std::uint64_t tick) const noexcept;
    // Link `timer` into the slot of its tick (at least now_).
    void place(Timer &timer) noexcept;
    // Move every timer of a higher-level slot reached now to the lower levels.
    void cascade(std::size_t level) noexcept;
    // Unlink the timers of the level-0 slot of now_ into `due`.
    void fire(std::vector<Timer *> &due);
    // Unlink and return the whole list of one slot.
    Timer *take(std::size_t level, std::size_t slot) noexcept;

    // Slot heads per level, and which slots hold timers.
    std::array<std::array<Timer *, SLOTS>, LEVELS> slots_{};
    std::array<std::uint64_t, LEVELS> occupied_{};
    // The tick the wheel has advanced to, and how many timers are armed.
    std::uint64_t now_ = 0;
    std::size_t count_ = 0;
};
} // namespace clause::runtime::detail
