#pragma once
#include "../memory/heap_object.hpp"
#include "process_heap.hpp"
#include "term_layout.hpp"
#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <utility>

// Off-heap maps (OPT0.5A): a map above MAP_TREE_THRESHOLD entries is a heap stub (layout::TreeMapCell) naming the
// root of a process-private persistent B+ tree in exact key order. Leaves hold the key and value words, which point
// into the owner's heap; the collector rewrites them through the stub (docs/runtime-heap.md#off-heap-maps).
namespace clause::runtime::detail {
class HeapStorage;
struct HeapArea;

// Entries up to which a map stays a classic heap cell (OTP's MAP_SMALL_MAP_LIMIT).
inline constexpr std::size_t MAP_TREE_THRESHOLD = 32;
// Entries per leaf and children per branch.
inline constexpr std::size_t MAP_FANOUT = 32;

struct MapNode {
    // Stubs and branches referencing this node; it is freed with its last reference.
    std::size_t references_ = 1;
    // The update that created it: that update alone may change it in place while it is unshared.
    std::size_t edit_ = 0;
    // Collection that last rewrote its term words, so a node shared by several stubs is visited once.
    std::size_t epoch_ = 0;
    // Entries below this node, and slots used: entries of a leaf or children of a branch.
    std::size_t size_ = 0;
    std::size_t count_ = 0;
    // Leaves hold entries; branches hold children.
    bool leaf_ = true;
};

struct MapLeaf final : MapNode {
    // Key and value words of count_ entries in exact key order.
    std::array<Word, MAP_FANOUT> keys_{};
    std::array<Word, MAP_FANOUT> values_{};
};

struct MapBranch final : MapNode {
    // The first key below each child, for the search, and the children in key order.
    std::array<Word, MAP_FANOUT> keys_{};
    std::array<MapNode *, MAP_FANOUT> children_{};
};

using MapWords = std::pair<Word, Word>;

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

struct InsertFlags {
    // Whether the update consumed its source stub, so the nodes it holds exclusively may change in place (OPT0.5B).
    bool owned;
    // Set when the key was new.
    bool &added;
};

// A new edit token: nodes created under it may change in place until their tree is published.
std::size_t map_edit(HeapStorage &storage) noexcept;
// Build a tree of sorted, distinct entries under a fresh edit; null for none.
std::expected<MapNode *, HeapError> map_tree_build(HeapStorage &storage, std::span<const MapWords> entries) noexcept;
// Insert or replace one association under `edit`, replacing `root` (one owned reference) by the new root; nodes
// shared with other trees are copied first. On failure `root` is unchanged.
std::expected<void, TreeFailure> map_tree_insert(HeapStorage &storage, MapNode *&root, std::size_t edit,
                                                 MapWords entry, bool exact, const MapProbe &probe,
                                                 InsertFlags flags) noexcept;
// The value of the probed key, if present.
TermResult<std::optional<Word>> map_tree_find(const MapNode *root, const MapProbe &probe) noexcept;
// The entry at canonical position `index` < size.
MapWords map_tree_at(const MapNode *root, std::size_t index) noexcept;

// Every entry in key order.
template <typename Visit> void map_tree_entries(const MapNode *node, Visit &&visit) {
    if (node == nullptr) {
        return; // A consumed stub's (OPT0.5B).
    }
    if (node->leaf_) {
        const auto &leaf = static_cast<const MapLeaf &>(*node);
        for (std::size_t i = 0; i < leaf.count_; ++i) {
            visit(MapWords{leaf.keys_.at(i), leaf.values_.at(i)});
        }
        return;
    }
    const auto &branch = static_cast<const MapBranch &>(*node);
    for (std::size_t i = 0; i < branch.count_; ++i) {
        map_tree_entries(branch.children_.at(i), visit);
    }
}

// Visit every term word of the tree once per `epoch` (a collection), so the collector can rewrite it.
template <typename Visit> void map_tree_words(MapNode *node, std::size_t epoch, Visit &&visit) {
    if (node == nullptr || node->epoch_ == epoch) {
        return;
    }
    node->epoch_ = epoch;
    if (node->leaf_) {
        auto &leaf = static_cast<MapLeaf &>(*node);
        for (std::size_t i = 0; i < leaf.count_; ++i) {
            visit(leaf.keys_.at(i));
            visit(leaf.values_.at(i));
        }
        return;
    }
    auto &branch = static_cast<MapBranch &>(*node);
    for (std::size_t i = 0; i < branch.count_; ++i) {
        visit(branch.keys_.at(i));
        map_tree_words(branch.children_.at(i), epoch, visit);
    }
}

// Add a reference to a node, as a new stub or branch does.
void map_tree_retain(MapNode *node) noexcept;
// Drop one reference; the last frees the node, its words return to the storage and its children are released.
void map_tree_release(HeapStorage &storage, MapNode *node) noexcept;

// Link a just-published stub as the newest entry of the owner's off-heap map list.
void link_tree_map(HeapStorage &storage, layout::TreeMapCell &cell) noexcept;
// After a copy into copies, relink the copies of forwarded stubs in list order and release dead stubs' trees.
void sweep_tree_maps(HeapStorage &storage, HeapArea &copies) noexcept;
// Release every listed stub's tree at teardown.
void release_tree_maps(HeapStorage &storage) noexcept;
} // namespace clause::runtime::detail
