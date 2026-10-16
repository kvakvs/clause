#pragma once
#include "../terms/term_layout.hpp"
#include "process_heap.hpp"
#include <expected>

// Per-process list of off-heap binary cells (BEAM MSO list); see docs/runtime-heap.md#off-heap-binaries.
namespace clause::runtime::detail {
class HeapStorage;
struct HeapArea;
struct Survivors;

// Words an off-heap buffer is charged: its bytes rounded up to words.
inline std::size_t buffer_words(const layout::BinaryBuffer &buffer) noexcept {
    return (buffer.size() + sizeof(Word) - 1) / sizeof(Word);
}

// Copy bytes into a new buffer charged to the runtime-wide account until its last reference, in any process, dies.
// Refused with limit_exceeded when the words leave no room; may throw std::bad_alloc, leaving nothing charged.
std::expected<std::shared_ptr<layout::BinaryBuffer>, HeapError> make_buffer(HeapStorage &storage,
                                                                            std::span<const std::byte> bytes);
// The same with `capacity` bytes (at least the input's), zero after the input: room an appendable binary grows into
// without moving (OPT0.6).
std::expected<std::shared_ptr<layout::BinaryBuffer>, HeapError>
make_buffer(HeapStorage &storage, std::span<const std::byte> bytes, std::size_t capacity);
// Count one more cell of this process referencing buffer; the first charges its words to the process budget.
// May throw std::bad_alloc, changing nothing.
std::expected<void, HeapError> hold_off_heap(HeapStorage &storage, const layout::BinaryBuffer &buffer);
// Undo one hold, for a cell that died or was never published; the last returns the process charge.
void drop_off_heap(HeapStorage &storage, const layout::BinaryBuffer &buffer) noexcept;
// Give a just-published cell its buffer reference and make it the newest entry of the owner's list; the cell's
// buffer must already be held.
void link_off_heap(HeapStorage &storage, layout::RefcBinaryCell &cell,
                   std::shared_ptr<const layout::BinaryBuffer> buffer) noexcept;
// Move a cell into fresh storage, transferring its buffer reference; the source keeps none.
// List links are the caller's to repair, as a collector sweep rebuilds them.
layout::RefcBinaryCell &relocate_off_heap(layout::RefcBinaryCell &from, std::byte *to) noexcept;
// After a collection, relink the copies of forwarded cells in list order, keep cells of the old area a minor
// collection left in place, and destroy dead cells; a buffer's process charge returns when the process's last cell
// for it dies.
void sweep_off_heap(HeapStorage &storage, const Survivors &survivors) noexcept;
// Drop every reference held by the owner's cells at teardown, newest first.
void release_off_heap(HeapStorage &storage) noexcept;
} // namespace clause::runtime::detail
