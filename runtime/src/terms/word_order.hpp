#pragma once
#include "process_heap.hpp"
#include <optional>

namespace clause::runtime::detail {
// The structural order of two words when both are small integers, floats or whole-byte binaries, read straight from
// their cells (cheap comparisons); none for any other pair, which takes structural_order.
std::optional<int> word_order(ProcessHeap &heap, Word left, Word right) noexcept;
} // namespace clause::runtime::detail
