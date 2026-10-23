#pragma once
#include "../modules/literal_areas.hpp"
#include "runtime_memory.hpp"
#include <atomic>
#include <clause/runtime/process_context.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <vector>

// The runtime-wide persistent term table (plan step 41M1, docs/runtime.md#persistent-terms): each key and value copied
// into an area of its own in the persistent term range, found by a hash of the key and exact equality. A read returns
// the stored word in place, as OTP's literal persistent terms (persistent terms in place). A replaced or erased value's
// area retires: every copy copies its words from then on, each process copies those it holds into its own heap at its
// next collection, and the area is freed once every process registered at retirement has done so or ended
// (persistent term reclamation).
namespace clause::runtime::detail {
namespace layout {
struct RefcBinaryCell;
} // namespace layout

// A term copied into the persistent term range: its root word, its area (empty for an immediate or a module literal),
// the binary cells in it, which share their buffers, and the account its words are charged to.
struct PersistentCopy {
    Word root = 0;
    std::span<Word> area;
    std::vector<layout::RefcBinaryCell *> binaries;
    std::shared_ptr<RuntimeMemory> memory;
};

// Copy a term of `writer` into a new area of `region`, sharing only module literals; implemented with the graph copy.
TermResult<PersistentCopy> copy_persistent(ProcessContext &writer, PersistentRegion &region,
                                           const Term &value) noexcept;
// Destroy a copy's binary cells and return its area and charge.
void free_persistent(PersistentRegion &region, const PersistentCopy &copy) noexcept;

// What one evacuating collection copies out (persistent term reclamation): the areas of every retirement after the
// process's last one, which stay allocated until it reports this epoch, and their binary cells, whose buffers it holds
// first.
struct Evacuation {
    std::uint64_t epoch = 0;
    RetiringRanges ranges;
    std::vector<const layout::RefcBinaryCell *> binaries;
};

class PersistentTerms final {
  public:
    // One stored association, as words of the persistent term range (or immediates).
    struct Entry {
        Word key;
        Word value;
    };

    // Whether a stored key is the key looked for: exact equality, decided by the caller who holds a process.
    using SameKey = std::function<bool(Word)>;

    // Copy areas into the runtime's persistent term range, which outlives the table.
    explicit PersistentTerms(PersistentRegion &region) : region_(region) {}

    // Free every area; no process may still run.
    ~PersistentTerms();
    PersistentTerms(const PersistentTerms &) = delete;
    PersistentTerms &operator=(const PersistentTerms &) = delete;

    // The value stored under the key hashed to `hash` that `same` accepts, if any.
    std::optional<Word> find(std::uint64_t hash, const SameKey &same) const;
    // Copy `key` and `value`, terms of `writer`, and store them under `hash`, retiring the value of the entry `same`
    // accepts.
    TermResult<void> store(ProcessContext &writer, std::uint64_t hash, const Term &key, const Term &value,
                           const SameKey &same);
    // Remove and retire the entry `same` accepts; whether there was one.
    bool erase(std::uint64_t hash, const SameKey &same);
    // Every entry, in no particular order.
    std::vector<Entry> entries() const;
    // The number of entries and the words their copies take.
    std::size_t count() const;
    std::size_t words() const;

    // A new process: it holds no word of an area retired so far.
    void enter(const ProcessContext &process);
    // An ended process: it no longer holds any word.
    void leave(const ProcessContext &process);
    // Whether a process may still hold words of areas retired since its last copying collection; lock-free while no
    // area waits.
    bool behind(const ProcessContext &process) const;

    // The areas an evacuating collection of `process` copies out now; may throw std::bad_alloc.
    Evacuation evacuation(const ProcessContext &process) const;
    // A process finished the evacuating collection of `epoch`; frees the areas nobody holds any more.
    void evacuated(const ProcessContext &process, std::uint64_t epoch);

    // Whether some retired area waits for processes to copy it out; lock-free.
    bool waiting() const noexcept { return waiting_.load(std::memory_order_acquire); }

    // Retirements not freed yet (tests).
    std::size_t retired() const;

  private:
    // A stored association and the copies holding it.
    struct Stored {
        Entry entry;
        PersistentCopy key;
        PersistentCopy value;
    };

    // The areas one retirement took out of the table, and how many processes registered then may still hold them.
    struct Retired {
        std::vector<PersistentCopy> copies;
        std::size_t waiting = 0;
    };

    // Prepare the retirement of copies about to leave the table: their areas marked retiring and an empty record
    // under the next epoch; null when nothing needs one (no area, or no registered process). Throws std::bad_alloc
    // with nothing changed. Caller holds mutex_ alone.
    Retired *prepare(std::span<const PersistentCopy *const> copies);
    // Move copies that left the table into their prepared retirement and publish its epoch, or free them at once
    // without one. Caller holds mutex_ alone.
    void commit(Retired *retired, std::vector<PersistentCopy> copies) noexcept;

    // The retirements after `after` up to `upto`.
    struct Epochs {
        std::uint64_t after;
        std::uint64_t upto;
    };

    // Count one process off every retirement in `epochs`, freeing those nobody holds. Caller holds mutex_ alone.
    void release(Epochs epochs);
    // Free a retirement's areas. Caller holds mutex_ alone.
    void free(Retired &retired) noexcept;

    // Readers share the lock; put, erase and the process bookkeeping take it alone.
    mutable std::shared_mutex mutex_;
    // Entries by key hash; keys with equal hashes share a bucket.
    std::unordered_multimap<std::uint64_t, Stored> entries_;
    // The range the areas live in.
    PersistentRegion &region_;
    // Retirements so far; each retirement takes the next number.
    std::atomic<std::uint64_t> epoch_{0};
    // Retirements not freed yet, by epoch; empty most of the time, which waiting_ tells without the lock.
    std::map<std::uint64_t, Retired> retired_;
    std::atomic<bool> waiting_{false};
    // Registered processes; each keeps the epoch its last copying collection began at (or its creation saw).
    std::size_t registered_ = 0;
};
} // namespace clause::runtime::detail
