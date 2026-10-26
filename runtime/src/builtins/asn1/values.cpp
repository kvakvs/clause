#include "codec.hpp"
#include <algorithm>

// Added for ASN.1 sources (plan 21 step LIB84): INTEGER and REAL contents (asn1rtt_ber, asn1rtt_real_common) and the
// DER comparisons with DEFAULT values (asn1rtt_check).
namespace clause::runtime::builtins::asn1 {
namespace {
bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

Term make_integer(Builder &builder, const detail::Integer &value) {
    return need(builder.factory().integer_decimal(detail::integer_text(value)));
}

// The magnitude's bytes, most significant first, without leading zeros (real_mininum_octets/1).
std::vector<std::byte> minimum_octets(detail::Integer value) {
    std::vector<std::byte> bytes;
    for (; value > 0; value >>= 8) {
        bytes.insert(bytes.begin(), static_cast<std::byte>(static_cast<unsigned>(value & 0xFF)));
    }
    return bytes;
}
} // namespace

detail::Integer integer_of(const Term &term) { return need(detail::integer_read(term)); }

// <<Int:Len/signed-unit:8>>; an empty value is 0.
Term integer_from_bytes(Builder &builder, const std::span<const std::byte> bytes) {
    detail::Integer value = 0;
    for (const auto byte : bytes) {
        value = (value << 8) | static_cast<unsigned>(byte);
    }
    if (!bytes.empty() && (static_cast<unsigned>(bytes.front()) & 0x80U) != 0) {
        value -= detail::Integer(1) << (8 * bytes.size());
    }
    return make_integer(builder, value);
}

// encode_integer_pos/2 and encode_integer_neg/2: the shortest two's complement bytes.
std::vector<std::byte> integer_bytes(const detail::Integer &value) {
    std::vector<std::byte> bytes;
    auto rest = value;
    for (;;) {
        const auto low = static_cast<unsigned>(static_cast<std::int64_t>(rest & 0xFF) & 0xFF);
        bytes.insert(bytes.begin(), static_cast<std::byte>(low));
        rest >>= 8;
        const bool done = value >= 0 ? (rest == 0 && low < 128) : (rest == -1 && low > 127);
        if (done) {
            return bytes;
        }
    }
}

namespace {
// decode_integer2/3 of a REAL exponent: `length` two's complement bytes.
detail::Integer signed_bytes(const std::span<const std::byte> bytes) {
    detail::Integer value = 0;
    for (const auto byte : bytes) {
        value = (value << 8) | static_cast<unsigned>(byte);
    }
    if (!bytes.empty() && (static_cast<unsigned>(bytes.front()) & 0x80U) != 0) {
        value -= detail::Integer(1) << (8 * bytes.size());
    }
    return value;
}

[[noreturn]] void bad_real(Builder &builder, const std::span<const std::byte> bytes) {
    const auto term = builder.binary(bytes);
    erlang_error(abi::v1::ErrorReason::badmatch, &term);
}
} // namespace

namespace {
// The binary form: the exponent's length code (or a length byte), the exponent, then the mantissa's magnitude.
Term binary_real_term(Builder &builder, const std::span<const std::byte> bytes) {
    const auto first = static_cast<unsigned>(bytes.front());
    auto rest = bytes.subspan(1);
    std::size_t exponent_length = (first & 3U) + 1;
    if ((first & 3U) == 3) {
        if (rest.empty()) {
            bad_real(builder, bytes);
        }
        exponent_length = static_cast<std::size_t>(rest.front());
        rest = rest.subspan(1);
    }
    if (rest.size() < exponent_length) {
        bad_real(builder, bytes);
    }
    const auto exponent = signed_bytes(rest.first(exponent_length));
    detail::Integer mantissa = 0;
    for (const auto byte : rest.subspan(exponent_length)) {
        mantissa = (mantissa << 8) | static_cast<unsigned>(byte);
    }
    if ((first & 0x40U) != 0) {
        mantissa = -mantissa;
    }
    return builder.tuple({make_integer(builder, mantissa), builder.integer(2), make_integer(builder, exponent)});
}
} // namespace

// decode_real/1: 0, the infinities, a decimal string (NR1, NR2, NR3) as a list, or {Mantissa, 2, Exponent}.
Term decode_real(Builder &builder, const std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return builder.integer(0);
    }
    const auto first = static_cast<unsigned>(bytes.front());
    if (first == 0x40U || first == 0x41U) {
        return builder.atom(first == 0x40U ? "PLUS-INFINITY" : "MINUS-INFINITY");
    }
    if (first >= 1 && first <= 3) {
        return builder.string(bytes.subspan(1));
    }
    if (((first >> 4U) & 3U) != 0) {
        asn1_exit(builder, builder.tuple({builder.atom("non_supported_base"), builder.integer((first >> 4U) & 3U)}));
    }
    return binary_real_term(builder, bytes);
}

