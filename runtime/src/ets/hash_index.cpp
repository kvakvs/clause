#include "index.hpp"
#include <algorithm>
#include <optional>
#include <utility>

// The hash index of set, bag and duplicate_bag tables (plan 21 step LIB10A, docs/runtime.md#ets-tables): a
// power-of-two array of buckets, each holding the entries of the keys hashed to it in insertion order. An entry
// holds a key's objects. The index doubles its buckets above two keys per bucket and halves them below one in eight;
// it never moves keys while fixed, and a key removed then stays as a tombstone holding one object for its key, so a
// walk visits each bucket in order and every key in it once.
namespace clause::runtime::detail::ets {
namespace {
constexpr std::size_t MIN_BUCKETS = 16;
// Keys per bucket above which the buckets double.
constexpr std::size_t GROW_LOAD = 2;
// Buckets per key above which they halve.
constexpr std::size_t SHRINK_LOAD = 8;

class HashIndex final : public Index {
  public:
    HashIndex(std::size_t keypos, Insertion insertion)
        : keypos_(keypos), insertion_(insertion), buckets_(MIN_BUCKETS) {}

    const Objects *find(const Key &key) const override {
        const auto *entry = locate(key);
        return entry != nullptr && !entry->erased ? &entry->objects : nullptr;
    }

    void insert(const Key &key, DetachedTerm object) override {
        auto *entry = locate(key);
        if (entry == nullptr) {
            add_entry(key, std::move(object));
        } else if (entry->erased) {
            revive(*entry, std::move(object));
        } else {
            store(*entry, std::move(object));
        }
    }

    void replace(const Key &key, DetachedTerm object) override {
        auto &front = locate(key)->objects.front();
        removed(front);
        front = std::move(object);
        added(front);
    }

    bool erase(const Key &key) override {
        auto *entry = locate(key);
        if (entry == nullptr || entry->erased) {
            return false;
        }
        remove_entry(bucket(key.hash), *entry);
        return true;
    }

    void erase_object(const Key &key, Word object) override {
        auto *entry = locate(key);
        if (entry == nullptr || entry->erased) {
            return;
        }
        const auto matches = std::ranges::count_if(
            entry->objects, [object](const DetachedTerm &stored) { return same_object(stored.word(), object); });
        if (static_cast<std::size_t>(matches) == entry->objects.size()) {
            remove_entry(bucket(key.hash), *entry);
            return;
        }
        const auto kept =
            std::remove_if(entry->objects.begin(), entry->objects.end(), [this, object](const DetachedTerm &stored) {
                const auto same = same_object(stored.word(), object);
                if (same) {
                    removed(stored);
                }
                return same;
            });
        entry->objects.erase(kept, entry->objects.end());
    }

    void clear() override {
        for (auto &bucket : buckets_) {
            for (auto &entry : bucket) {
                if (!entry.erased) {
                    remove_objects(entry);
                }
            }
        }
        if (!fixed_) {
            buckets_ = std::vector<Bucket>(MIN_BUCKETS);
            keys_ = 0;
        }
    }

    const Objects *first() const override { return first_from(0); }

    const Objects *last() const override { return first(); }

    Step next(const Key &key) const override {
        const auto &holder = bucket(key.hash);
        const auto *entry = locate(key);
        if (entry == nullptr) {
            return {.missing = true};
        }
        for (auto at = static_cast<std::size_t>(entry - holder.data()) + 1; at < holder.size(); ++at) {
            if (!holder[at].erased) {
                return {.objects = &holder[at].objects};
            }
        }
        return {.objects = first_from(index_of(key.hash) + 1)};
    }

    Step prev(const Key &key) const override { return next(key); }

    void each(const std::function<void(const Objects &)> &visit) const override {
        for (const auto &bucket : buckets_) {
            for (const auto &entry : bucket) {
                if (!entry.erased) {
                    visit(entry.objects);
                }
            }
        }
    }

    std::size_t slots() const override { return buckets_.size(); }

