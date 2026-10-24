#include "domain.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <utility>

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

// An answer key packs the operation into 2 bits, the depth into 4 and each operand's index into 29. Added for
// memoized lattice operations (OPTC.B).
constexpr unsigned ANSWER_OPERAND_BITS = 29;
constexpr std::size_t ANSWER_OPERAND_LIMIT = std::size_t{1} << ANSWER_OPERAND_BITS;
constexpr std::size_t ANSWER_DEPTH_LIMIT = 16;
constexpr std::size_t FIRST_ANSWERS = std::size_t{1} << 12;

// The slot a key's probe starts at, in a table of a power-of-two capacity.
std::size_t answer_slot(const std::uint64_t key, const std::size_t capacity) {
    return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ULL) >> 32) & (capacity - 1);
}
} // namespace

Graph::Graph(const Limits limits) : owner_(next_owner.fetch_add(1)), limits_(limits) {
    nodes_.push_back({Kind::top});
    nodes_.push_back({Kind::bottom});
    identities_.emplace(nodes_[0], 0);
    identities_.emplace(nodes_[1], 1);
}

const Answers::Answer *Answers::find(const std::uint64_t key) const {
    if (keys_.empty()) {
        return nullptr;
    }
    for (auto slot = answer_slot(key, keys_.size());; slot = (slot + 1) & (keys_.size() - 1)) {
        if (keys_[slot] == key) {
            return &answers_[slot];
        }
        if (keys_[slot] == 0) {
            return nullptr;
        }
    }
}

void Answers::keep(const std::uint64_t key, const Answer answer) {
    // Kept at most half full, so probes stay short.
    if (2 * (filled_ + 1) > keys_.size()) {
        grow();
    }
    auto slot = answer_slot(key, keys_.size());
    while (keys_[slot] != 0 && keys_[slot] != key) {
        slot = (slot + 1) & (keys_.size() - 1);
    }
    filled_ += keys_[slot] == 0 ? 1 : 0;
    keys_[slot] = key;
    answers_[slot] = answer;
}

void Answers::grow() {
    const auto capacity = std::max(FIRST_ANSWERS, 2 * keys_.size());
    auto keys = std::exchange(keys_, std::vector<std::uint64_t>(capacity));
    auto answers = std::exchange(answers_, std::vector<Answer>(capacity));
    filled_ = 0;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (keys[index] != 0) {
            keep(keys[index], answers[index]);
        }
    }
}

std::uint64_t Graph::answer_key(const Operation operation, const Id left, const Id right, const std::size_t depth) {
    if (left.index >= ANSWER_OPERAND_LIMIT || right.index >= ANSWER_OPERAND_LIMIT || depth >= ANSWER_DEPTH_LIMIT) {
        return 0;
    }
    return (static_cast<std::uint64_t>(operation) << 62) | (static_cast<std::uint64_t>(depth) << 58) |
           (static_cast<std::uint64_t>(left.index) << ANSWER_OPERAND_BITS) | static_cast<std::uint64_t>(right.index);
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
