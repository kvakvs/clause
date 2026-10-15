#pragma once

// Private target-runtime cell layouts (docs/runtime-heap.md), not an allocator, public ABI or wire format.
// Trailing storage starts after each fixed prefix; every cell moves by copying words except RefcBinaryCell.
#include "terms.hpp"
#include <array>
#include <boost/multiprecision/cpp_int.hpp>
#include <clause/abi/term.hpp>
#include <clause/runtime/base_types.hpp>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <vector>

namespace clause::runtime {
struct RecordDefinition;
struct FunDefinition;
} // namespace clause::runtime

namespace clause::runtime::detail {
struct MapNode;
} // namespace clause::runtime::detail

namespace clause::runtime::detail::layout {
using Bignum = boost::multiprecision::cpp_int;

struct alignas(Word) BoxHeader final {
    // Reserve low primary tag 00, then five kind bits, then the count of words AFTER this header.
    static constexpr unsigned BOXED_KIND_BITS = 5;
    static constexpr unsigned CONTENT_SHIFT = 2 + BOXED_KIND_BITS;
    // The largest count of following words: 2^25 - 1 on 32-bit targets, 2^57 - 1 on 64-bit ones. Every producer's
    // size limit stays within it (static assertions beside the tuple, bignum and map producers).
    static constexpr std::size_t MAX_COUNT = static_cast<std::size_t>(~Word{0} >> CONTENT_SHIFT);
    // Future checked heap constructors encode this word; bitfields and union type-punning are forbidden.
    Word value_;

    // Encode a header for kind followed by count words.
    static constexpr Word make(BoxedKind kind, std::size_t count) noexcept {
        return (static_cast<Word>(count) << CONTENT_SHIFT) | (static_cast<Word>(kind) << 2);
    }

    // Decode the kind bits of a header word.
    static constexpr BoxedKind kind(Word header) noexcept {
        return static_cast<BoxedKind>((header >> 2) & ((1U << BOXED_KIND_BITS) - 1));
    }

