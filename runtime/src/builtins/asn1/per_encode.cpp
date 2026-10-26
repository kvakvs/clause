#include "per.hpp"
#include <algorithm>

// Added for PER (plan 21 step LIB84): primitive types, as asn1ct_gen_per:gen_encode_prim_imm/3 encodes them.
namespace clause::runtime::builtins::asn1 {
namespace {
bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The universal tags of the character strings.
constexpr std::uint64_t NUMERIC_STRING = 18;
constexpr std::uint64_t PRINTABLE_STRING = 19;
constexpr std::uint64_t IA5_STRING = 22;
constexpr std::uint64_t VISIBLE_STRING = 26;
constexpr std::uint64_t UNIVERSAL_STRING = 28;
constexpr std::uint64_t BMP_STRING = 30;

std::string_view printable_characters() {
    return " '()+,-./0123456789:=?ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
}

// The known-multiplier strings (asn1ct_gen_per:simplify_type/1).
bool known_multiplier_tag(const std::uint64_t tag) {
    return tag == NUMERIC_STRING || tag == PRINTABLE_STRING || tag == IA5_STRING || tag == VISIBLE_STRING ||
           tag == UNIVERSAL_STRING || tag == BMP_STRING;
}

std::vector<std::uint32_t> node_alphabet(const NodeView &node) {
    std::vector<std::uint32_t> result;
    if (node.kind() != Kind::restricted_string) {
        return result;
    }
    for (auto list = node.field(2); list.is_cons(); list = list.tail_unchecked()) {
        result.push_back(static_cast<std::uint32_t>(list.head_unchecked().small_integer_unchecked()));
    }
    return result;
}

// The characters of a type with no FROM constraint (asn1ct_imm:string_num_bits/3).
std::size_t default_characters(const std::uint64_t tag) {
    switch (tag) {
    case IA5_STRING:
        return 128;
    case PRINTABLE_STRING:
        return 74;
    case NUMERIC_STRING:
        return 11;
    default:
        return 95;
    }
}

// charbits/2: the bits of a character in unaligned PER, rounded up to a power of two in aligned PER.
std::size_t charbits(const std::size_t characters, const bool aligned) {
    const auto bits = uper_bits(characters);
    return aligned ? std::size_t{1} << uper_bits(bits) : bits;
}

// encode_unconstrained_number/1: the shortest two's complement octets after their count.
void put_unconstrained(BitWriter &out, const detail::Integer &value) {
    out.align();
    const auto bytes = integer_bytes(value);
    if (bytes.size() < 128) {
        out.put(bytes.size(), 8);
    } else {
        out.put(0x8000U | bytes.size(), 16);
    }
    out.put_bytes(bytes);
}

std::uint64_t universal_tag(const NodeView &node) {
    return node.kind() == Kind::restricted_string ? static_cast<std::uint64_t>(node.small_field(0)) : VISIBLE_STRING;
}
} // namespace

NodeView content_of(const Schema &schema, NodeView node) {
    while (node.kind() == Kind::reference) {
        node = schema.node(node.small_field(0));
    }
    return node;
}

std::size_t character_bits(const NodeView &node, const std::uint64_t tag, const bool aligned) {
    const auto alphabet = node_alphabet(node);
    if (!alphabet.empty()) {
        return charbits(alphabet.size(), aligned);
    }
    if (tag == UNIVERSAL_STRING) {
        return 32;
    }
    if (tag == BMP_STRING) {
        return 16;
    }
    return charbits(default_characters(tag), aligned);
}

// char_tab/3: the sorted characters when one does not fit its unit as its code point.
std::vector<std::uint32_t> character_table(const std::uint64_t tag, const NodeView &node, const std::size_t unit) {
    auto alphabet = node_alphabet(node);
    if (alphabet.empty() && (tag == PRINTABLE_STRING || tag == NUMERIC_STRING)) {
        const auto chars = tag == PRINTABLE_STRING ? printable_characters() : std::string_view(" 0123456789");
        alphabet.assign(chars.begin(), chars.end());
    }
    std::ranges::sort(alphabet);
    if (alphabet.empty() || unit >= 32 || alphabet.back() < (std::uint32_t{1} << unit)) {
        return {};
    }
    return alphabet;
}

BitWriter PerEncoder::top(const std::int64_t node, const Term &value) {
    BitWriter out(schema_.rule() == "per");
    encode(node, value, out);
    if (!schema_.flag(FLAG_NO_FINAL_PADDING)) {
        out.pad_to_octet();
        if (out.bits() == 0) {
            out.put(0, 8);
        }
    }
    return out;
}

void PerEncoder::encode(const std::int64_t node, const Term &value, BitWriter &out) {
    // Indexed by Kind.
    static constexpr std::array<Method, 20> METHODS = {
        &PerEncoder::boolean,
        &PerEncoder::integer,
        &PerEncoder::enumerated,
        &PerEncoder::null,
        &PerEncoder::bit_string,
        &PerEncoder::octet_string,
        &PerEncoder::object_identifier,
        &PerEncoder::relative_oid,
        &PerEncoder::real,
        &PerEncoder::restricted_string,
        &PerEncoder::time,
        &PerEncoder::time,
        &PerEncoder::descriptor,
        &PerEncoder::sequence,
        &PerEncoder::sequence,
        &PerEncoder::collection,
        &PerEncoder::collection,
        &PerEncoder::choice,
        &PerEncoder::open_type,
        &PerEncoder::reference,
    };
    const auto view = schema_.node(node);
    (this->*METHODS[static_cast<std::size_t>(view.kind())])(view, value, out);
}

void PerEncoder::reference(const NodeView &node, const Term &value, BitWriter &out) {
    encode(node.small_field(0), value, out);
}

void PerEncoder::boolean(const NodeView & /*node*/, const Term &value, BitWriter &out) {
    if (!atom_is(value, "true") && !atom_is(value, "false")) {
        asn1_exit(builder_, builder_.tuple({builder_.atom("illegal_boolean"), value}));
    }
    out.put(atom_is(value, "true") ? 1 : 0, 1);
}

void PerEncoder::null(const NodeView & /*node*/, const Term & /*value*/, BitWriter & /*out*/) {}

// A named number is encoded as its value.
void PerEncoder::integer(const NodeView &node, const Term &value, BitWriter &out) {
    for (auto named = node.field(0); named.is_cons() && value.is_atom(); named = named.tail_unchecked()) {
        const auto pair = named.head_unchecked();
        if (need(pair.tuple_element_unchecked(0).exactly_equal(value))) {
            whole_number(node.field(1), pair.tuple_element_unchecked(1), out);
            return;
        }
    }
    whole_number(node.field(1), value, out);
}

namespace {
std::optional<detail::Integer> bound(const Term &bounds, const std::size_t index) {
    const auto term = bounds.tuple_element_unchecked(index);
    return term.is_integer() ? std::optional(integer_of(term)) : std::nullopt;
}

bool within(const Term &bounds, const detail::Integer &value) {
    const auto lower = bound(bounds, 0);
    const auto upper = bound(bounds, 1);
    return (!lower || value >= *lower) && (!upper || value <= *upper);
}

bool extensible_bounds(const Term &bounds) {
    return bounds.is_tuple() && atom_is(bounds.tuple_element_unchecked(2), "true");
}

// The root values of `(1 | 17, ...)`, else empty.
std::vector<detail::Integer> root_values(const Term &bounds) {
    std::vector<detail::Integer> values;
    if (bounds.is_tuple() && bounds.tuple_size_unchecked() > 3) {
        for (auto list = bounds.tuple_element_unchecked(3); list.is_cons(); list = list.tail_unchecked()) {
            values.push_back(integer_of(list.head_unchecked()));
        }
    }
    return values;
}
} // namespace

// per_enc_integer_1/3: unconstrained, within the root (failing with illegal_integer outside it), or extensible.
void PerEncoder::whole_number(const Term &bounds, const Term &value, BitWriter &out) {
    // Val - Lb fails first when there is a lower bound; else the comparisons fail.
    if (!value.is_integer() && bounds.is_tuple() && bound(bounds, 0).value_or(0) != 0) {
        erlang_error(abi::v1::ErrorReason::badarith);
    }
    if (!value.is_integer()) {
        asn1_exit(builder_, builder_.tuple({builder_.atom("illegal_integer"), value}));
    }
    const auto number = integer_of(value);
    if (!bounds.is_tuple()) {
        put_unconstrained(out, number);
        return;
    }
    if (extensible_bounds(bounds)) {
        extensible_number(bounds, number, out);
        return;
    }
    if (!within(bounds, number)) {
        asn1_exit(builder_, builder_.tuple({builder_.atom("illegal_integer"), value}));
    }
    root_number(bounds, number, out);
}

// The extension bit, then the root encoding, or the unconstrained number of a value outside the root.
void PerEncoder::extensible_number(const Term &bounds, const detail::Integer &value, BitWriter &out) {
    const auto roots = root_values(bounds);
    const bool root = roots.empty() ? within(bounds, value) : std::ranges::find(roots, value) != roots.end();
    out.put(root ? 0 : 1, 1);
    if (!root) {
        put_unconstrained(out, value);
        return;
    }
    root_number(bounds, value, out);
}

// The root encoding of a value within the bounds: nothing for a single value, a constrained, semi-constrained or
// unconstrained whole number.
void PerEncoder::root_number(const Term &bounds, const detail::Integer &value, BitWriter &out) {
    const auto lower = bound(bounds, 0);
    const auto upper = bound(bounds, 1);
    if (lower && upper) {
        put_constrained(out, value - *lower, *upper - *lower + 1);
    } else if (lower) {
        put_semi_constrained(out, value - *lower);
    } else {
        put_unconstrained(out, value);
    }
}

namespace {
// The enumeration items sorted by their numbers, as asn1ct_check orders them.
std::vector<Term> sorted_items(const Term &list) {
    std::vector<Term> items;
    for (auto rest = list; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    std::ranges::stable_sort(items, [](const Term &a, const Term &b) {
        return integer_of(a.tuple_element_unchecked(1)) < integer_of(b.tuple_element_unchecked(1));
    });
    return items;
}

std::optional<std::size_t> item_index(const std::vector<Term> &items, const Term &value) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (need(items[i].tuple_element_unchecked(0).exactly_equal(value))) {
            return i;
        }
    }
    return std::nullopt;
}
} // namespace

