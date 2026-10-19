#pragma once
#include "index.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <vector>

// An ETS table (plan 21 step LIB10A, docs/runtime.md#ets-tables): its options, owner and heir, its objects in one
// index part or, for hash tables with write_concurrency, in 64 parts chosen by key hash, and its fixations. Locks:
// the table lock is shared by single-key operations and taken alone by whole-table ones (lists of objects, clearing,
// fixation, deletion); each part has its own lock under it.
namespace clause::runtime::detail::ets {
enum class TableType : std::uint8_t { set, ordered_set, bag, duplicate_bag };
enum class Protection : std::uint8_t { public_access, protected_access, private_access };
enum class WriteConcurrency : std::uint8_t { off, on, automatic };

// The options of ets:new/2 a table keeps; protection may change later (setopts/2).
struct TableOptions {
    TableType type = TableType::set;
    Protection protection = Protection::protected_access;
    std::size_t keypos = 1;
    bool named = false;
    bool compressed = false;
    bool read_concurrency = false;
    WriteConcurrency write_concurrency = WriteConcurrency::off;
    bool decentralized_counters = false;
};

// Who a table is at its creation: its identifier number, name and owner.
struct TableIdentity {
    std::uint64_t number;
    Word name;
    Word owner;
};

// When a fixation began, in monotonic nanoseconds and system microseconds.
struct FixTime {
    std::int64_t monotonic_nanos;
    std::int64_t system_micros;
};

// Where a walk step landed.
enum class Reached : std::uint8_t { objects, end, missing, deleted };

// When the first fixation of a table began and who holds fixations now, highest pid first (ets:info/2
// safe_fixed).
struct Fixation {
    std::int64_t monotonic_nanos;
    std::int64_t system_micros;
    std::vector<std::pair<Word, std::size_t>> holders;
};

class Table final {
  public:
    // Parts of a hash table with write_concurrency.
    static constexpr std::size_t CONCURRENT_PARTS = 64;

    Table(const TableIdentity &identity, const TableOptions &options);

    // The reference number of the table identifier.
    std::uint64_t number() const noexcept { return number_; }

    const TableOptions &options() const noexcept { return options_; }

    // Every type but ordered_set hashes keys.
    bool hashed() const noexcept { return options_.type != TableType::ordered_set; }

    Word name() const noexcept { return name_.load(std::memory_order_acquire); }

    void rename(Word name) noexcept { name_.store(name, std::memory_order_release); }

    Word owner() const noexcept { return owner_.load(std::memory_order_acquire); }

    void set_owner(Word owner) noexcept { owner_.store(owner, std::memory_order_release); }

    Protection protection() const noexcept { return protection_.load(std::memory_order_acquire); }

    void set_protection(Protection protection) noexcept { protection_.store(protection, std::memory_order_release); }

    // The heir's pid, 0 for none; the registry changes it with its data.
    Word heir() const noexcept { return heir_.load(std::memory_order_acquire); }

    // Run f(index) on the part of a key under shared locks, or under its part lock alone for f(Index &); false when
    // the table was deleted.
    template <typename F> bool read(const Key &key, F &&f) const {
        const std::shared_lock table(mutex_);
        if (deleted_) {
            return false;
        }
        const auto &part = part_of(key);
        const std::shared_lock lock(part.mutex);
        f(static_cast<const Index &>(*part.index));
        return true;
    }

    template <typename F> bool write(const Key &key, F &&f) {
        const std::shared_lock table(mutex_);
        if (deleted_) {
            return false;
        }
        auto &part = part_of(key);
        const std::unique_lock lock(part.mutex);
        f(*part.index);
        return true;
    }

    // Run f(table) with the table locked alone; index() and indexes() serve inside it. False when deleted.
    template <typename F> bool exclusive(F &&f) {
        const std::unique_lock table(mutex_);
        if (deleted_) {
            return false;
        }
        f(*this);
        return true;
    }

    // Run f(index) on every part in order, each under its shared lock, the table lock shared; false when deleted.
    template <typename F> bool read_parts(F &&f) const {
        const std::shared_lock table(mutex_);
        if (deleted_) {
            return false;
        }
        for (std::size_t at = 0; at < part_count_; ++at) {
            const std::shared_lock lock(parts_[at].mutex);
            f(static_cast<const Index &>(*parts_[at].index));
        }
        return true;
    }

    // The index of a key's part, and of each part, for a caller holding the table alone.
    Index &index(const Key &key) noexcept { return *part_of(key).index; }

    std::size_t part_count() const noexcept { return part_count_; }

    Index &part(std::size_t at) noexcept { return *parts_[at].index; }

    // Walks (first/next/last/prev): f(objects) runs under the lock of the part holding them.
    template <typename F> Reached first(F &&f) const {
        const std::shared_lock table(mutex_);
        return deleted_ ? Reached::deleted : forward(0, f);
    }

    template <typename F> Reached next(const Key &key, F &&f) const {
        const std::shared_lock table(mutex_);
        if (deleted_) {
            return Reached::deleted;
        }
        const auto at = part_number(key);
        {
            const std::shared_lock lock(parts_[at].mutex);
            const auto step = parts_[at].index->next(key);
            if (step.missing) {
                return Reached::missing;
            }
            if (step.objects != nullptr) {
                f(*step.objects);
                return Reached::objects;
            }
        }
        return forward(at + 1, f);
    }

