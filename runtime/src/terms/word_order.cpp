#include "word_order.hpp"
#include "../memory/heap_object.hpp"
#include "atom_table.hpp"
#include "structural_order.hpp"
#include "term_layout.hpp"
#include <algorithm>
#include <bit>
#include <clause/abi/equality.hpp>
#include <clause/abi/term.hpp>
#include <cmath>
#include <cstring>

// Word-level ordering (cheap comparisons): numbers, atoms, whole-byte binaries and the tuples and lists made of them
// compare straight from their cells, without admitting a Term for any; every other pair takes the structural order.
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

// Nesting of tuples and lists the word-level order follows before leaving a pair to the structural order. Added for
// the lists:sort builtin.
constexpr unsigned MAX_DEPTH = 8;

// The term-order categories word-level order tells apart, in OTP's order (number < atom < reference < tuple < nil <
// list < bitstring); every other kind takes the structural order. Added for the lists:sort builtin.
enum class Rank : std::uint8_t { number = 0, atom = 1, reference = 2, tuple = 6, nil = 9, list = 10, bitstring = 11 };

// The words of the boxed cell a word of a validated term names, its header first (trusted walks); inline, unlike
// TermAccess::trusted_cell, since compound order reads one per field.
std::span<const Word> boxed_cell(Word word) noexcept {
    const auto *words = TermAccess::trusted(word);
    return {words, 1 + BoxHeader::count(words[0])};
}

// The category of a boxed cell's kind.
std::optional<Rank> boxed_rank(BoxedKind kind) noexcept {
    switch (kind) {
    case BoxedKind::tuple:
        return Rank::tuple;
    case BoxedKind::floating:
    case BoxedKind::bignum:
        return Rank::number;
    case BoxedKind::heap_binary:
    case BoxedKind::refc_binary:
    case BoxedKind::sub_binary:
        return Rank::bitstring;
    case BoxedKind::reference:
        return Rank::reference;
    default:
        return std::nullopt;
    }
}

// The number of a local reference's cell, right after its header in either reference layout. Added for reference
// comparisons.
std::uint64_t reference_of(Word word) noexcept {
    std::uint64_t number = 0;
    std::memcpy(&number, TermAccess::trusted(word) + 1, sizeof(number));
    return number;
}

// Whether a word is a small integer.
bool is_small(Word word) noexcept { return abi::v1::NativeIntegerEncoding::decode(word).has_value(); }

// The category of a word of a validated term (trusted walks).
std::optional<Rank> rank_of(Word word) noexcept {
    if (is_small(word)) {
        return Rank::number;
    }
    if (is_atom_word(word)) {
        return Rank::atom;
    }
    if (word == abi::v1::empty_list) {
        return Rank::nil;
    }
    switch (word & abi::v1::primary_mask) {
    case abi::v1::list_tag:
        return Rank::list;
    case abi::v1::boxed_tag:
        return boxed_rank(BoxHeader::kind(TermAccess::trusted(word)[0]));
    default:
        return std::nullopt;
    }
}

// The value of a small integer word against a float word, exact while the integer converts to a double without
// rounding; none for larger integers or any other word.
std::optional<int> small_float_order(std::int64_t small, Word right) noexcept {
    constexpr std::int64_t exact = std::int64_t{1} << 53;
    if (small < -exact || small > exact || (right & abi::v1::primary_mask) != abi::v1::boxed_tag) {
        return std::nullopt;
    }
    const auto cell = boxed_cell(right);
    if (BoxHeader::kind(cell[0]) != BoxedKind::floating) {
        return std::nullopt;
    }
    return order_of(static_cast<double>(small), float_of(cell));
}

// A small integer word against a float word either way round; none for any other pair.
std::optional<int> small_float(Word left, Word right) noexcept {
    if (const auto small = abi::v1::NativeIntegerEncoding::decode(left)) {
        return small_float_order(*small, right);
    }
    if (const auto small = abi::v1::NativeIntegerEncoding::decode(right)) {
        const auto order = small_float_order(*small, left);
        return order ? std::optional{-*order} : std::nullopt;
    }
    return std::nullopt;
}

// The cell of a bignum word: its header, its sign word (1 when negative) and its magnitude's limbs, least
// significant first; empty for any other word. Added for the lists:sort builtin.
std::span<const Word> bignum_cell(Word word) noexcept {
    if ((word & abi::v1::primary_mask) != abi::v1::boxed_tag) {
        return {};
    }
    const auto cell = boxed_cell(word);
    return BoxHeader::kind(cell[0]) == BoxedKind::bignum ? cell : std::span<const Word>{};
}

