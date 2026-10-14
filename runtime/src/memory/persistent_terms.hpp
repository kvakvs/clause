#pragma once
#include "message_buffer.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

// The runtime-wide persistent term table (plan step 41M1, docs/runtime.md#persistent-terms): keys and values copied
// into message buffers outside every process, found by a hash of the key and exact equality. Readers copy a value
// out of its buffer into their own heap; a replaced or erased buffer is freed when the last reader holding it is
// done, so no process ever refers into the table.
namespace clause::runtime::detail {
class PersistentTerms final {
  public:
    using Buffer = std::shared_ptr<const MessageBuffer>;

    // One stored association.
    struct Entry {
        // The key and value, each copied out of the process that stored them.
        Buffer key;
        Buffer value;
    };

    // Whether a stored key is the key looked for: exact equality, decided by the caller who holds a process.
    using SameKey = std::function<bool(const MessageBuffer &)>;

    // The value stored under the key hashed to `hash` that `same` accepts, if any.
    std::optional<Buffer> find(std::uint64_t hash, const SameKey &same) const;
    // Store `entry` under `hash`, replacing the entry `same` accepts; the replaced value, if any.
    std::optional<Buffer> store(std::uint64_t hash, Entry entry, const SameKey &same);
    // Remove the entry `same` accepts; whether there was one.
    bool erase(std::uint64_t hash, const SameKey &same);
    // Every entry, in no particular order.
    std::vector<Entry> entries() const;
    // The number of entries and the words their buffers hold.
    std::size_t count() const;
    std::size_t words() const;

  private:
    // Readers share the lock; put and erase take it alone.
    mutable std::shared_mutex mutex_;
    // Entries by key hash; keys with equal hashes share a bucket.
    std::unordered_multimap<std::uint64_t, Entry> entries_;
};
} // namespace clause::runtime::detail
