#include "persistent_terms.hpp"
#include <mutex>

namespace clause::runtime::detail {
std::optional<PersistentTerms::Buffer> PersistentTerms::find(std::uint64_t hash, const SameKey &same) const {
    const std::shared_lock lock(mutex_);
    auto [item, end] = entries_.equal_range(hash);
    for (; item != end; ++item) {
        if (same(*item->second.key)) {
            return item->second.value;
        }
    }
    return std::nullopt;
}

std::optional<PersistentTerms::Buffer> PersistentTerms::store(std::uint64_t hash, Entry entry, const SameKey &same) {
    const std::unique_lock lock(mutex_);
    auto [item, end] = entries_.equal_range(hash);
    for (; item != end; ++item) {
        if (same(*item->second.key)) {
            return std::exchange(item->second.value, std::move(entry.value));
        }
    }
    entries_.emplace(hash, std::move(entry));
    return std::nullopt;
}

bool PersistentTerms::erase(std::uint64_t hash, const SameKey &same) {
    const std::unique_lock lock(mutex_);
    auto [item, end] = entries_.equal_range(hash);
    for (; item != end; ++item) {
        if (same(*item->second.key)) {
            entries_.erase(item);
            return true;
        }
    }
    return false;
}

std::vector<PersistentTerms::Entry> PersistentTerms::entries() const {
    const std::shared_lock lock(mutex_);
    std::vector<Entry> result;
    result.reserve(entries_.size());
    for (const auto &[hash, entry] : entries_) {
        result.push_back(entry);
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
    for (const auto &[hash, entry] : entries_) {
        total += entry.key->words() + entry.value->words();
    }
    return total;
}
} // namespace clause::runtime::detail
