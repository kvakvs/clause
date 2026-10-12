#include "map_tree.hpp"
#include "../memory/heap_storage.hpp"
#include "../memory/heap_walk.hpp"
#include <algorithm>
#include <new>
#include <type_traits>
#include <vector>

namespace clause::runtime::detail {
namespace {
// Words a node is charged: its bytes rounded up to words.
template <typename Node> constexpr std::size_t node_words() { return (sizeof(Node) + sizeof(Word) - 1) / sizeof(Word); }

// Charge new node words to the process and runtime accounts; enough garbage triggers a collection as binaries do.
void charge(HeapStorage &storage, std::size_t words) noexcept {
    storage.memory_->force(words);
    storage.off_heap_words_ += words;
    storage.map_words_ += words;
    if (storage.off_heap_words_ >= storage.binary_limit_words_) {
        storage.signal_collection();
    }
}

// Return a freed node's words.
void refund(HeapStorage &storage, std::size_t words) noexcept {
    storage.memory_->release(words);
    storage.off_heap_words_ -= words;
    storage.map_words_ -= words;
}

// A new empty node created by `edit`, charged to the storage; no exception escapes.
template <typename Node> std::expected<Node *, HeapError> allocate(HeapStorage &storage, std::size_t edit) noexcept {
    constexpr auto words = node_words<Node>();
    if (words > storage.room()) {
        return std::unexpected(HeapError::limit_exceeded);
    }
    auto *node = new (std::nothrow) Node();
    if (node == nullptr) {
        return std::unexpected(HeapError::out_of_memory);
    }
    node->edit_ = edit;
    node->leaf_ = std::is_same_v<Node, MapLeaf>;
    charge(storage, words);
    return node;
}

// Free one node whose last reference is gone, without touching its children.
void free_node(HeapStorage &storage, MapNode *node) noexcept {
    if (node->leaf_) {
        delete static_cast<MapLeaf *>(node);
        refund(storage, node_words<MapLeaf>());
    } else {
        delete static_cast<MapBranch *>(node);
        refund(storage, node_words<MapBranch>());
    }
}

// The first key stored below a node.
Word first_key(const MapNode &node) noexcept {
    return node.leaf_ ? static_cast<const MapLeaf &>(node).keys_.front()
                      : static_cast<const MapBranch &>(node).keys_.front();
}

// A tree failure for an allocation error.
TreeFailure memory_failure(HeapError error) noexcept {
    return {error == HeapError::out_of_memory ? TreeFault::out_of_memory : TreeFault::limit_exceeded};
}

struct Slot {
    // Lower bound of the probe among count keys, and whether the key there equals it.
    std::size_t index;
    bool found;
};

// Binary search for the probe's lower bound among the first count keys.
TermResult<Slot> search(std::span<const Word> keys, const MapProbe &probe) noexcept {
    std::size_t first = 0;
    std::size_t last = keys.size();
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        const auto order = probe.order(keys[middle]);
        if (!order) {
            return std::unexpected(order.error());
        }
        if (*order == 0) {
            return Slot{middle, true};
        }
        if (*order < 0) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    return Slot{first, false};
}

// The child of a branch whose key range holds the probe: the last child whose first key is not after it.
TermResult<std::size_t> child_index(const MapBranch &branch, const MapProbe &probe) noexcept {
    return search(std::span(branch.keys_).first(branch.count_), probe).transform([](Slot slot) {
        return slot.found || slot.index == 0 ? slot.index : slot.index - 1;
    });
}

// The node itself when this edit created it and nothing else shares it, else a copy owned by the edit whose
// children gain a reference.
template <typename Node>
std::expected<Node *, HeapError> writable(HeapStorage &storage, Node &node, std::size_t edit) noexcept {
    if (node.edit_ == edit && node.references_ == 1) {
        return &node;
    }
    auto copy = allocate<Node>(storage, edit);
    if (!copy) {
        return copy;
    }
    **copy = node;
    (*copy)->references_ = 1;
    (*copy)->edit_ = edit;
    if constexpr (std::is_same_v<Node, MapBranch>) {
        std::ranges::for_each(std::span((*copy)->children_).first(node.count_), map_tree_retain);
    }
    return copy;
}

// Open a gap at index among the first count of items, moving the rest one place right; count < MAP_FANOUT.
template <typename Item> void open_gap(std::array<Item, MAP_FANOUT> &items, std::size_t count, std::size_t index) {
    const auto moved = std::span(items).subspan(index, count + 1 - index);
    std::shift_right(moved.begin(), moved.end(), 1);
}

struct Insertion {
    // The storage, the edit allowed to change nodes in place, the entry and its probe; `added` reports a new key.
    HeapStorage &storage;
    std::size_t edit;
    MapWords entry;
    bool exact;
    const MapProbe &probe;
    bool added = false;
};

struct Grown {
    // The node replacing the one inserted into, and a new right sibling when it split (else null).
    MapNode *node;
    MapNode *split;
};

using Grow = std::expected<Grown, TreeFailure>;

// Drop a node made for a failed insertion; the original node is left alone.
void discard(HeapStorage &storage, MapNode *made, const MapNode &original) noexcept {
    if (made != &original) {
        map_tree_release(storage, made);
    }
}

// Move the upper half of a full leaf, with the new entry at index, into a new right leaf.
Grow split_leaf(Insertion &insertion, MapLeaf &left, std::size_t index) noexcept {
    auto right = allocate<MapLeaf>(insertion.storage, insertion.edit);
    if (!right) {
        return std::unexpected(memory_failure(right.error()));
    }
    std::vector<MapWords> all;
    try {
        all.reserve(MAP_FANOUT + 1);
    } catch (const std::bad_alloc &) {
        map_tree_release(insertion.storage, *right);
        return std::unexpected(TreeFailure{TreeFault::out_of_memory});
    }
    for (std::size_t i = 0; i < left.count_; ++i) {
        all.emplace_back(left.keys_.at(i), left.values_.at(i));
    }
    all.insert(all.begin() + static_cast<std::ptrdiff_t>(index), insertion.entry);
    const auto half = all.size() / 2 + all.size() % 2;
    for (std::size_t i = 0; i < all.size(); ++i) {
        auto &leaf = i < half ? left : **right;
        const auto at = i < half ? i : i - half;
        leaf.keys_.at(at) = all[i].first;
        leaf.values_.at(at) = all[i].second;
    }
    left.count_ = left.size_ = half;
    (*right)->count_ = (*right)->size_ = all.size() - half;
    return Grown{&left, *right};
}

// Insert into a leaf: replace a present key, add an absent one unless the update is exact, split when full.
Grow insert_leaf(Insertion &insertion, MapLeaf &leaf) noexcept {
    const auto slot = search(std::span(leaf.keys_).first(leaf.count_), insertion.probe);
    if (!slot) {
        return std::unexpected(TreeFailure{TreeFault::compare, slot.error()});
    }
    if (!slot->found && insertion.exact) {
        return std::unexpected(TreeFailure{TreeFault::missing_key});
    }
    auto target = writable(insertion.storage, leaf, insertion.edit);
    if (!target) {
        return std::unexpected(memory_failure(target.error()));
    }
    auto &out = **target;
    if (slot->found) {
        out.values_.at(slot->index) = insertion.entry.second;
        return Grown{&out, nullptr};
    }
    insertion.added = true;
    if (out.count_ == MAP_FANOUT) {
        auto grown = split_leaf(insertion, out, slot->index);
        if (!grown) {
            discard(insertion.storage, &out, leaf);
        }
        return grown;
    }
    open_gap(out.keys_, out.count_, slot->index);
    open_gap(out.values_, out.count_, slot->index);
    out.keys_.at(slot->index) = insertion.entry.first;
    out.values_.at(slot->index) = insertion.entry.second;
    ++out.count_;
    ++out.size_;
    return Grown{&out, nullptr};
}

Grow insert(Insertion &insertion, MapNode &node) noexcept;

// Move the upper half of a full branch, with the new child at index, into a new right branch.
Grow split_branch(Insertion &insertion, MapBranch &left, std::size_t index, MapNode *child) noexcept {
    auto right = allocate<MapBranch>(insertion.storage, insertion.edit);
    if (!right) {
        return std::unexpected(memory_failure(right.error()));
    }
    std::array<MapNode *, MAP_FANOUT + 1> all{};
    std::ranges::copy(std::span(left.children_).first(left.count_), all.begin());
    std::shift_right(all.begin() + static_cast<std::ptrdiff_t>(index), all.end(), 1);
    all.at(index) = child;
    constexpr auto half = (MAP_FANOUT + 1) / 2 + (MAP_FANOUT + 1) % 2;
    left.count_ = left.size_ = (*right)->size_ = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        auto &branch = i < half ? left : **right;
        branch.children_.at(branch.count_) = all.at(i);
        branch.keys_.at(branch.count_++) = first_key(*all.at(i));
        branch.size_ += all.at(i)->size_;
    }
    return Grown{&left, *right};
}

// Put a child's replacement and its new right sibling into a writable branch at the child's place.
Grow adopt(Insertion &insertion, MapBranch &out, std::size_t index, const Grown &grown) noexcept {
    if (grown.node != out.children_.at(index)) {
        map_tree_release(insertion.storage, out.children_.at(index));
        out.children_.at(index) = grown.node;
    }
    out.keys_.at(index) = first_key(*grown.node);
    out.size_ += insertion.added ? 1 : 0;
    if (grown.split == nullptr) {
        return Grown{&out, nullptr};
    }
    if (out.count_ == MAP_FANOUT) {
        return split_branch(insertion, out, index + 1, grown.split);
    }
    open_gap(out.children_, out.count_, index + 1);
    open_gap(out.keys_, out.count_, index + 1);
    out.children_.at(index + 1) = grown.split;
    out.keys_.at(index + 1) = first_key(*grown.split);
    ++out.count_;
    return Grown{&out, nullptr};
}

// Insert below a branch, then replace the child it went into, copying the branch first when it is shared.
Grow insert_branch(Insertion &insertion, MapBranch &branch) noexcept {
    const auto index = child_index(branch, insertion.probe);
    if (!index) {
        return std::unexpected(TreeFailure{TreeFault::compare, index.error()});
    }
    auto *child = branch.children_.at(*index);
    auto grown = insert(insertion, *child);
    if (!grown) {
        return grown;
    }
    auto target = writable(insertion.storage, branch, insertion.edit);
    if (!target) {
        discard(insertion.storage, grown->node, *child);
        map_tree_release(insertion.storage, grown->split);
        return std::unexpected(memory_failure(target.error()));
    }
    auto adopted = adopt(insertion, **target, *index, *grown);
    if (!adopted) {
        map_tree_release(insertion.storage, grown->split);
        discard(insertion.storage, *target, branch);
    }
    return adopted;
}

Grow insert(Insertion &insertion, MapNode &node) noexcept {
    return node.leaf_ ? insert_leaf(insertion, static_cast<MapLeaf &>(node))
                      : insert_branch(insertion, static_cast<MapBranch &>(node));
}

// A branch over the given children, created by edit.
std::expected<MapBranch *, HeapError> branch_of(HeapStorage &storage, std::size_t edit,
                                                std::span<MapNode *const> children) noexcept {
    auto branch = allocate<MapBranch>(storage, edit);
    if (!branch) {
        return branch;
    }
    for (auto *child : children) {
        (*branch)->children_.at((*branch)->count_) = child;
        (*branch)->keys_.at((*branch)->count_++) = first_key(*child);
        (*branch)->size_ += child->size_;
    }
    return branch;
}

// A leaf holding the given entries, created by edit.
std::expected<MapLeaf *, HeapError> leaf_of(HeapStorage &storage, std::size_t edit,
                                            std::span<const MapWords> entries) noexcept {
    auto leaf = allocate<MapLeaf>(storage, edit);
    if (!leaf) {
        return leaf;
    }
    for (const auto &[key, value] : entries) {
        (*leaf)->keys_.at((*leaf)->count_) = key;
        (*leaf)->values_.at((*leaf)->count_++) = value;
    }
    (*leaf)->size_ = (*leaf)->count_;
    return leaf;
}

// Release every node of a partly built level.
void release_all(HeapStorage &storage, std::span<MapNode *const> nodes) noexcept {
    for (auto *node : nodes) {
        map_tree_release(storage, node);
    }
}

// Group one level of nodes into the branches of the level above.
std::expected<std::vector<MapNode *>, HeapError> parents(HeapStorage &storage, std::size_t edit,
                                                         std::span<MapNode *const> level) {
    std::vector<MapNode *> above;
    above.reserve((level.size() + MAP_FANOUT - 1) / MAP_FANOUT);
    for (std::size_t first = 0; first < level.size(); first += MAP_FANOUT) {
        const auto children = level.subspan(first, std::min(MAP_FANOUT, level.size() - first));
        auto branch = branch_of(storage, edit, children);
        if (!branch) {
            release_all(storage, above);
            release_all(storage, level.subspan(first));
            return std::unexpected(branch.error());
        }
        above.push_back(*branch);
    }
    return above;
}

// Build the leaves of sorted entries, then their branches level by level.
std::expected<MapNode *, HeapError> build(HeapStorage &storage, std::span<const MapWords> entries) {
    const auto edit = map_edit(storage);
    std::vector<MapNode *> level;
    level.reserve((entries.size() + MAP_FANOUT - 1) / MAP_FANOUT);
    for (std::size_t first = 0; first < entries.size(); first += MAP_FANOUT) {
        auto leaf = leaf_of(storage, edit, entries.subspan(first, std::min(MAP_FANOUT, entries.size() - first)));
        if (!leaf) {
            release_all(storage, level);
            return std::unexpected(leaf.error());
        }
        level.push_back(*leaf);
    }
    while (level.size() > 1) {
        auto above = parents(storage, edit, level);
        if (!above) {
            return std::unexpected(above.error());
        }
        level = std::move(*above);
    }
    return level.empty() ? nullptr : level.front();
}
} // namespace

