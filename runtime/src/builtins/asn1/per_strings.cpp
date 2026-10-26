#include "per.hpp"
#include <algorithm>
#include <ranges>

// Added for PER (plan 21 step LIB84): BIT STRING values in their three formats and with named bits
// (asn1rtt_per_common's bitstring_from_positions, to_bitstring, adjust_trailing_zeroes and the decoders).
namespace clause::runtime::builtins::asn1 {
namespace {
// A bitstring under construction.
struct Bits {
    std::vector<std::byte> bytes_;
    std::size_t size_ = 0;

    void push(const bool bit) {
        if (size_ % 8 == 0) {
            bytes_.push_back(std::byte{0});
        }
        if (bit) {
            bytes_.back() |= static_cast<std::byte>(0x80U >> (size_ % 8));
        }
        ++size_;
    }
};

// constr_min_size/1: the lower size bound, if any.
std::optional<std::size_t> minimum_size(const SizeRange &size) {
    if (size.form_ == SizeRange::Form::none) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(size.lower_);
}

Bits from_bitstring(const Term &value) {
    if (!value.is_bitstring()) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    return {need(value.bitstring_bytes()), need(value.bit_size())};
}

// bs_drop_trailing_zeroes/1, or adjust_trailing_zeroes/2 down to (or padded up to) `lower` bits. As asn1rtt's,
// dropping the zeroes of an all-zero tail whose size is not a whole number of octets fails with badmatch.
Bits trimmed(Builder &builder, Bits bits, const std::size_t lower) {
    std::size_t size = bits.size_;
    const auto tail = size > lower ? size - lower : 0;
    if (tail % 8 != 0 && std::ranges::none_of(std::views::iota(lower, size),
                                              [&](const std::size_t i) { return bit_at(bits.bytes_, i); })) {
        const auto payload = builder.bitstring(std::vector<std::byte>{std::byte{0}}, tail % 8);
        erlang_error(abi::v1::ErrorReason::badmatch, &payload);
    }
    while (size > lower && !bit_at(bits.bytes_, size - 1)) {
        --size;
    }
    Bits result;
    for (std::size_t i = 0; i < std::max(size, lower); ++i) {
        result.push(i < size && bit_at(bits.bytes_, i));
    }
    return result;
}

// bitstring_from_positions/1,2: ones at the positions, zero-padded to `lower` bits.
Bits from_positions(std::vector<std::int64_t> positions, const std::size_t lower) {
    std::ranges::sort(positions);
    const auto size = positions.empty() ? std::size_t{0} : static_cast<std::size_t>(positions.back()) + 1;
    Bits result;
    for (std::size_t i = 0; i < std::max(size, lower); ++i) {
        result.push(std::ranges::binary_search(positions, static_cast<std::int64_t>(i)));
    }
    return result;
}

// One item of a named bit list: the position of a name, or N of {bit, N}.
std::optional<std::int64_t> position_of(const NodeView &node, const Term &item) {
    for (auto list = node.field(0); list.is_cons(); list = list.tail_unchecked()) {
        const auto pair = list.head_unchecked();
        if (need(pair.tuple_element_unchecked(0).exactly_equal(item))) {
            return pair.tuple_element_unchecked(1).small_integer_unchecked();
        }
    }
    const bool bit = item.is_tuple() && item.tuple_size_unchecked() == 2 && item.tuple_element_unchecked(0).is_atom() &&
                     item.tuple_element_unchecked(0).atom_spelling_unchecked() == "bit" &&
                     item.tuple_element_unchecked(1).is_integer();
    if (!bit) {
        return std::nullopt;
    }
    return item.tuple_element_unchecked(1).small_integer_unchecked();
}

// The generated bit_string_name2pos function: names and {bit, N} to positions; nothing for anything else.
std::optional<std::vector<std::int64_t>> positions_of(const NodeView &node, const Term &value) {
    std::vector<std::int64_t> positions;
    auto rest = value;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto position = position_of(node, rest.head_unchecked());
        if (!position) {
            return std::nullopt;
        }
        positions.push_back(*position);
    }
    if (!rest.is_nil()) {
        return std::nullopt;
    }
    return positions;
}

// {Unused, Binary}: the binary's bits but the unused ones.
Bits compact_value(const Term &value) {
    Bits result;
    const auto bytes = need(value.tuple_element_unchecked(1).binary_bytes());
    const auto unused = static_cast<std::size_t>(value.tuple_element_unchecked(0).small_integer_unchecked());
    for (std::size_t i = 0; i + unused < bytes.size() * 8; ++i) {
        result.push(bit_at(bytes, i));
    }
    return result;
}

// << <<B:1>> || B <- L >>: badarg for anything but a list of integers.
Bits list_value(const Term &value) {
    Bits result;
    auto rest = value;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (!rest.head_unchecked().is_integer()) {
            erlang_error(abi::v1::ErrorReason::badarg);
        }
        result.push(bit_test(integer_of(rest.head_unchecked()), 0));
    }
    if (!rest.is_nil()) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    return result;
}