    void slot(std::size_t index, std::vector<Word> &objects) const override {
        for (const auto &entry : buckets_[index]) {
            if (!entry.erased) {
                std::ranges::transform(entry.objects, std::back_inserter(objects), &DetachedTerm::word);
            }
        }
    }

    std::size_t edit(const Key *only, const EditDecision &decide) override {
        std::size_t changed = 0;
        if (only != nullptr) {
            if (auto *entry = locate(*only); entry != nullptr && !entry->erased) {
                auto &holder = bucket(only->hash);
                changed = edit_entry(holder, static_cast<std::size_t>(entry - holder.data()), decide);
            }
        } else {
            for (auto &holder : buckets_) {
                changed += edit_bucket(holder, decide);
            }
        }
        resize();
        return changed;
    }

    void fix() override { fixed_ = true; }

    void unfix() override {
        fixed_ = false;
        if (kept_ != 0) {
            for (auto &bucket : buckets_) {
                const auto live = std::remove_if(bucket.begin(), bucket.end(), [this](const Entry &entry) {
                    if (entry.erased) {
                        dropped(entry.objects.front().words());
                    }
                    return entry.erased;
                });
                bucket.erase(live, bucket.end());
            }
            keys_ -= kept_;
            kept_ = 0;
        }
        resize();
    }

    std::size_t kept() const noexcept override { return kept_; }

    bool shed(std::size_t &budget) override {
        while (!buckets_.empty() && budget > 0) {
            auto &bucket = buckets_.back();
            while (!bucket.empty() && budget > 0) {
                budget -= std::min(budget, bucket.back().objects.size());
                bucket.pop_back();
            }
            if (bucket.empty()) {
                buckets_.pop_back();
            }
        }
        return buckets_.empty();
    }

  private:
    // The objects of one key, or a removed key kept while fixed, holding one object for its key.
    struct Entry {
        std::uint64_t hash;
        Objects objects;
        bool erased = false;
    };

    using Bucket = boost::container::small_vector<Entry, 1>;

    std::size_t index_of(std::uint64_t hash) const noexcept {
        return static_cast<std::size_t>(hash & (buckets_.size() - 1));
    }

    Bucket &bucket(std::uint64_t hash) noexcept { return buckets_[index_of(hash)]; }

    const Bucket &bucket(std::uint64_t hash) const noexcept { return buckets_[index_of(hash)]; }

    // The entry of a key, live or kept.
    const Entry *locate(const Key &key) const noexcept {
        for (const auto &entry : bucket(key.hash)) {
            if (entry.hash == key.hash && same_key(key_of(entry.objects.front().word(), keypos_), key.word)) {
                return &entry;
            }
        }
        return nullptr;
    }

    Entry *locate(const Key &key) noexcept { return const_cast<Entry *>(std::as_const(*this).locate(key)); }

    // The objects of the first live entry in a bucket at or after `from`.
    const Objects *first_from(std::size_t from) const noexcept {
        for (auto index = from; index < buckets_.size(); ++index) {
            for (const auto &entry : buckets_[index]) {
                if (!entry.erased) {
                    return &entry.objects;
                }
            }
        }
        return nullptr;
    }

    void add_entry(const Key &key, DetachedTerm object) {
        auto &entry = bucket(key.hash).emplace_back(Entry{.hash = key.hash, .objects = {}, .erased = false});
        entry.objects.push_back(std::move(object));
        added(entry.objects.front());
        ++keys_;
        resize();
    }

    // A kept key inserted again: its tombstone object gives way to the new one.
    void revive(Entry &entry, DetachedTerm object) {
        dropped(entry.objects.front().words());
        entry.objects.front() = std::move(object);
        entry.erased = false;
        --kept_;
        added(entry.objects.front());
    }

