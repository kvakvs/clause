#include "persistent_terms.hpp"
#include <mutex>

namespace clause::runtime::detail {
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

TermResult<Word> PersistentTerms::append(ProcessContext &writer, const Term &value) {
    if (!area_) {
        auto made = MessageBuffer::area(writer);
        if (!made) {
            return std::unexpected(made.error());
        }
        area_ = std::move(*made);
    }
    const auto before = area_->words();
    const auto copied = area_->append(value);
    if (!copied) {
        return copied;
    }
    taken_ = area_->words() - before;
    // Blocks keep their order, so only the newest ones need registering; their words are published only after.
    const auto blocks = area_->blocks();
    for (; registered_ < blocks.size(); ++registered_) {
        literals_.add(blocks[registered_]);
    }
    return copied;
}

TermResult<void> PersistentTerms::store(ProcessContext &writer, std::uint64_t hash, const Term &key, const Term &value,
                                        const SameKey &same) {
    const std::unique_lock lock(mutex_);
    auto [item, end] = entries_.equal_range(hash);
    for (; item != end && !same(item->second.entry.key); ++item) {
    }
    const bool fresh = item == end;
    if (fresh) {
        const auto stored_key = append(writer, key);
        if (!stored_key) {
            return std::unexpected(stored_key.error());
        }
        item = entries_.emplace(hash, Stored{.entry = {*stored_key, 0}, .key_words = taken_, .value_words = 0});
    }
    const auto stored_value = append(writer, value);
    if (!stored_value) {
        // A new key is stored only with its value.
        if (fresh) {
            entries_.erase(item);
        }
        return std::unexpected(stored_value.error());
    }
    item->second.entry.value = *stored_value;
    item->second.value_words = taken_;
    return {};
}

bool PersistentTerms::erase(std::uint64_t hash, const SameKey &same) {
    const std::unique_lock lock(mutex_);
    auto [item, end] = entries_.equal_range(hash);
    for (; item != end; ++item) {
        if (same(item->second.entry.key)) {
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
        total += stored.key_words + stored.value_words;
    }
    return total;
}
} // namespace clause::runtime::detail
