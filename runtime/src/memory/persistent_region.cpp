#include "persistent_region.hpp"
#include <algorithm>
#include <new>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace clause::runtime::detail {
namespace {
// Commit granularity: generous enough for every host page size.
constexpr std::size_t COMMIT_BYTES = std::size_t{64} * 1024;
// Areas take whole multiples of two words, so every free run can list itself in its own first two words.
constexpr std::size_t GRANULE_BYTES = 2 * sizeof(Word);

// An area's bytes in the range: its words rounded up to whole granules.
std::size_t area_bytes(std::size_t words) noexcept {
    const auto bytes = std::max<std::size_t>(words, 1) * sizeof(Word);
    return (bytes + GRANULE_BYTES - 1) / GRANULE_BYTES * GRANULE_BYTES;
}

// Reserve address space with no access and no backing; null when the host refuses.
void *reserve_range(std::size_t bytes) noexcept {
#if defined(_WIN32)
    return VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_NOACCESS);
#else
    void *range = mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return range == MAP_FAILED ? nullptr : range;
#endif
}

// Make reserved bytes readable and writable; false when the host refuses.
bool commit_range(void *at, std::size_t bytes) noexcept {
#if defined(_WIN32)
    return VirtualAlloc(at, bytes, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
    return mprotect(at, bytes, PROT_READ | PROT_WRITE) == 0;
#endif
}

// Give the whole reservation back.
void release_range(void *range, std::size_t bytes) noexcept {
#if defined(_WIN32)
    static_cast<void>(bytes);
    VirtualFree(range, 0, MEM_RELEASE);
#else
    munmap(range, bytes);
#endif
}
} // namespace

RetiringRanges::RetiringRanges(std::vector<std::pair<std::uintptr_t, std::uintptr_t>> ranges)
    : ranges_(std::move(ranges)) {
    std::ranges::sort(ranges_);
}

bool RetiringRanges::contains(std::uintptr_t address) const noexcept {
    const auto after =
        std::ranges::upper_bound(ranges_, address, {}, &std::pair<std::uintptr_t, std::uintptr_t>::first);
    return after != ranges_.begin() && address < std::prev(after)->second;
}

std::size_t RetiringRanges::words() const noexcept {
    std::size_t words = 0;
    for (const auto &[begin, end] : ranges_) {
        words += (end - begin) / sizeof(Word);
    }
    return words;
}

PersistentRegion::~PersistentRegion() {
    if (auto *base = base_.load(std::memory_order_relaxed); base != nullptr) {
        release_range(base, RESERVED_BYTES);
    }
}

void PersistentRegion::reserve() {
    void *range = reserve_range(RESERVED_BYTES);
    if (range == nullptr) {
        throw std::bad_alloc();
    }
    base_.store(static_cast<std::byte *>(range), std::memory_order_release);
}

void PersistentRegion::commit(std::size_t bytes) {
    const auto committed = committed_.load(std::memory_order_relaxed);
    if (bytes <= committed) {
        return;
    }
    const auto wanted = std::min(RESERVED_BYTES, (bytes + COMMIT_BYTES - 1) / COMMIT_BYTES * COMMIT_BYTES);
    if (!commit_range(base_.load(std::memory_order_relaxed) + committed, wanted - committed)) {
        throw std::bad_alloc();
    }
    committed_.store(wanted, std::memory_order_release);
}

Word *PersistentRegion::run(std::size_t offset) const noexcept {
    return reinterpret_cast<Word *>(base_.load(std::memory_order_relaxed) + offset);
}

