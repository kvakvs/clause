#include "../memory/heap_object.hpp"
#include "bitstrings.hpp"
#include "term_layout.hpp"
#include <algorithm>
#include <new>

namespace clause::runtime::detail {
TermResult<BitView> bit_view(const Term &term) {
    const auto object = TermAccess::object(term);
    if (!object) {
        return std::unexpected(object.error());
    }
    if (object->kind != TermKind::bitstring) {
        return std::unexpected(TermError::wrong_type);
    }
    const auto words = object->words;
    if (layout::BoxHeader::kind(words[0]) == BoxedKind::sub_binary) {
        // A range of its original's bits (OPT1.4B).
        const auto &cell = *reinterpret_cast<const layout::SubBinaryCell *>(words.data());
        const auto original = TermAccess::child(term, cell.original_).and_then(bit_view);
        if (!original) {
            return std::unexpected(original.error());
        }
        return BitView{original->bytes, original->offset + cell.offset_, cell.bits_};
    }
    if (layout::BoxHeader::kind(words[0]) == BoxedKind::refc_binary) {
        const auto &cell = *reinterpret_cast<const layout::RefcBinaryCell *>(words.data());
        return BitView{*cell.buffer_, cell.offset_, cell.bits_};
    }
    const auto &cell = *reinterpret_cast<const layout::HeapBinaryCell *>(words.data());
    const auto data = std::as_bytes(words.subspan(sizeof(layout::HeapBinaryCell) / sizeof(Word)));
    return BitView{data.first((cell.bits_ + 7) / 8), 0, cell.bits_};
}

namespace {
// The view of an inline or off-heap binary cell; none for any other cell.
std::optional<BitView> stored_view(const Word *cell) noexcept {
    const auto kind = layout::BoxHeader::kind(cell[0]);
    if (kind == BoxedKind::refc_binary) {
        const auto &refc = *reinterpret_cast<const layout::RefcBinaryCell *>(cell);
        return BitView{std::span(refc.data_, (refc.offset_ + refc.bits_ + 7) / 8), refc.offset_, refc.bits_};
    }
    if (kind == BoxedKind::heap_binary) {
        const auto &heap = *reinterpret_cast<const layout::HeapBinaryCell *>(cell);
        const auto *data = reinterpret_cast<const std::byte *>(cell + sizeof(layout::HeapBinaryCell) / sizeof(Word));
        return BitView{std::span(data, (heap.bits_ + 7) / 8), 0, heap.bits_};
    }
    return std::nullopt;
}
} // namespace

std::optional<BitView> trusted_bit_view(Word word) noexcept {
    if ((word & abi::v1::primary_mask) != abi::v1::boxed_tag) {
        return std::nullopt;
    }
    const auto *cell = TermAccess::trusted(word);
    if (layout::BoxHeader::kind(cell[0]) != BoxedKind::sub_binary) {
        return stored_view(cell);
    }
    // A range of its original's bits; the original is never another sub-binary (OPT1.4B).
    const auto &sub = *reinterpret_cast<const layout::SubBinaryCell *>(cell);
    const auto original = stored_view(TermAccess::trusted(sub.original_));
    if (!original) {
        return std::nullopt;
    }
    return BitView{original->bytes, original->offset + sub.offset_, sub.bits_};
}

std::optional<std::span<const std::byte>> trusted_binary_bytes(Word word) noexcept {
    const auto view = trusted_bit_view(word);
    if (!view || view->offset % 8 != 0 || view->length % 8 != 0) {
        return std::nullopt;
    }
    return view->bytes.subspan(view->offset / 8, view->length / 8);
}

std::optional<Word> claim_binary(ProcessHeap &heap, std::span<const std::byte> bytes) noexcept {
    if (bytes.size() > layout::heap_binary_bytes) {
        return std::nullopt;
    }
    const auto payload = layout::heap_binary_payload_words(bytes.size() * 8, sizeof(Word));
    const auto words = TermAccess::claim(heap, 1 + payload);
    if (words.empty()) {
        return std::nullopt;
    }
    words[0] = layout::BoxHeader::make(BoxedKind::heap_binary, payload);
    words[1] = bytes.size() * 8;
    const auto data = std::as_writable_bytes(words.subspan(2));
    std::ranges::fill(data, std::byte{0});
    std::ranges::copy(bytes, data.begin());
    return reinterpret_cast<Word>(words.data()) | static_cast<Word>(TermKindPrimary::boxed);
}

bool bit_at(const BitView &view, std::size_t index) noexcept {
    const auto position = view.offset + index;
    return (std::to_integer<unsigned>(view.bytes[position / 8]) & (1U << (7 - position % 8))) != 0;
}

namespace {
// Compare the whole leading bytes of the first `count` bits of two byte-aligned views at once: the order of the first
// differing byte, else 0 with `compared` set to the bits already equal (0 for unaligned views).
int byte_order(const BitView &lhs, const BitView &rhs, std::size_t count, std::size_t &compared) noexcept {
    compared = 0;
    if (lhs.offset % 8 != 0 || rhs.offset % 8 != 0) {
        return 0;
    }
    const auto a = lhs.bytes.subspan(lhs.offset / 8, count / 8);
    const auto b = rhs.bytes.subspan(rhs.offset / 8, count / 8);
    const auto [at, other] = std::ranges::mismatch(a, b);
    if (at != a.end()) {
        return std::to_integer<unsigned>(*at) < std::to_integer<unsigned>(*other) ? -1 : 1;
    }
    compared = count / 8 * 8;
    return 0;
}
} // namespace

TermResult<int> bit_order(const Term &left, const Term &right) {
    const auto lhs = bit_view(left).value();
    const auto rhs = bit_view(right).value();
    const auto count = std::min(lhs.length, rhs.length);
    std::size_t i = 0;
    if (const auto order = byte_order(lhs, rhs, count, i); order != 0) {
        return order;
    }
    for (; i < count; ++i) {
        const bool a = bit_at(lhs, i);
        const bool b = bit_at(rhs, i);
        if (a != b) {
            return a ? 1 : -1;
        }
    }
    return static_cast<int>(lhs.length > rhs.length) - static_cast<int>(lhs.length < rhs.length);
}
} // namespace clause::runtime::detail

namespace clause::runtime {
bool Term::is_bitstring() const { return kind() == TermKind::bitstring; }

bool Term::is_binary() const {
    return bit_size().transform([](std::size_t count) { return count % 8 == 0; }).value_or(false);
}

TermResult<std::size_t> Term::bit_size() const {
    return detail::bit_view(*this).transform([](const detail::BitView &view) { return view.length; });
}

TermResult<std::vector<std::byte>> Term::bitstring_bytes() const {
    const auto view = detail::bit_view(*this);
    if (!view) {
        return std::unexpected(view.error());
    }
    try {
        detail::BitWriter writer;
        const auto copied = writer.append(*view);
        if (!copied) {
            return std::unexpected(copied.error());
        }
        return std::vector<std::byte>(writer.bytes.begin(), writer.bytes.end());
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

TermResult<std::vector<std::byte>> Term::binary_bytes() const {
    const auto count = bit_size();
    if (!count) {
        return std::unexpected(count.error());
    }
    return *count % 8 == 0 ? bitstring_bytes()
                           : TermResult<std::vector<std::byte>>(std::unexpected(TermError::wrong_type));
}
} // namespace clause::runtime
