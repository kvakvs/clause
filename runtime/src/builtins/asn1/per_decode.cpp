#include "per.hpp"
#include <algorithm>

// Added for PER (plan 21 step LIB84): primitive types, as asn1ct_gen_per:gen_dec_imm/2 decodes them.
namespace clause::runtime::builtins::asn1 {
namespace {
bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

constexpr std::uint64_t UTF8_STRING = 12;
constexpr std::uint64_t VISIBLE_STRING = 26;

bool known_multiplier_tag(const std::uint64_t tag) {
    return tag == 18 || tag == 19 || tag == 22 || tag == VISIBLE_STRING || tag == 28 || tag == 30;
}

std::optional<detail::Integer> bound(const Term &bounds, const std::size_t index) {
    const auto term = bounds.tuple_element_unchecked(index);
    return term.is_integer() ? std::optional(integer_of(term)) : std::nullopt;
}

// The signed number in `bytes` octets (decode_unconstrained_number).
detail::Integer signed_value(BitReader &in, const std::size_t bytes) {
    auto value = in.get_integer(bytes * 8);
    if (bytes > 0 && bit_test(value, static_cast<unsigned>(bytes * 8 - 1))) {
        value -= detail::Integer(1) << (bytes * 8);
    }
    return value;
}

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
} // namespace

Term PerDecoder::decode(const std::int64_t node, BitReader &in) {
    // Indexed by Kind.
    static constexpr std::array<Method, 20> METHODS = {
        &PerDecoder::boolean,
        &PerDecoder::integer,
        &PerDecoder::enumerated,
        &PerDecoder::null,
        &PerDecoder::bit_string,
        &PerDecoder::octet_string,
        &PerDecoder::object_identifier,
        &PerDecoder::relative_oid,
        &PerDecoder::real,
        &PerDecoder::restricted_string,
        &PerDecoder::time,
        &PerDecoder::time,
        &PerDecoder::descriptor,
        &PerDecoder::sequence,
        &PerDecoder::sequence,
        &PerDecoder::collection,
        &PerDecoder::collection,
        &PerDecoder::choice,
        &PerDecoder::open_type,
        &PerDecoder::reference,
    };
    const auto view = schema_.node(node);
    return (this->*METHODS[static_cast<std::size_t>(view.kind())])(view, in);
}

Term PerDecoder::reference(const NodeView &node, BitReader &in) { return decode(node.small_field(0), in); }

Term PerDecoder::boolean(const NodeView & /*node*/, BitReader &in) {
    return builder_.atom(in.get(1) != 0 ? "true" : "false");
}

Term PerDecoder::null(const NodeView & /*node*/, BitReader & /*in*/) { return builder_.atom("NULL"); }

Term PerDecoder::number(const detail::Integer &value) {
    return need(builder_.factory().integer_decimal(detail::integer_text(value)));
}

// A named number decodes as its name.
Term PerDecoder::integer(const NodeView &node, BitReader &in) {
    const auto value = whole_number(node.field(1), in);
    for (auto named = node.field(0); named.is_cons(); named = named.tail_unchecked()) {
        const auto pair = named.head_unchecked();
        if (need(pair.tuple_element_unchecked(1).exactly_equal(value))) {
            return pair.tuple_element_unchecked(0);
        }
    }
    return value;
}

Term PerDecoder::whole_number(const Term &bounds, BitReader &in) {
    const bool extensible = bounds.is_tuple() && atom_is(bounds.tuple_element_unchecked(2), "true");
    if (!bounds.is_tuple() || (extensible && in.get(1) != 0)) {
        const auto length = get_unconstrained_length(builder_, in, false);
        return number(signed_value(in, length));
    }
    return root_number(bounds, in);
}

Term PerDecoder::root_number(const Term &bounds, BitReader &in) {
    const auto lower = bound(bounds, 0);
    const auto upper = bound(bounds, 1);
    if (lower && upper) {
        return number(*lower + get_constrained(in, *upper - *lower + 1));
    }
    if (lower) {
        return number(*lower + get_semi_constrained(builder_, in));
    }
    const auto length = get_unconstrained_length(builder_, in, false);
    return number(signed_value(in, length));
}

// A root index as a constrained number (decode_enumerated for an unknown one), or an addition's as a normally
// small number ({asn1_enum, N} for an unknown one).
Term PerDecoder::enumerated(const NodeView &node, BitReader &in) {
    const auto root = sorted_items(node.field(0));
    const auto additions = sorted_items(node.field(1));
    const bool extensible = atom_is(node.field(2), "true");
    if (extensible && in.get(1) != 0) {
        const auto index = get_normally_small(builder_, in);
        if (index < additions.size()) {
            return additions[index].tuple_element_unchecked(0);
        }
        return builder_.tuple({builder_.atom("asn1_enum"), builder_.integer(static_cast<std::int64_t>(index))});
    }
    const auto index = static_cast<std::size_t>(get_constrained(in, root.size()));
    if (index >= root.size()) {
        asn1_exit(builder_, builder_.tuple({builder_.atom("decode_enumerated"),
                                            builder_.integer(static_cast<std::int64_t>(index))}));
    }
    return root[index].tuple_element_unchecked(0);
}

Term PerDecoder::bit_string(const NodeView &node, BitReader &in) {
    std::size_t count = 0;
    auto bytes = get_string(builder_, in, 1, size_range(node.field(1)), StringKind::bits, count);
    return bits_value(node, std::move(bytes), count);
}

Term PerDecoder::octet_string(const NodeView &node, BitReader &in) {
    std::size_t count = 0;
    const auto bytes = get_string(builder_, in, 8, size_range(node.field(0)), StringKind::octets, count);
    return schema_.flag(FLAG_LEGACY_TYPES) ? builder_.string(bytes) : builder_.binary(bytes);
}

// per_dec_restricted_string/1: octets after an unconstrained length.
std::vector<std::byte> PerDecoder::octets_with_length(BitReader &in) {
    const auto length = get_unconstrained_length(builder_, in, true);
    return in.get_bits(length * 8);
}

Term PerDecoder::restricted_string(const NodeView &node, BitReader &in) {
    const auto tag = static_cast<std::uint64_t>(node.small_field(0));
    if (known_multiplier_tag(tag)) {
        return known_multiplier(node, tag, in);
    }
    const auto bytes = octets_with_length(in);
    return tag == UTF8_STRING ? builder_.binary(bytes) : builder_.string(bytes);
}

Term PerDecoder::time(const NodeView &node, BitReader &in) { return known_multiplier(node, VISIBLE_STRING, in); }

Term PerDecoder::descriptor(const NodeView & /*node*/, BitReader &in) {
    return builder_.string(octets_with_length(in));
}

Term PerDecoder::real(const NodeView & /*node*/, BitReader &in) {
    const auto length = get_unconstrained_length(builder_, in, true);
    in.align();
    return decode_real(builder_, in.get_bits(length * 8));
}

Term PerDecoder::known_multiplier(const NodeView &node, const std::uint64_t tag, BitReader &in) {
    const auto unit = character_bits(node, tag, in.aligned());
    const auto size = node.kind() == Kind::restricted_string ? node.field(1) : node.field(0);
    std::size_t count = 0;
    const auto bytes = get_string(builder_, in, unit, size_range(size), StringKind::characters, count);
    return characters(bytes, count, unit, character_table(tag, node, unit));
}

namespace {
// decode_chars_16bit/1 and decode_big_chars/2: a code point below 256, else the code's octets as a quadruple.
Term wide_character(Builder &builder, const std::uint64_t code) {
    if (code < 256) {
        return builder.integer(static_cast<std::int64_t>(code));
    }
    return builder.tuple({builder.integer(static_cast<std::int64_t>((code >> 24U) & 0xFFU)),
                          builder.integer(static_cast<std::int64_t>((code >> 16U) & 0xFFU)),
                          builder.integer(static_cast<std::int64_t>((code >> 8U) & 0xFFU)),
                          builder.integer(static_cast<std::int64_t>(code & 0xFFU))});
}
} // namespace

// The characters of `count` units: code points, table entries, or wide characters above eight bits.
Term PerDecoder::characters(const std::span<const std::byte> bytes, const std::size_t count, const std::size_t unit,
                            const std::vector<std::uint32_t> &table) {
    BitReader units(bytes, count * unit, false);
    std::vector<Term> items;
    items.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto code = units.get(unit);
        if (!table.empty()) {
            if (code >= table.size()) {
                erlang_error(abi::v1::ErrorReason::badarg);
            }
            items.push_back(builder_.integer(table[code]));
        } else if (unit > 8) {
            items.push_back(wide_character(builder_, code));
        } else {
            items.push_back(builder_.integer(static_cast<std::int64_t>(code)));
        }
    }
    return builder_.list(items);
}