// The root item's index as a constrained number, or an addition's as a normally small number.
void PerEncoder::enumerated(const NodeView &node, const Term &value, BitWriter &out) {
    const auto root = sorted_items(node.field(0));
    const auto additions = sorted_items(node.field(1));
    const bool extensible = atom_is(node.field(2), "true");
    if (const auto index = item_index(root, value)) {
        if (extensible) {
            out.put(0, 1);
        }
        put_constrained(out, *index, root.size());
        return;
    }
    const auto addition = extensible ? item_index(additions, value) : std::nullopt;
    if (!addition) {
        asn1_exit(builder_, builder_.tuple({builder_.atom("illegal_enumerated"), value}));
    }
    out.put(1, 1);
    put_normally_small(out, *addition);
}

void PerEncoder::bit_string(const NodeView &node, const Term &value, BitWriter &out) { bits_value(node, value, out); }

void PerEncoder::octet_string(const NodeView &node, const Term &value, BitWriter &out) {
    std::vector<std::byte> bytes;
    if (schema_.flag(FLAG_LEGACY_TYPES)) {
        append_iolist(bytes, value);
    } else if (value.is_binary()) {
        bytes = need(value.binary_bytes());
    } else {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    put_string(out, bytes, bytes.size(), 8, size_range(node.field(0)), StringKind::octets);
}

// The octets of a restricted string, an OBJECT IDENTIFIER or a REAL after an unconstrained length.
void PerEncoder::octets_with_length(const std::span<const std::byte> content, BitWriter &out) {
    put_string(out, content, content.size(), 8, {}, StringKind::octets);
}

void PerEncoder::restricted_string(const NodeView &node, const Term &value, BitWriter &out) {
    const auto tag = universal_tag(node);
    if (known_multiplier_tag(tag)) {
        known_multiplier(node, value, tag, out);
        return;
    }
    std::vector<std::byte> bytes;
    append_iolist(bytes, value);
    octets_with_length(bytes, out);
}

// UTCTime and GeneralizedTime are VisibleStrings in PER.
void PerEncoder::time(const NodeView &node, const Term &value, BitWriter &out) {
    known_multiplier(node, value, VISIBLE_STRING, out);
}

void PerEncoder::descriptor(const NodeView & /*node*/, const Term &value, BitWriter &out) {
    std::vector<std::byte> bytes;
    append_iolist(bytes, value);
    octets_with_length(bytes, out);
}

void PerEncoder::real(const NodeView & /*node*/, const Term &value, BitWriter &out) {
    octets_with_length(encode_real(builder_, value), out);
}

namespace {
std::vector<Term> list_items(const Term &value) {
    if (!value.is_list()) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    std::vector<Term> items;
    for (auto rest = value; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    return items;
}

std::uint64_t code_point(const Term &character) {
    if (!character.is_integer()) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    return static_cast<std::uint64_t>(character.small_integer_unchecked());
}

// encode_chars_16bit/1 and encode_big_chars/1: a code point, or {0, 0, A, B} / a quadruple's octets.
void put_wide(BitWriter &out, const Term &character, const std::size_t unit) {
    if (character.is_tuple() && character.tuple_size_unchecked() == 4) {
        for (std::size_t i = 4 - unit / 8; i < 4; ++i) {
            out.put(code_point(character.tuple_element_unchecked(i)), 8);
        }
        return;
    }
    const auto code = code_point(character);
    if (code > 255) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    out.put(code, unit);
}

[[noreturn]] void forbidden_character(Builder &builder) {
    const auto reason =
        builder.tuple({builder.atom("error"),
                       builder.tuple({builder.atom("asn1"), builder.string(std::as_bytes(std::span(std::string_view(
                                                                "value forbidden by FROM constraint"))))})});
    erlang_error(abi::v1::ErrorReason::raised_error, &reason);
}
} // namespace

// The characters packed in `unit` bits: code points, indexes into the table, or octets for 16 and 32 bits.
std::vector<std::byte> PerEncoder::characters(const Term &value, const std::size_t unit,
                                              const std::vector<std::uint32_t> &table) {
    BitWriter packed(false);
    for (const auto &character : list_items(value)) {
        if (unit == 16 || unit == 32) {
            put_wide(packed, character, unit);
            continue;
        }
        const auto code = code_point(character);
        if (table.empty()) {
            packed.put(code, unit);
            continue;
        }
        const auto found = std::ranges::lower_bound(table, code);
        if (found == table.end() || *found != code) {
            forbidden_character(builder_);
        }
        packed.put(static_cast<std::uint64_t>(found - table.begin()), unit);
    }
    return packed.bytes();
}

void PerEncoder::known_multiplier(const NodeView &node, const Term &value, const std::uint64_t tag, BitWriter &out) {
    const auto unit = character_bits(node, tag, out.aligned());
    if (unit == 8 && !value.is_list()) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    const auto count = list_items(value).size();
    const auto bytes = characters(value, unit, character_table(tag, node, unit));
    const auto size = node.kind() == Kind::restricted_string ? node.field(1) : node.field(0);
    put_string(out, bytes, count, unit, size_range(size), StringKind::characters);
}

namespace {
// e_object_element/1: base-128 octets, the high bit marking all but the last.
void put_subidentifier(std::vector<std::byte> &out, const detail::Integer &value) {
    std::vector<std::byte> digits{static_cast<std::byte>(static_cast<unsigned>(value & 127))};
    for (auto rest = detail::Integer(value >> 7); rest > 0; rest >>= 7) {
        digits.insert(digits.begin(), static_cast<std::byte>(static_cast<unsigned>(rest & 127) | 0x80U));
    }
    out.insert(out.end(), digits.begin(), digits.end());
}

std::vector<Term> arcs(const Term &value) {
    if (value.is_tuple()) {
        std::vector<Term> items;
        items.reserve(value.tuple_size_unchecked());
        for (std::size_t i = 0; i < value.tuple_size_unchecked(); ++i) {
            items.push_back(value.tuple_element_unchecked(i));
        }
        return items;
    }
    return list_items(value);
}

detail::Integer arc_value(const Term &arc) {
    if (!arc.is_integer()) {
        erlang_error(abi::v1::ErrorReason::badarith);
    }
    return integer_of(arc);
}
} // namespace

// encode_oid/1: the first two arcs combined (0 or 1 with a second arc below 40, or 2), then the others.
void PerEncoder::object_identifier(const NodeView & /*node*/, const Term &value, BitWriter &out) {
    const auto items = arcs(value);
    if (items.size() < 2) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    const auto first = arc_value(items[0]);
    const auto second = arc_value(items[1]);
    const bool valid = (first >= 0 && first < 2 && second < 40) || first == 2;
    if (!valid) {
        asn1_exit(builder_, builder_.tuple({builder_.atom("illegal_value"), builder_.list(items)}));
    }
    std::vector<std::byte> bytes;
    put_subidentifier(bytes, first * 40 + second);
    for (std::size_t i = 2; i < items.size(); ++i) {
        put_subidentifier(bytes, arc_value(items[i]));
    }
    octets_with_length(bytes, out);
}

void PerEncoder::relative_oid(const NodeView & /*node*/, const Term &value, BitWriter &out) {
    std::vector<std::byte> bytes;
    for (const auto &item : arcs(value)) {
        put_subidentifier(bytes, arc_value(item));
    }
    octets_with_length(bytes, out);
}
} // namespace clause::runtime::builtins::asn1
