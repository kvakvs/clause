#include "codec.hpp"
#include <algorithm>

// Added for ASN.1 sources (plan 21 step LIB84): BER and DER encoding of SEQUENCE, SET, the collections, CHOICE and
// open types as asn1ct_constructed_ber_bin_v2 generates it. DER sorts SET components by tag and SET OF elements by
// their encodings, and leaves out DEFAULT values by asn1ct_gen_check's comparisons.
namespace clause::runtime::builtins::asn1 {
namespace {
bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The value standing for a component the maps representation leaves out.
constexpr std::string_view MISSING_IN_MAP = "asn1__MISSING_IN_MAP";

bool equal(const Term &left, const Term &right) { return need(left.exactly_equal(right)); }
} // namespace

// The values of a record (`{_, C1, ..., Cn} = Val`) or of a map (mandatory components with `:=`).
std::vector<Term> Encoder::fields(const NodeView & /*node*/, const std::vector<ComponentView> &list,
                                  const Term &value) {
    return schema_.flag(FLAG_MAPS) ? map_fields(list, value) : record_fields(list, value);
}

std::vector<Term> Encoder::record_fields(const std::vector<ComponentView> &list, const Term &value) {
    if (!value.is_tuple() || value.tuple_size_unchecked() != list.size() + 1) {
        erlang_error(abi::v1::ErrorReason::badmatch, &value);
    }
    std::vector<Term> values;
    values.reserve(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        values.push_back(value.tuple_element_unchecked(i + 1));
    }
    return values;
}

// A component missing from the map is MISSING_IN_MAP; a mandatory one fails the match.
std::vector<Term> Encoder::map_fields(const std::vector<ComponentView> &list, const Term &value) {
    if (!value.is_map()) {
        erlang_error(abi::v1::ErrorReason::badmatch, &value);
    }
    std::vector<Term> values;
    values.reserve(list.size());
    for (const auto &item : list) {
        const auto found = need(value.map_find(item.name_));
        if (!found && item.presence_ == 0) {
            erlang_error(abi::v1::ErrorReason::badmatch, &value);
        }
        values.push_back(found ? *found : builder_.atom(MISSING_IN_MAP));
    }
    return values;
}

// An OPTIONAL component without a value, or a DEFAULT one without one or equal to its default.
bool Encoder::omitted(const ComponentView &component, const Term &value) {
    if (component.presence_ == 0) {
        return false;
    }
    if (atom_is(value, MISSING_IN_MAP)) {
        return true;
    }
    if (component.presence_ == 1) {
        return atom_is(value, "asn1_NOVALUE");
    }
    if (atom_is(value, "asn1_DEFAULT")) {
        return true;
    }
    return schema_.flag(FLAG_DER) ? der_default(component.node_, value, component.default_)
                                  : equal(value, component.default_);
}

void Encoder::sequence(const NodeView &node, const Term &value, std::vector<std::byte> &out) {
    const auto list = components_of(node);
    frames_.push_back({node.index(), fields(node, list, value)});
    const bool sorted = node.kind() == Kind::set && schema_.flag(FLAG_DER);
    std::vector<std::vector<std::byte>> parts;
    for (std::size_t i = 0; i < list.size(); ++i) {
        frames_.back().current_ = i;
        const auto field = frames_.back().values_[i];
        if (omitted(list[i], field)) {
            continue;
        }
        const auto tags = schema_.node(list[i].node_).tags();
        if (sorted) {
            encode(list[i].node_, field, tags, parts.emplace_back());
        } else {
            encode(list[i].node_, field, tags, out);
        }
    }
    frames_.pop_back();
    if (sorted) {
        sorted_components(parts, out);
    }
}

// dynamicsort_SET_components/1: a DER SET's components in the order of their tags (class, then number).
void Encoder::sorted_components(std::vector<std::vector<std::byte>> &parts, std::vector<std::byte> &out) {
    const auto key = [](const std::vector<std::byte> &part) {
        const auto first = static_cast<unsigned>(part.front());
        std::uint64_t number = first & 0x1FU;
        if (number == 31) {
            number = 0;
            for (std::size_t i = 1; i < part.size(); ++i) {
                number = (number << 7U) | (static_cast<unsigned>(part[i]) & 0x7FU);
                if ((static_cast<unsigned>(part[i]) & 0x80U) == 0) {
                    break;
                }
            }
        }
        return std::pair{first >> 6U, number};
    };
    std::ranges::stable_sort(parts, [&](const auto &a, const auto &b) { return key(a) < key(b); });
    for (const auto &part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
}

// SEQUENCE OF and SET OF: the elements in order; a DER SET OF sorts their encodings.
void Encoder::collection(const NodeView &node, const Term &value, std::vector<std::byte> &out) {
    auto rest = value;
    const auto element = node.small_field(0);
    const auto tags = schema_.node(element).tags();
    std::vector<std::vector<std::byte>> parts;
    const bool sorted = node.kind() == Kind::set_of && schema_.flag(FLAG_DER);
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        encode(element, rest.head_unchecked(), tags, sorted ? parts.emplace_back() : out);
    }
    if (!rest.is_nil()) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    std::ranges::sort(parts);
    for (const auto &part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
}

// {Alternative, Value}: element(1, Val) chooses, an unknown one is {invalid_choice_type, Name}.
void Encoder::choice(const NodeView &node, const Term &value, std::vector<std::byte> &out) {
    if (!value.is_tuple() || value.tuple_size_unchecked() == 0) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    const auto name = value.tuple_element_unchecked(0);
    const auto alternatives = node.field(0);
    for (std::size_t i = 0; i < alternatives.tuple_size_unchecked(); ++i) {
        const auto alternative = alternatives.tuple_element_unchecked(i);
        if (!equal(alternative.tuple_element_unchecked(0), name)) {
            continue;
        }
        if (value.tuple_size_unchecked() < 2) {
            erlang_error(abi::v1::ErrorReason::badarg);
        }
        const auto type = alternative.tuple_element_unchecked(1).small_integer_unchecked();
        encode(type, value.tuple_element_unchecked(1), schema_.node(type).tags(), out);
        return;
    }
    asn1_exit(builder_, builder_.tuple({builder_.atom("invalid_choice_type"), name}));
}

// An open type takes the bytes of an encoding (a binary or an iolist), or under a component relation a value of
// the type its object gives.
void Encoder::open_type(const NodeView &node, const Term &value, std::vector<std::byte> &out) {
    if (node.field(0).is_tuple()) {
        table_value(node, value, out);
        return;
    }
    if (!value.is_binary() && !value.is_list()) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    append_iolist(out, value);
}

void Encoder::table_value(const NodeView &node, const Term &value, std::vector<std::byte> &out) {
    const auto table = node.field(0);
    const auto levels = static_cast<std::size_t>(table.tuple_element_unchecked(0).small_integer_unchecked());
    const auto key = relation_key(schema_, frames_, levels, table.tuple_element_unchecked(1), schema_.flag(FLAG_MAPS));
    const auto entry = key ? need(table.tuple_element_unchecked(2).map_find(*key)) : std::nullopt;
    if (entry && entry->is_integer()) {
        const auto type = entry->small_integer_unchecked();
        encode(type, value, schema_.node(type).tags(), out);
        return;
    }
    if (entry) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    if (atom_is(table.tuple_element_unchecked(3), "true")) {
        unknown_object_value(value, out);
        return;
    }
    plain_exit(builder_.tuple({builder_.atom("Type not compatible with table constraint"),
                               builder_.tuple({builder_.atom("component"), table.tuple_element_unchecked(4)}),
                               builder_.tuple({builder_.atom("value"), value}),
                               builder_.tuple({builder_.atom("unique_name_and_value"), table.tuple_element_unchecked(5),
                                               key ? *key : builder_.atom("undefined")})}));
}

// Under an extensible object set, a key without an object takes {asn1_OPENTYPE, Binary}: the encoding's bytes.
void Encoder::unknown_object_value(const Term &value, std::vector<std::byte> &out) {
    const bool wrapped = value.is_tuple() && value.tuple_size_unchecked() == 2 &&
                         atom_is(value.tuple_element_unchecked(0), "asn1_OPENTYPE") &&
                         value.tuple_element_unchecked(1).is_binary();
    if (!wrapped) {
        erlang_error(abi::v1::ErrorReason::case_clause, &value);
    }
    const auto bytes = need(value.tuple_element_unchecked(1).binary_bytes());
    out.insert(out.end(), bytes.begin(), bytes.end());
}
} // namespace clause::runtime::builtins::asn1