std::span<Word> PersistentRegion::allocate(std::size_t words) {
    const std::scoped_lock lock(mutex_);
    if (base_.load(std::memory_order_relaxed) == nullptr) {
        reserve();
    }
    const auto bytes = area_bytes(words);
    // First fit among the free runs; a larger run keeps its rest, which is a whole number of granules.
    auto *link = &free_;
    for (auto offset = free_; offset != NO_RUN; offset = run(offset)[1]) {
        auto *found = run(offset);
        if (found[0] >= bytes) {
            const auto rest = found[0] - bytes;
            const auto next = found[1];
            if (rest == 0) {
                *link = next;
            } else {
                auto *tail = run(offset + bytes);
                tail[0] = rest;
                tail[1] = next;
                *link = offset + bytes;
            }
            return {run(offset), words};
        }
        link = &found[1];
    }
    if (bytes > RESERVED_BYTES - top_) {
        throw std::bad_alloc();
    }
    commit(top_ + bytes);
    const auto offset = std::exchange(top_, top_ + bytes);
    return {run(offset), words};
}

void PersistentRegion::release(std::span<Word> area) noexcept {
    if (area.data() == nullptr) {
        return;
    }
    const std::scoped_lock lock(mutex_);
    const auto offset =
        static_cast<std::size_t>(reinterpret_cast<std::byte *>(area.data()) - base_.load(std::memory_order_relaxed));
    const auto [start, bytes] = insert_run(offset, area_bytes(area.size()));
    // The last run, when it reaches the top, lowers the top instead of staying listed.
    if (start + bytes == top_) {
        auto *link = &free_;
        while (*link != start) {
            link = &run(*link)[1];
        }
        *link = NO_RUN;
        top_ = start;
    }
}

std::pair<std::size_t, std::size_t> PersistentRegion::insert_run(std::size_t offset, std::size_t bytes) noexcept {
    // The runs on both sides of the new one, in offset order.
    auto previous = NO_RUN;
    auto next = free_;
    while (next != NO_RUN && next < offset) {
        previous = std::exchange(next, run(next)[1]);
    }
    if (next != NO_RUN && offset + bytes == next) {
        bytes += run(next)[0];
        next = run(next)[1];
    }
    if (previous != NO_RUN && previous + run(previous)[0] == offset) {
        offset = previous;
        bytes += run(previous)[0];
    } else if (previous == NO_RUN) {
        free_ = offset;
    } else {
        run(previous)[1] = offset;
    }
    run(offset)[0] = bytes;
    run(offset)[1] = next;
    return {offset, bytes};
}

void PersistentRegion::retire(std::span<const Word> area) {
    const std::scoped_lock lock(mutex_);
    const auto begin = reinterpret_cast<std::uintptr_t>(area.data());
    retiring_.emplace_back(begin, begin + area.size_bytes());
    retiring_count_.store(retiring_.size(), std::memory_order_release);
}

void PersistentRegion::forget(std::span<const Word> area) noexcept {
    const std::scoped_lock lock(mutex_);
    const auto begin = reinterpret_cast<std::uintptr_t>(area.data());
    std::erase_if(retiring_, [&](const auto &range) { return range.first == begin; });
    retiring_count_.store(retiring_.size(), std::memory_order_release);
}

bool PersistentRegion::retiring(std::uintptr_t address) const noexcept {
    if (retiring_count_.load(std::memory_order_acquire) == 0 || !contains(address)) {
        return false;
    }
    const std::scoped_lock lock(mutex_);
    return std::ranges::any_of(retiring_,
                               [&](const auto &range) { return address >= range.first && address < range.second; });
}

RetiringRanges PersistentRegion::retiring_ranges() const {
    if (retiring_count_.load(std::memory_order_acquire) == 0) {
        return {};
    }
    const std::scoped_lock lock(mutex_);
    return RetiringRanges(retiring_);
}

std::span<const Word> PersistentRegion::words_from(std::uintptr_t address) const noexcept {
    const auto *base = base_.load(std::memory_order_acquire);
    const auto start = reinterpret_cast<std::uintptr_t>(base);
    const auto committed = committed_.load(std::memory_order_acquire);
    if (base == nullptr || address < start || address - start >= committed || address % sizeof(Word) != 0) {
        return {};
    }
    const auto offset = address - start;
    return {reinterpret_cast<const Word *>(base + offset), (committed - offset) / sizeof(Word)};
}
} // namespace clause::runtime::detail
