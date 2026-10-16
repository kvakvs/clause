#include "../memory/heap_storage.hpp"
#include "maps.hpp"
#include "structural_order.hpp"
#include "term_layout.hpp"
#include "terms.hpp"
#include <algorithm>
#include <boost/container/small_vector.hpp>
#include <exception>
#include <new>
#include <stdexcept>

namespace clause::runtime::detail {
namespace {
// Most entries a map header can count: key and value words; on 32-bit targets 2^24 - 1, on 64-bit beyond memory.
constexpr std::size_t MAX_MAP_SIZE = layout::BoxHeader::MAX_COUNT / 2;

// A failed key comparison inside a sort, carried out of the comparator.
struct OrderFailure final : std::exception {
    explicit OrderFailure(TermError failure) noexcept : error(failure) {}

    TermError error;
};

// Exact key order of two entries; a failed comparison throws OrderFailure.
int key_order(const MapEntry &left, const MapEntry &right) {
    const auto order = structural_order(left.first, right.first, true);
    if (!order) {
        throw OrderFailure(order.error());
    }
    return *order;
}

// Sort entries by exact key with O(n log n) comparisons; for equal keys the first key stays with the last value,
// as successive associations would leave it.
void canonical(MapEntries &entries) {
    // Strictly ascending keys, as from a generator over sorted input, need neither sorting nor deduplication.
    const auto unordered = [](const MapEntry &a, const MapEntry &b) { return key_order(a, b) >= 0; };
    if (std::ranges::adjacent_find(entries, unordered) == entries.end()) {
        return;
    }
    std::ranges::stable_sort(entries, [](const MapEntry &a, const MapEntry &b) { return key_order(a, b) < 0; });
    std::size_t kept = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (kept != 0 && key_order(entries[kept - 1], entries[i]) == 0) {
            entries[kept - 1].second = std::move(entries[i].second);
            continue;
        }
        if (kept != i) {
            entries[kept] = std::move(entries[i]);
        }
        ++kept;
    }
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(kept), entries.end());
}

// Every child belongs to this heap/runtime before it can enter immutable map storage.
TermResult<void> validate(ProcessHeap &heap, const Term &key, const Term &value) {
    const auto checked_key = heap.retain(key);
    if (!checked_key) {
        return std::unexpected(checked_key.error());
    }
    return heap.retain(value).transform([](const Term &) {});
}

// Construct associations in source order, retaining the last value for each exact key; no size or work cap.
TermResult<Term> make(ProcessHeap &heap, std::span<const MapEntry> values) {
    for (const auto &[key, value] : values) {
        if (const auto checked = validate(heap, key, value); !checked) {
            return std::unexpected(checked.error());
        }
    }
    MapEntries entries(values.begin(), values.end());
    try {
        canonical(entries);
    } catch (const OrderFailure &failure) {
        return std::unexpected(failure.error);
    }
    return MapAccess::publish(heap, entries);
}

// The fault an off-heap map update reports for a failed tree insertion of key (OPT0.5A).
MapFault tree_fault(const TreeFailure &failure, const Term &map, const MapUpdate &update) {
    switch (failure.fault) {
    case TreeFault::compare:
        return {failure.error, update.key};
    case TreeFault::missing_key:
        return {TermError::missing_key, update.key};
    case TreeFault::out_of_memory:
        return {TermError::out_of_memory, map};
    default:
        return {TermError::resource_limit, map};
    }
}

struct TreeEdit {
    // The storage, the tree an update builds (one owned reference), its edit token and whether it consumed its source
    // stub.
    HeapStorage &storage;
    MapNode *&root;
    std::size_t edit;
    bool owned;
};

// Validate and insert one association into the tree under construction; true when the key is new.
std::expected<bool, MapFault> insert_entry(ProcessHeap &heap, const Term &map, const MapUpdate &entry,
                                           const TreeEdit &tree) {
    if (const auto checked = validate(heap, entry.key, entry.value); !checked) {
        return std::unexpected(MapFault{checked.error(), entry.key});
    }
    const TermProbe probe({.map = map, .key = entry.key});
    bool added = false;
    const auto inserted = map_tree_insert(tree.storage, tree.root, tree.edit, {entry.key.word(), entry.value.word()},
                                          entry.exact, probe, {.owned = tree.owned, .added = added});
    if (!inserted) {
        return std::unexpected(tree_fault(inserted.error(), map, entry));
    }
    return added;
}

