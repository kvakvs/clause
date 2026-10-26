#include "codec.hpp"
#include <algorithm>
#include <array>

// Added for ASN.1 sources (plan 21 step LIB84): BER and DER encoding of simple types as asn1rtt_ber does it: which
// Erlang values each type accepts and the errors for the others.
namespace clause::runtime::builtins::asn1 {
namespace {
bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

void append_length(std::vector<std::byte> &out, std::size_t length) {
    if (length < 128) {
        out.push_back(static_cast<std::byte>(length));
        return;
    }
    std::vector<std::byte> digits;
    for (; length > 0; length >>= 8U) {
        digits.insert(digits.begin(), static_cast<std::byte>(length & 0xFFU));
    }
    out.push_back(static_cast<std::byte>(0x80U | digits.size()));
    out.insert(out.end(), digits.begin(), digits.end());
}

void append_tag(std::vector<std::byte> &out, const Tag &tag, const bool constructed) {
    const auto head = static_cast<std::uint8_t>((tag.class_ << 6U) | (constructed ? 0x20U : 0U));
    if (tag.number_ <= 30) {
        out.push_back(static_cast<std::byte>(head | tag.number_));
        return;
    }
    std::vector<std::byte> digits{static_cast<std::byte>(tag.number_ & 127U)};
    for (auto number = tag.number_ >> 7U; number > 0; number >>= 7U) {
        digits.insert(digits.begin(), static_cast<std::byte>((number & 127U) | 0x80U));
    }
    out.push_back(static_cast<std::byte>(head | 0x1FU));
    out.insert(out.end(), digits.begin(), digits.end());
}

// Kinds whose encodings are constructed: their innermost tag has the constructed bit.
bool constructed_kind(const Kind kind) {
    return kind == Kind::sequence || kind == Kind::set || kind == Kind::sequence_of || kind == Kind::set_of ||
           kind == Kind::choice || kind == Kind::open_type;
}

// An iolist element: a byte, or a nested binary or list; badarg otherwise.
void append_iolist_item(std::vector<std::byte> &out, const Term &item) {
    const auto byte = small(item.word());
    if (byte && *byte >= 0 && *byte <= 255) {
        out.push_back(static_cast<std::byte>(*byte));
        return;
    }
    if (!item.is_list() && !item.is_binary()) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    append_iolist(out, item);
}
} // namespace

// The bytes of an iolist (iolist_to_binary/1): integers 0..255, binaries and nested lists; badarg otherwise.
void append_iolist(std::vector<std::byte> &out, const Term &term) {
    if (term.is_binary()) {
        const auto bytes = need(term.binary_bytes());
        out.insert(out.end(), bytes.begin(), bytes.end());
        return;
    }
    auto list = term;
    for (; list.is_cons(); list = list.tail_unchecked()) {
        append_iolist_item(out, list.head_unchecked());
    }
    if (list.is_binary()) {
        append_iolist(out, list);
    } else if (!list.is_nil()) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
}

void wrap(std::vector<std::byte> &out, const std::size_t start, const std::span<const Tag> tags,
          const bool constructed) {
    for (auto tag = tags.rbegin(); tag != tags.rend(); ++tag) {
        std::vector<std::byte> header;
        append_tag(header, *tag, constructed || tag != tags.rbegin());
        append_length(header, out.size() - start);
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(start), header.begin(), header.end());
    }
}

std::vector<std::byte> Encoder::top(const std::int64_t node, const Term &value) {
    std::vector<std::byte> out;
    const auto tags = schema_.node(node).tags();
    encode(node, value, tags, out);
    return out;
}

void Encoder::encode(const std::int64_t node, const Term &value, const std::span<const Tag> tags,
                     std::vector<std::byte> &out) {
    // Indexed by Kind.
    static constexpr std::array<Method, 20> METHODS = {&Encoder::boolean,
                                                       &Encoder::integer,
                                                       &Encoder::enumerated,
                                                       &Encoder::null,
                                                       &Encoder::bit_string,
                                                       &Encoder::octets,
                                                       &Encoder::object_identifier,
                                                       &Encoder::relative_oid,
                                                       &Encoder::real,
                                                       &Encoder::restricted_string,
                                                       &Encoder::octets,
                                                       &Encoder::octets,
                                                       &Encoder::octets,
                                                       &Encoder::sequence,
                                                       &Encoder::sequence,
                                                       &Encoder::collection,
                                                       &Encoder::collection,
                                                       &Encoder::choice,
                                                       &Encoder::open_type,
                                                       &Encoder::reference};
    auto view = schema_.node(node);
    auto content = view;
    while (content.kind() == Kind::reference) {
        content = schema_.node(content.small_field(0));
    }
    const auto start = out.size();
    (this->*METHODS[static_cast<std::size_t>(content.kind())])(content, value, out);
    wrap(out, start, tags, constructed_kind(content.kind()));
}

