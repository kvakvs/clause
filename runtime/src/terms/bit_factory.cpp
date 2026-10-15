#include "../memory/heap_storage.hpp"
#include "../memory/off_heap.hpp"
#include "bitstrings.hpp"
#include "term_layout.hpp"
#include "terms.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <optional>

namespace clause::runtime::detail {
namespace {
using layout::BinaryBuffer;
using layout::BoxHeader;
using layout::HeapBinaryCell;
using layout::RefcBinaryCell;

// Keep allocation failures distinct from the configured backing ceiling.
TermError heap_error(HeapError error) {
    return error == HeapError::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}

// Zero the bits after the last valid one so equal values carry equal padding.
void clear_padding(std::span<std::byte> bytes, std::size_t count) {
    if (count % 8 != 0) {
        bytes[count / 8] &= static_cast<std::byte>(0xffU << (8 - count % 8));
    }
}

} // namespace

TermResult<Term> BitAccess::heap_binary(ProcessHeap &heap, std::span<const std::byte> bytes, std::size_t count) {
    const auto size = (count + 7) / 8;
    const auto total = 1 + layout::heap_binary_payload_words(count, sizeof(Word));
    auto reserved = heap.reserve(total);
    if (!reserved) {
        return std::unexpected(heap_error(reserved.error()));
    }
    const auto storage = reserved->bytes();
    auto *cell = std::construct_at(
        reinterpret_cast<HeapBinaryCell *>(storage.data()),
        HeapBinaryCell{{BoxHeader::make(BoxedKind::heap_binary, total - 1)}, static_cast<Word>(count)});
    const auto data = storage.subspan(sizeof(HeapBinaryCell), size);
    std::ranges::copy(bytes.first(size), data.begin());
    clear_padding(data, count);
    return publish(heap.storage_, *reserved, reinterpret_cast<Word>(cell) | static_cast<Word>(TermKindPrimary::boxed));
}

TermResult<Term> BitAccess::refc_binary(ProcessHeap &heap, std::shared_ptr<const BinaryBuffer> buffer, BitRange range) {
    constexpr auto total = sizeof(RefcBinaryCell) / sizeof(Word);
    if (const auto held = heap.hold_off_heap(*buffer); !held) {
        return std::unexpected(heap_error(held.error()));
    }
    auto reserved = heap.reserve(total);
    if (!reserved) {
        drop_off_heap(*heap.storage_, *buffer);
        return std::unexpected(heap_error(reserved.error()));
    }
    auto *cell = std::construct_at(reinterpret_cast<RefcBinaryCell *>(reserved->bytes().data()));
    cell->header_ = {BoxHeader::make(BoxedKind::refc_binary, total - 1)};
    cell->offset_ = static_cast<Word>(range.offset);
    cell->bits_ = static_cast<Word>(range.length);
    auto published =
        publish(heap.storage_, *reserved, reinterpret_cast<Word>(cell) | static_cast<Word>(TermKindPrimary::boxed));
    if (published) {
        link_off_heap(*heap.storage_, *cell, std::move(buffer));
    } else {
        drop_off_heap(*heap.storage_, *buffer);
    }
    return published;
}

TermResult<Term> BitAccess::sub_binary(ProcessHeap &heap, Word original, BitRange range) {
    constexpr auto total = sizeof(layout::SubBinaryCell) / sizeof(Word);
    auto reserved = heap.reserve(total);
    if (!reserved) {
        return std::unexpected(heap_error(reserved.error()));
    }
    auto *cell = std::construct_at(reinterpret_cast<layout::SubBinaryCell *>(reserved->bytes().data()));
    cell->header_ = {BoxHeader::make(BoxedKind::sub_binary, total - 1)};
    cell->offset_ = static_cast<Word>(range.offset);
    cell->bits_ = static_cast<Word>(range.length);
    cell->original_ = original;
    return publish(heap.storage_, *reserved, reinterpret_cast<Word>(cell) | static_cast<Word>(TermKindPrimary::boxed));
}

TermResult<Term> BitAccess::shared_binary(ProcessHeap &heap, std::span<const std::byte> bytes, std::size_t count) {
    auto buffer = heap.off_heap_buffer(bytes.first((count + 7) / 8));
    if (!buffer) {
        return std::unexpected(heap_error(buffer.error()));
    }
    clear_padding(**buffer, count);
    // A failed publication drops the last reference, which returns the buffer's charge.
    return refc_binary(heap, std::move(*buffer), {0, count});
}

TermResult<Term> BitAccess::make(ProcessHeap &heap, std::span<const std::byte> bytes, std::size_t count) {
    const auto size = count / 8 + (count % 8 != 0 ? 1 : 0);
    if (size > bytes.size()) {
        return std::unexpected(TermError::invalid_argument);
    }
    if (size <= layout::heap_binary_bytes) {
        return heap_binary(heap, bytes, count);
    }
    try {
        return shared_binary(heap, bytes, count);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

TermResult<Term> BitAccess::appendable(ProcessHeap &heap, std::span<const std::byte> bytes, std::size_t count) {
    const auto size = (count + 7) / 8;
    // Double the room each time, so a loop of appends copies each byte a constant number of times.
    auto buffer = make_buffer(*heap.storage_, bytes.first(size), std::max<std::size_t>(2 * size, 256));
    if (!buffer) {
        return std::unexpected(heap_error(buffer.error()));
    }
    clear_padding(**buffer, count);
    const auto *key = buffer->get();
    auto made = refc_binary(heap, std::move(*buffer), {0, count});
    if (made) {
        heap.storage_->appendable_[key] = count;
    }
    return made;
}

std::optional<TermResult<Term>> BitAccess::append(ProcessHeap &heap, const Term &accumulator, const BitWriter &rest) {
    const auto object = TermAccess::object(accumulator);
    if (!object || BoxHeader::kind(object->words[0]) != BoxedKind::refc_binary) {
        return std::nullopt;
    }
    const auto &cell = *reinterpret_cast<const RefcBinaryCell *>(object->words.data());
    auto &storage = *heap.storage_;
    const auto end = static_cast<std::size_t>(cell.offset_ + cell.bits_);
    const auto found = storage.appendable_.find(cell.buffer_.get());
    if (found == storage.appendable_.end() || found->second != end || end % 8 != 0 || cell.offset_ % 8 != 0) {
        return std::nullopt;
    }
    const auto total = static_cast<std::size_t>(cell.bits_) + rest.length;
    if ((end + rest.length + 7) / 8 > cell.buffer_->size()) {
        BitWriter joined;
        joined.bytes.assign(cell.buffer_->begin() + static_cast<std::ptrdiff_t>(cell.offset_ / 8),
                            cell.buffer_->begin() + static_cast<std::ptrdiff_t>(end / 8));
        joined.length = cell.bits_;
        if (const auto added = joined.append({{rest.bytes.data(), rest.bytes.size()}, 0, rest.length}); !added) {
            return std::unexpected(added.error());
        }
        return appendable(heap, {joined.bytes.data(), joined.bytes.size()}, joined.length);
    }
    // The bytes past `end` belong to no view, so writing them changes no term.
    auto *spare = const_cast<std::byte *>(cell.buffer_->data()) + end / 8;
    std::ranges::copy(std::span(rest.bytes.data(), (rest.length + 7) / 8), spare);
    auto made = refc_binary(heap, cell.buffer_, {cell.offset_, total});
    if (made) {
        found->second = cell.offset_ + total;
    }
    return made;
}

TermResult<Term> BitAccess::slice(ProcessHeap &heap, const Term &source, std::size_t offset, std::size_t count) {
    const auto owned = heap.retain(source);
    if (!owned) {
        return std::unexpected(owned.error());
    }
    const auto view = bit_view(source);
    if (!view) {
        return std::unexpected(view.error());
    }
    if (offset > view->length || count > view->length - offset) {
        return std::unexpected(TermError::out_of_range);
    }
    const auto words = TermAccess::object(source).value().words;
    if (BoxHeader::kind(words[0]) == BoxedKind::sub_binary) {
        const auto &range = *reinterpret_cast<const layout::SubBinaryCell *>(words.data());
        return sub_binary(heap, range.original_, {range.offset_ + offset, count});
    }
    if (BoxHeader::kind(words[0]) == BoxedKind::refc_binary) {
        // A range of a shared binary is a sub-binary of its view: no buffer reference to take (OPT1.4B).
        return sub_binary(heap, source.word(), {offset, count});
    }
    BitWriter writer;
    const auto copied = writer.append({view->bytes, view->offset + offset, count});
    if (!copied) {
        return std::unexpected(copied.error());
    }
    return make(heap, writer.bytes, count);
}
} // namespace clause::runtime::detail

namespace clause::runtime {
TermResult<Term> TermFactory::binary(std::span<const std::byte> bytes) {
    // Only a size whose bit count leaves the address range is refused; there is no size cap.
    if (bytes.size() > std::numeric_limits<std::size_t>::max() / 8) {
        return std::unexpected(TermError::resource_limit);
    }
    return bitstring(bytes, bytes.size() * 8);
}

TermResult<Term> TermFactory::bitstring(std::span<const std::byte> bytes, std::size_t count) {
    return heap().and_then([&](ProcessHeap *owner) { return detail::BitAccess::make(*owner, bytes, count); });
}
} // namespace clause::runtime
