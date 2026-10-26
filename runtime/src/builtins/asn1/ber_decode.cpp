#include "codec.hpp"
#include <array>

// Added for ASN.1 sources (plan 21 step LIB84): BER decoding of simple types as asn1rtt_ber does it, with its result
// shapes (strings as lists, OCTET STRING and UTF8String as binaries, named numbers and bits as atoms) and errors.
namespace clause::runtime::builtins::asn1 {
std::vector<ComponentView> components_of(const NodeView &node) {
    std::vector<ComponentView> result;
    const auto tuple = node.field(1);
    for (std::size_t i = 0; i < tuple.tuple_size_unchecked(); ++i) {
        const auto item = tuple.tuple_element_unchecked(i);
        result.push_back({item.tuple_element_unchecked(0), item.tuple_element_unchecked(1).small_integer_unchecked(),
                          item.tuple_element_unchecked(2).small_integer_unchecked(), item.tuple_element_unchecked(3),
                          item.tuple_element_unchecked(4).small_integer_unchecked(),
                          item.tuple_element_unchecked(5).atom_spelling_unchecked() == "true"});
    }
    return result;
}

Term Decoder::top(const std::int64_t node, const Data &data) {
    const auto tags = decoded_tags(schema_.node(node).tags());
    return decode(node, data, tags);
}

Term Decoder::decode(const std::int64_t node, const Data &data, const Tags tags) {
    // Indexed by Kind.
    static constexpr std::array<Method, 20> METHODS = {&Decoder::boolean,
                                                       &Decoder::integer,
                                                       &Decoder::enumerated,
                                                       &Decoder::null,
                                                       &Decoder::bit_string,
                                                       &Decoder::octet_string,
                                                       &Decoder::object_identifier,
                                                       &Decoder::relative_oid,
                                                       &Decoder::real,
                                                       &Decoder::restricted_string,
                                                       &Decoder::simple_string,
                                                       &Decoder::simple_string,
                                                       &Decoder::simple_string,
                                                       &Decoder::sequence,
                                                       &Decoder::set,
                                                       &Decoder::collection,
                                                       &Decoder::collection,
                                                       &Decoder::choice,
                                                       &Decoder::open_type,
                                                       &Decoder::reference};
    const auto view = schema_.node(node);
    return (this->*METHODS[static_cast<std::size_t>(view.kind())])(view, data, tags);
}

// A reference decodes as its target with the tags of the place of use.
Term Decoder::reference(const NodeView &node, const Data &data, const Tags tags) {
    return decode(node.small_field(0), data, tags);
}

// The binary a primitive encoding leaves after its tags; byte_size/1 of anything else fails with badarg.
std::span<const std::byte> Decoder::primitive(const Data &data, const Tags tags) {
    const auto value = match_tags(builder_, data, tags);
    if (value.shape_ != Data::Shape::bytes) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    return value.bytes_;
}

// asn1rtt_ber:match_and_collect/2: a constructed string's parts joined.
std::vector<std::byte> Decoder::collected(const Data &data, const Tags tags) {
    const auto value = match_tags(builder_, data, tags);
    if (value.shape_ == Data::Shape::list && !value.list_.empty()) {
        return collect_parts(value.list_);
    }
    if (value.shape_ != Data::Shape::bytes) {
        const auto term = value.term(builder_);
        erlang_error(abi::v1::ErrorReason::case_clause, &term);
    }
    return {value.bytes_.begin(), value.bytes_.end()};
}

Term Decoder::boolean(const NodeView & /*node*/, const Data &data, const Tags tags) {
    const auto value = match_tags(builder_, data, tags);
    if (value.shape_ == Data::Shape::bytes && value.bytes_.size() == 1) {
        return builder_.atom(value.bytes_.front() == std::byte{0} ? "false" : "true");
    }
    asn1_exit(builder_, builder_.tuple({builder_.atom("decode_boolean"), value.term(builder_)}));
}

// The value, checked against a value range that is not extensible (asn1ct's bad_range), then named.
Term Decoder::integer(const NodeView &node, const Data &data, const Tags tags) {
    const auto value = integer_from_bytes(builder_, primitive(data, tags));
    // asn1ct's int_constr: checked only when not extensible and the lower bound is not MIN.
    const auto bounds = node.field(1);
    if (bounds.is_tuple() && bounds.tuple_element_unchecked(2).atom_spelling_unchecked() == "false" &&
        bounds.tuple_element_unchecked(0).is_integer()) {
        const auto number = integer_of(value);
        const auto upper = bounds.tuple_element_unchecked(1);
        const bool below = number < integer_of(bounds.tuple_element_unchecked(0));
        const bool above = upper.is_integer() && number > integer_of(upper);
        if (below || above) {
            asn1_exit(builder_, builder_.atom("bad_range"));
        }
    }
    for (auto named = node.field(0); named.is_cons(); named = named.tail_unchecked()) {
        const auto pair = named.head_unchecked();
        if (need(pair.tuple_element_unchecked(1).exactly_equal(value))) {
            return pair.tuple_element_unchecked(0);
        }
    }
    return value;
}

Term Decoder::enumerated(const NodeView &node, const Data &data, const Tags tags) {
    const auto value = integer_from_bytes(builder_, primitive(data, tags));
    for (const std::size_t field : {std::size_t{0}, std::size_t{1}}) {
        for (auto items = node.field(field); items.is_cons(); items = items.tail_unchecked()) {
            const auto pair = items.head_unchecked();
            if (need(pair.tuple_element_unchecked(1).exactly_equal(value))) {
                return pair.tuple_element_unchecked(0);
            }
        }
    }
    if (node.field(2).atom_spelling_unchecked() == "true") {
        return builder_.tuple({builder_.atom("asn1_enum"), value});
    }
    asn1_exit(builder_, builder_.tuple({builder_.atom("illegal_enumerated"), value}));
}

Term Decoder::null(const NodeView & /*node*/, const Data &data, const Tags tags) {
    const auto value = match_tags(builder_, data, tags);
    if (value.shape_ == Data::Shape::bytes && value.bytes_.empty()) {
        return builder_.atom("NULL");
    }
    asn1_exit(builder_, builder_.tuple({builder_.atom("decode_null"), value.term(builder_)}));
}

// asn1ct's check_constraint: a size outside a SIZE constraint that is not extensible is bad_range.
void Decoder::check_size(const Term &bounds, const std::size_t size) {
    if (!bounds.is_tuple() || bounds.tuple_element_unchecked(2).atom_spelling_unchecked() != "false") {
        return;
    }
    const auto lower = bounds.tuple_element_unchecked(0);
    const auto upper = bounds.tuple_element_unchecked(1);
    const auto count = static_cast<std::int64_t>(size);
    const bool below = lower.is_integer() && count < lower.small_integer_unchecked();
    const bool above = upper.is_integer() && count > upper.small_integer_unchecked();
    if (below || above) {
        asn1_exit(builder_, builder_.atom("bad_range"));
    }
}

Term Decoder::octet_string(const NodeView &node, const Data &data, const Tags tags) {
    const auto bytes = collected(data, tags);
    if (schema_.flag(FLAG_LEGACY_TYPES)) {
        return builder_.string(bytes);
    }
    check_size(node.field(0), bytes.size());
    return builder_.binary(bytes);
}

// NumericString ... GeneralString, UTCTime, GeneralizedTime and ObjectDescriptor: binary_to_list of the bytes.
Term Decoder::simple_string(const NodeView &node, const Data &data, const Tags tags) {
    const auto bytes = collected(data, tags);
    check_size(node.field(0), bytes.size());
    return builder_.string(bytes);
}

namespace {
// mk_BMP_string/1: pairs of bytes, ASCII as integers and others as {0,0,C,D}; an odd byte fails.
Term bmp_string(Builder &builder, const std::span<const std::byte> bytes) {
    if (bytes.size() % 2 != 0) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    std::vector<Term> codes;
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        const auto high = static_cast<std::int64_t>(bytes[i]);
        const auto low = static_cast<std::int64_t>(bytes[i + 1]);
        codes.push_back(high == 0 ? builder.integer(low)
                                  : builder.tuple({builder.integer(0), builder.integer(0), builder.integer(high),
                                                   builder.integer(low)}));
    }
    return builder.list(codes);
}

// mk_universal_string/1: quadruples of bytes; those with three leading zeros as integers.
Term universal_string(Builder &builder, const std::span<const std::byte> bytes) {
    if (bytes.size() % 4 != 0) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    std::vector<Term> codes;
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        const bool small = bytes[i] == std::byte{0} && bytes[i + 1] == std::byte{0} && bytes[i + 2] == std::byte{0};
        if (small) {
            codes.push_back(builder.integer(static_cast<std::int64_t>(bytes[i + 3])));
            continue;
        }
        std::array<Term, 4> quad;
        for (std::size_t j = 0; j < 4; ++j) {
            quad[j] = builder.integer(static_cast<std::int64_t>(bytes[i + j]));
        }
        codes.push_back(builder.tuple(quad));
    }
    return builder.list(codes);
}
} // namespace