// After a failed update, drop its tree, or return it to the stub it consumed, which then holds what it holds now.
void give_back(HeapStorage &storage, layout::TreeMapCell &stub, MapNode *root, bool owned) noexcept {
    if (!owned) {
        map_tree_release(storage, root);
        return;
    }
    stub.root_ = root;
    stub.size_ = static_cast<Word>(root->size_);
}

// Term error for a refused heap or off-heap allocation.
TermError heap_fault(HeapError error) {
    return error == HeapError::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}

// The slot of `key` among word entries in exact key order, by binary search with a probe of the map's heap.
TermResult<MapPosition> word_position(std::span<const MapWords> entries, const Term &map, const Term &key) {
    const TermProbe probe({.map = map, .key = key});
    std::size_t first = 0;
    std::size_t last = entries.size();
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        const auto order = probe.order(entries[middle].first);
        if (!order) {
            return std::unexpected(order.error());
        }
        if (*order == 0) {
            return MapPosition{middle, true};
        }
        if (*order < 0) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    return MapPosition{first, false};
}

// Word entries of a classic map, with room for a few new keys without allocating (OPT0.5).
using WordEntries = boost::container::small_vector<MapWords, 40>;

// Insert or replace one association at its exact-order slot among word entries; an exact update needs the key.
std::expected<void, MapFault> associate_word(WordEntries &entries, const Term &map, const MapUpdate &update) {
    const auto position = word_position({entries.data(), entries.size()}, map, update.key);
    if (!position) {
        return std::unexpected(MapFault{position.error(), update.key});
    }
    if (position->found) {
        entries[position->index].second = update.value.word();
    } else if (update.exact) {
        return std::unexpected(MapFault{TermError::missing_key, update.key});
    } else {
        entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(position->index),
                       MapWords{update.key.word(), update.value.word()});
    }
    return {};
}

// Apply updates to a classic map's key and value words, never staging terms (OPT0.5); the heap does not move until
// the result is published, so the words stay valid.
MapResult update_words(ProcessHeap &heap, const Term &map, std::span<const Word> cell,
                       std::span<const MapUpdate> updates) {
    WordEntries entries;
    for (std::size_t slot = 1; slot + 1 < cell.size(); slot += 2) {
        entries.emplace_back(cell[slot], cell[slot + 1]);
    }
    for (const auto &entry : updates) {
        if (const auto checked = validate(heap, entry.key, entry.value); !checked) {
            return std::unexpected(MapFault{checked.error(), entry.key});
        }
        if (const auto added = associate_word(entries, map, entry); !added) {
            return std::unexpected(added.error());
        }
    }
    return MapAccess::publish_words(heap, {entries.data(), entries.size()}).transform_error([&](TermError error) {
        return MapFault{error, map};
    });
}

// Keep the source map immutable while exact updates see preceding associations from the same expression, unless the
// compiler proved it unread afterwards (owned, OPT0.5B).
MapResult update(ProcessHeap &heap, const Term &map, std::span<const MapUpdate> updates, bool owned) {
    const auto retained = heap.retain(map);
    if (!retained) {
        return std::unexpected(MapFault{retained.error(), map});
    }
    const auto object = TermAccess::object(map);
    if (!object) {
        return std::unexpected(MapFault{object.error(), map});
    }
    if (const auto *tree = tree_of(*object)) {
        // The stub is this heap's own cell (retained above), so an owned update may consume it.
        return MapAccess::update_tree(heap, map, const_cast<layout::TreeMapCell &>(*tree), updates, owned);
    }
    if (layout::BoxHeader::kind(object->words[0]) != BoxedKind::map) {
        return std::unexpected(MapFault{TermError::wrong_type, map});
    }
    return update_words(heap, map, object->words, updates);
}
} // namespace

