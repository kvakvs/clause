#include "registry.hpp"
#include <algorithm>
#include <format>
#include <limits>

namespace clause::runtime::epmd {
namespace {
// Remembered names beyond this many are forgotten, oldest first.
constexpr std::size_t MAX_UNUSED = 1000;
// Creations below this are kept for nodes of older protocol versions.
constexpr std::uint32_t FIRST_CREATION = 4;

// The position of `name` in `nodes`.
auto position(auto &nodes, std::string_view name) { return std::ranges::find(nodes, name, &Node::name_); }

// The creation after `creation`, skipping the reserved ones when it wraps.
std::uint32_t next_creation(std::uint32_t creation) noexcept {
    return creation == std::numeric_limits<std::uint32_t>::max() ? FIRST_CREATION : creation + 1;
}
} // namespace

Registry::Registry(std::uint32_t first_creation) : first_creation_(std::max(first_creation, FIRST_CREATION)) {}

std::uint32_t Registry::creation_for(std::string_view name) {
    const auto old = position(unused_, name);
    if (old == unused_.end()) {
        return first_creation_;
    }
    const auto creation = next_creation(old->creation_);
    unused_.erase(old);
    return creation;
}

std::optional<std::uint32_t> Registry::add(Node node) {
    if (find(node.name_)) {
        return std::nullopt;
    }
    node.creation_ = creation_for(node.name_);
    const auto creation = node.creation_;
    active_.insert(active_.begin(), std::move(node));
    return creation;
}

void Registry::remove(std::string_view name) {
    const auto node = position(active_, name);
    if (node == active_.end()) {
        return;
    }
    unused_.insert(unused_.begin(), std::move(*node));
    active_.erase(node);
    if (unused_.size() > MAX_UNUSED) {
        unused_.pop_back();
    }
}

const Node *Registry::find(std::string_view name) const {
    const auto node = position(active_, name);
    return node == active_.end() ? nullptr : &*node;
}

std::string Registry::names_text() const {
    std::string text;
    for (const auto &node : active_) {
        text += std::format("name {} at port {}\n", node.name_, node.port_);
    }
    return text;
}

std::string Registry::dump_text() const {
    // OTP's epmd ends every DUMP line with a NUL byte.
    std::string text;
    for (const auto &node : active_) {
        text += std::format("active name     <{}> at port {}, fd = {}\n", node.name_, node.port_, node.descriptor_);
        text += '\0';
    }
    for (const auto &node : unused_) {
        text += std::format("old/unused name <{}>, port = {}, fd = {} \n", node.name_, node.port_, node.descriptor_);
        text += '\0';
    }
    return text;
}

std::uint16_t short_creation(std::uint32_t creation) noexcept { return static_cast<std::uint16_t>(creation % 3 + 1); }
} // namespace clause::runtime::epmd
