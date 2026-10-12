#pragma once
#include "map_tree.hpp"
#include <clause/runtime/terms.hpp>

namespace clause::runtime::detail {
using MapEntry = std::pair<Term, Term>;
using MapEntries = std::vector<MapEntry>;

struct MapUpdate {
    // Preserve source update order and distinguish replacement-only entries from associations.
    Term key;
    Term value;
    bool exact;
};

struct MapFault {
    // Keep the offending key/map owned until generated error handling transfers its root.
    TermError error;
    Term payload;
};

using MapResult = std::expected<Term, MapFault>;

struct MapPosition {
    // A lower-bound slot also records equality, avoiding a second potentially expensive key comparison.
    std::size_t index;
    bool found;
};

// Find the exact-order slot of key by binary search; shared by staged updates and published lookup.
TermResult<MapPosition> map_position(std::span<const MapEntry> entries, const Term &key);
// Read one canonical pair after the owning map's shape and lifetime have been proved.
TermResult<MapEntry> map_entry(const Term &map, std::size_t index);
// The off-heap stub of an admitted map object, or null for a classic map cell (OPT0.5A).
const layout::TreeMapCell *tree_of(const HeapObject &object) noexcept;

struct ProbeTarget {
    // The map whose heap holds the stored keys, and the admitted key they are ordered against.
    const Term &map;
    const Term &key;
};

// Orders the stored keys of one map's tree against an admitted probe key, in exact order (OPT0.5A).
class TermProbe final : public MapProbe {
  public:
    explicit TermProbe(ProbeTarget target) : map_(target.map), key_(target.key) {}

    TermResult<int> order(Word stored) const override;

  private:
    // The map whose heap holds the stored keys, and the probe key.
    const Term &map_;
    const Term &key_;
};

struct MapAccess {
    // Validate and normalize all associations before publishing a single immutable map.
    static TermResult<Term> make(ProcessHeap &heap, std::span<const MapEntry> entries);
    // Apply updates to a temporary canonical table; failure publishes no partial map.
    static MapResult update(ProcessHeap &heap, const Term &map, std::span<const MapUpdate> updates);
    // Publish only an already validated canonical table, using stable owner-indexed backing; above
    // MAP_TREE_THRESHOLD entries as an off-heap tree (OPT0.5A).
    static TermResult<Term> publish(ProcessHeap &heap, std::span<const MapEntry> entries);
    // Apply updates to an off-heap map by path copying its tree; the source map stays unchanged (OPT0.5A).
    static MapResult update_tree(ProcessHeap &heap, const Term &map, const layout::TreeMapCell &stub,
                                 std::span<const MapUpdate> updates);
    // Publish a stub that takes over one reference to root, a tree of size entries; failure releases the tree.
    static TermResult<Term> publish_stub(ProcessHeap &heap, MapNode *root, std::size_t size);
};
} // namespace clause::runtime::detail