TermResult<Term> MapAccess::publish(ProcessHeap &heap, std::span<const MapEntry> entries) {
    if (entries.size() > MAP_TREE_THRESHOLD) {
        std::vector<MapWords> words;
        words.reserve(entries.size());
        for (const auto &[key, value] : entries) {
            words.emplace_back(key.word(), value.word());
        }
        const auto root = map_tree_build(*heap.storage_, words);
        if (!root) {
            return std::unexpected(heap_fault(root.error()));
        }
        return publish_stub(heap, *root, entries.size());
    }
    if (entries.size() > MAX_MAP_SIZE) {
        return std::unexpected(TermError::resource_limit);
    }
    const auto count = 1 + entries.size() * 2;
    auto reserved = heap.reserve(count);
    if (!reserved) {
        return std::unexpected(reserved.error() == HeapError::out_of_memory ? TermError::out_of_memory
                                                                            : TermError::resource_limit);
    }
    auto *words = ::new (reserved->bytes().data()) Word[count]{};
    words[0] = layout::BoxHeader::make(BoxedKind::map, count - 1);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        words[1 + 2 * i] = entries[i].first.word();
        words[2 + 2 * i] = entries[i].second.word();
    }
    return detail::publish(heap.storage_, *reserved,
                           reinterpret_cast<Word>(words) | static_cast<Word>(TermKindPrimary::boxed));
}

TermResult<Term> MapAccess::entries(ProcessHeap &heap, const Term &map) {
    const auto object = TermAccess::object(map);
    if (!object || object->kind != TermKind::map) {
        return std::unexpected(object ? TermError::wrong_type : object.error());
    }
    const auto *tree = tree_of(*object);
    if (!tree) {
        return map;
    }
    const auto count = 1 + 2 * object->count;
    auto reserved = heap.reserve(count);
    if (!reserved) {
        return std::unexpected(heap_fault(reserved.error()));
    }
    auto *words = ::new (reserved->bytes().data()) Word[count]{};
    words[0] = layout::BoxHeader::make(BoxedKind::tuple, count - 1);
    auto *next = words + 1;
    map_tree_entries(tree->root_, [&](const MapWords &entry) {
        *next++ = entry.first;
        *next++ = entry.second;
    });
    return detail::publish(heap.storage_, *reserved,
                           reinterpret_cast<Word>(words) | static_cast<Word>(TermKindPrimary::boxed));
}

TermResult<Term> MapAccess::publish_words(ProcessHeap &heap, std::span<const MapWords> entries) {
    if (entries.size() > MAP_TREE_THRESHOLD) {
        const auto root = map_tree_build(*heap.storage_, entries);
        if (!root) {
            return std::unexpected(heap_fault(root.error()));
        }
        return publish_stub(heap, *root, entries.size());
    }
    const auto count = 1 + entries.size() * 2;
    auto reserved = heap.reserve(count);
    if (!reserved) {
        return std::unexpected(heap_fault(reserved.error()));
    }
    auto *words = ::new (reserved->bytes().data()) Word[count]{};
    words[0] = layout::BoxHeader::make(BoxedKind::map, count - 1);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        words[1 + 2 * i] = entries[i].first;
        words[2 + 2 * i] = entries[i].second;
    }
    return detail::publish(heap.storage_, *reserved,
                           reinterpret_cast<Word>(words) | static_cast<Word>(TermKindPrimary::boxed));
}

namespace {
// Orders stored keys against a small-integer key by their words alone; any other stored key is undecided, which ends
// a word-level update (word-level map updates).
class SmallKeyProbe final : public MapProbe {
  public:
    explicit SmallKeyProbe(Word key) : key_(key) {}

    TermResult<int> order(Word stored) const override {
        if (const auto order = small_order(stored, key_)) {
            return *order;
        }
        return std::unexpected(TermError::invalid_encoding);
    }

  private:
    Word key_;
};
} // namespace

