#include "map_tree.hpp"
#include "../memory/heap_storage.hpp"
#include <algorithm>
#include <new>
#include <vector>

namespace clause::runtime::detail {
namespace {
using layout::BoxHeader;

// Words of a node of `count` entries or children, its header included.
constexpr std::size_t node_words(std::size_t count) noexcept { return 1 + layout::map_node_payload_words(count); }

// Entries or children of the left node when `count` of them split into two.
constexpr std::size_t left_half(std::size_t count) noexcept { return (count + 1) / 2; }

// A tree failure for an allocation error.
TreeFailure memory_failure(HeapError error) noexcept {
    return {error == HeapError::out_of_memory ? TreeFault::out_of_memory : TreeFault::limit_exceeded};
}

// Words for new nodes, not zeroed: each is written before the heap is walked again.
std::expected<std::span<Word>, HeapError> claim(HeapStorage &storage, std::size_t words) noexcept {
    try {
        return storage.claim(words);
    } catch (const std::bad_alloc &) {
        return std::unexpected(HeapError::out_of_memory);
    }
}

struct Piece {
    // A node written for the new tree (its boxed pointer, 0 while counting), its first key and the entries below it;
    // or a leaf entry: its value, key and size 0.
    Word word;
    Word first;
    std::size_t size;
};

struct Pieces {
    // The node replacing a rewritten one, and its new right sibling when it split.
    std::array<Piece, 2> nodes;
    std::size_t count;
};

struct Range {
    // Items [begin, end) of a node: those an update replaces, or those one new node takes.
    std::size_t begin;
    std::size_t end;
};

// A node as an update rewrites it, read in place: its `replaced` items give way to up to two pieces. With no node
// it holds the pieces alone (a new root).
class Edited final {
  public:
    Edited() noexcept = default;

    Edited(MapNode node, Range replaced) noexcept
        : node_(node), old_(node.count()), index_(replaced.begin), removed_(replaced.end - replaced.begin) {}

    void add(const Piece &piece) noexcept { pieces_.at(added_++) = piece; }

    std::size_t count() const noexcept { return old_ - removed_ + added_; }

    Word key(std::size_t at) const noexcept { return from_node(at) ? node_.keys()[old(at)] : piece(at).first; }

    Word item(std::size_t at) const noexcept { return from_node(at) ? node_.items()[old(at)] : piece(at).word; }

    // Write the node, or two halves when it holds more than MAP_FANOUT; `size` counts its entries.
    template <typename Sink> Pieces emit(Sink &sink, bool leaf, std::size_t size) const noexcept {
        const auto all = count();
        if (all <= MAP_FANOUT) {
            return {{sink.node(leaf, size, *this, {0, all})}, 1};
        }
        const auto half = left_half(all);
        const auto left = leaf ? half : below(half);
        return {{sink.node(leaf, left, *this, {0, half}), sink.node(leaf, size - left, *this, {half, all})}, 2};
    }

  private:
    // Whether item `at` is the node's own, and its index there.
    bool from_node(std::size_t at) const noexcept { return at < index_ || at >= index_ + added_; }

    std::size_t old(std::size_t at) const noexcept { return at < index_ ? at : at - added_ + removed_; }

    const Piece &piece(std::size_t at) const noexcept { return pieces_.at(at - index_); }

    // Entries below the first `count` children: a piece's recorded size, else the child's own.
    std::size_t below(std::size_t count) const noexcept {
        std::size_t entries = 0;
        for (std::size_t at = 0; at < count; ++at) {
            entries += from_node(at) ? MapNode::at(item(at)).size() : piece(at).size;
        }
        return entries;
    }

    MapNode node_;
    std::size_t old_ = 0;
    std::size_t index_ = 0;
    std::size_t removed_ = 0;
    std::array<Piece, 2> pieces_{};
    std::size_t added_ = 0;
};

// Counts the words of the nodes an insertion writes, so it allocates once and exactly.
class WordCounter final {
  public:
    Piece node(bool, std::size_t size, const Edited &source, Range range) noexcept {
        words_ += node_words(range.end - range.begin);
        return {0, source.key(range.begin), size};
    }

    // Nothing changes in place while counting.
    std::span<Word> patch(MapNode) noexcept { return {}; }

    std::size_t words() const noexcept { return words_; }