std::size_t map_edit(HeapStorage &storage) noexcept { return ++storage.map_edits_; }

std::expected<MapNode *, HeapError> map_tree_build(HeapStorage &storage, std::span<const MapWords> entries) noexcept {
    try {
        return build(storage, entries);
    } catch (const std::bad_alloc &) {
        return std::unexpected(HeapError::out_of_memory);
    }
}

std::expected<void, TreeFailure> map_tree_insert(HeapStorage &storage, MapNode *&root, std::size_t edit, MapWords entry,
                                                 bool exact, const MapProbe &probe, bool &added) noexcept {
    Insertion insertion{storage, edit, entry, exact, probe};
    auto grown = insert(insertion, *root);
    if (!grown) {
        return std::unexpected(grown.error());
    }
    auto *top = grown->node;
    if (grown->split != nullptr) {
        const std::array<MapNode *, 2> children{grown->node, grown->split};
        auto branch = branch_of(storage, edit, children);
        if (!branch) {
            map_tree_release(storage, grown->split);
            discard(storage, grown->node, *root);
            return std::unexpected(memory_failure(branch.error()));
        }
        top = *branch;
    }
    if (top != root && grown->node != root) {
        map_tree_release(storage, root);
    }
    root = top;
    added = insertion.added;
    return {};
}

