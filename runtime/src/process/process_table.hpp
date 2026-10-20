#pragma once
#include <atomic>
#include <clause/runtime/process_context.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// The live processes of a runtime by pid number (arrival buffers). A send on a worker finds its receiver's arrivals
// without any lock: an open-addressed array of atomic slots. Pid numbers are never reused, so an erased slot stays a
// tombstone until the array is rebuilt; a rebuilt array is retired, and the executor frees it once no worker can
// still read it (quiescent reclamation). Inserts and erases take the writer lock.
namespace clause::runtime::detail {
class ProcessTable final {
  public:
    // One slot: number 0 is empty; a number without a process is a tombstone.
    struct Slot {
        std::atomic<Word> number{0};
        std::atomic<ProcessContext *> process{nullptr};
        std::atomic<Arrivals *> arrivals{nullptr};
    };

    // One array of slots, probed linearly from a hash of the number; a power of two at most half used.
    struct Array final : Reclaimable {
        explicit Array(std::size_t capacity) : slots(std::make_unique<Slot[]>(capacity)), mask(capacity - 1) {}

        std::unique_ptr<Slot[]> slots;
        std::size_t mask;
        // Numbered slots, live and tombstones; written under the writer lock only.
        std::size_t used = 0;
    };

    ProcessTable() : current_(std::make_unique<Array>(MIN_CAPACITY)), array_(current_.get()) {}

    // Publish a process. Allocation failure throws, leaving the table as it was.
    void insert(Word number, ProcessContext &process) {
        const std::scoped_lock lock(writer_);
        if (2 * (current_->used + 1) > current_->mask + 1) {
            rebuild();
        }
        place(*current_, number, process);
        size_.fetch_add(1, std::memory_order_relaxed);
    }

    // Unpublish a process: lookups from now on do not find it.
    void erase(Word number) noexcept {
        const std::scoped_lock lock(writer_);
        auto *slot = find_slot(*current_, number);
        if (slot != nullptr && slot->process.load(std::memory_order_relaxed) != nullptr) {
            slot->arrivals.store(nullptr, std::memory_order_release);
            slot->process.store(nullptr, std::memory_order_release);
            size_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    // The live process of `number`, or null. The caller holds the executor's lock, under which every insert and erase
    // of a running program happens, so the process stays live.
    ProcessContext *find(Word number) const noexcept {
        const auto *slot = find_slot(*array_.load(std::memory_order_acquire), number);
        return slot != nullptr ? slot->process.load(std::memory_order_acquire) : nullptr;
    }

    // The arrivals of the live process of `number`, or null; read without any lock by a worker, for which they stay
    // allocated until its next quiescent point.
    Arrivals *arrivals(Word number) const noexcept {
        const auto *slot = find_slot(*array_.load(std::memory_order_acquire), number);
        return slot != nullptr ? slot->arrivals.load(std::memory_order_acquire) : nullptr;
    }

    // The number of live processes.
    std::size_t size() const noexcept { return size_.load(std::memory_order_relaxed); }

    // The pid numbers of every live process, in no order. Allocation failure throws.
    std::vector<Word> numbers() const {
        const std::scoped_lock lock(writer_);
        std::vector<Word> numbers;
        numbers.reserve(size());
        for (std::size_t index = 0; index <= current_->mask; ++index) {
            const auto &slot = current_->slots[index];
            if (slot.process.load(std::memory_order_relaxed) != nullptr) {
                numbers.push_back(slot.number.load(std::memory_order_relaxed));
            }
        }
        return numbers;
    }

    // The arrays replaced since the last call, which workers may still read.
    std::vector<std::unique_ptr<Array>> take_retired() noexcept {
        const std::scoped_lock lock(writer_);
        return std::exchange(retired_, {});
    }

  private:
    static constexpr std::size_t MIN_CAPACITY = 64;
    // Fibonacci hashing spreads consecutive pid numbers.
    static constexpr std::uint64_t SPREAD = 0x9E37'79B9'7F4A'7C15ULL;
    static constexpr unsigned SPREAD_SHIFT = 32;

    static std::size_t home(Word number, std::size_t mask) noexcept {
        return static_cast<std::size_t>((static_cast<std::uint64_t>(number) * SPREAD) >> SPREAD_SHIFT) & mask;
    }

    // The slot numbered `number`, live or a tombstone, or null.
    static Slot *find_slot(const Array &array, Word number) noexcept {
        for (auto index = home(number, array.mask);; index = (index + 1) & array.mask) {
            auto &slot = array.slots[index];
            const auto found = slot.number.load(std::memory_order_acquire);
            if (found == number) {
                return &slot;
            }
            if (found == 0) {
                return nullptr;
            }
        }
    }

    // Fill the first empty slot of `number`'s probe; the number is stored last, publishing the slot.
    static void place(Array &array, Word number, ProcessContext &process) noexcept {
        for (auto index = home(number, array.mask);; index = (index + 1) & array.mask) {
            auto &slot = array.slots[index];
            if (slot.number.load(std::memory_order_relaxed) == 0) {
                slot.process.store(&process, std::memory_order_relaxed);
                slot.arrivals.store(&process.mailbox().arrivals(), std::memory_order_relaxed);
                slot.number.store(number, std::memory_order_release);
                ++array.used;
                return;
            }
        }
    }

    // Copy the live slots into a new array sized for them, without tombstones, and retire the old one.
    void rebuild() {
        auto capacity = MIN_CAPACITY;
        while (capacity < 4 * (size() + 1)) {
            capacity *= 2;
        }
        auto fresh = std::make_unique<Array>(capacity);
        for (std::size_t index = 0; index <= current_->mask; ++index) {
            const auto &slot = current_->slots[index];
            if (auto *process = slot.process.load(std::memory_order_relaxed)) {
                place(*fresh, slot.number.load(std::memory_order_relaxed), *process);
            }
        }
        retired_.reserve(retired_.size() + 1);
        array_.store(fresh.get(), std::memory_order_release);
        retired_.push_back(std::exchange(current_, std::move(fresh)));
    }

    // Serializes inserts and erases.
    mutable std::mutex writer_;
    // The array inserts write; owned here, read through array_.
    std::unique_ptr<Array> current_;
    std::atomic<Array *> array_;
    // Live processes.
    std::atomic<std::size_t> size_{0};
    // Replaced arrays not yet handed to the executor.
    std::vector<std::unique_ptr<Array>> retired_;
};
} // namespace clause::runtime::detail
