#pragma once
#include <atomic>
#include <clause/runtime/base_types.hpp>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

// The address range persistent terms live in (persistent term reclamation): one reservation per runtime, so whether a
// word names a persistent term is one range comparison, as OTP tells literals by their address. Areas are allocated
// and freed inside it; pages are committed as the used part grows and stay committed for reuse.
namespace clause::runtime::detail {
// Address ranges [begin, end) sorted by begin, as one copy or collection saw the retiring areas.
class RetiringRanges final {
  public:
    RetiringRanges() = default;
    explicit RetiringRanges(std::vector<std::pair<std::uintptr_t, std::uintptr_t>> ranges);
    // Whether an address lies in one of the ranges.
    bool contains(std::uintptr_t address) const noexcept;

    bool empty() const noexcept { return ranges_.empty(); }

    // The words of every range.
    std::size_t words() const noexcept;

  private:
    // The ranges, sorted by begin.
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> ranges_;
};

class PersistentRegion final {
  public:
    // Bytes reserved: plenty of address space on 64-bit hosts, a modest share of it on 32-bit ones.
    static constexpr std::size_t RESERVED_BYTES = sizeof(void *) == 8 ? std::size_t{1} << 36 : std::size_t{1} << 28;

    PersistentRegion() = default;
    // Release the reservation; every area must have been freed or be unreachable.
    ~PersistentRegion();
    PersistentRegion(const PersistentRegion &) = delete;
    PersistentRegion &operator=(const PersistentRegion &) = delete;

    // Committed words for one area, reserving the range on first use; throws std::bad_alloc when the range or the
    // host refuses.
    std::span<Word> allocate(std::size_t words);
    // Return an area's words for later areas; allocates nothing (the free runs are listed in their own words).
    void release(std::span<Word> area) noexcept;

    // Whether an address lies in the range; lock-free.
    bool contains(std::uintptr_t address) const noexcept {
        const auto *base = base_.load(std::memory_order_acquire);
        return base != nullptr && address - reinterpret_cast<std::uintptr_t>(base) < RESERVED_BYTES;
    }

    // Mark an area as retiring: every copy begun from now on copies its words instead of sharing them.
    void retire(std::span<const Word> area);
    // Forget a retiring area before it is freed.
    void forget(std::span<const Word> area) noexcept;
    // Whether an address lies in a retiring area now; lock-free while none is retiring.
    bool retiring(std::uintptr_t address) const noexcept;
    // The retiring areas now, for one copy to decide consistently; empty without allocating while none is retiring.
    RetiringRanges retiring_ranges() const;
    // The committed words from a word-aligned address in the range on; empty for any other address.
    std::span<const Word> words_from(std::uintptr_t address) const noexcept;

  private:
    // No free run: the end of the free list.
    static constexpr std::size_t NO_RUN = SIZE_MAX;

    // Reserve the range; throws std::bad_alloc when the host refuses.
    void reserve();
    // Commit the pages up to `bytes` from the base; throws std::bad_alloc when the host refuses.
    void commit(std::size_t bytes);
    // The two words at the start of a free run: its size in bytes and the offset of the next run, or NO_RUN.
    Word *run(std::size_t offset) const noexcept;
    // List a free run of `bytes` at `offset`, merged with the runs it touches; the merged run's offset and bytes.
    std::pair<std::size_t, std::size_t> insert_run(std::size_t offset, std::size_t bytes) noexcept;

    // The range's first byte, null until the first area; written once under mutex_.
    std::atomic<std::byte *> base_{nullptr};
    // Serializes allocation, release and the retiring list.
    mutable std::mutex mutex_;
    // The first free run below top_, by offset; runs are sorted by offset and coalesced.
    std::size_t free_ = NO_RUN;
    // Bytes handed out from the base so far, and bytes committed.
    std::size_t top_ = 0;
    std::atomic<std::size_t> committed_{0};
    // Retiring areas as [begin, end) addresses, and their count for the lock-free fast path.
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> retiring_;
    std::atomic<std::size_t> retiring_count_{0};
};
} // namespace clause::runtime::detail