TermResult<std::optional<Word>> map_tree_find(const MapNode *root, const MapProbe &probe) noexcept {
    const auto *node = root;
    while (!node->leaf_) {
        const auto &branch = static_cast<const MapBranch &>(*node);
        const auto index = child_index(branch, probe);
        if (!index) {
            return std::unexpected(index.error());
        }
        node = branch.children_.at(*index);
    }
    const auto &leaf = static_cast<const MapLeaf &>(*node);
    return search(std::span(leaf.keys_).first(leaf.count_), probe).transform([&](Slot slot) -> std::optional<Word> {
        return slot.found ? std::optional<Word>(leaf.values_.at(slot.index)) : std::nullopt;
    });
}

MapWords map_tree_at(const MapNode *root, std::size_t index) noexcept {
    const auto *node = root;
    while (!node->leaf_) {
        const auto &branch = static_cast<const MapBranch &>(*node);
        std::size_t child = 0;
        for (; index >= branch.children_.at(child)->size_; ++child) {
            index -= branch.children_.at(child)->size_;
        }
        node = branch.children_.at(child);
    }
    const auto &leaf = static_cast<const MapLeaf &>(*node);
    return {leaf.keys_.at(index), leaf.values_.at(index)};
}

void map_tree_retain(MapNode *node) noexcept { ++node->references_; }