namespace {
// dec_subidentifiers/1: base-128 numbers of at most 16 octets each.
std::vector<detail::Integer> subidentifiers(const std::vector<std::byte> &bytes) {
    std::vector<detail::Integer> result;
    detail::Integer value = 0;
    std::size_t octets = 0;
    for (const auto byte : bytes) {
        if (octets >= 16) {
            erlang_error(abi::v1::ErrorReason::function_clause);
        }
        value = (value << 7) | (static_cast<unsigned>(byte) & 0x7FU);
        ++octets;
        if ((static_cast<unsigned>(byte) & 0x80U) == 0) {
            result.push_back(value);
            value = 0;
            octets = 0;
        }
    }
    return result;
}
} // namespace

// decode_oid/1: the first subidentifier split into two arcs.
Term PerDecoder::object_identifier(const NodeView & /*node*/, BitReader &in) {
    const auto values = subidentifiers(octets_with_length(in));
    if (values.empty()) {
        erlang_error(abi::v1::ErrorReason::badmatch);
    }
    const auto &first = values.front();
    const int head = first < 40 ? 0 : (first < 80 ? 1 : 2);
    std::vector<Term> arcs{builder_.integer(head), number(first - 40 * head)};
    for (std::size_t i = 1; i < values.size(); ++i) {
        arcs.push_back(number(values[i]));
    }
    return builder_.tuple(arcs);
}

Term PerDecoder::relative_oid(const NodeView & /*node*/, BitReader &in) {
    std::vector<Term> arcs;
    for (const auto &value : subidentifiers(octets_with_length(in))) {
        arcs.push_back(number(value));
    }
    return builder_.tuple(arcs);
}
} // namespace clause::runtime::builtins::asn1