    // Decode the number of words following a header word.
    static constexpr std::size_t count(Word header) noexcept {
        return static_cast<std::size_t>(header >> CONTENT_SHIFT);
    }
};

// Canonical magnitude limbs follow this private prefix in least-significant-word order.
struct alignas(Word) BignumCell final {
    // Identify the untraced sign word and following immutable native-word limbs.
    BoxHeader header_;
    // Zero denotes positive and one denotes negative; zero itself always uses a small integer.
    Word negative_;
};

struct alignas(Word) FloatCell final {
    // Identify untraced IEEE binary64 bytes without depending on the target's double alignment.
    BoxHeader header_;
    std::array<std::byte, 8> value_;
};

struct alignas(Word) ReferenceCell final {
    // Identify an untraced reference number (docs/terms.md#pids-and-references), unique within the program run.
    BoxHeader header_;
    std::array<std::byte, 8> number_;
};

struct alignas(Word) RemoteIdentityCell final {
    // Distinguish identity kinds; the registry key is not a pointer for the future GC.
    BoxHeader header_;
    Word identity_id_;
    // Trace the remote host atom independently of the registry key.
    Word remote_host_;
};

// Cons cells have no header: the list primary tag points to exactly two traceable terms.
struct alignas(Word) ConsCell final {
    // Trace the element and the arbitrary (possibly improper) tail independently.
    Word head_;
    Word tail_;
};

struct alignas(Word) TupleCell final {
    // Trailing storage contains arity Term slots; an empty tuple is reserved as an immediate.
    BoxHeader header_;
};

struct KeyValuePair final {
    // Trace both map-key and mapped-value slots.
    Word key;
    Word value;
};

struct alignas(Word) MapCell final {
    // Trailing storage contains KeyValuePair entries; the word count includes both slots per entry.
    BoxHeader header_;
};

// Words after the header of a float cell on a target with word_bytes-wide words.
constexpr std::size_t float_payload_words(std::size_t word_bytes) noexcept { return 8 / word_bytes; }

// Words after the header of a reference cell on a target with word_bytes-wide words.
constexpr std::size_t reference_payload_words(std::size_t word_bytes) noexcept { return 8 / word_bytes; }

// Words after the header of an off-heap binary: offset, bits, data, append end and limit, two-pointer shared_ptr,
// list link.
constexpr std::size_t refc_payload_words(std::size_t) noexcept { return 8; }

// Words after the header of an off-heap map stub: size, tree root, list link (OPT0.5A).
constexpr std::size_t tree_map_payload_words(std::size_t) noexcept { return 3; }

// Words after the header of a heap binary holding bits: the bit length, then data rounded up to words.
constexpr std::size_t heap_binary_payload_words(std::size_t bits, std::size_t word_bytes) noexcept {
    return 1 + ((bits + 7) / 8 + word_bytes - 1) / word_bytes;
}

// Whether a header starts a cell of the off-heap list: an off-heap binary, or a handle reference (step 41N,
// docs/terms.md#handle-references), a reference cell laid out as RefcBinaryCell whose number fills offset_ and bits_.
constexpr bool off_heap_cell(Word header) noexcept {
    return BoxHeader::kind(header) == BoxedKind::refc_binary ||
           (BoxHeader::kind(header) == BoxedKind::reference &&
            BoxHeader::count(header) == refc_payload_words(sizeof(Word)));
}

static_assert(float_payload_words(4) == 2 && float_payload_words(8) == 1);
static_assert(heap_binary_payload_words(512, 4) == 17 && heap_binary_payload_words(512, 8) == 9);
static_assert(heap_binary_payload_words(0, 4) == 1 && heap_binary_payload_words(9, 8) == 2);
static_assert(refc_payload_words(4) == 8 && refc_payload_words(8) == 8);

// Packed MSB-first binary data shared by off-heap cells; immutable once published.
using BinaryBuffer = std::vector<std::byte>;

// Binaries up to this many bytes live inline on the heap; larger ones use a shared off-heap buffer.
inline constexpr std::size_t heap_binary_bytes = HEAP_BINARY_THRESHOLD_WORDS * sizeof(Word);

struct alignas(Word) SubBinaryCell final {
    // A bit range of another binary (OPT1.4B): its offset into the original's bits and its length; the original, an
    // inline or off-heap binary (never another sub-binary), is the one traced word.
    BoxHeader header_;
    Word offset_;
    Word bits_;
    Word original_;
};

struct alignas(Word) HeapBinaryCell final {
    // Identify untraced trailing data words; the header count is 1 plus the rounded-up data words.
    BoxHeader header_;
    // Exact logical length in bits; data bytes follow, zero padded after the last valid bit.
    Word bits_;
};

struct alignas(Word) RefcBinaryCell final {
    // Identify an off-heap binary view (BEAM ProcBin); the header count is 8 on every word width.
    BoxHeader header_;
    // Select this view's bits within the shared buffer.
    Word offset_;
    Word bits_;
    // The buffer's first byte, which generated code reads matches from (OPT1.4); buffers never move or resize.
    const std::byte *data_ = nullptr;
    // The bits in use of the buffer and its capacity in bits when this process may append to it in place, which
    // only the buffer's first cell does (OPT0.11B); both zero otherwise, and in every copy to another heap.
    Word append_end_ = 0;
    Word append_limit_ = 0;
    // Hold one reference to the buffer; only the off-heap list sweep, teardown or relocation touches it.
    std::shared_ptr<const BinaryBuffer> buffer_;
    // Link the owning process's off-heap list, newest cell first; null ends the list.
    RefcBinaryCell *next_;
};

struct alignas(Word) TreeMapCell final {
    // Identify an off-heap map (OPT0.5A); the header count is 3 on every word width.
    BoxHeader header_;
    // Entries of the map.
    Word size_;
    // One reference to the root of its tree (map_tree.hpp); only builders, sweeps and teardown touch it.
    MapNode *root_;
    // Link the owning process's off-heap map list, newest stub first; null ends the list.
    TreeMapCell *next_;
};

struct alignas(Word) FunCell final {
    // The header count is 1 plus the captured value count; the captured values follow (docs/funs.md).
    BoxHeader header_;
    // Untraced address of the runtime's definition the fun was created from.
    const FunDefinition *definition_;
};

struct alignas(Word) NativeRecordCell final {
    // The header count is 1 plus the field count; the field values follow in definition order.
    BoxHeader header_;
    // Untraced address of the runtime's definition captured at construction (docs/native-records.md).
    const RecordDefinition *definition_;
};

static_assert(static_cast<unsigned>(BoxedKind::empty_list) < (1U << BoxHeader::BOXED_KIND_BITS));
static_assert(BoxHeader::kind(BoxHeader::make(BoxedKind::refc_binary, 5)) == BoxedKind::refc_binary);
static_assert(BoxHeader::count(BoxHeader::make(BoxedKind::map, 6)) == 6);
// Generated code reads tuple headers and cons cells inline after a proof (abi/term.hpp).
static_assert(BoxHeader::CONTENT_SHIFT == abi::v1::header_count_shift);
// The header kinds and identity tags generated code tests inline (OPT0.2B).
static_assert(BoxHeader::CONTENT_SHIFT - BoxHeader::BOXED_KIND_BITS == abi::v1::header_kind_shift &&
              (Word{1} << BoxHeader::BOXED_KIND_BITS) - 1 == abi::v1::header_kind_mask);
static_assert([] {
    using abi::v1::HeaderKind;
    const auto same = [](BoxedKind runtime, HeaderKind abi) {
        return static_cast<unsigned>(runtime) == static_cast<unsigned>(abi);
    };
    return same(BoxedKind::tuple, HeaderKind::tuple) && same(BoxedKind::native_record, HeaderKind::native_record) &&
           same(BoxedKind::bignum, HeaderKind::bignum) && same(BoxedKind::reference, HeaderKind::reference) &&
           same(BoxedKind::fun_closure, HeaderKind::fun_closure) && same(BoxedKind::floating, HeaderKind::floating) &&
           same(BoxedKind::refc_binary, HeaderKind::refc_binary) &&
           same(BoxedKind::heap_binary, HeaderKind::heap_binary) &&
           same(BoxedKind::sub_binary, HeaderKind::sub_binary) && same(BoxedKind::map, HeaderKind::map) &&
           same(BoxedKind::tree_map, HeaderKind::tree_map);
}());
static_assert(BoxHeader::make(BoxedKind::tuple, 3) == Word{3} << abi::v1::header_count_shift);
static_assert(static_cast<unsigned>(TermKindPrimary::list) == abi::v1::list_tag &&
              static_cast<unsigned>(TermKindPrimary::boxed) == abi::v1::boxed_tag);
static_assert(sizeof(BoxHeader) == sizeof(Word));
static_assert(alignof(BoxHeader) == alignof(Word));
static_assert(sizeof(ConsCell) == 2 * sizeof(Word));
static_assert(offsetof(ConsCell, tail_) == sizeof(Word));
static_assert(sizeof(TupleCell) == sizeof(Word));
static_assert(sizeof(MapCell) == sizeof(Word));
static_assert(sizeof(KeyValuePair) == 2 * sizeof(Word));
static_assert(sizeof(FloatCell) == sizeof(Word) + 8);
static_assert(offsetof(FloatCell, value_) == sizeof(Word));
static_assert(sizeof(RemoteIdentityCell) == 3 * sizeof(Word));
static_assert(sizeof(HeapBinaryCell) == 2 * sizeof(Word));
static_assert(sizeof(NativeRecordCell) == 2 * sizeof(Word));
static_assert(sizeof(BignumCell) % sizeof(Word) == 0);
static_assert(alignof(BignumCell) >= alignof(Word));
static_assert(sizeof(FunCell) == 2 * sizeof(Word));
// The shared_ptr is the only C++ member a cell may hold; it is two pointers on every supported STL.
static_assert(sizeof(std::shared_ptr<const BinaryBuffer>) == 2 * sizeof(Word));
static_assert(alignof(std::shared_ptr<const BinaryBuffer>) <= alignof(Word));
static_assert(sizeof(RefcBinaryCell) == (1 + refc_payload_words(sizeof(Word))) * sizeof(Word));
// Generated code reads binary lengths and data inline (OPT1.4, abi/term.hpp).
static_assert(offsetof(RefcBinaryCell, offset_) == abi::v1::refc_offset_word * sizeof(Word) &&
              offsetof(RefcBinaryCell, bits_) == abi::v1::refc_bits_word * sizeof(Word) &&
              offsetof(RefcBinaryCell, data_) == abi::v1::refc_data_word * sizeof(Word) &&
              offsetof(RefcBinaryCell, append_end_) == abi::v1::refc_append_end_word * sizeof(Word) &&
              offsetof(RefcBinaryCell, append_limit_) == abi::v1::refc_append_limit_word * sizeof(Word));
static_assert(offsetof(SubBinaryCell, offset_) == abi::v1::sub_offset_word * sizeof(Word) &&
              offsetof(SubBinaryCell, bits_) == abi::v1::sub_bits_word * sizeof(Word) &&
              offsetof(SubBinaryCell, original_) == abi::v1::sub_original_word * sizeof(Word));
static_assert(offsetof(HeapBinaryCell, bits_) == abi::v1::heap_binary_bits_word * sizeof(Word) &&
              sizeof(HeapBinaryCell) == abi::v1::heap_binary_data_word * sizeof(Word));
// A handle reference keeps its number where a reference cell does (step 41N).
static_assert(offsetof(RefcBinaryCell, offset_) == offsetof(ReferenceCell, number_) &&
              offsetof(RefcBinaryCell, buffer_) >= offsetof(ReferenceCell, number_) + 8);
static_assert(sizeof(FloatCell) == (1 + float_payload_words(sizeof(Word))) * sizeof(Word));
static_assert(sizeof(TreeMapCell) == (1 + tree_map_payload_words(sizeof(Word))) * sizeof(Word));
static_assert(!std::is_trivially_copyable_v<RefcBinaryCell>);
static_assert(std::is_nothrow_move_constructible_v<RefcBinaryCell>);
// Every other cell moves by copying its words.
static_assert(std::is_trivially_copyable_v<BoxHeader> && std::is_trivially_copyable_v<BignumCell> &&
              std::is_trivially_copyable_v<FloatCell> && std::is_trivially_copyable_v<RemoteIdentityCell> &&
              std::is_trivially_copyable_v<ConsCell> && std::is_trivially_copyable_v<TupleCell> &&
              std::is_trivially_copyable_v<KeyValuePair> && std::is_trivially_copyable_v<MapCell> &&
              std::is_trivially_copyable_v<HeapBinaryCell> && std::is_trivially_copyable_v<FunCell> &&
              std::is_trivially_copyable_v<NativeRecordCell> && std::is_trivially_copyable_v<TreeMapCell>);
} // namespace clause::runtime::detail::layout
