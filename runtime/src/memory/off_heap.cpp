#include "off_heap.hpp"
#include "heap_storage.hpp"
#include "heap_walk.hpp"
#include <algorithm>
#include <memory>
#include <utility>

namespace clause::runtime::detail {
namespace {
using layout::BinaryBuffer;

// Delete a buffer and return its words to the runtime-wide account it was charged to.
struct BufferCharge {
    // The account charged at creation, kept alive by every reference to the buffer.
    std::shared_ptr<RuntimeMemory> memory_;
    std::size_t words_;

    void operator()(BinaryBuffer *buffer) const noexcept {
        memory_->release(words_);
        std::default_delete<BinaryBuffer>{}(buffer);
    }
};
} // namespace

std::expected<std::shared_ptr<BinaryBuffer>, HeapError> make_buffer(HeapStorage &storage,
                                                                    std::span<const std::byte> bytes) {
    return make_buffer(storage, bytes, bytes.size());
}

std::expected<std::shared_ptr<BinaryBuffer>, HeapError>
make_buffer(HeapStorage &storage, std::span<const std::byte> bytes, const std::size_t capacity) {
    const auto size = std::max(capacity, bytes.size());
    const auto words = (size + sizeof(Word) - 1) / sizeof(Word);
    if (words > storage.room()) {
        return std::unexpected(HeapError::limit_exceeded);
    }
    auto buffer = std::make_unique<BinaryBuffer>(size);
    std::ranges::copy(bytes, buffer->begin());
    storage.memory_->force(words);
    // A throwing control block allocation runs the deleter, which returns the charge.
    return std::shared_ptr<BinaryBuffer>(buffer.release(), BufferCharge{storage.memory_, words});
}

std::expected<void, HeapError> hold_off_heap(HeapStorage &storage, const BinaryBuffer &buffer) {
    const auto [entry, fresh] = storage.buffers_.try_emplace(&buffer, 0);
    if (fresh) {
        const auto words = buffer_words(buffer);
        if (words > storage.options_.limit_bytes / sizeof(Word) - storage.capacity_words_ - storage.off_heap_words_) {
            storage.buffers_.erase(entry);
            return std::unexpected(HeapError::limit_exceeded);
        }
        storage.off_heap_words_ += words;
        if (storage.off_heap_words_ >= storage.binary_limit_words_) {
            storage.signal_collection();
        }
    }
    ++entry->second;
    return {};
}

void drop_off_heap(HeapStorage &storage, const BinaryBuffer &buffer) noexcept {
    const auto entry = storage.buffers_.find(&buffer);
    if (--entry->second == 0) {
        storage.off_heap_words_ -= buffer_words(buffer);
        storage.buffers_.erase(entry);
        storage.appendable_.erase(&buffer);
    }
}

void link_off_heap(HeapStorage &storage, layout::RefcBinaryCell &cell,
                   std::shared_ptr<const BinaryBuffer> buffer) noexcept {
    cell.data_ = buffer->data();
    cell.buffer_ = std::move(buffer);
    cell.next_ = storage.off_heap_;
    storage.off_heap_ = &cell;
}

layout::RefcBinaryCell &relocate_off_heap(layout::RefcBinaryCell &from, std::byte *to) noexcept {
    return *std::construct_at(reinterpret_cast<layout::RefcBinaryCell *>(to), std::move(from));
}

void sweep_off_heap(HeapStorage &storage, HeapArea &copies) noexcept {
    layout::RefcBinaryCell *newest = nullptr;
    auto **link = &newest;
    for (auto *cell = std::exchange(storage.off_heap_, nullptr); cell != nullptr;) {
        auto *next = cell->next_;
        const auto first = cell->header_.value_;
        if (is_header(first)) {
            drop_off_heap(storage, *cell->buffer_);
            std::destroy_at(cell);
        } else {
            // A forwarded cell's first word is the boxed pointer to its copy.
            const auto copy =
                copies.from(static_cast<std::uintptr_t>(first & ~static_cast<Word>(abi::v1::primary_mask)));
            *link = reinterpret_cast<layout::RefcBinaryCell *>(copy.data());
            link = &(*link)->next_;
        }
        cell = next;
    }
    *link = nullptr;
    storage.off_heap_ = newest;
}

void release_off_heap(HeapStorage &storage) noexcept {
    for (auto *cell = std::exchange(storage.off_heap_, nullptr); cell != nullptr;) {
        auto *next = cell->next_;
        std::destroy_at(cell);
        cell = next;
    }
    storage.buffers_.clear();
    storage.appendable_.clear();
    storage.off_heap_words_ = 0;
}
} // namespace clause::runtime::detail
