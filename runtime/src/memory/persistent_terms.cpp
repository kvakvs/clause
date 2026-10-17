#include "persistent_terms.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <mutex>

namespace clause::runtime::detail {
PersistentTerms::~PersistentTerms() {
    for (auto &[hash, stored] : entries_) {
        free_persistent(region_, stored.key);
        free_persistent(region_, stored.value);
    }
    for (auto &[epoch, retired] : retired_) {
        free(retired);
    }
}

std::optional<Word> PersistentTerms::find(std::uint64_t hash, const SameKey &same) const {
    const std::shared_lock lock(mutex_);
    auto [item, end] = entries_.equal_range(hash);
    for (; item != end; ++item) {
        if (same(item->second.entry.key)) {
            return item->second.entry.value;
        }
    }
    return std::nullopt;
}

TermResult<void> PersistentTerms::store(ProcessContext &writer, std::uint64_t hash, const Term &key, const Term &value,
                                        const SameKey &same) {
    // Copy outside the lock: readers of other keys need not wait for a large value.
    auto key_copy = copy_persistent(writer, region_, key);
    if (!key_copy) {
        return std::unexpected(key_copy.error());
    }
    auto value_copy = copy_persistent(writer, region_, value);
    if (!value_copy) {
        free_persistent(region_, *key_copy);
        return std::unexpected(value_copy.error());
    }
    try {
        const std::unique_lock lock(mutex_);
        auto [item, end] = entries_.equal_range(hash);
        for (; item != end && !same(item->second.entry.key); ++item) {
        }
        if (item == end) {
            // Copied, not moved, into the entry: a failed insertion leaves the copies with their owners here.
            const Entry entry{key_copy->root, value_copy->root};
            entries_.emplace(hash, Stored{.entry = entry, .key = *key_copy, .value = *value_copy});
            return {};
        }
        // Everything that can fail comes before the entry changes; the stored key stays.
        std::vector<PersistentCopy> replaced;
        replaced.reserve(1);
        const std::array<const PersistentCopy *, 1> leaving{&item->second.value};
        auto *retired = prepare(leaving);
        free_persistent(region_, *key_copy);
        item->second.entry.value = value_copy->root;
        replaced.push_back(std::exchange(item->second.value, std::move(*value_copy)));
        commit(retired, std::move(replaced));
        return {};
    } catch (const std::bad_alloc &) {
        // Nothing was published.
        free_persistent(region_, *key_copy);
        free_persistent(region_, *value_copy);
        return std::unexpected(TermError::out_of_memory);
    } catch (...) {
        // A key comparison failed (a builtin failure from the matcher): nothing was published either.
        free_persistent(region_, *key_copy);
        free_persistent(region_, *value_copy);
        throw;
    }
}

bool PersistentTerms::erase(std::uint64_t hash, const SameKey &same) {
    const std::unique_lock lock(mutex_);
    auto [item, end] = entries_.equal_range(hash);
    for (; item != end && !same(item->second.entry.key); ++item) {
    }
    if (item == end) {
        return false;
    }
    std::vector<PersistentCopy> removed;
    removed.reserve(2);
    const std::array<const PersistentCopy *, 2> leaving{&item->second.key, &item->second.value};
    auto *retired = prepare(leaving);
    removed.push_back(std::move(item->second.key));
    removed.push_back(std::move(item->second.value));
    entries_.erase(item);
    commit(retired, std::move(removed));
    return true;
}

PersistentTerms::Retired *PersistentTerms::prepare(std::span<const PersistentCopy *const> copies) {
    const bool areas = std::ranges::any_of(copies, [](const PersistentCopy *copy) { return !copy->area.empty(); });
    if (!areas || seen_.empty()) {
        return nullptr;
    }
    auto &retired = retired_.try_emplace(epoch_.load(std::memory_order_relaxed) + 1).first->second;
    std::size_t marked = 0;
    try {
        for (; marked < copies.size(); ++marked) {
            if (!copies[marked]->area.empty()) {
                region_.retire(copies[marked]->area);
            }
        }
    } catch (...) {
        for (std::size_t index = 0; index < marked; ++index) {
            region_.forget(copies[index]->area);
        }
        retired_.erase(epoch_.load(std::memory_order_relaxed) + 1);
        throw;
    }
    retired.waiting = seen_.size();
    return &retired;
}

void PersistentTerms::commit(Retired *retired, std::vector<PersistentCopy> copies) noexcept {
    if (retired == nullptr) {
        for (const auto &copy : copies) {
            free_persistent(region_, copy);
        }
        return;
    }
    // The areas were marked retiring before the epoch advances, so an evacuation that sees the new epoch copies them.
    retired->copies = std::move(copies);
    epoch_.store(epoch_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    waiting_.store(true, std::memory_order_release);
}

void PersistentTerms::release(Epochs epochs) {
    for (auto at = retired_.upper_bound(epochs.after); at != retired_.end() && at->first <= epochs.upto;) {
        if (--at->second.waiting == 0) {
            free(at->second);
            at = retired_.erase(at);
        } else {
            ++at;
        }
    }
    waiting_.store(!retired_.empty(), std::memory_order_release);
}

void PersistentTerms::free(Retired &retired) noexcept {
    for (const auto &copy : retired.copies) {
        region_.forget(copy.area);
        free_persistent(region_, copy);
    }
    retired.copies.clear();
}

std::vector<PersistentTerms::Entry> PersistentTerms::entries() const {
    const std::shared_lock lock(mutex_);
    std::vector<Entry> result;
    result.reserve(entries_.size());
    for (const auto &[hash, stored] : entries_) {
        result.push_back(stored.entry);
    }
    return result;
}

std::size_t PersistentTerms::count() const {
    const std::shared_lock lock(mutex_);
    return entries_.size();
}

std::size_t PersistentTerms::words() const {
    const std::shared_lock lock(mutex_);
    std::size_t total = 0;
    for (const auto &[hash, stored] : entries_) {
        total += stored.key.area.size() + stored.value.area.size();
    }
    return total;
}

void PersistentTerms::enter(const ProcessContext &process) {
    const std::unique_lock lock(mutex_);
    seen_.insert_or_assign(&process, epoch_.load(std::memory_order_relaxed));
}

void PersistentTerms::leave(const ProcessContext &process) {
    const std::unique_lock lock(mutex_);
    const auto found = seen_.find(&process);
    if (found == seen_.end()) {
        return;
    }
    const auto seen = found->second;
    seen_.erase(found);
    release({.after = seen, .upto = std::numeric_limits<std::uint64_t>::max()});
}

bool PersistentTerms::behind(const ProcessContext &process) const {
    if (!waiting_.load(std::memory_order_acquire)) {
        return false;
    }
    const std::shared_lock lock(mutex_);
    const auto found = seen_.find(&process);
    return found != seen_.end() && found->second < epoch_.load(std::memory_order_relaxed);
}

Evacuation PersistentTerms::evacuation(const ProcessContext &process) const {
    const std::shared_lock lock(mutex_);
    Evacuation result;
    result.epoch = epoch_.load(std::memory_order_relaxed);
    const auto found = seen_.find(&process);
    if (found == seen_.end()) {
        return result;
    }
    // Only retirements after the process's last evacuation count it as waiting, which keeps their areas allocated.
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> ranges;
    for (auto at = retired_.upper_bound(found->second); at != retired_.end(); ++at) {
        for (const auto &copy : at->second.copies) {
            const auto begin = reinterpret_cast<std::uintptr_t>(copy.area.data());
            ranges.emplace_back(begin, begin + copy.area.size_bytes());
            result.binaries.insert(result.binaries.end(), copy.binaries.begin(), copy.binaries.end());
        }
    }
    result.ranges = RetiringRanges(std::move(ranges));
    return result;
}

void PersistentTerms::evacuated(const ProcessContext &process, std::uint64_t epoch) {
    const std::unique_lock lock(mutex_);
    const auto found = seen_.find(&process);
    if (found == seen_.end() || epoch <= found->second) {
        return;
    }
    release({.after = std::exchange(found->second, epoch), .upto = epoch});
}

std::size_t PersistentTerms::retired() const {
    const std::shared_lock lock(mutex_);
    return retired_.size();
}
} // namespace clause::runtime::detail
