#pragma once
#include "../memory/detached_term.hpp"
#include <boost/container/small_vector.hpp>
#include <clause/runtime/terms.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// The object indexes of ETS tables (plan 21 step LIB10A, docs/runtime.md#ets-tables): a hash index for set, bag and
// duplicate_bag tables and a tree for ordered_set ones. An index holds detached copies of objects; its owner, a table
// part, locks it.
namespace clause::runtime::detail::ets {
// The objects of one key in insertion order; a set or ordered_set key holds one.
using Objects = boost::container::small_vector<DetachedTerm, 1>;

// A key looked for: its word, readable by any process, and its hash (hash indexes only).
struct Key {
    Word word;
    std::uint64_t hash = 0;
};

// How a hash index stores an object whose key it holds: replacing the object (set), adding it unless an exactly equal
// object is there (bag) or adding it anyway (duplicate_bag).
enum class Insertion : std::uint8_t { replace, unique, duplicate };

// Where a walk step from a key lands: the objects of the next key (null at the end of the index), or a key the index
// does not hold.
struct Step {
    const Objects *objects = nullptr;
    bool missing = false;
};

// The key word of an object stored at 1-based position `keypos`.
Word key_of(Word object, std::size_t keypos) noexcept;
// Whether two keys are one key of a hash table: exactly equal (=:=).
bool same_key(Word left, Word right) noexcept;
// Whether two objects are exactly equal (bag duplicates, delete_object/2).
bool same_object(Word left, Word right) noexcept;
// The term order of two keys of an ordered_set (==): negative, zero or positive.
int key_order(Word left, Word right) noexcept;

class Index {
  public:
    virtual ~Index() = default;
    Index() = default;
    Index(const Index &) = delete;
    Index &operator=(const Index &) = delete;

    // The live objects of a key, if any.
    virtual const Objects *find(const Key &key) const = 0;
    // Store an object under its key.
    virtual void insert(const Key &key, DetachedTerm object) = 0;
    // Replace the one object of a live key of a set or ordered_set.
    virtual void replace(const Key &key, DetachedTerm object) = 0;
    // Remove a key's objects; whether it held any.
    virtual bool erase(const Key &key) = 0;
    // Remove the objects of a key exactly equal to `object`.
    virtual void erase_object(const Key &key, Word object) = 0;
    // Remove every object.
    virtual void clear() = 0;

    // The objects of the first and last keys; null when empty. A hash index's last key is its first.
    virtual const Objects *first() const = 0;
    virtual const Objects *last() const = 0;
    // The objects of the key after or before `key`; a hash index walks backwards as forwards.
    virtual Step next(const Key &key) const = 0;
    virtual Step prev(const Key &key) const = 0;
    // Visit the objects of every live key in walk order.
    virtual void each(const std::function<void(const Objects &)> &visit) const = 0;

    // The slots of slot/2: hash buckets, or one object each in a tree.
    virtual std::size_t slots() const = 0;
    // Append the objects of one slot.
    virtual void slot(std::size_t index, std::vector<Word> &objects) const = 0;

    // The first fixation began or the last one ended: while fixed, a hash index neither moves keys between buckets
    // nor forgets removed keys, so a walk visits every key once (safe_fixtable/2).
    virtual void fix() = 0;
    virtual void unfix() = 0;

    // Removed keys kept for walks while fixed.
    virtual std::size_t kept() const noexcept { return 0; }

    // Live objects and the words of every held copy.
    std::size_t size() const noexcept { return objects_; }

    std::size_t words() const noexcept { return words_; }

  protected:
    // Count objects that join or leave the index.
    void added(const DetachedTerm &object) noexcept {
        ++objects_;
        words_ += object.words();
    }

    void removed(const DetachedTerm &object) noexcept {
        --objects_;
        words_ -= object.words();
    }

    // Count held words that are no live object (a removed key kept while fixed).
    void held(std::size_t words) noexcept { words_ += words; }

    void dropped(std::size_t words) noexcept { words_ -= words; }

  private:
    std::size_t objects_ = 0;
    std::size_t words_ = 0;
};

// A hash index of objects keyed at `keypos`, storing as `insertion` says.
std::unique_ptr<Index> hash_index(std::size_t keypos, Insertion insertion);
// A tree of objects keyed at `keypos` in term order, one object per key.
std::unique_ptr<Index> tree_index(std::size_t keypos);
} // namespace clause::runtime::detail::ets
