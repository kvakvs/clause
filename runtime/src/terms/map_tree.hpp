#pragma once
#include "process_heap.hpp"
#include "term_layout.hpp"
#include <array>
#include <bit>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <utility>

// Map trees (on-heap map trees): a map above MAP_TREE_THRESHOLD entries is a tree_map stub naming the root of a
// persistent B+ tree in exact key order whose nodes are map_node cells of the owner's heap, so collections move,
// promote and free them like any cell (docs/runtime-heap.md#map-trees).
namespace clause::runtime::detail {
class HeapStorage;

// Entries up to which a map stays a classic heap cell (OTP's MAP_SMALL_MAP_LIMIT).
inline constexpr std::size_t MAP_TREE_THRESHOLD = 32;
// Entries per leaf and children per branch: a path copy writes about 35 words per level.
inline constexpr std::size_t MAP_FANOUT = 16;
// Levels a tree can have: every node but the root holds at least half of MAP_FANOUT, more levels than memory holds.
inline constexpr std::size_t MAP_MAX_DEPTH = 24;

using MapWords = std::pair<Word, Word>;

// Read access to one map_node cell: header, entries below, edit word (token and leaf bit, layout::map_edit_word),
// then `count` keys and `count` values (a leaf) or child pointers (a branch), in exact key order.
class MapNode final {
  public:
    MapNode() noexcept = default;

    explicit MapNode(const Word *words) noexcept : words_(words) {}

    // The node a tree word (a boxed pointer to a map_node cell) names.
    static MapNode at(Word pointer) noexcept {
        return MapNode(std::bit_cast<const Word *>(pointer & ~static_cast<Word>(abi::v1::primary_mask)));
    }

    std::size_t count() const noexcept { return (layout::BoxHeader::count(words_[0]) - 2) / 2; }

    std::size_t size() const noexcept { return static_cast<std::size_t>(words_[1]); }

    bool leaf() const noexcept { return (words_[2] & 1) != 0; }

    std::size_t edit() const noexcept { return static_cast<std::size_t>(words_[2] >> 1); }

    std::span<const Word> keys() const noexcept { return {words_ + 3, count()}; }

    // A leaf's values, or a branch's child pointers.
    std::span<const Word> items() const noexcept { return {words_ + 3 + count(), count()}; }

    MapNode child(std::size_t index) const noexcept { return at(items()[index]); }

    const Word *words() const noexcept { return words_; }

  private:
    const Word *words_ = nullptr;
};

// Whether a stub's root word names a tree: a consumed stub's (OPT0.5B) holds [] instead.
constexpr bool map_tree_present(Word root) noexcept {
    return (root & static_cast<Word>(abi::v1::primary_mask)) == abi::v1::boxed_tag;
}

// Orders stored keys against one probe key: negative, zero or positive as the stored key sorts before, equal to
// or after it, or the comparison failure.
class MapProbe {
  public:
    virtual ~MapProbe() = default;
    MapProbe() = default;
    MapProbe(const MapProbe &) = delete;
    MapProbe &operator=(const MapProbe &) = delete;
    MapProbe(MapProbe &&) = delete;
    MapProbe &operator=(MapProbe &&) = delete;

    virtual TermResult<int> order(Word stored) const = 0;
};

// Why an insertion failed: a comparison failed, the key of an exact update is missing, or no memory was left.
enum class TreeFault : std::uint8_t { compare, missing_key, out_of_memory, limit_exceeded };

struct TreeFailure {
    TreeFault fault;
    // The comparison's error when fault is compare.
    TermError error = TermError::invalid_encoding;
};

struct TreeEdit {
    // The storage new nodes are allocated in, and the edit token whose nodes may change in place: nodes created by
    // this update, or by the consumed source of an owned update (OPT0.5B); 0 allows none.
    HeapStorage &storage;
    std::size_t edit;
};

// A new edit token: nodes created under it may change in place until the next collection.
std::size_t map_edit(HeapStorage &storage) noexcept;
// Whether a stub's token still allows its nodes to change in place: no collection moved them since it was handed out.
bool map_edit_current(const HeapStorage &storage, std::size_t edit) noexcept;
// The root word of a new tree of sorted, distinct entries (more than MAP_TREE_THRESHOLD) created under `edit`.
std::expected<Word, HeapError> map_tree_build(const TreeEdit &edit, std::span<const MapWords> entries) noexcept;
// Insert or replace one association, replacing `root` by the new tree's root word; the source tree stays unchanged
// apart from nodes of `edit.edit`. True when the key was new; on failure `root` is unchanged.
std::expected<bool, TreeFailure> map_tree_insert(const TreeEdit &edit, Word &root, MapWords entry, bool exact,
                                                 const MapProbe &probe) noexcept;
// map_tree_insert at a small-integer key ordered by words alone; a compare failure when another key kind is met
// (word-level map updates, OPT3.15).
std::expected<bool, TreeFailure> map_tree_insert_small(const TreeEdit &edit, Word &root, MapWords entry,
                                                       bool exact) noexcept;
// The value of the probed key, if present.
TermResult<std::optional<Word>> map_tree_find(Word root, const MapProbe &probe) noexcept;
// The entry at canonical position `index` < size of the tree under `root`.
MapWords map_tree_at(MapNode root, std::size_t index) noexcept;

// Every entry in key order.
template <typename Visit> void map_tree_entries(Word root, Visit &&visit) {
    if (!map_tree_present(root)) {
        return;
    }
    const auto node = MapNode::at(root);
    for (std::size_t i = 0; i < node.count(); ++i) {
        if (node.leaf()) {
            visit(MapWords{node.keys()[i], node.items()[i]});
        } else {
            map_tree_entries(node.items()[i], visit);
        }
    }
}
} // namespace clause::runtime::detail
