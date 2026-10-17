#pragma once
#include "../modules/literal_areas.hpp"
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
// into one shared area outside every process, found by a hash of the key and exact equality. The area's blocks are
// literal areas of the runtime, so a read returns the stored word in place and any process may hold it, as OTP's
// literal persistent terms (persistent terms in place). A replaced or erased value stays in the area until the
// runtime is torn down.
namespace clause::runtime::detail {
class PersistentTerms final {
  public:
    // One stored association, as words of the shared area (or immediates).
    struct Entry {
        Word key;
        Word value;
    };

    // Whether a stored key is the key looked for: exact equality, decided by the caller who holds a process.
    using SameKey = std::function<bool(Word)>;

    // Register the area's blocks with the runtime's literal areas, which outlive the table.
    explicit PersistentTerms(LiteralAreas &literals) : literals_(literals) {}

    // The value stored under the key hashed to `hash` that `same` accepts, if any.
    std::optional<Word> find(std::uint64_t hash, const SameKey &same) const;
    // Copy `key` and `value`, terms of `writer`, into the area and store them under `hash`, replacing the value of the
    // entry `same` accepts.
    TermResult<void> store(ProcessContext &writer, std::uint64_t hash, const Term &key, const Term &value,
                           const SameKey &same);
    // Remove the entry `same` accepts; whether there was one.
    bool erase(std::uint64_t hash, const SameKey &same);
    // Every entry, in no particular order.
    std::vector<Entry> entries() const;
    // The number of entries and the words their copies took.
    std::size_t count() const;
    std::size_t words() const;

  private:
    // A stored association and the area words its key's and current value's copies took.
    struct Stored {
        Entry entry;
        std::size_t key_words;
        std::size_t value_words;
    };

    // Copy one term into the area and register any block it added; its word, and its words in taken_.
    TermResult<Word> append(ProcessContext &writer, const Term &value);

    // Readers share the lock; put and erase take it alone.
    mutable std::shared_mutex mutex_;
    // Entries by key hash; keys with equal hashes share a bucket.
    std::unordered_multimap<std::uint64_t, Stored> entries_;
    // The runtime's literal areas, which the area's blocks join before any of their words is stored.
    LiteralAreas &literals_;
    // The shared area, made by the first store.
    std::shared_ptr<MessageBuffer> area_;
    // The area blocks already registered as literal areas.
    std::size_t registered_ = 0;
    // The area words the last append took.
    std::size_t taken_ = 0;
};
} // namespace clause::runtime::detail