// Unused: encode() follows references itself.
void Encoder::reference(const NodeView & /*node*/, const Term & /*value*/, std::vector<std::byte> & /*out*/) {}

void Encoder::boolean(const NodeView & /*node*/, const Term &value, std::vector<std::byte> &out) {
    if (atom_is(value, "true") || atom_is(value, "false")) {
        out.push_back(atom_is(value, "true") ? std::byte{0xFF} : std::byte{0});
        return;
    }
    asn1_exit(builder_, builder_.tuple({builder_.atom("encode_boolean"), value}));
}

// An integer, or a name of the type's named numbers.
void Encoder::integer(const NodeView &node, const Term &value, std::vector<std::byte> &out) {
    const auto named = node.field(0);
    if (value.is_integer()) {
        const auto bytes = integer_bytes(integer_of(value));
        out.insert(out.end(), bytes.begin(), bytes.end());
        return;
    }
    if (!named.is_cons()) {
        asn1_exit(builder_, builder_.tuple({builder_.atom("encode_integer"), value}));
    }
    if (!value.is_atom()) {
        erlang_error(abi::v1::ErrorReason::badarith);
    }
    for (auto items = named; items.is_cons(); items = items.tail_unchecked()) {
        const auto pair = items.head_unchecked();
        if (need(pair.tuple_element_unchecked(0).exactly_equal(value))) {
            const auto bytes = integer_bytes(integer_of(pair.tuple_element_unchecked(1)));
            out.insert(out.end(), bytes.begin(), bytes.end());
            return;
        }
    }
    asn1_exit(builder_, builder_.tuple({builder_.atom("encode_integer_namednumber"), value}));
}

void Encoder::enumerated(const NodeView &node, const Term &value, std::vector<std::byte> &out) {
    for (const std::size_t field : {std::size_t{0}, std::size_t{1}}) {
        for (auto items = node.field(field); items.is_cons(); items = items.tail_unchecked()) {
            const auto pair = items.head_unchecked();
            if (need(pair.tuple_element_unchecked(0).exactly_equal(value))) {
                const auto bytes = integer_bytes(integer_of(pair.tuple_element_unchecked(1)));
                out.insert(out.end(), bytes.begin(), bytes.end());
                return;
            }
        }
    }
    asn1_exit(builder_, builder_.tuple({builder_.atom("enumerated_not_in_range"), value}));
}

// encode_null/2 accepts any value.
void Encoder::null(const NodeView & /*node*/, const Term & /*value*/, std::vector<std::byte> & /*out*/) {}

// OCTET STRING, the 8-bit strings, UTCTime, GeneralizedTime, ObjectDescriptor: a binary or a list of bytes
// (asn1rtt_ber:encode_restricted_string/2).
void Encoder::octets(const NodeView & /*node*/, const Term &value, std::vector<std::byte> &out) {
    if (!value.is_binary() && !value.is_list()) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    append_iolist(out, value);
}

// One character of a BMPString (width 2) or UniversalString (width 4): a code point, or a quadruple's last bytes.
void Encoder::wide_character(const Term &character, const std::size_t width, std::vector<std::byte> &out) {
    if (character.is_tuple() && character.tuple_size_unchecked() == 4) {
        for (auto i = 4 - width; i < 4; ++i) {
            append_iolist(out, builder_.list(std::array{character.tuple_element_unchecked(i)}));
        }
        return;
    }
    out.insert(out.end(), width - 1, std::byte{0});
    append_iolist(out, builder_.list(std::array{character}));
}

void Encoder::restricted_string(const NodeView &node, const Term &value, std::vector<std::byte> &out) {
    const auto tag = node.small_field(0);
    if (tag != 28 && tag != 30) {
        octets(node, value, out);
        return;
    }
    // mk_BMP_list/1 and mk_uni_list/1: characters as two or four bytes, quadruples as their bytes.
    const std::size_t width = tag == 30 ? 2 : 4;
    auto list = value;
    for (; list.is_cons(); list = list.tail_unchecked()) {
        wide_character(list.head_unchecked(), width, out);
    }
    if (!list.is_nil()) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
}

