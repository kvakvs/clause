#include "word_order.hpp"
#include "../memory/heap_object.hpp"
#include "structural_order.hpp"
#include "term_layout.hpp"
#include <algorithm>
#include <bit>
#include <cstring>

// Word-level ordering (cheap comparisons): two floats or two whole-byte binaries compare straight from their cells,
// without admitting a Term for either; every other pair takes the full structural order.
namespace clause::runtime::detail {
namespace {
using layout::BoxHeader;

// The bytes of a whole-byte binary's view; none for any other object or a view that is not whole bytes.
std::optional<std::span<const std::byte>> binary_bytes(ProcessHeap &heap, std::span<const Word> words,
                                                       std::size_t offset = 0);

// The bytes of an off-heap binary's view from `offset` bits on its own view: its buffer's bytes in that range.
std::optional<std::span<const std::byte>> refc_bytes(std::span<const Word> words, std::size_t offset,
                                                     std::optional<std::size_t> bits) {
    const auto &cell = *reinterpret_cast<const layout::RefcBinaryCell *>(words.data());
    const auto start = cell.offset_ + offset;
    const auto length = bits.value_or(cell.bits_);
    if (start % 8 != 0 || length % 8 != 0) {
        return std::nullopt;
    }
    return std::span(cell.data_ + start / 8, length / 8);
}

// The bytes a sub-binary names in its original.
std::optional<std::span<const std::byte>> sub_bytes(ProcessHeap &heap, std::span<const Word> words) {
    const auto &cell = *reinterpret_cast<const layout::SubBinaryCell *>(words.data());
    const auto original = TermAccess::boxed_words(heap, cell.original_);
    if (original.empty()) {
        return std::nullopt;
    }
    if (BoxHeader::kind(original[0]) == BoxedKind::refc_binary) {
        return refc_bytes(original, cell.offset_, cell.bits_);
    }
    const auto all = binary_bytes(heap, original);
    if (!all || cell.offset_ % 8 != 0 || cell.bits_ % 8 != 0 || (cell.offset_ + cell.bits_) / 8 > all->size()) {
        return std::nullopt;
    }
    return all->subspan(cell.offset_ / 8, cell.bits_ / 8);
}

std::optional<std::span<const std::byte>> binary_bytes(ProcessHeap &heap, std::span<const Word> words,
                                                       std::size_t offset) {
    switch (BoxHeader::kind(words[0])) {
    case BoxedKind::heap_binary: {
        const auto bits = words[1];
        if (bits % 8 != 0 || offset != 0) {
            return std::nullopt;
        }
        return std::as_bytes(words.subspan(2)).first(bits / 8);
    }
    case BoxedKind::refc_binary:
        return refc_bytes(words, offset, std::nullopt);
    case BoxedKind::sub_binary:
        return sub_bytes(heap, words);
    default:
        return std::nullopt;
    }
}

// The value of a float cell.
double float_of(std::span<const Word> words) {
    const auto &cell = *reinterpret_cast<const layout::FloatCell *>(words.data());
    return std::bit_cast<double>(cell.value_);
}

// -1, 0 or 1 as `a` orders before, with or after `b`.
template <typename T> int order_of(const T &a, const T &b) { return a < b ? -1 : (a > b ? 1 : 0); }

// Bytes in lexicographic order, a prefix first.
int byte_order(std::span<const std::byte> a, std::span<const std::byte> b) {
    const auto common = std::min(a.size(), b.size());
    const auto bytes = common == 0 ? 0 : std::memcmp(a.data(), b.data(), common);
    return bytes != 0 ? order_of(bytes, 0) : order_of(a.size(), b.size());
}

// The order of two cells of the same simple kind; none for any other pair.
std::optional<int> cell_order(ProcessHeap &heap, std::span<const Word> left, std::span<const Word> right) {
    if (BoxHeader::kind(left[0]) == BoxedKind::floating && BoxHeader::kind(right[0]) == BoxedKind::floating) {
        return order_of(float_of(left), float_of(right));
    }
    const auto a = binary_bytes(heap, left);
    const auto b = a ? binary_bytes(heap, right) : std::nullopt;
    return b ? std::optional{byte_order(*a, *b)} : std::nullopt;
}
} // namespace

std::optional<int> word_order(ProcessHeap &heap, Word left, Word right) noexcept {
    if (const auto order = small_order(left, right)) {
        return order;
    }
    constexpr auto boxed = static_cast<Word>(abi::v1::boxed_tag);
    if ((left & abi::v1::primary_mask) != boxed || (right & abi::v1::primary_mask) != boxed) {
        return std::nullopt;
    }
    const auto a = TermAccess::boxed_words(heap, left);
    const auto b = a.empty() ? a : TermAccess::boxed_words(heap, right);
    return b.empty() ? std::nullopt : cell_order(heap, a, b);
}
} // namespace clause::runtime::detail