void map_tree_release(HeapStorage &storage, MapNode *node) noexcept {
    if (node == nullptr || --node->references_ != 0) {
        return;
    }
    if (!node->leaf_) {
        const auto &branch = static_cast<const MapBranch &>(*node);
        release_all(storage, std::span(branch.children_).first(branch.count_));
    }
    free_node(storage, node);
}

void link_tree_map(HeapStorage &storage, layout::TreeMapCell &cell) noexcept {
    cell.next_ = storage.tree_maps_;
    storage.tree_maps_ = &cell;
}

void sweep_tree_maps(HeapStorage &storage, HeapArea &copies) noexcept {
    layout::TreeMapCell *newest = nullptr;
    auto **link = &newest;
    for (auto *cell = std::exchange(storage.tree_maps_, nullptr); cell != nullptr;) {
        auto *next = cell->next_;
        const auto first = cell->header_.value_;
        if (is_header(first)) {
            map_tree_release(storage, cell->root_);
        } else {
            // A forwarded stub's first word is the boxed pointer to its copy, which kept the root reference.
            const auto copy =
                copies.from(static_cast<std::uintptr_t>(first & ~static_cast<Word>(abi::v1::primary_mask)));
            *link = reinterpret_cast<layout::TreeMapCell *>(copy.data());
            link = &(*link)->next_;
        }
        cell = next;
    }
    *link = nullptr;
    storage.tree_maps_ = newest;
}

void release_tree_maps(HeapStorage &storage) noexcept {
    for (auto *cell = std::exchange(storage.tree_maps_, nullptr); cell != nullptr; cell = cell->next_) {
        map_tree_release(storage, cell->root_);
    }
}
} // namespace clause::runtime::detail