    // An ordered_set walks backwards; a hash table walks backwards as forwards.
    template <typename F> Reached last(F &&f) const {
        if (hashed()) {
            return first(f);
        }
        return backward(f, [](const Index &index) { return Step{.objects = index.last()}; });
    }

    template <typename F> Reached prev(const Key &key, F &&f) const {
        if (hashed()) {
            return next(key, f);
        }
        return backward(f, [&key](const Index &index) { return index.prev(key); });
    }

    // The objects of slot `index`, added to `objects`; end just past the last slot, missing beyond it.
    Reached slot(std::size_t index, std::vector<Word> &objects) const;

    // Run f(objects) on the objects of slot `index` (numbered over the parts as slot/2 numbers them) while its part
    // is locked; end just past the last slot, missing beyond it (select walks, plan 21 step LIB11).
    template <typename F> Reached visit_slot(std::size_t index, F &&f) const {
        const std::shared_lock table(mutex_);
        if (deleted_) {
            return Reached::deleted;
        }
        auto rest = index;
        for (std::size_t at = 0; at < part_count_; ++at) {
            const std::shared_lock lock(parts_[at].mutex);
            const auto slots = parts_[at].index->slots();
            if (rest < slots) {
                std::vector<Word> objects;
                parts_[at].index->slot(rest, objects);
                f(std::span<const Word>(objects));
                return Reached::objects;
            }
            rest -= slots;
        }
        return rest == 0 ? Reached::end : Reached::missing;
    }

    // Run f(index) on every part in order, each locked alone under the table lock shared (select edits); false when
    // deleted.
    template <typename F> bool edit_parts(F &&f) {
        const std::shared_lock table(mutex_);
        if (deleted_) {
            return false;
        }
        for (std::size_t at = 0; at < part_count_; ++at) {
            const std::unique_lock lock(parts_[at].mutex);
            f(*parts_[at].index);
        }
        return true;
    }

    // Live objects and the words the table takes (ets:info/2 size and memory); none when deleted.
    std::optional<std::size_t> size() const;
    std::optional<std::size_t> words() const;

    // A process fixes or releases the table once (safe_fixtable/2); whether its own count went from or to zero.
    // False when deleted.
    std::optional<bool> fix(Word pid, FixTime time);
    std::optional<bool> unfix(Word pid);
    // A process ended: release all of its fixations.
    void unfix_all(Word pid);
    // The fixations, if any; none when not fixed or deleted.
    std::optional<Fixation> fixation() const;
    // Whether the table is fixed; none when deleted.
    std::optional<bool> fixed() const;

    // Mark the table deleted and free its objects; every later operation fails.
    void destroy();

    // The heir and its data, changed by the registry under its lock.
    void set_heir(Word heir, std::shared_ptr<const DetachedTerm> data) noexcept;

    const std::shared_ptr<const DetachedTerm> &heir_data() const noexcept { return heir_data_; }

  private:
    // One index and its lock.
    struct Part {
        mutable std::shared_mutex mutex;
        std::unique_ptr<Index> index;
    };

    std::size_t part_number(const Key &key) const noexcept {
        // The top bits choose the part; the low ones the bucket inside it.
        return part_count_ == 1 ? 0 : static_cast<std::size_t>(key.hash >> 58);
    }

    const Part &part_of(const Key &key) const noexcept { return parts_[part_number(key)]; }

    Part &part_of(const Key &key) noexcept { return parts_[part_number(key)]; }

    // The first objects of the parts from `from` on; the table lock is held shared.
    template <typename F> Reached forward(std::size_t from, F &f) const {
        for (auto at = from; at < part_count_; ++at) {
            const std::shared_lock lock(parts_[at].mutex);
            if (const auto *objects = parts_[at].index->first()) {
                f(*objects);
                return Reached::objects;
            }
        }
        return Reached::end;
    }

    // A backward step of an ordered_set, its one part locked shared.
    template <typename F, typename S> Reached backward(F &f, S step) const {
        const std::shared_lock table(mutex_);
        if (deleted_) {
            return Reached::deleted;
        }
        const std::shared_lock lock(parts_[0].mutex);
        const auto reached = step(static_cast<const Index &>(*parts_[0].index));
        if (reached.objects == nullptr) {
            return Reached::end;
        }
        f(*reached.objects);
        return Reached::objects;
    }

    std::uint64_t number_;
    TableOptions options_;
    std::atomic<Word> name_;
    std::atomic<Word> owner_;
    std::atomic<Protection> protection_;
    std::atomic<Word> heir_;
    // The heir's data, read when the owner ends; guarded by the registry lock.
    std::shared_ptr<const DetachedTerm> heir_data_;

    mutable std::shared_mutex mutex_;
    std::size_t part_count_;
    std::unique_ptr<Part[]> parts_;
    // Set once by destroy(); guarded by mutex_.
    bool deleted_ = false;
    // Fixation counts by pid and when the first one began; guarded by mutex_ held alone (read shared).
    std::map<Word, std::size_t> fixers_;
    std::int64_t fixed_monotonic_ = 0;
    std::int64_t fixed_system_ = 0;
};
} // namespace clause::runtime::detail::ets