// Two bignum magnitudes: more limbs is larger (the top limb is never zero), then limb by limb from the top.
int magnitude_order(std::span<const Word> left, std::span<const Word> right) noexcept {
    if (left.size() != right.size()) {
        return order_of(left.size(), right.size());
    }
    for (auto i = left.size(); i-- > 2;) {
        if (left[i] != right[i]) {
            return order_of(left[i], right[i]);
        }
    }
    return 0;
}

// A small integer against a bignum cell: a bignum lies outside the small range, so its sign alone orders the pair.
// None when the word is not a small integer.
std::optional<int> small_bignum_order(Word small, std::span<const Word> bignum) noexcept {
    if (!is_small(small)) {
        return std::nullopt;
    }
    return bignum[1] != 0 ? 1 : -1;
}

// Two integers of which at least one is a bignum; none for any other pair. Added for the lists:sort builtin.
std::optional<int> bignum_order(Word left, Word right) noexcept {
    const auto a = bignum_cell(left);
    const auto b = bignum_cell(right);
    if (a.empty() && b.empty()) {
        return std::nullopt;
    }
    if (a.empty()) {
        return small_bignum_order(left, b);
    }
    if (b.empty()) {
        const auto order = small_bignum_order(right, a);
        return order ? std::optional{-*order} : std::nullopt;
    }
    if (a[1] != b[1]) {
        return a[1] != 0 ? -1 : 1;
    }
    const auto order = magnitude_order(a, b);
    return a[1] != 0 ? -order : order;
}

// A float's value against a bignum cell when the float's binary exponent alone separates their magnitudes; none
// when both have the same bit length. Erlang floats are finite. Added for the lists:sort builtin.
std::optional<int> float_bignum_order(double value, std::span<const Word> bignum) noexcept {
    const int sign = bignum[1] != 0 ? -1 : 1;
    if ((value < 0) != (sign < 0)) {
        return value < 0 ? -1 : 1;
    }
    int exponent = 0;
    static_cast<void>(std::frexp(value, &exponent));
    // |value| lies in [2^(exponent - 1), 2^exponent) and the bignum's magnitude in [2^(bits - 1), 2^bits).
    const auto bits = static_cast<int>((bignum.size() - 3) * sizeof(Word) * 8 + std::bit_width(bignum.back()));
    if (exponent == bits) {
        return std::nullopt;
    }
    return exponent < bits ? -sign : sign;
}

// A float against a bignum either way round; none for any other pair.
std::optional<int> float_bignum(Word left, Word right) noexcept {
    const auto a = boxed_cell(left);
    const auto b = boxed_cell(right);
    if (BoxHeader::kind(a[0]) == BoxedKind::floating && BoxHeader::kind(b[0]) == BoxedKind::bignum) {
        return float_bignum_order(float_of(a), b);
    }
    if (BoxHeader::kind(a[0]) == BoxedKind::bignum && BoxHeader::kind(b[0]) == BoxedKind::floating) {
        const auto order = float_bignum_order(float_of(b), a);
        return order ? std::optional{-*order} : std::nullopt;
    }
    return std::nullopt;
}

std::optional<int> trusted_order(ProcessHeap &heap, Word left, Word right, unsigned depth) noexcept;

// Two numbers that are not both small integers, so at least one is a cell: two floats, a small integer and a float,
// integers of which one is a bignum, or a float and a bignum. A pair too close for these rules takes the structural
// order.
std::optional<int> numbers(ProcessHeap &heap, Word left, Word right) noexcept {
    if (const auto order = small_float(left, right)) {
        return order;
    }
    if (const auto order = bignum_order(left, right)) {
        return order;
    }
    if (is_small(left) || is_small(right)) {
        return std::nullopt;
    }
    if (const auto order = float_bignum(left, right)) {
        return order;
    }
    return cell_order(heap, boxed_cell(left), boxed_cell(right));
}

// Tuples by arity, then field by field from the first.
std::optional<int> tuples(ProcessHeap &heap, std::span<const Word> left, std::span<const Word> right,
                          unsigned depth) noexcept {
    if (left.size() != right.size()) {
        return order_of(left.size(), right.size());
    }
    for (std::size_t i = 1; i < left.size(); ++i) {
        const auto order = trusted_order(heap, left[i], right[i], depth + 1);
        if (!order || *order != 0) {
            return order;
        }
    }
    return 0;
}