// UTF8String, BMPString and UniversalString; the other restricted strings decode as simple_string does.
Term Decoder::restricted_string(const NodeView &node, const Data &data, const Tags tags) {
    switch (node.small_field(0)) {
    case 12: {
        const auto value = match_tags(builder_, data, tags);
        if (value.shape_ == Data::Shape::list && !value.list_.empty()) {
            return builder_.binary(collect_parts(value.list_));
        }
        return value.term(builder_);
    }
    case 28:
    case 30: {
        const auto bytes = collected(data, tags);
        const auto width = node.small_field(0) == 28 ? std::size_t{4} : std::size_t{2};
        const auto value = width == 4 ? universal_string(builder_, bytes) : bmp_string(builder_, bytes);
        check_size(node.field(1), bytes.size() / width);
        return value;
    }
    default: {
        const auto bytes = collected(data, tags);
        check_size(node.field(1), bytes.size());
        return builder_.string(bytes);
    }
    }
}

namespace {
// dec_subidentifiers/1: base-128 components, each at most 16 bytes long.
std::vector<detail::Integer> subidentifiers(const std::span<const std::byte> bytes) {
    std::vector<detail::Integer> result;
    detail::Integer value = 0;
    std::size_t length = 0;
    for (const auto byte : bytes) {
        if (length >= 16) {
            erlang_error(abi::v1::ErrorReason::function_clause);
        }
        value = (value << 7) | detail::Integer(static_cast<unsigned>(byte) & 0x7FU);
        ++length;
        if ((static_cast<unsigned>(byte) & 0x80U) == 0) {
            result.push_back(value);
            value = 0;
            length = 0;
        }
    }
    return result;
}

Term integer_term(Builder &builder, const detail::Integer &value) {
    return need(builder.factory().integer_decimal(detail::integer_text(value)));
}
} // namespace

