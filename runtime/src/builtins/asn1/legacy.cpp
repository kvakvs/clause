#include "codec.hpp"
#include <algorithm>

// Added for ASN.1 sources (plan 21 step LIB84): BIT STRING values under legacy_erlang_types, as
// asn1rtt_ber:encode_bit_string/4 takes them: a bitstring, {Unused, Binary}, a list of 0s and 1s, an integer (its
// least significant bit first), or named bits, checked against the SIZE constraint.
namespace clause::runtime::builtins::asn1 {
namespace {
SizeLimit size_limit(const Term &bounds) {
    if (!bounds.is_tuple() || bounds.tuple_element_unchecked(2).atom_spelling_unchecked() != "false") {
        return {};
    }
    SizeLimit limit;
    limit.present_ = true;
    const auto lower = bounds.tuple_element_unchecked(0);
    const auto upper = bounds.tuple_element_unchecked(1);
    limit.lower_ = lower.is_integer() ? lower.small_integer_unchecked() : 0;
    if (upper.is_integer()) {
        limit.upper_ = upper.small_integer_unchecked();
    }
    limit.exact_ = limit.upper_ && *limit.upper_ == limit.lower_;
    return limit;
}

// encode_bitstring/1: bits packed most significant first, after the count of unused bits.
void append_bit_list(std::vector<std::byte> &out, const std::vector<int> &bits) {
    out.push_back(static_cast<std::byte>((8 - bits.size() % 8) % 8));
    std::vector<std::byte> bytes((bits.size() + 7) / 8);
    for (std::size_t i = 0; i < bits.size(); ++i) {
        bytes[i / 8] |= static_cast<std::byte>((bits[i] & 0xFF) << (7 - i % 8));
    }
    out.insert(out.end(), bytes.begin(), bytes.end());
}

// The bits of a list of 0s and 1s, or of an integer, least significant bit first.
std::vector<int> bit_list(const Term &value) {
    std::vector<int> bits;
    if (value.is_integer()) {
        for (auto rest = integer_of(value); rest > 0; rest >>= 1) {
            bits.push_back(static_cast<int>(rest & 1));
        }
        return bits;
    }
    for (auto rest = value; rest.is_cons(); rest = rest.tail_unchecked()) {
        bits.push_back(static_cast<int>(rest.head_unchecked().small_integer_unchecked()));
    }
    return bits;
}

// A tuple {Unused, Binary} of the compact bit string format.
bool compact_bits(const Term &value) {
    return value.is_tuple() && value.tuple_size_unchecked() == 2 && value.tuple_element_unchecked(0).is_integer() &&
           value.tuple_element_unchecked(1).is_binary();
}
} // namespace

void Encoder::legacy_bits(const NodeView &node, const Term &value, std::vector<std::byte> &out) {
    const auto limit = size_limit(node.field(1));
    if (value.is_bitstring()) {
        const auto size = need(value.bit_size());
        const auto bytes = need(value.bitstring_bytes());
        legacy_compact(limit, (8 - size % 8) % 8, bytes, out);
        return;
    }
    if (compact_bits(value)) {
        const auto unused = static_cast<std::size_t>(value.tuple_element_unchecked(0).small_integer_unchecked());
        legacy_compact(limit, unused, need(value.tuple_element_unchecked(1).binary_bytes()), out);
        return;
    }
    const bool named = value.is_cons() && (value.head_unchecked().is_atom() || value.head_unchecked().is_tuple());
    if (named) {
        legacy_named(node, limit, value, out);
        return;
    }
    legacy_list(limit, bit_list(value), out);
}

// encode_bin_bit_string/4: {Unused, Binary} with the unused bits cleared, its length checked.
void Encoder::legacy_compact(const SizeLimit &limit, const std::size_t unused, std::vector<std::byte> bytes,
                             std::vector<std::byte> &out) {
    const auto length = static_cast<std::int64_t>(bytes.size() * 8) - static_cast<std::int64_t>(unused);
    if (limit.present_ && limit.upper_ && length > *limit.upper_) {
        const char *kind = limit.exact_ ? "should_be" : "maximum";
        asn1_exit(
            builder_,
            builder_.tuple({builder_.atom("bitstring_length"),
                            builder_.tuple({builder_.tuple({builder_.atom("was"), builder_.integer(length)}),
                                            builder_.tuple({builder_.atom(kind), builder_.integer(*limit.upper_)})})}));
    }
    if (bytes.empty()) {
        out.push_back(std::byte{0});
        return;
    }
    if (unused != 0) {
        bytes.back() = static_cast<std::byte>((static_cast<unsigned>(bytes.back()) >> unused) << unused);
    }
    out.push_back(static_cast<std::byte>(unused));
    out.insert(out.end(), bytes.begin(), bytes.end());
}

// encode_bit_string_bits/4: a list of bits, padded to an exact SIZE, checked against a range.
void Encoder::legacy_list(const SizeLimit &limit, std::vector<int> bits, std::vector<std::byte> &out) {
    const auto length = static_cast<std::int64_t>(bits.size());
    const auto fail = [&](const char *kind, const std::int64_t bound) {
        asn1_exit(builder_,
                  builder_.tuple({builder_.atom("bitstring_length"),
                                  builder_.tuple({builder_.tuple({builder_.atom("was"), builder_.integer(length)}),
                                                  builder_.tuple({builder_.atom(kind), builder_.integer(bound)})})}));
    };
    if (limit.present_ && limit.exact_) {
        if (length > limit.lower_) {
            fail("should_be", limit.lower_);
        }
        bits.resize(static_cast<std::size_t>(limit.lower_), 0);
    } else if (limit.present_ && limit.upper_ && length > *limit.upper_) {
        fail("maximum", *limit.upper_);
    } else if (limit.present_ && length < limit.lower_) {
        // asn1rtt_ber reports the maximum here, not the minimum it checked.
        fail("minimum", limit.upper_.value_or(0));
    }
    if (bits.empty()) {
        out.push_back(std::byte{0});
        return;
    }
    append_bit_list(out, bits);
}

// encode_bit_string_named/4: the named bits set in a list as long as the largest position, or the SIZE.
void Encoder::legacy_named(const NodeView &node, const SizeLimit &limit, const Term &value,
                           std::vector<std::byte> &out) {
    std::vector<std::int64_t> positions;
    for (auto rest = value; rest.is_cons(); rest = rest.tail_unchecked()) {
        positions.push_back(named_position(node, rest.head_unchecked()));
    }
    std::ranges::sort(positions);
    auto size = positions.back() + 1;
    if (limit.present_ && limit.upper_) {
        size = *limit.upper_;
    }
    if (positions.back() >= size) {
        asn1_exit(builder_, builder_.atom("bitstring_sizeconstraint"));
    }
    std::vector<int> bits(static_cast<std::size_t>(size), 0);
    for (const auto position : positions) {
        bits[static_cast<std::size_t>(position)] = 1;
    }
    append_bit_list(out, bits);
}
} // namespace clause::runtime::builtins::asn1