namespace {
// truncate_zeros/1: the mantissa without trailing zero bits, and how many there were (DER).
std::pair<detail::Integer, detail::Integer> truncate_zeros(detail::Integer mantissa) {
    detail::Integer shift = 0;
    while (mantissa != 0 && (mantissa & 0xF) == (mantissa & 0xE)) {
        mantissa >>= 1;
        ++shift;
    }
    return {mantissa, shift};
}

// A base 2 REAL value: Mantissa * 2^Exponent.
struct BinaryReal {
    detail::Integer mantissa_;
    detail::Integer exponent_;
};

std::vector<std::byte> binary_real(Builder &builder, const BinaryReal &real) {
    const auto [mantissa, shift] = truncate_zeros(real.mantissa_);
    const auto exponent_bytes = integer_bytes(real.exponent_ + shift);
    if (exponent_bytes.size() > 255) {
        asn1_exit(builder, builder.tuple({builder.atom("to_big_exp_in_encode_real"),
                                          builder.integer(static_cast<std::int64_t>(exponent_bytes.size()))}));
    }
    const auto length_code = std::min<std::size_t>(exponent_bytes.size() - 1, 3);
    std::vector<std::byte> out{static_cast<std::byte>(0x80U | (mantissa > 0 ? 0U : 0x40U) | length_code)};
    if (length_code == 3) {
        out.push_back(static_cast<std::byte>(exponent_bytes.size()));
    }
    out.insert(out.end(), exponent_bytes.begin(), exponent_bytes.end());
    const auto magnitude = minimum_octets(mantissa > 0 ? mantissa : detail::Integer(-mantissa));
    out.insert(out.end(), magnitude.begin(), magnitude.end());
    return out;
}

std::string without_trailing_zeros(const std::string &digits) {
    const auto end = digits.find_last_not_of('0');
    return end == std::string::npos ? "0" : digits.substr(0, end + 1);
}

// encode_real_as_string/2: NR3 with trailing zeros moved into the exponent.
std::vector<std::byte> decimal_real(const std::string &mantissa, const detail::Integer &exponent) {
    const auto truncated = without_trailing_zeros(mantissa);
    const auto shifted = exponent + static_cast<std::int64_t>(mantissa.size() - truncated.size());
    const auto exponent_text = shifted == 0 ? std::string("+0") : detail::integer_text(shifted);
    const auto text = std::string(1, '\3') + truncated + ".E" + exponent_text;
    std::vector<std::byte> out;
    for (const char c : text) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}
} // namespace

namespace {
// {Mantissa, Base, Exponent} with integers and base 2 or 10.
std::vector<std::byte> triple_real(Builder &builder, const Term &value) {
    const auto mantissa = value.tuple_element_unchecked(0);
    const auto base = value.tuple_element_unchecked(1);
    const auto exponent = value.tuple_element_unchecked(2);
    const bool integers = mantissa.is_integer() && base.is_integer() && exponent.is_integer();
    if (integers && integer_of(base) == 2) {
        return binary_real(builder, {integer_of(mantissa), integer_of(exponent)});
    }
    if (integers && integer_of(base) == 10) {
        return decimal_real(detail::integer_text(integer_of(mantissa)), integer_of(exponent));
    }
    asn1_exit(builder, builder.tuple({builder.atom("encode_real_non_supported_encoding"), base}));
}
} // namespace

// encode_real/1 of 0, the infinities and {Mantissa, Base, Exponent} with base 2 or 10.
std::vector<std::byte> encode_real(Builder &builder, const Term &value) {
    if (value.is_integer() && integer_of(value) == 0) {
        return {};
    }
    if (atom_is(value, "PLUS-INFINITY") || atom_is(value, "MINUS-INFINITY")) {
        return {static_cast<std::byte>(atom_is(value, "PLUS-INFINITY") ? 0x40 : 0x41)};
    }
    if (!value.is_tuple() || value.tuple_size_unchecked() != 3) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    return triple_real(builder, value);
}

namespace {
std::vector<Term> sorted_names(const Term &list) {
    std::vector<Term> names;
    for (auto rest = list; rest.is_cons(); rest = rest.tail_unchecked()) {
        names.push_back(rest.head_unchecked());
    }
    std::ranges::sort(names, [](const Term &a, const Term &b) {
        return a.is_atom() && b.is_atom() && a.atom_spelling_unchecked() < b.atom_spelling_unchecked();
    });
    return names;
}

// The default's bits, or those followed by zero bits.
bool same_bits(const Term &value, const Term &fallback) {
    const auto size = need(fallback.bit_size());
    const auto bits = need(value.bit_size());
    if (bits < size) {
        return false;
    }
    const auto left = need(value.bitstring_bytes());
    const auto right = need(fallback.bitstring_bytes());
    for (std::size_t i = 0; i < bits; ++i) {
        const bool expected = i < size && bit_at(right, i);
        if (bit_at(left, i) != expected) {
            return false;
        }
    }
    return true;
}

// check_named_bitstring/3,4: a list of names as the default's in any order, or a bitstring as same_bits/2.
bool named_bits_default(const Term &value, const Term &fallback) {
    if (fallback.is_list() && value.is_list()) {
        return std::ranges::equal(sorted_names(value), sorted_names(fallback),
                                  [](const Term &a, const Term &b) { return need(a.exactly_equal(b)); });
    }
    return value.is_bitstring() && fallback.is_bitstring() && same_bits(value, fallback);
}
} // namespace

// asn1ct_gen_check's is_default functions: literal equality, except for named INTEGERs (a name of the default's
// number), named BIT STRINGs (names in any order, trailing zero bits), CHOICE, SEQUENCE and SET (per component) and
// the collections (sorted).
bool Encoder::der_default(const std::int64_t node, const Term &value, const Term &fallback) {
    auto view = schema_.node(node);
    while (view.kind() == Kind::reference) {
        view = schema_.node(view.small_field(0));
    }
    if (view.kind() == Kind::integer && view.field(0).is_cons() && value.is_atom()) {
        for (auto named = view.field(0); named.is_cons(); named = named.tail_unchecked()) {
            const auto pair = named.head_unchecked();
            if (need(pair.tuple_element_unchecked(0).exactly_equal(value))) {
                return need(pair.tuple_element_unchecked(1).exactly_equal(fallback));
            }
        }
        return false;
    }
    if (view.kind() == Kind::bit_string && view.field(0).is_cons()) {
        return named_bits_default(value, fallback);
    }
    return need(value.exactly_equal(fallback));
}
} // namespace clause::runtime::builtins::asn1