Term Decoder::object_identifier(const NodeView & /*node*/, const Data &data, const Tags tags) {
    const auto parts = subidentifiers(primitive(data, tags));
    if (parts.empty()) {
        const auto empty = builder_.list({});
        erlang_error(abi::v1::ErrorReason::badmatch, &empty);
    }
    const auto &first = parts.front();
    const detail::Integer root = first < 40 ? 0 : (first < 80 ? 1 : 2);
    std::vector<Term> arcs{integer_term(builder_, root), integer_term(builder_, first - root * 40)};
    for (std::size_t i = 1; i < parts.size(); ++i) {
        arcs.push_back(integer_term(builder_, parts[i]));
    }
    return builder_.tuple(arcs);
}

Term Decoder::relative_oid(const NodeView & /*node*/, const Data &data, const Tags tags) {
    std::vector<Term> arcs;
    for (const auto &part : subidentifiers(primitive(data, tags))) {
        arcs.push_back(integer_term(builder_, part));
    }
    return builder_.tuple(arcs);
}

Term Decoder::real(const NodeView & /*node*/, const Data &data, const Tags tags) {
    return decode_real(builder_, primitive(data, tags));
}

Term Decoder::bit_string(const NodeView &node, const Data &data, const Tags tags) {
    const auto bytes = collected(data, tags);
    if (bytes.empty()) {
        const auto empty = builder_.binary({});
        erlang_error(abi::v1::ErrorReason::case_clause, &empty);
    }
    return bits(node, bytes);
}

