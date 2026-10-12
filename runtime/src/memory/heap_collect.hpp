#pragma once
#include "heap_storage.hpp"
#include "heap_walk.hpp"

// Full-sweep copying collection of one process heap (docs/runtime-heap.md#collection).
namespace clause::runtime::detail {
// Return the smallest ERTS heap size of at least words: 12, 38, then Fibonacci-like steps, then 20% steps.
std::size_t heap_size_at_least(std::size_t words) noexcept;

// One Cheney copy: the caller evacuates every root word, then finish() copies the rest and swaps the heap.
class Copier final {
  public:
    // Allocate the to-space block of capacity words, then mark host Terms stale; a throw leaves the heap untouched.
    Copier(HeapStorage &storage, std::size_t capacity);
    // Return the word naming the to-space copy of value's object, copying it once; other words come back unchanged.
    Word evacuate(Word value) noexcept;
    // Copy everything reachable from the copies, sweep the off-heap list, and replace the heap and fragments.
    void finish() noexcept;

  private:
    // Copy one cons cell or boxed object and leave a forwarding word behind in from-space.
    Word copy_cons(std::span<Word> from) noexcept;
    Word copy_boxed(std::span<Word> from) noexcept;
    // Evacuate the term slots of every copied object, left to right, until the scan reaches the top.
    void scan() noexcept;
    // Evacuate the term slots of one copied object starting at the first word of object.
    void evacuate_slots(std::span<Word> object, const HeapCell &cell) noexcept;
    // Evacuate the key and value words of a copied off-heap map stub's tree, each shared node once (OPT0.5A).
    void evacuate_tree(const HeapCell &cell) noexcept;

    // The collected process storage; from-space is its current heap block and fragments.
    HeapStorage &storage_;
    // The new heap block; copies are appended at its top.
    HeapArea to_;
};
} // namespace clause::runtime::detail
