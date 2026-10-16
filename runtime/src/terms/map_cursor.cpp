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
    if (stub.root_ != nullptr && first < size_) {
        descend(stub.root_, first);
    }
}

void MapCursor::descend(const MapNode *node, std::size_t skip) noexcept {
    while (!node->leaf_ && depth_ < MAX_DEPTH) {
        const auto &branch = static_cast<const MapBranch &>(*node);
        std::size_t child = 0;
        for (; child + 1 < branch.count_ && skip >= branch.children_.at(child)->size_; ++child) {
            skip -= branch.children_.at(child)->size_;
        }
        path_.at(depth_++) = {&branch, child};
        node = branch.children_.at(child);
    }
    leaf_ = node->leaf_ ? static_cast<const MapLeaf *>(node) : nullptr;
    position_ = skip;
}

bool MapCursor::advance() noexcept {
    while (depth_ > 0) {
        auto &[branch, child] = path_.at(depth_ - 1);
        if (++child < branch->count_) {
            descend(branch->children_.at(child), 0);
            return leaf_ != nullptr;
        }
        --depth_;
    }
    leaf_ = nullptr;
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
    while (leaf_ != nullptr && position_ >= leaf_->count_) {
        if (!advance()) {
            return std::nullopt;
        }
    }
    if (leaf_ == nullptr) {
        return std::nullopt;
    }
    const auto at = position_++;
    return MapWords{leaf_->keys_.at(at), leaf_->values_.at(at)};
}
} // namespace clause::runtime::detail