    // Another object of a live key, as the table type stores it.
    void store(Entry &entry, DetachedTerm object) {
        if (insertion_ == Insertion::replace) {
            removed(entry.objects.front());
            entry.objects.front() = std::move(object);
            added(entry.objects.front());
            return;
        }
        if (insertion_ == Insertion::unique &&
            std::ranges::any_of(entry.objects, [&object](const DetachedTerm &stored) {
                return same_object(stored.word(), object.word());
            })) {
            return;
        }
        entry.objects.push_back(std::move(object));
        added(entry.objects.back());
    }

    // Remove a live entry, or keep it as a tombstone while fixed.
    void remove_entry(Bucket &holder, Entry &entry) {
        remove_objects(entry);
        if (!fixed_) {
            holder.erase(holder.begin() + (&entry - holder.data()));
            --keys_;
            resize();
        }
    }

    // Count a live entry's objects out; while fixed it stays as a tombstone holding its first object for its key.
    void remove_objects(Entry &entry) {
        for (const auto &object : entry.objects) {
            removed(object);
        }
        if (fixed_) {
            entry.objects.erase(entry.objects.begin() + 1, entry.objects.end());
            held(entry.objects.front().words());
            entry.erased = true;
            ++kept_;
        }
    }

    // Edit the live entries of one bucket; an entry that goes leaves its place to the next one (select edits, plan 21
    // step LIB11).
    std::size_t edit_bucket(Bucket &holder, const EditDecision &decide) {
        std::size_t changed = 0;
        for (std::size_t at = 0; at < holder.size();) {
            const auto before = holder.size();
            if (!holder[at].erased) {
                changed += edit_entry(holder, at, decide);
            }
            at += holder.size() == before ? 1 : 0;
        }
        return changed;
    }

    // Edit the objects of the entry at `at`: kept and replacing objects stay in order; with none left the entry goes,
    // or stays as a tombstone of its first erased object while fixed.
    std::size_t edit_entry(Bucket &holder, std::size_t at, const EditDecision &decide) {
        auto &entry = holder[at];
        std::size_t changed = 0;
        Objects kept;
        std::optional<DetachedTerm> erased;
        for (auto &object : entry.objects) {
            auto edit = decide(object.word());
            if (edit.action == Edit::Action::keep) {
                kept.push_back(std::move(object));
                continue;
            }
            ++changed;
            removed(object);
            if (edit.action == Edit::Action::replace) {
                added(edit.replacement);
                kept.push_back(std::move(edit.replacement));
            } else if (!erased) {
                erased = std::move(object);
            }
        }
        if (!kept.empty()) {
            entry.objects = std::move(kept);
        } else if (fixed_) {
            entry.objects.clear();
            entry.objects.push_back(std::move(*erased));
            held(entry.objects.front().words());
            entry.erased = true;
            ++kept_;
        } else {
            holder.erase(holder.begin() + static_cast<std::ptrdiff_t>(at));
            --keys_;
        }
        return changed;
    }

    // Double or halve the buckets when the keys outgrow or underuse them; never while fixed.
    void resize() {
        if (fixed_) {
            return;
        }
        auto count = buckets_.size();
        if (keys_ > count * GROW_LOAD) {
            count *= 2;
        } else if (count > MIN_BUCKETS && keys_ * SHRINK_LOAD < count) {
            count /= 2;
        } else {
            return;
        }
        std::vector<Bucket> moved(count);
        for (auto &bucket : buckets_) {
            for (auto &entry : bucket) {
                moved[static_cast<std::size_t>(entry.hash & (count - 1))].push_back(std::move(entry));
            }
        }
        buckets_ = std::move(moved);
    }

    std::size_t keypos_;
    Insertion insertion_;
    std::vector<Bucket> buckets_;
    // Entries, live or kept, and the kept ones.
    std::size_t keys_ = 0;
    std::size_t kept_ = 0;
    bool fixed_ = false;
};
} // namespace

std::unique_ptr<Index> hash_index(std::size_t keypos, Insertion insertion) {
    return std::make_unique<HashIndex>(keypos, insertion);
}
} // namespace clause::runtime::detail::ets