std::optional<Word> MapAccess::update_small_key(ProcessHeap &heap, Word map, MapWords entry, bool exact) {
    auto &storage = *heap.storage_;
    const auto words = TermAccess::boxed_words(heap, map);
    const auto address = static_cast<std::uintptr_t>(map & ~static_cast<Word>(abi::v1::primary_mask));
    if (words.empty() || layout::BoxHeader::kind(words[0]) != BoxedKind::tree_map || storage.owned(address).empty()) {
        return std::nullopt;
    }
    const auto &stub = *reinterpret_cast<const layout::TreeMapCell *>(words.data());
    auto *root = stub.root_;
    if (root == nullptr) {
        return std::nullopt;
    }
    map_tree_retain(root);
    bool added = false;
    const SmallKeyProbe probe(entry.first);
    if (!map_tree_insert(storage, root, map_edit(storage), entry, exact, probe, {.owned = false, .added = added})) {
        map_tree_release(storage, root);
        return std::nullopt;
    }
    const auto published = publish_stub(heap, root, static_cast<std::size_t>(stub.size_) + (added ? 1 : 0));
    return published ? std::optional{published->word()} : std::nullopt;
}

TermResult<Term> MapAccess::publish_stub(ProcessHeap &heap, MapNode *root, std::size_t size) {
    constexpr auto total = sizeof(layout::TreeMapCell) / sizeof(Word);
    auto reserved = heap.reserve(total);
    if (!reserved) {
        map_tree_release(*heap.storage_, root);
        return std::unexpected(heap_fault(reserved.error()));
    }
    auto *cell = std::construct_at(reinterpret_cast<layout::TreeMapCell *>(reserved->bytes().data()));
    cell->header_ = {layout::BoxHeader::make(BoxedKind::tree_map, total - 1)};
    cell->size_ = static_cast<Word>(size);
    cell->root_ = root;
    cell->next_ = nullptr;
    auto published = detail::publish(heap.storage_, *reserved,
                                     reinterpret_cast<Word>(cell) | static_cast<Word>(TermKindPrimary::boxed));
    if (published) {
        link_tree_map(*heap.storage_, *cell);
    } else {
        map_tree_release(*heap.storage_, root);
    }
    return published;
}

MapResult MapAccess::update_tree(ProcessHeap &heap, const Term &map, layout::TreeMapCell &stub,
                                 std::span<const MapUpdate> updates, bool owned) {
    auto &storage = *heap.storage_;
    auto *root = stub.root_;
    auto size = static_cast<std::size_t>(stub.size_);
    // The update holds its own reference: a new one, or the consumed stub's (OPT0.5B). Nodes it creates, and an owned
    // update's exclusively held nodes, change in place until the new stub is published.
    owned = owned && root != nullptr;
    if (owned) {
        stub.root_ = nullptr;
        stub.size_ = 0;
    } else if (root != nullptr) {
        map_tree_retain(root);
    } else {
        return std::unexpected(MapFault{TermError::invalid_encoding, map});
    }
    const auto edit = map_edit(storage);
    for (const auto &entry : updates) {
        auto inserted =
            insert_entry(heap, map, entry, {.storage = storage, .root = root, .edit = edit, .owned = owned});
        if (!inserted) {
            give_back(storage, stub, root, owned);
            return std::unexpected(inserted.error());
        }
        size += *inserted ? 1 : 0;
    }
    return publish_stub(heap, root, size).transform_error([&](TermError error) { return MapFault{error, map}; });
}

TermResult<Term> MapAccess::make(ProcessHeap &heap, std::span<const MapEntry> entries) {
    try {
        return detail::make(heap, entries);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    } catch (const std::length_error &) {
        return std::unexpected(TermError::resource_limit);
    }
}

MapResult MapAccess::update(ProcessHeap &heap, const Term &map, std::span<const MapUpdate> updates, bool owned) {
    try {
        return detail::update(heap, map, updates, owned);
    } catch (const std::bad_alloc &) {
        return std::unexpected(MapFault{TermError::out_of_memory, map});
    } catch (const std::length_error &) {
        return std::unexpected(MapFault{TermError::resource_limit, map});
    }
}
} // namespace clause::runtime::detail

namespace clause::runtime {
TermResult<Term> TermFactory::map(std::span<const std::pair<Term, Term>> entries) {
    return heap().and_then([&](ProcessHeap *owner) { return detail::MapAccess::make(*owner, entries); });
}
} // namespace clause::runtime
