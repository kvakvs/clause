#include "off_heap.hpp"
#include "heap_collect.hpp"
#include "heap_storage.hpp"
#include "heap_walk.hpp"
#include <algorithm>
#include <bit>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace clause::runtime::detail {
namespace {
using layout::BinaryBuffer;

// Buffers below this many bytes come from the C++ heap's own free lists cheaply and are not kept.
constexpr std::size_t CACHED_MIN_BYTES = 256;
// Buffers and bytes one thread keeps at most.
constexpr std::size_t CACHED_BUFFERS = 32;
constexpr std::size_t CACHED_BYTES = std::size_t{8} << 20;

// Large buffers of power-of-two sizes (appendable capacities) whose last reference died on one thread, kept for
// that thread's next buffer of the same size, as heap blocks are; freed when the thread ends (buffer reuse).
class BufferCache final {
  public:
    // A kept buffer of exactly `size` bytes, taken out of the cache; none when no kept buffer has that size.
    std::unique_ptr<BinaryBuffer> take(std::size_t size) noexcept {
        const auto found =
            std::ranges::find_if(buffers_, [size](const auto &buffer) { return buffer->size() == size; });
        if (found == buffers_.end()) {
            return nullptr;
        }
        auto buffer = std::move(*found);
        buffers_.erase(found);
        held_ -= size;
        return buffer;
    }

    // Keep a released buffer of a power-of-two size while the cache has room; false leaves it to the caller.
    bool keep(BinaryBuffer *buffer) noexcept {
        const auto size = buffer->size();
        if (size < CACHED_MIN_BYTES || !std::has_single_bit(size) || buffers_.size() == CACHED_BUFFERS ||
            held_ + size > CACHED_BYTES) {
            return false;
        }
        if (buffers_.capacity() == 0) {
            try {
                buffers_.reserve(CACHED_BUFFERS);
            } catch (const std::bad_alloc &) {
                return false;
            }
        }
        buffers_.emplace_back(buffer);
        held_ += size;
        return true;
    }

  private:
    // The kept buffers, each owned here until taken.
    std::vector<std::unique_ptr<BinaryBuffer>> buffers_;
    // Bytes of all kept buffers.
    std::size_t held_ = 0;
};

// This thread's kept buffers (buffer reuse).
BufferCache &buffer_cache() noexcept {
    thread_local BufferCache buffers;
    return buffers;
}

// Delete or keep a buffer and return its words to the runtime-wide account it was charged to.
struct BufferCharge {
    // The account charged at creation, kept alive by every reference to the buffer.
    std::shared_ptr<RuntimeMemory> memory_;
    std::size_t words_;

    void operator()(BinaryBuffer *buffer) const noexcept {
        memory_->release(words_);
        if (!buffer_cache().keep(buffer)) {
            std::default_delete<BinaryBuffer>{}(buffer);
        }
    }
};

// A buffer of `size` bytes starting with `bytes`: a kept one of that size when there is one, its room after the
// input left as it was, else a new one zeroed after the input. Throws std::bad_alloc.
std::unique_ptr<BinaryBuffer> new_buffer(std::span<const std::byte> bytes, std::size_t size) {
    if (size >= CACHED_MIN_BYTES && std::has_single_bit(size)) {
        if (auto kept = buffer_cache().take(size)) {
            std::ranges::copy(bytes, kept->begin());
            return kept;
        }
    }
    auto buffer = std::make_unique<BinaryBuffer>();
    buffer->reserve(size);
    buffer->assign(bytes.begin(), bytes.end());
    buffer->resize(size);
    return buffer;
}
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
    auto buffer = new_buffer(bytes, size);
    storage.memory_->force(words);
    // A throwing control block allocation runs the deleter, which returns the charge.
    return std::shared_ptr<BinaryBuffer>(buffer.release(), BufferCharge{storage.memory_, words});
}

std::expected<std::shared_ptr<BinaryBuffer>, HeapError> make_buffer(HeapStorage &storage, BinaryBuffer &&bytes) {
    const auto words = (bytes.size() + sizeof(Word) - 1) / sizeof(Word);
    if (words > storage.room()) {
        return std::unexpected(HeapError::limit_exceeded);
    }
    auto buffer = std::make_unique<BinaryBuffer>(std::move(bytes));
    storage.memory_->force(words);
    // A throwing control block allocation runs the deleter, which returns the charge.
    return std::shared_ptr<BinaryBuffer>(buffer.release(), BufferCharge{storage.memory_, words});
}

std::expected<void, HeapError> hold_off_heap(HeapStorage &storage, const BinaryBuffer &buffer) {
    const auto [entry, fresh] = storage.buffers_.try_emplace(&buffer, 0);
    if (fresh) {
        const auto words = buffer_words(buffer);
        if (words > words_left(storage.options_.limit_bytes / sizeof(Word),
                               storage.capacity_words_ + storage.off_heap_words_)) {
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

void hold_off_heap_unbudgeted(HeapStorage &storage, const BinaryBuffer &buffer) {
    const auto [entry, fresh] = storage.buffers_.try_emplace(&buffer, 0);
    if (fresh) {
        storage.off_heap_words_ += buffer_words(buffer);
    }
    ++entry->second;
}

void drop_off_heap(HeapStorage &storage, const BinaryBuffer &buffer) noexcept {
    const auto entry = storage.buffers_.find(&buffer);
    if (--entry->second == 0) {
        storage.off_heap_words_ -= buffer_words(buffer);
        storage.buffers_.erase(entry);
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

void sweep_off_heap(HeapStorage &storage, const Survivors &survivors) noexcept {
    layout::RefcBinaryCell *newest = nullptr;
    auto **link = &newest;
    for (auto *cell = std::exchange(storage.off_heap_, nullptr); cell != nullptr;) {
        auto *next = cell->next_;
        const auto first = cell->header_.value_;
        if (survivors.kept(cell)) {
            *link = cell;
            link = &cell->next_;
        } else if (is_header(first)) {
            drop_off_heap(storage, *cell->buffer_);
            std::destroy_at(cell);
        } else {
            // A forwarded cell's first word is the boxed pointer to its copy.
            const auto copy = survivors.copy(first);
            *link = reinterpret_cast<layout::RefcBinaryCell *>(copy.data());
            link = &(*link)->next_;
        }
        cell = next;
    }
    *link = nullptr;
    storage.off_heap_ = newest;
}

void splice_off_heap(HeapStorage &to, HeapStorage &from) noexcept {
    if (from.off_heap_ == nullptr) {
        return;
    }
    auto *last = from.off_heap_;
    while (last->next_ != nullptr) {
        last = last->next_;
    }
    last->next_ = to.off_heap_;
    to.off_heap_ = std::exchange(from.off_heap_, nullptr);
}

void release_off_heap(HeapStorage &storage) noexcept {
    for (auto *cell = std::exchange(storage.off_heap_, nullptr); cell != nullptr;) {
        auto *next = cell->next_;
        std::destroy_at(cell);
        cell = next;
    }
    storage.buffers_.clear();
    storage.off_heap_words_ = 0;
}
} // namespace clause::runtime::detail