// Lists head first, then tail, walking both spines in a loop; the tails left when one spine ends compare last.
std::optional<int> lists(ProcessHeap &heap, Word left, Word right, unsigned depth) noexcept {
    while ((left & abi::v1::primary_mask) == abi::v1::list_tag &&
           (right & abi::v1::primary_mask) == abi::v1::list_tag) {
        if (left == right) {
            return 0;
        }
        const auto *a = TermAccess::trusted(left);
        const auto *b = TermAccess::trusted(right);
        const auto order = trusted_order(heap, a[0], b[0], depth + 1);
        if (!order || *order != 0) {
            return order;
        }
        left = a[1];
        right = b[1];
    }
    return trusted_order(heap, left, right, depth + 1);
}

// Two words of one category.
std::optional<int> same_rank(ProcessHeap &heap, Rank rank, Word left, Word right, unsigned depth) noexcept {
    switch (rank) {
    case Rank::number:
        return numbers(heap, left, right);
    case Rank::atom:
        return atom_order(left, right);
    case Rank::tuple:
        return tuples(heap, boxed_cell(left), boxed_cell(right), depth);
    case Rank::list:
        return lists(heap, left, right, depth);
    case Rank::bitstring:
        return cell_order(heap, boxed_cell(left), boxed_cell(right));
    case Rank::reference:
        return order_of(reference_of(left), reference_of(right));
    default:
        return 0;
    }
}

// The order of two words inside validated terms, `depth` levels down; none for a pair left to the structural order.
std::optional<int> trusted_order(ProcessHeap &heap, Word left, Word right, unsigned depth) noexcept {
    if (left == right) {
        return 0;
    }
    if (const auto order = small_order(left, right)) {
        return order;
    }
    const auto a = rank_of(left);
    const auto b = a && depth <= MAX_DEPTH ? rank_of(right) : std::nullopt;
    if (!b) {
        return std::nullopt;
    }
    return *a == *b ? same_rank(heap, *a, left, right, depth) : order_of(*a, *b);
}

// Whether the cell a word names, if any, is in this process's heap or literal areas.
bool readable(ProcessHeap &heap, Word word) noexcept {
    switch (word & abi::v1::primary_mask) {
    case abi::v1::boxed_tag:
        return !TermAccess::boxed_words(heap, word).empty();
    case abi::v1::list_tag:
        return !TermAccess::cons_words(heap, word).empty();
    default:
        return true;
    }
}
} // namespace

std::optional<int> atom_order(Word left, Word right) noexcept {
    if (!is_atom_word(left) || !is_atom_word(right)) {
        return std::nullopt;
    }
    if (left == right) {
        return 0;
    }
    const auto a = atom_table::spelling(left);
    const auto b = a ? atom_table::spelling(right) : std::nullopt;
    if (!b) {
        return std::nullopt;
    }
    // Distinct atoms have distinct spellings.
    return *a < *b ? -1 : 1;
}

std::optional<int> word_order(ProcessHeap &heap, Word left, Word right) noexcept {
    if (const auto order = small_order(left, right)) {
        return order;
    }
    if (!readable(heap, left) || !readable(heap, right)) {
        return std::nullopt;
    }
    return trusted_order(heap, left, right, 0);
}

std::optional<int> trusted_word_order(ProcessHeap &heap, Word left, Word right) noexcept {
    return trusted_order(heap, left, right, 0);
}

std::optional<bool> word_exact(ProcessHeap &heap, Word left, Word right) noexcept {
    // Immediates keep their admission checks; cells are checked against this process's areas below.
    const auto cell = [](Word word) {
        const auto primary = word & abi::v1::primary_mask;
        return primary == abi::v1::boxed_tag || primary == abi::v1::list_tag;
    };
    if (!cell(left) || !cell(right)) {
        return std::nullopt;
    }
    const auto order = word_order(heap, left, right);
    if (!order || *order != 0) {
        return order ? std::optional{false} : std::nullopt;
    }
    // Equal under == is equal under =:= unless an integer met a float: certain only for one word or two references.
    return left == right || rank_of(left) == Rank::reference ? std::optional{true} : std::nullopt;
}
} // namespace clause::runtime::detail
