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
using layout::SubBinaryCell;

// Keep allocation failures distinct from the configured backing ceiling.
TermError heap_error(HeapError error) {
    return error == HeapError::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}

// What an accumulator views of an off-heap buffer: the refc cell (and its word) owning the buffer and the bit range
// within the buffer. A sub-binary of an appendable binary may reach past its original's own bits (appends).
struct AppendSource {
    Word original;
    const RefcBinaryCell *cell;
    std::size_t start;
    std::size_t bits;
};

// The refc cell under a refc binary or a sub-binary of one; none for any other term.
std::optional<AppendSource> append_source(const Term &accumulator) {
    const auto object = TermAccess::object(accumulator);
    if (!object) {
        return std::nullopt;
    }
    const auto kind = BoxHeader::kind(object->words[0]);
    if (kind == BoxedKind::refc_binary) {
        const auto *cell = reinterpret_cast<const RefcBinaryCell *>(object->words.data());
        return AppendSource{accumulator.word(), cell, cell->offset_, cell->bits_};
    }
    if (kind != BoxedKind::sub_binary) {
        return std::nullopt;
    }
    const auto &sub = *reinterpret_cast<const SubBinaryCell *>(object->words.data());
    const auto inner = TermAccess::child(accumulator, sub.original_).and_then(TermAccess::object);
    if (!inner || BoxHeader::kind(inner->words[0]) != BoxedKind::refc_binary) {
        return std::nullopt;
    }
    const auto *cell = reinterpret_cast<const RefcBinaryCell *>(inner->words.data());
    return AppendSource{sub.original_, cell, cell->offset_ + sub.offset_, sub.bits_};
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
    const auto source = append_source(accumulator);
    if (!source) {
        return std::nullopt;
    }
    const auto &cell = *source->cell;
    auto &storage = *heap.storage_;
    const auto end = source->start + source->bits;
    const auto found = storage.appendable_.find(cell.buffer_.get());
    if (found == storage.appendable_.end() || found->second != end || end % 8 != 0 || source->start % 8 != 0) {
        return std::nullopt;
    }
    const auto total = source->bits + rest.length;
    if ((end + rest.length + 7) / 8 > cell.buffer_->size()) {
        BitWriter joined;
        joined.bytes.assign(cell.buffer_->begin() + static_cast<std::ptrdiff_t>(source->start / 8),
                            cell.buffer_->begin() + static_cast<std::ptrdiff_t>(end / 8));
        joined.length = source->bits;
        if (const auto added = joined.append({{rest.bytes.data(), rest.bytes.size()}, 0, rest.length}); !added) {
            return std::unexpected(added.error());
        }
        return appendable(heap, {joined.bytes.data(), joined.bytes.size()}, joined.length);
    }
    // The bytes past `end` belong to no view, so writing them changes no term. The result is a sub-binary of the
    // buffer's refc cell: no buffer reference or off-heap cell per append.
    auto *spare = const_cast<std::byte *>(cell.buffer_->data()) + end / 8;
    std::ranges::copy(std::span(rest.bytes.data(), (rest.length + 7) / 8), spare);
    auto made = sub_binary(heap, source->original, {source->start - cell.offset_, total});
    if (made) {
        found->second = end + rest.length;
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
