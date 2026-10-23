#include "domain.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <stdexcept>

namespace clause::semantic::types {
namespace {
std::atomic<std::uint64_t> next_owner{1};

// Flatten an already canonical union and omit bottom before sorting its alternatives.
void append_member(std::vector<Id> &flat, const Id member, const Node &node) {
    if (node.kind == Kind::bottom) {
        return;
    }
    if (node.kind == Kind::union_type) {
        flat.insert(flat.end(), node.children.begin(), node.children.end());
    } else {
        flat.push_back(member);
    }
}
} // namespace

Graph::Graph(const Limits limits) : owner_(next_owner.fetch_add(1)), limits_(limits) {
    nodes_.push_back({Kind::top});
    nodes_.push_back({Kind::bottom});
    identities_.emplace(nodes_[0], 0);
    identities_.emplace(nodes_[1], 1);
}

void Graph::foreign() { throw std::invalid_argument("foreign semantic type identity"); }

void Graph::keep_nesting(const Id id, const std::size_t limit, const std::size_t depth) const {
    if (limit >= NESTING_LIMITS) {
        return;
    }
    if (nesting_.size() <= id.index) {
        std::array<std::uint8_t, NESTING_LIMITS> unknown{};
        unknown.fill(UNKNOWN_NESTING);
        nesting_.resize(nodes_.size(), unknown);
    }
    nesting_[id.index][limit] = static_cast<std::uint8_t>(depth);
}

std::size_t NodeHash::operator()(const Node &node) const noexcept {
    // Mix each part in, as boost::hash_combine does.
    std::size_t hash = static_cast<std::size_t>(node.kind);
    const auto mix = [&](const std::size_t value) {
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    };
    mix(std::hash<std::string>{}(node.name));
    mix(std::hash<std::string>{}(node.module));
    for (const auto child : node.children) {
        mix(child.index);
    }
    for (const auto &label : node.labels) {
        mix(std::hash<std::string>{}(label));
    }
    return hash;
}

Id Graph::exhausted() {
    widened_ = true;
    ++widenings_;
    return top();
}

Id Graph::intern(Node node) {
    for (const auto child : node.children) {
        (void)get(child);
    }
    if (const auto found = identities_.find(node); found != identities_.end()) {
        return {owner_, found->second};
    }
    if (nodes_.size() >= limits_.nodes) {
        return exhausted();
    }
    const auto index = nodes_.size();
    identities_.emplace(node, index);
    nodes_.push_back(std::move(node));
    return {owner_, index};
}

Id Graph::join(const std::span<const Id> members) {
    std::vector<Id> flat;
    for (const auto member : members) {
        const auto &node = get(member);
        if (node.kind == Kind::top) {
            return top();
        }
        append_member(flat, member, node);
    }
    std::ranges::sort(flat);
    flat.erase(std::unique(flat.begin(), flat.end()), flat.end());
    if (flat.empty()) {
        return bottom();
    }
    if (flat.size() == 1) {
        return flat.front();
    }
    if (flat.size() > limits_.union_members) {
        return exhausted();
    }
    return intern({Kind::union_type, {}, {}, std::move(flat)});
}

Id Graph::widen(const Id previous, const Id next) { return join(std::array{previous, next}); }
} // namespace clause::semantic::types
