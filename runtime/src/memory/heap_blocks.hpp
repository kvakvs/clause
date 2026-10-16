#pragma once
#include <clause/runtime/base_types.hpp>
#include <cstddef>
#include <memory>

// Heap block reuse: blocks that collections release stay with their thread for its next collections, as ERTS's
// carriers keep memory, so a new heap block does not take fresh pages from the operating system each time
// (docs/runtime-heap.md#heap-blocks).
namespace clause::runtime::detail {
// Returns a block of `words` words to its thread's cache, or frees it.
struct BlockRelease {
    std::size_t words = 0;

    void operator()(Word *block) const noexcept;
};

using BlockWords = std::unique_ptr<Word[], BlockRelease>;

// A block of at least `words` words, not zeroed: a cached one when one fits, else a new one. Throws std::bad_alloc.
BlockWords allocate_block(std::size_t words);
} // namespace clause::runtime::detail
