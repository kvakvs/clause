#include "../memory/heap_object.hpp"
#include "maps.hpp"
#include "structural_order.hpp"
#include <new>
#include <stdexcept>

namespace clause::runtime::detail {
TermResult<MapPosition> map_position(std::span<const MapEntry> entries, const Term &key) {
    std::size_t first = 0;
    std::size_t last = entries.size();
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        const auto order = structural_order(entries[middle].first, key, true);
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

const layout::TreeMapCell *tree_of(const HeapObject &object) noexcept {
    return layout::BoxHeader::kind(object.words[0]) == BoxedKind::tree_map
               ? reinterpret_cast<const layout::TreeMapCell *>(object.words.data())
               : nullptr;
}

TermResult<int> TermProbe::order(Word stored) const {
    const auto key = TermAccess::child(map_, stored);
    if (!key) {
        return std::unexpected(TermError::invalid_encoding);
    }
    return structural_order(*key, key_, true);
}

TermResult<MapEntry> map_entry(const Term &map, std::size_t index) {
    const auto size = map.map_size();
    if (!size) {
        return std::unexpected(size.error());
    }
    if (index >= *size) {
        return std::unexpected(TermError::out_of_range);
    }
    const auto object = TermAccess::object(map).value();
    const auto *tree = tree_of(object);
    const auto words =
        tree ? map_tree_at(tree->root_, index) : MapWords{object.words[1 + 2 * index], object.words[2 + 2 * index]};
    const auto key = TermAccess::child(map, words.first);
    const auto value = TermAccess::child(map, words.second);
    if (!key || !value) {
        return std::unexpected(TermError::invalid_encoding);
    }
    return MapEntry{*key, *value};
}

namespace {
// Search an off-heap map's tree (OPT0.5A).
TermResult<std::optional<Term>> find_in_tree(const Term &map, const Term &key, const layout::TreeMapCell &tree) {
    const TermProbe probe({.map = map, .key = key});
    const auto found = map_tree_find(tree.root_, probe);
    if (!found) {
        return std::unexpected(found.error());
    }
    if (!*found) {
        return std::nullopt;
    }
    return TermAccess::child(map, **found).transform([](const Term &value) { return std::optional<Term>(value); });
}

// Search published storage without materializing an entry vector or coercing key types.
TermResult<std::optional<Term>> find(const Term &map, const Term &key, std::size_t size) {
    const auto admitted = TermAccess::child(map, key.word());
    if (!admitted) {
        return std::unexpected(admitted.error());
    }
    const auto object = TermAccess::object(map).value();
    if (const auto *tree = tree_of(object)) {
        return find_in_tree(map, *admitted, *tree);
    }
    // Probe the stored key words; only the found value becomes a term (OPT0.5).
    const TermProbe probe({.map = map, .key = *admitted});
    std::size_t first = 0;
    std::size_t last = size;
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        const auto order = probe.order(object.words[1 + 2 * middle]);
        if (!order) {
            return std::unexpected(order.error());
        }
        if (*order == 0) {
            return TermAccess::child(map, object.words[2 + 2 * middle]).transform([](const Term &value) {
                return std::optional<Term>(value);
            });
        }
        if (*order < 0) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    return std::nullopt;
}
} // namespace
} // namespace clause::runtime::detail

namespace clause::runtime {
bool Term::is_map() const { return kind() == TermKind::map; }

TermResult<std::size_t> Term::map_size() const {
    const auto object = detail::TermAccess::object(*this);
    if (!object) {
        return std::unexpected(object.error());
    }
    if (object->kind != TermKind::map) {
        return std::unexpected(TermError::wrong_type);
    }
    return object->count;
}

TermResult<std::optional<Term>> Term::map_find(const Term &key) const {
    return map_size().and_then([&](std::size_t size) { return detail::find(*this, key, size); });
}

TermResult<bool> Term::map_contains(const Term &key) const {
    return map_find(key).transform([](const auto &value) { return value.has_value(); });
}

TermResult<std::vector<std::pair<Term, Term>>> Term::map_entries() const {
    const auto size = map_size();
    if (!size) {
        return std::unexpected(size.error());
    }
    try {
        detail::MapEntries entries;
        entries.reserve(*size);
        if (const auto *tree = detail::tree_of(detail::TermAccess::object(*this).value())) {
            // An in-order walk instead of one descent per entry (OPT0.5A).
            detail::map_tree_entries(tree->root_, [&](detail::MapWords words) {
                entries.emplace_back(detail::TermAccess::child(*this, words.first).value(),
                                     detail::TermAccess::child(*this, words.second).value());
            });
            return entries;
        }
        for (std::size_t i = 0; i < *size; ++i) {
            entries.push_back(detail::map_entry(*this, i).value());
        }
        return entries;
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    } catch (const std::length_error &) {
        return std::unexpected(TermError::resource_limit);
    }
}
} // namespace clause::runtime
