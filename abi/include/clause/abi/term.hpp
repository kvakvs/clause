#pragma once
#include "v1.hpp"
#include <cstdint>
#include <expected>
#include <type_traits>

namespace clause::abi::v1 {
enum class IntegerError : std::uint8_t { out_of_range, wrong_tag };

// Select the emitted target width explicitly, independently of this C++ compiler's pointers.
template <unsigned Bits>
    requires(Bits == 32 || Bits == 64)
struct IntegerEncoding {
    using Word = std::conditional_t<Bits == 32, std::uint32_t, std::uint64_t>;
    // Four low bits identify an immediate integer; the remaining payload is signed.
    static constexpr unsigned tag_bits = small_integer_bits;
    static constexpr Word tag = small_integer_tag;
    static constexpr Word payload_mask = static_cast<Word>(~Word{0}) >> tag_bits;
    // Keep checked bounds in int64_t so both target widths accept the same exact input type.
    static constexpr std::int64_t minimum = -(std::int64_t{1} << (Bits - tag_bits - 1));
    static constexpr std::int64_t maximum = -minimum - 1;

    // Reject overflow before shifting an unsigned value; no signed shift or wrapping source semantics.
    static constexpr std::expected<Word, IntegerError> encode(std::int64_t value) noexcept {
        if (value < minimum || value > maximum) {
            return std::unexpected(IntegerError::out_of_range);
        }
        return static_cast<Word>((static_cast<Word>(value) << tag_bits) | tag);
    }

    // Reject other term kinds and reconstruct negatives without out-of-range unsigned-to-signed casts.
    static constexpr std::expected<std::int64_t, IntegerError> decode(Word value) noexcept {
        if ((value & tag) != tag) {
            return std::unexpected(IntegerError::wrong_tag);
        }
        const Word payload = value >> tag_bits;
        if (payload <= static_cast<Word>(maximum)) {
            return static_cast<std::int64_t>(payload);
        }
        return -1 - static_cast<std::int64_t>(payload_mask - payload);
    }
};

// Native runtime consumers share the contract; cross compilers must select 32 or 64 explicitly.
using NativeIntegerEncoding = IntegerEncoding<sizeof(TermWord) * 8>;

// Primary tags of list and boxed words, and the header layout generated code reads once inference or a dominating
// shape test proved a tuple or cons cell (docs/runtime-heap.md#word-layout).
inline constexpr unsigned list_tag = 0x1;
inline constexpr unsigned boxed_tag = 0x2;
// A header holds its kind from bit 2 and the count of following words from this bit; a tuple's kind is 0.
inline constexpr unsigned header_count_shift = 7;
// Header kinds generated code tests inline (OPT0.2B); the runtime's BoxedKind asserts them.
inline constexpr unsigned header_kind_shift = 2;
inline constexpr unsigned header_kind_mask = 0x1f;
enum class HeaderKind : std::uint8_t {
    tuple = 0,
    native_record = 1,
    bignum = 2,
    reference = 4,
    fun_closure = 5,
    floating = 6,
    refc_binary = 8,
    heap_binary = 9,
    sub_binary = 10,
    map = 15,
    tree_map = 18
};
// Words of binary cells generated code reads (OPT1.4): an inline (heap) binary's bit length and the word its data
// bytes start at; an off-heap view's bit offset into its buffer, its bit length and the buffer's first byte.
inline constexpr unsigned heap_binary_bits_word = 1;
inline constexpr unsigned heap_binary_data_word = 2;
inline constexpr unsigned refc_offset_word = 1;
inline constexpr unsigned refc_bits_word = 2;
inline constexpr unsigned refc_data_word = 3;
// The bits in use of an appendable buffer and its capacity in bits, set only in the cell of the process that may
// append to it in place; zero in every other cell (OPT0.11B).
inline constexpr unsigned refc_append_end_word = 4;
inline constexpr unsigned refc_append_limit_word = 5;
// A sub-binary (OPT1.4B): a bit range of an inline or off-heap binary, the original's word last.
inline constexpr unsigned sub_offset_word = 1;
inline constexpr unsigned sub_bits_word = 2;
inline constexpr unsigned sub_original_word = 3;
// Low four bits of local pids and ports (OPT0.2B).
inline constexpr unsigned immediate_tag_mask = 0xf;
inline constexpr unsigned pid_tag = 0x3;
inline constexpr unsigned port_tag = 0x7;
} // namespace clause::abi::v1