  private:
    std::size_t words_ = 0;
};

// Writes nodes into words allocated for one insertion or build, under one edit token.
class NodeWriter final {
  public:
    NodeWriter(HeapStorage &storage, std::span<Word> words, std::size_t edit) noexcept
        : storage_(storage), words_(words), edit_(edit) {}

    Piece node(bool leaf, std::size_t size, const Edited &source, Range range) noexcept {
        const auto count = range.end - range.begin;
        const auto out = start(count, leaf, size);
        for (std::size_t i = 0; i < count; ++i) {
            out[3 + i] = source.key(range.begin + i);
            out[3 + count + i] = source.item(range.begin + i);
        }
        return {tagged(out), out[3], size};
    }

    Piece node(bool leaf, std::size_t size, std::span<const Word> keys, std::span<const Word> items) noexcept {
        const auto out = start(keys.size(), leaf, size);
        std::ranges::copy(keys, out.begin() + 3);
        std::ranges::copy(items, out.begin() + 3 + static_cast<std::ptrdiff_t>(keys.size()));
        return {tagged(out), keys.front(), size};
    }

    // The words of a node this edit created, to change in place.
    std::span<Word> patch(MapNode node) noexcept {
        const auto words = storage_.owned(reinterpret_cast<std::uintptr_t>(node.words()));
        return words.size() < node_words(node.count()) ? std::span<Word>{} : words.first(node_words(node.count()));
    }

  private:
    // The next node's words for `count` items, with its header, size and edit word written.
    std::span<Word> start(std::size_t count, bool leaf, std::size_t size) noexcept {
        const auto out = words_.subspan(used_, node_words(count));
        used_ += out.size();
        out[0] = BoxHeader::make(BoxedKind::map_node, layout::map_node_payload_words(count));
        out[1] = static_cast<Word>(size);
        out[2] = layout::map_edit_word(edit_, leaf);
        return out;
    }

    static Word tagged(std::span<Word> out) noexcept {
        return reinterpret_cast<Word>(out.data()) | static_cast<Word>(TermKindPrimary::boxed);
    }

