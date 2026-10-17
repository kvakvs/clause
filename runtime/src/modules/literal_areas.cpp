#include "literal_areas.hpp"
#include <algorithm>

namespace clause::runtime::detail {
LiteralAreas::~LiteralAreas() = default;

void LiteralAreas::add(const std::span<const Word> area) {
    if (area.empty()) {
        return;
    }
    const std::scoped_lock lock(mutex_);
    const auto *previous = current_.load(std::memory_order_acquire);
    auto next = std::make_unique<std::vector<Range>>(previous ? *previous : std::vector<Range>{});
    const auto begin = reinterpret_cast<std::uintptr_t>(area.data());
    const Range range{begin, begin + area.size_bytes(), area.data()};
    next->insert(std::ranges::upper_bound(*next, begin, {}, &Range::begin), range);
    snapshots_.reserve(snapshots_.size() + 1);
    current_.store(next.get(), std::memory_order_release);
    snapshots_.push_back(std::move(next));
}

std::span<const Word> LiteralAreas::find(const std::uintptr_t address) const noexcept {
    return persistent_.contains(address) ? persistent_.words_from(address) : module_words(address);
}

bool LiteralAreas::module(const std::uintptr_t address) const noexcept { return !module_words(address).empty(); }

std::span<const Word> LiteralAreas::module_words(const std::uintptr_t address) const noexcept {
    const auto *ranges = current_.load(std::memory_order_acquire);
    if (!ranges || address % sizeof(Word) != 0) {
        return {};
    }
    const auto after = std::ranges::upper_bound(*ranges, address, {}, &Range::begin);
    if (after == ranges->begin()) {
        return {};
    }
    const auto &range = *std::prev(after);
    if (address >= range.end) {
        return {};
    }
    const auto first = (address - range.begin) / sizeof(Word);
    return {range.words + first, (range.end - address) / sizeof(Word)};
}
} // namespace clause::runtime::detail