// to_bitstring/1: {Unused, Binary}, a bitstring, an integer (least significant bit first) or a list of bits.
Bits legacy_value(const Term &value) {
    if (value.is_tuple() && value.tuple_size_unchecked() == 2 && value.tuple_element_unchecked(0).is_integer() &&
        value.tuple_element_unchecked(1).is_binary()) {
        return compact_value(value);
    }
    if (value.is_bitstring()) {
        return from_bitstring(value);
    }
    if (!value.is_integer()) {
        return list_value(value);
    }
    Bits result;
    for (auto rest = integer_of(value); rest > 0; rest >>= 1) {
        result.push((rest & 1) != 0);
    }
    return result;
}

Bits padded(Bits bits, const std::size_t lower) {
    while (bits.size_ < lower) {
        bits.push(false);
    }
    return bits;
}
} // namespace

std::pair<std::vector<std::byte>, std::size_t> PerEncoder::named_bits(const NodeView &node, const Term &value,
                                                                      const SizeRange &size) {
    const auto lower = minimum_size(size);
    const bool legacy = schema_.flag(FLAG_LEGACY_TYPES);
    if (const auto positions = positions_of(node, value)) {
        auto bits = from_positions(*positions, lower.value_or(0));
        return {bits.bytes_, bits.size_};
    }
    const auto bits = legacy ? legacy_value(value) : from_bitstring(value);
    const auto result = trimmed(builder_, bits, lower.value_or(0));
    return {result.bytes_, result.size_};
}

// per_enc_bit_string/4 and per_enc_legacy_bit_string/4.
void PerEncoder::bits_value(const NodeView &node, const Term &value, BitWriter &out) {
    const auto size = size_range(node.field(1));
    if (node.field(0).is_cons()) {
        const auto [bytes, count] = named_bits(node, value, size);
        put_string(out, bytes, count, 1, size, StringKind::bits);
        return;
    }
    auto bits = schema_.flag(FLAG_LEGACY_TYPES) ? padded(legacy_value(value), minimum_size(size).value_or(0))
                                                : from_bitstring(value);
    put_string(out, bits.bytes_, bits.size_, 1, size, StringKind::bits);
}

namespace {
// The name of bit `position`, or {bit, Position}.
Term bit_name(Builder &builder, const Term &named, const std::size_t position) {
    for (auto list = named; list.is_cons(); list = list.tail_unchecked()) {
        const auto pair = list.head_unchecked();
        if (pair.tuple_element_unchecked(1).small_integer_unchecked() == static_cast<std::int64_t>(position)) {
            return pair.tuple_element_unchecked(0);
        }
    }
    return builder.tuple({builder.atom("bit"), builder.integer(static_cast<std::int64_t>(position))});
}
} // namespace

// decode_named_bit_string/2: the names of the bits set, {bit, N} for unnamed ones.
Term PerDecoder::named_bit_list(const NodeView &node, const std::span<const std::byte> bytes, const std::size_t bits) {
    std::vector<Term> items;
    for (std::size_t i = 0; i < bits; ++i) {
        if (bit_at(bytes, i)) {
            items.push_back(bit_name(builder_, node.field(0), i));
        }
    }
    return builder_.list(items);
}

// decode_named_bit_string/2, decode_compact_bit_string/1, decode_legacy_bit_string/1 or the bitstring.
Term PerDecoder::bits_value(const NodeView &node, std::vector<std::byte> bytes, const std::size_t bits) {
    std::vector<Term> items;
    if (node.field(0).is_cons()) {
        return named_bit_list(node, bytes, bits);
    }
    if (schema_.flag(FLAG_COMPACT_BIT_STRING)) {
        const auto unused = static_cast<std::int64_t>((8 - bits % 8) % 8);
        return builder_.tuple({builder_.integer(unused), builder_.binary(bytes)});
    }
    if (schema_.flag(FLAG_LEGACY_BIT_STRING)) {
        for (std::size_t i = 0; i < bits; ++i) {
            items.push_back(builder_.integer(bit_at(bytes, i) ? 1 : 0));
        }
        return builder_.list(items);
    }
    return builder_.bitstring(bytes, bits);
}
} // namespace clause::runtime::builtins::asn1