    HeapStorage &storage_;
    std::span<Word> words_;
    std::size_t used_ = 0;
    std::size_t edit_;
};

// Whether a node may change in place: the update's edit token created it.
bool writable(MapNode node, std::size_t edit) noexcept { return edit != 0 && node.edit() == edit; }

struct Slot {
    // Lower bound of the probe among the keys, and whether the key there equals it.
    std::size_t index;
    bool found;
};

// Binary search for the probe's lower bound among sorted keys.
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
std::size_t child_of(Slot slot) noexcept { return slot.found || slot.index == 0 ? slot.index : slot.index - 1; }

struct Step {
    // A node on the path and the child (a branch) or entry slot (the leaf) the key leads to.
    MapNode node;
    std::size_t index;
};

struct Path {
    // The nodes from the root to the leaf of the key, and whether the leaf holds it.
    std::array<Step, MAP_MAX_DEPTH> steps;
    std::size_t depth = 0;
    bool found = false;
};

// The path from the root to the leaf where the probed key is or belongs.
std::expected<Path, TreeFailure> descend(Word root, const MapProbe &probe) noexcept {
    Path path;
    auto node = MapNode::at(root);
    while (path.depth < MAP_MAX_DEPTH) {
        const auto slot = search(node.keys(), probe);
        if (!slot) {
            return std::unexpected(TreeFailure{TreeFault::compare, slot.error()});
        }
        if (node.leaf()) {
            path.steps.at(path.depth++) = {node, slot->index};
            path.found = slot->found;
            return path;
        }
        path.steps.at(path.depth++) = {node, child_of(*slot)};
        node = node.child(child_of(*slot));
    }
    return std::unexpected(TreeFailure{TreeFault::limit_exceeded});
}

// Change a branch in place to hold the piece at `index`, one more entry below when the key was new.
void patch_branch(std::span<Word> words, std::size_t index, const Piece &piece, bool added) noexcept {
    if (words.empty()) {
        return;
    }
    const auto count = (words.size() - 3) / 2;
    words[3 + index] = piece.first;
    words[3 + count + index] = piece.word;
    words[1] += added ? 1 : 0;
}

struct Rewrite {
    // One insertion: the path to its leaf, the entry, the edit token whose nodes change in place and the old root.
    const Path &path;
    MapWords entry;
    std::size_t edit;
    Word root;
};

// The pieces replacing the leaf of the path, or none when its value changed in place.
template <typename Sink> std::optional<Pieces> rewrite_leaf(Sink &sink, const Rewrite &rewrite) noexcept {
    const auto &path = rewrite.path;
    const auto &[leaf, slot] = path.steps.at(path.depth - 1);
    if (path.found && writable(leaf, rewrite.edit)) {
        if (const auto words = sink.patch(leaf); !words.empty()) {
            words[3 + leaf.count() + slot] = rewrite.entry.second;
        }
        return std::nullopt;
    }
    Edited edited(leaf, {slot, slot + (path.found ? 1 : 0)});
    edited.add({rewrite.entry.second, rewrite.entry.first, 0});
    return edited.emit(sink, true, leaf.size() + (path.found ? 0 : 1));
}

// The pieces replacing a branch of the path whose child became `below`, or none when nothing above it changes.
template <typename Sink>
std::optional<Pieces> rewrite_branch(Sink &sink, const Rewrite &rewrite, const Step &step,
                                     const Pieces &below) noexcept {
    const bool added = !rewrite.path.found;
    const auto &[node, index] = step;
    if (below.count == 1 && writable(node, rewrite.edit)) {
        patch_branch(sink.patch(node), index, below.nodes[0], added);
        if (!added) {
            return std::nullopt; // The branch's own pointer and size are unchanged above it.
        }
        const auto self = reinterpret_cast<Word>(node.words()) | static_cast<Word>(TermKindPrimary::boxed);
        const auto first = index == 0 ? below.nodes[0].first : node.keys()[0];
        return Pieces{{Piece{self, first, node.size() + 1}}, 1};
    }
    Edited edited(node, {index, index + 1});
    for (std::size_t i = 0; i < below.count; ++i) {
        edited.add(below.nodes.at(i));
    }
    return edited.emit(sink, false, node.size() + (added ? 1 : 0));
}

// The root word over the pieces that replaced the old root: a new root when it split.
template <typename Sink> Word root_of(Sink &sink, const Pieces &pieces) noexcept {
    if (pieces.count == 1) {
        return pieces.nodes[0].word;
    }
    Edited root;
    root.add(pieces.nodes[0]);
    root.add(pieces.nodes[1]);
    return sink.node(false, pieces.nodes[0].size + pieces.nodes[1].size, root, {0, 2}).word;
}

// Rebuild the path bottom-up into `sink`: copy each node the update changes unless its edit created it, and share
// every other node; returns the new root word (meaningless while counting).
template <typename Sink> Word rebuild(Sink &sink, const Rewrite &rewrite) noexcept {
    auto pieces = rewrite_leaf(sink, rewrite);
    for (auto level = rewrite.path.depth - 1; pieces && level-- > 0;) {
        pieces = rewrite_branch(sink, rewrite, rewrite.path.steps.at(level), *pieces);
    }
    return pieces ? root_of(sink, *pieces) : rewrite.root;
}

// Nodes in a level of `count` items: each holds at most MAP_FANOUT, and at least half that unless it is the only one.
std::size_t nodes_for(std::size_t count) noexcept { return (count + MAP_FANOUT - 1) / MAP_FANOUT; }

// Items [begin, end) of node `node` among `nodes` sharing `count` items evenly.
Range share(std::size_t node, std::size_t nodes, std::size_t count) noexcept {
    return {node * count / nodes, (node + 1) * count / nodes};
}

// Words of the tree map_tree_build writes for `count` entries.
std::size_t build_words(std::size_t count) noexcept {
    std::size_t words = 0;
    for (auto items = count;; items = nodes_for(items)) {
        words += nodes_for(items) * node_words(0) + 2 * items;
        if (nodes_for(items) == 1) {
            return words;
        }
    }
}

// Group one level of pieces into the nodes of the level above, written into `above` (sized for them).
void group(NodeWriter &writer, std::span<const Piece> level, std::span<Piece> above) noexcept {
    std::array<Word, MAP_FANOUT> keys{};
    std::array<Word, MAP_FANOUT> items{};
    for (std::size_t node = 0; node < above.size(); ++node) {
        const auto [begin, end] = share(node, above.size(), level.size());
        std::size_t size = 0;
        for (auto i = begin; i < end; ++i) {
            keys.at(i - begin) = level[i].first;
            items.at(i - begin) = level[i].word;
            size += level[i].size;
        }
        above[node] = writer.node(false, size, std::span(keys).first(end - begin), std::span(items).first(end - begin));
    }
}

// Write the leaves of sorted entries, then their branches level by level into the two reserved levels; return the
// root.
Word build(NodeWriter &writer, std::span<const MapWords> entries, std::vector<Piece> &level,
           std::vector<Piece> &above) noexcept {
    std::array<Word, MAP_FANOUT> keys{};
    std::array<Word, MAP_FANOUT> items{};
    level.resize(nodes_for(entries.size()));
    for (std::size_t node = 0; node < level.size(); ++node) {
        const auto [begin, end] = share(node, level.size(), entries.size());
        for (auto i = begin; i < end; ++i) {
            keys.at(i - begin) = entries[i].first;
            items.at(i - begin) = entries[i].second;
        }
        level[node] =
            writer.node(true, end - begin, std::span(keys).first(end - begin), std::span(items).first(end - begin));
    }
    while (level.size() > 1) {
        above.resize(nodes_for(level.size()));
        group(writer, level, above);
        std::swap(level, above);
    }
    return level.front().word;
}
} // namespace

std::size_t map_edit(HeapStorage &storage) noexcept { return ++storage.map_edits_; }

bool map_edit_current(const HeapStorage &storage, std::size_t edit) noexcept {
    return edit > storage.map_edit_floor_ && edit <= storage.map_edits_;
}

std::expected<Word, HeapError> map_tree_build(const TreeEdit &edit, std::span<const MapWords> entries) noexcept {
    // Both levels are reserved first: nothing may fail once the unzeroed words are claimed.
    std::vector<Piece> level;
    std::vector<Piece> above;
    try {
        level.reserve(nodes_for(entries.size()));
        above.reserve(nodes_for(nodes_for(entries.size())));
    } catch (const std::bad_alloc &) {
        return std::unexpected(HeapError::out_of_memory);
    }
    const auto words = build_words(entries.size());
    const auto claimed = claim(edit.storage, words);
    if (!claimed) {
        return std::unexpected(claimed.error());
    }
    NodeWriter writer(edit.storage, *claimed, edit.edit);
    return build(writer, entries, level, above);
}

std::expected<bool, TreeFailure> map_tree_insert(const TreeEdit &edit, Word &root, MapWords entry, bool exact,
                                                 const MapProbe &probe) noexcept {
    const auto path = descend(root, probe);
    if (!path) {
        return std::unexpected(path.error());
    }
    if (!path->found && exact) {
        return std::unexpected(TreeFailure{TreeFault::missing_key});
    }
    const Rewrite rewrite{.path = *path, .entry = entry, .edit = edit.edit, .root = root};
    WordCounter counter;
    rebuild(counter, rewrite);
    std::span<Word> words;
    if (counter.words() != 0) {
        const auto claimed = claim(edit.storage, counter.words());
        if (!claimed) {
            return std::unexpected(memory_failure(claimed.error()));
        }
        words = *claimed;
    }
    NodeWriter writer(edit.storage, words, edit.edit);
    root = rebuild(writer, rewrite);
    return !path->found;
}

TermResult<std::optional<Word>> map_tree_find(Word root, const MapProbe &probe) noexcept {
    if (!map_tree_present(root)) {
        return std::nullopt; // A consumed stub's (OPT0.5B).
    }
    auto node = MapNode::at(root);
    for (std::size_t depth = 0; depth < MAP_MAX_DEPTH; ++depth) {
        const auto slot = search(node.keys(), probe);
        if (!slot) {
            return std::unexpected(slot.error());
        }
        if (node.leaf()) {
            return slot->found ? std::optional(node.items()[slot->index]) : std::nullopt;
        }
        node = node.child(child_of(*slot));
    }
    return std::nullopt;
}

MapWords map_tree_at(MapNode root, std::size_t index) noexcept {
    auto node = root;
    while (!node.leaf()) {
        std::size_t child = 0;
        for (; index >= node.child(child).size(); ++child) {
            index -= node.child(child).size();
        }
        node = node.child(child);
    }
    return {node.keys()[index], node.items()[index]};
}
} // namespace clause::runtime::detail
