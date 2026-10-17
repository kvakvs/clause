#pragma once
#include "heap_storage.hpp"
#include "heap_walk.hpp"

// Full-sweep copying collection of one process heap (docs/runtime-heap.md#collection).
namespace clause::runtime::detail {
// Return the smallest ERTS heap size of at least words: 12, 38, then Fibonacci-like steps, then 20% steps.
std::size_t heap_size_at_least(std::size_t words) noexcept;

// Which part of the heap a collection copies (docs/runtime-heap.md#generations): a minor collection copies the
// young heap and fragments, promoting objects below the high-water mark into the old area and leaving old objects in
// place; a full sweep copies everything into one new block.
// A young collection copies the young heap again without promoting, to shrink it after a minor one.
enum class Generation : std::uint8_t { minor, young, full };

// Where a collection put its copies, for the off-heap and tree sweeps (generational collection).
struct Survivors {
    // The new heap block, and the old area a minor collection promotes into and keeps; null for a full sweep.
    HeapArea &young;
    HeapArea *old;
    // Whether an object at address lies in the kept old area, so the sweep leaves it alone.
    bool kept(const void *object) const noexcept;
    // The copy a forwarding word names, in either area.
    std::span<Word> copy(Word forwarded) const noexcept;
};

// Whether no old-area object, nor an old stub's tree, points into the young heap or a fragment: the invariant minor
// collections rely on (immutable terms point only at older ones); Debug builds check it after each minor collection.
bool generations_valid(const HeapStorage &storage) noexcept;

// One Cheney copy: the caller evacuates every root word, then finish() copies the rest and swaps the heap.
class Copier final {
  public:
    // Allocate the to-space block of capacity words, a throw leaves the heap untouched.
    // A minor collection also creates the old area when it has none, sized for the words it may promote.
    // A full sweep given `retiring` also copies the objects of those retiring persistent terms it reaches, each once;
    // the capacity must leave room for all of their words (persistent term reclamation).
    Copier(HeapStorage &storage, std::size_t capacity, Generation generation = Generation::full,
           const RetiringRanges *retiring = nullptr);
    // Return the word naming the to-space copy of value's object, copying it once; other words come back unchanged.
    Word evacuate(Word value) noexcept;
    // Copy everything reachable from the copies, sweep the off-heap list, and replace the heap and fragments.
    void finish() noexcept;
    // Whether the collection copied the persistent term object at `object` (persistent term reclamation).
    bool copied_external(const void *object) const noexcept;

  private:
    // The copy of a from-space object: the one its forwarding word names, or a new one.
    Word forward(std::span<Word> from, TermKind kind) noexcept;
    // Copy one cons cell or boxed object into area and leave a forwarding word behind in from-space.
    Word copy_cons(std::span<Word> from, HeapArea &area) noexcept;
    Word copy_boxed(std::span<Word> from, HeapArea &area) noexcept;
    // The area a from-space object goes to: the old area for a promoted one, else the new block.
    HeapArea &target(std::span<const Word> from) noexcept;
    // Evacuate the term slots of every object copied into area from scanned on; true when it found any.
    bool scan(HeapArea &area, std::size_t &scanned) noexcept;
    // Evacuate the term slots of one copied object starting at the first word of object.
    void evacuate_slots(std::span<Word> object, const HeapCell &cell) noexcept;
    // Copy an object of a retiring persistent term into the new block once, leaving the shared original untouched.
    Word copy_external(Word value) noexcept;
    // The forwarding slot of a retiring object: the one holding its address and copy, or the empty one to fill.
    std::size_t slot_of(std::uintptr_t object) const noexcept;

    // The collected process storage; from-space is its current heap block and fragments, and the old area too for a
    // full sweep.
    HeapStorage &storage_;
    Generation generation_;
    // The new heap block; copies are appended at its top.
    HeapArea to_;
    // The young words below the high-water mark, promoted by a minor collection, and the old area's top before it.
    std::span<const Word> mature_;
    std::size_t old_start_ = 0;
    // The persistent term range whose retiring objects this collection copies, or null; their copies by original
    // address in an open-addressing table sized for every retiring object; and the copied binary cells, linked into
    // the off-heap list after the sweep (persistent term reclamation).
    const RetiringRanges *retiring_;
    std::vector<std::pair<std::uintptr_t, Word>> forwarded_;
    std::vector<layout::RefcBinaryCell *> external_binaries_;
};
} // namespace clause::runtime::detail
