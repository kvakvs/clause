#include "map_cursor.hpp"

namespace clause::runtime::detail {
MapCursor::MapCursor(std::span<const Word> words, std::size_t first) noexcept {
    if (layout::BoxHeader::kind(words[0]) != BoxedKind::tree_map) {
        flat_ = words;
        size_ = (words.size() - 1) / 2;
        position_ = first;
        return;
    }
    const auto &stub = *reinterpret_cast<const layout::TreeMapCell *>(words.data());
    size_ = static_cast<std::size_t>(stub.size_);
    if (map_tree_present(stub.root_) && first < size_) {
        descend(MapNode::at(stub.root_), first);
    }
}

void MapCursor::descend(MapNode node, std::size_t skip) noexcept {
    while (!node.leaf() && depth_ < MAP_MAX_DEPTH) {
        std::size_t child = 0;
        for (; child + 1 < node.count() && skip >= node.child(child).size(); ++child) {
            skip -= node.child(child).size();
        }
        path_.at(depth_++) = {node, child};
        node = node.child(child);
    }
    leaf_ = node.leaf() ? std::optional(node) : std::nullopt;
    position_ = skip;
}

bool MapCursor::advance() noexcept {
    while (depth_ > 0) {
        auto &[branch, child] = path_.at(depth_ - 1);
        if (++child < branch.count()) {
            descend(branch.child(child), 0);
            return leaf_.has_value();
        }
        --depth_;
    }
    leaf_.reset();
    return false;
}

std::optional<MapWords> MapCursor::next() noexcept {
    if (!flat_.empty()) {
        if (position_ >= size_) {
            return std::nullopt;
        }
        const auto at = 1 + 2 * position_++;
        return MapWords{flat_[at], flat_[at + 1]};
    }
    while (leaf_ && position_ >= leaf_->count()) {
        if (!advance()) {
            return std::nullopt;
        }
    }
    if (!leaf_) {
        return std::nullopt;
    }
    const auto at = position_++;
    return MapWords{leaf_->keys()[at], leaf_->items()[at]};
}
} // namespace clause::runtime::detail