namespace {
// mk_object_val/1: base-128 with the continuation bit; values up to 127 (negative ones too) as one byte.
void append_subidentifier(std::vector<std::byte> &out, const detail::Integer &value) {
    if (value <= 127) {
        out.push_back(static_cast<std::byte>(static_cast<std::int64_t>(value) & 0xFF));
        return;
    }
    std::vector<std::byte> digits{static_cast<std::byte>(static_cast<unsigned>(value & 127))};
    for (auto rest = detail::Integer(value >> 7); rest > 0; rest >>= 7) {
        digits.insert(digits.begin(), static_cast<std::byte>(static_cast<unsigned>(rest & 127) | 0x80U));
    }
    out.insert(out.end(), digits.begin(), digits.end());
}

// The arcs of an OBJECT IDENTIFIER value: a tuple, a list, or {'OBJECT IDENTIFIER', Value}.
std::vector<Term> arcs_of(const Term &value) {
    auto current = value;
    if (current.is_tuple() && current.tuple_size_unchecked() == 2 &&
        atom_is(current.tuple_element_unchecked(0), "OBJECT IDENTIFIER")) {
        current = current.tuple_element_unchecked(1);
    }
    std::vector<Term> arcs;
    if (current.is_tuple()) {
        for (std::size_t i = 0; i < current.tuple_size_unchecked(); ++i) {
            arcs.push_back(current.tuple_element_unchecked(i));
        }
        return arcs;
    }
    for (; current.is_cons(); current = current.tail_unchecked()) {
        arcs.push_back(current.head_unchecked());
    }
    return arcs;
}

detail::Integer arc(const Term &term) {
    if (!term.is_integer()) {
        erlang_error(abi::v1::ErrorReason::badarith);
    }
    return integer_of(term);
}
} // namespace

void Encoder::object_identifier(const NodeView & /*node*/, const Term &value, std::vector<std::byte> &out) {
    const auto arcs = arcs_of(value);
    if (arcs.size() < 2) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    append_subidentifier(out, arc(arcs[0]) * 40 + arc(arcs[1]));
    for (std::size_t i = 2; i < arcs.size(); ++i) {
        append_subidentifier(out, arc(arcs[i]));
    }
}

void Encoder::relative_oid(const NodeView & /*node*/, const Term &value, std::vector<std::byte> &out) {
    for (const auto &item : arcs_of(value)) {
        append_subidentifier(out, arc(item));
    }
}

void Encoder::real(const NodeView & /*node*/, const Term &value, std::vector<std::byte> &out) {
    const auto bytes = encode_real(builder_, value);
    out.insert(out.end(), bytes.begin(), bytes.end());
}

namespace {
// The bits of a bitstring value with the count of unused bits in front (encode_unnamed_bit_string/2).
void append_bits(std::vector<std::byte> &out, const std::vector<std::byte> &bytes, const std::size_t bits) {
    const auto unused = (8 - bits % 8) % 8;
    out.push_back(static_cast<std::byte>(unused));
    out.insert(out.end(), bytes.begin(), bytes.end());
    if (unused != 0 && !bytes.empty()) {
        out.back() = static_cast<std::byte>(static_cast<unsigned>(out.back()) & (0xFFU << unused));
    }
}
} // namespace

// An unnamed BIT STRING takes a bitstring; a named one also takes a list of names and {bit, N}.
void Encoder::bit_string(const NodeView &node, const Term &value, std::vector<std::byte> &out) {
    if (schema_.flag(FLAG_LEGACY_TYPES)) {
        legacy_bits(node, value, out);
        return;
    }
    const auto named = node.field(0);
    if (value.is_bitstring()) {
        append_bits(out, need(value.bitstring_bytes()), need(value.bit_size()));
        return;
    }
    if (!named.is_cons()) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    if (!value.is_list()) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    std::vector<std::int64_t> positions;
    for (auto list = value; list.is_cons(); list = list.tail_unchecked()) {
        positions.push_back(named_position(node, list.head_unchecked()));
    }
    const auto size = positions.empty() ? 0 : static_cast<std::size_t>(*std::ranges::max_element(positions) + 1);
    std::vector<std::byte> bytes((size + 7) / 8);
    for (const auto position : positions) {
        bytes[static_cast<std::size_t>(position) / 8] |= static_cast<std::byte>(0x80U >> (position % 8));
    }
    append_bits(out, bytes, size);
}

// get_all_bitposes/3: the position of a name or {bit, N}; {bitstring_namedbit, Name} for an unknown name.
std::int64_t Encoder::named_position(const NodeView &node, const Term &item) {
    if (item.is_tuple() && item.tuple_size_unchecked() == 2 && atom_is(item.tuple_element_unchecked(0), "bit")) {
        return item.tuple_element_unchecked(1).small_integer_unchecked();
    }
    for (auto items = node.field(0); items.is_cons(); items = items.tail_unchecked()) {
        const auto pair = items.head_unchecked();
        if (need(pair.tuple_element_unchecked(0).exactly_equal(item))) {
            return pair.tuple_element_unchecked(1).small_integer_unchecked();
        }
    }
    if (!item.is_atom()) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    asn1_exit(builder_, builder_.tuple({builder_.atom("bitstring_namedbit"), item}));
}
} // namespace clause::runtime::builtins::asn1
