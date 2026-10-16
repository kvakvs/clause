#pragma once
#include "map_tree.hpp"
#include "term_layout.hpp"
#include <array>
#include <optional>
#include <span>

// Map cursors: word-level iteration over a map's entries in exact key order, a flat cell or an off-heap tree alike,
// with no Term per entry and no copy of the entries (docs/terms.md#maps).
namespace clause::runtime::detail {
class MapCursor final {
  public:
    // A cursor over the map whose object words, header first, are `words` (nonempty: the empty map is an immediate),
    // at entry `first` (none past the last).
    explicit MapCursor(std::span<const Word> words, std::size_t first = 0) noexcept;

    // The number of entries of the map.
    std::size_t size() const noexcept { return size_; }

    // The key and value words of the next entry; none after the last.
    std::optional<MapWords> next() noexcept;

  private:
    // Enter the subtree at `node` at its entry `skip`, recording each branch passed on the path.
    void descend(const MapNode *node, std::size_t skip) noexcept;
    // Move to the first entry of the next leaf; false after the last leaf.
    bool advance() noexcept;

    // A tree deeper than this would hold more entries than memory: every node but the root is at least half full.
    static constexpr std::size_t MAX_DEPTH = 16;

    // A flat map's words (header first), or empty for a tree.
    std::span<const Word> flat_;
    std::size_t size_ = 0;
    // The next entry of the flat map, or of the current leaf.
    std::size_t position_ = 0;
    // The current leaf of a tree and the branches above it with the child taken in each.
    const MapLeaf *leaf_ = nullptr;
    std::array<std::pair<const MapBranch *, std::size_t>, MAX_DEPTH> path_{};
    std::size_t depth_ = 0;
};
} // namespace clause::runtime::detail