// The unused-bit count byte, then the bits: a bitstring, named bits, or the legacy formats.
Term Decoder::bits(const NodeView &node, const std::span<const std::byte> bytes) {
    const auto unused = static_cast<std::size_t>(bytes.front());
    const auto body = bytes.subspan(1);
    if (body.size() * 8 < unused) {
        const auto term = builder_.binary(body);
        erlang_error(abi::v1::ErrorReason::badmatch, &term);
    }
    const auto size = body.size() * 8 - unused;
    if (node.field(0).is_cons()) {
        return named_bits(node.field(0), body, size);
    }
    check_size(node.field(1), size);
    return unnamed_bits(unused, body, size);
}

// A BIT STRING without named bits: {Unused, Binary}, a list of 0s and 1s, or a bitstring, by the format.
Term Decoder::unnamed_bits(const std::size_t unused, const std::span<const std::byte> body, const std::size_t size) {
    if (schema_.flag(FLAG_COMPACT_BIT_STRING)) {
        return builder_.tuple({builder_.integer(static_cast<std::int64_t>(unused)), builder_.binary(body)});
    }
    if (schema_.flag(FLAG_LEGACY_BIT_STRING)) {
        std::vector<Term> list;
        list.reserve(size);
        for (std::size_t i = 0; i < size; ++i) {
            list.push_back(builder_.integer(bit_at(body, i) ? 1 : 0));
        }
        return builder_.list(list);
    }
    return builder_.bitstring(body, size);
}

namespace {
// The name of bit `position` in a list of {Name, Position}.
std::optional<Term> bit_name(const Term &named, const std::int64_t position) {
    for (auto items = named; items.is_cons(); items = items.tail_unchecked()) {
        const auto pair = items.head_unchecked();
        if (pair.tuple_element_unchecked(1).small_integer_unchecked() == position) {
            return pair.tuple_element_unchecked(0);
        }
    }
    return std::nullopt;
}

// collect_parts_bit/3: the bits of the remaining BIT STRING segments, after the sum of their unused counts.
std::vector<std::byte> collect_bit_parts(const std::span<const Tlv> parts) {
    std::uint64_t unused = 0;
    std::vector<std::byte> bits;
    for (const auto &part : parts) {
        if (part.tag_ != 3 || part.constructed_ || part.value_.empty()) {
            erlang_error(abi::v1::ErrorReason::function_clause);
        }
        unused += static_cast<std::uint64_t>(part.value_.front());
        bits.insert(bits.end(), part.value_.begin() + 1, part.value_.end());
    }
    if (unused > 255) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    bits.insert(bits.begin(), static_cast<std::byte>(unused));
    return bits;
}
} // namespace

// decode_bitstring_NNL/2: the names of the set bits in order, {bit, N} for unnamed ones.
Term Decoder::named_bits(const Term &named, const std::span<const std::byte> body, const std::size_t size) {
    std::vector<Term> names;
    for (std::size_t i = 0; i < size; ++i) {
        if (!bit_at(body, i)) {
            continue;
        }
        const auto name = bit_name(named, static_cast<std::int64_t>(i));
        names.push_back(name ? *name
                             : builder_.tuple({builder_.atom("bit"), builder_.integer(static_cast<std::int64_t>(i))}));
    }
    return builder_.list(names);
}

bool bit_at(const std::span<const std::byte> bytes, const std::size_t index) {
    return ((static_cast<unsigned>(bytes[index / 8]) >> (7 - index % 8)) & 1U) != 0;
}

std::vector<std::byte> collect_parts(const std::span<const Tlv> parts) {
    std::vector<std::byte> result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto &part = parts[i];
        if (part.constructed_) {
            const auto inner = collect_parts(part.children_);
            result.insert(result.end(), inner.begin(), inner.end());
        } else if (part.tag_ == 3 && !part.value_.empty()) {
            // The parts collected before a BIT STRING segment are dropped, as asn1rtt_ber drops them.
            return collect_bit_parts(parts.subspan(i));
        } else {
            result.insert(result.end(), part.value_.begin(), part.value_.end());
        }
    }
    return result;
}
} // namespace clause::runtime::builtins::asn1
