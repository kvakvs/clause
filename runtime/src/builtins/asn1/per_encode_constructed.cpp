#include "per.hpp"
#include <algorithm>

// Added for PER (plan 21 step LIB84): SEQUENCE, SET, SEQUENCE OF, CHOICE and open types as asn1ct_constructed_per
// encodes them: extension bit, presence bitmap, root components, then extension additions as open types.
namespace clause::runtime::builtins::asn1 {
namespace {
bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The value standing for a component the maps representation leaves out.
constexpr std::string_view MISSING_IN_MAP = "asn1__MISSING_IN_MAP";
constexpr std::size_t FRAGMENT = 16384;

// The position of a named bit, or -1.
std::int64_t bit_position(const NodeView &node, const Term &name) {
    for (auto named = node.field(0); named.is_cons(); named = named.tail_unchecked()) {
        const auto pair = named.head_unchecked();
        if (need(pair.tuple_element_unchecked(0).exactly_equal(name))) {
            return pair.tuple_element_unchecked(1).small_integer_unchecked();
        }
    }
    return -1;
}

bool missing(const Term &value) { return atom_is(value, MISSING_IN_MAP) || atom_is(value, "asn1_NOVALUE"); }

// The order root components are encoded in: textual for a SEQUENCE; for a SET by tag (asn1ct_check's
// sort_components(per, ...)), an untagged component keeping its place among them.
std::vector<std::size_t> root_order(const Schema &schema, const NodeView &node,
                                    const std::vector<ComponentView> &list) {
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].addition_ == 0) {
            order.push_back(i);
        }
    }
    if (node.kind() == Kind::set) {
        const auto key = [&](const std::size_t i) {
            const auto tags = schema.node(list[i].node_).tags();
            return tags.empty() ? std::pair{0U, std::uint64_t{0}}
                                : std::pair{3 - tags.front().class_, tags.front().number_};
        };
        std::ranges::stable_sort(order, [&](const std::size_t a, const std::size_t b) { return key(a) < key(b); });
    }
    return order;
}
} // namespace

std::vector<std::size_t> addition_starts(const std::vector<ComponentView> &list) {
    std::vector<std::size_t> starts;
    std::int64_t previous = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].addition_ != 0 && list[i].addition_ != previous) {
            starts.push_back(i);
        }
        previous = list[i].addition_;
    }
    return starts;
}

// The component values: element(N, Val) of a record, or the map's values (mandatory root components matched).
std::vector<Term> PerEncoder::fields(const std::vector<ComponentView> &list, const Term &value) {
    if (!schema_.flag(FLAG_MAPS)) {
        return record_fields(list.size(), value);
    }
    std::vector<Term> values;
    values.reserve(list.size());
    if (!value.is_map()) {
        erlang_error(abi::v1::ErrorReason::badmatch, &value);
    }
    for (const auto &item : list) {
        const auto found = need(value.map_find(item.name_));
        if (!found && item.presence_ == 0 && item.addition_ == 0) {
            erlang_error(abi::v1::ErrorReason::badmatch, &value);
        }
        values.push_back(found ? *found : builder_.atom(MISSING_IN_MAP));
    }
    return values;
}

// element(N + 1, Val) for each component: badarg when the value has fewer elements.
std::vector<Term> PerEncoder::record_fields(const std::size_t count, const Term &value) {
    if (!value.is_tuple() || value.tuple_size_unchecked() < count + 1) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    std::vector<Term> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        values.push_back(value.tuple_element_unchecked(i + 1));
    }
    return values;
}

namespace {
// A name of a named INTEGER whose number is the default.
bool named_default(const NodeView &node, const Term &value, const Term &fallback) {
    for (auto named = node.field(0); named.is_cons(); named = named.tail_unchecked()) {
        const auto pair = named.head_unchecked();
        if (need(pair.tuple_element_unchecked(0).exactly_equal(value))) {
            return need(pair.tuple_element_unchecked(1).exactly_equal(fallback));
        }
    }
    return false;
}
} // namespace

// def_values/2: the default itself, the name of a named INTEGER default, or a BIT STRING equal but for trailing
// zero bits (is_default_bitstring/3).
bool PerEncoder::default_value(const ComponentView &component, const Term &value) {
    if (need(value.exactly_equal(component.default_))) {
        return true;
    }
    const auto node = content_of(schema_, schema_.node(component.node_));
    if (node.kind() == Kind::integer && value.is_atom()) {
        return named_default(node, value, component.default_);
    }
    if (node.kind() != Kind::bit_string || !value.is_bitstring() || !component.default_.is_list()) {
        return false;
    }
    return bits_default(node, value, component.default_);
}

// A bitstring with the named bits of the default set and only those, maybe followed by zero bits.
bool PerEncoder::bits_default(const NodeView &node, const Term &value, const Term &fallback) {
    if (!value.is_bitstring() || !fallback.is_list()) {
        return false;
    }
    std::vector<std::int64_t> positions;
    for (auto list = fallback; list.is_cons(); list = list.tail_unchecked()) {
        positions.push_back(bit_position(node, list.head_unchecked()));
    }
    const auto bits = need(value.bit_size());
    const auto bytes = need(value.bitstring_bytes());
    for (std::size_t i = 0; i < bits; ++i) {
        if (bit_at(bytes, i) != std::ranges::contains(positions, static_cast<std::int64_t>(i))) {
            return false;
        }
    }
    return std::ranges::all_of(positions, [&](const std::int64_t p) { return static_cast<std::size_t>(p) < bits; });
}

// Whether a root OPTIONAL or DEFAULT component is left out: asn1_NOVALUE for OPTIONAL, asn1_DEFAULT or the default
// for DEFAULT (def_values/2), absence from a map for either.
bool PerEncoder::absent(const ComponentView &component, const Term &value, const bool /*extension*/) {
    if (component.presence_ == 0) {
        return false;
    }
    if (atom_is(value, MISSING_IN_MAP) || (component.presence_ == 1 && atom_is(value, "asn1_NOVALUE"))) {
        return true;
    }
    return component.presence_ == 2 && (atom_is(value, "asn1_DEFAULT") || default_value(component, value));
}

namespace {
// An addition slot is left out without a value; a DEFAULT one also when asn1_DEFAULT stands for its value.
bool absent_addition(const ComponentView &first, const Term &value) {
    if (!first.group_ && first.presence_ == 2) {
        return atom_is(value, "asn1_DEFAULT") || atom_is(value, MISSING_IN_MAP);
    }
    return missing(value);
}

std::size_t group_end(const std::vector<ComponentView> &list, const std::size_t first) {
    auto end = first;
    while (end < list.size() && list[end].addition_ == list[first].addition_) {
        ++end;
    }
    return end;
}
} // namespace

// An addition group's value as asn1ct's encoder packs it: {extaddgroup, V1, ...}, or asn1_NOVALUE when none of its
// components has a value.
Term PerEncoder::packed_group(const std::vector<ComponentView> &list, const std::size_t first,
                              const std::vector<Term> &record) {
    const auto end = group_end(list, first);
    std::vector<Term> items{builder_.atom("extaddgroup")};
    bool present = false;
    for (auto i = first; i < end; ++i) {
        items.push_back(record[i]);
        present = present || !missing(record[i]);
    }
    return present ? builder_.tuple(items) : builder_.atom("asn1_NOVALUE");
}

// The additions' values as the generated encoder reads them. For records it stores each packed group at the group's
// slot position and reads a component after a group at its position less the group's length but one: asn1ct's quirk
// for groups of several components, mirrored. Maps are read by name.
std::vector<Term> PerEncoder::slot_values(const std::vector<ComponentView> &list, std::vector<Term> &record,
                                          const std::vector<std::size_t> &starts) {
    const bool maps = schema_.flag(FLAG_MAPS);
    std::vector<Term> values;
    values.reserve(starts.size());
    std::size_t shift = 0;
    for (std::size_t slot = 0; slot < starts.size(); ++slot) {
        const auto first = starts[slot];
        if (!list[first].group_) {
            values.push_back(record[maps ? first : first - shift]);
            continue;
        }
        values.push_back(packed_group(list, first, record));
        if (!maps) {
            record[starts.front() + slot] = values.back();
            shift = group_end(list, first) - first - 1;
        }
    }
    return values;
}

// The additions with their presence bits, which records read from consecutive positions (extension_bitmap/3).
PerEncoder::Additions PerEncoder::additions(const std::vector<ComponentView> &list, std::vector<Term> record) {
    Additions result;
    result.starts_ = addition_starts(list);
    result.values_ = slot_values(list, record, result.starts_);
    const bool maps = schema_.flag(FLAG_MAPS);
    for (std::size_t slot = 0; slot < result.starts_.size(); ++slot) {
        const auto &value = maps ? result.values_[slot] : record[result.starts_.front() + slot];
        result.bitmap_.push_back(!missing(value) && !atom_is(value, "asn1_DEFAULT"));
    }
    return result;
}

namespace {
// The root components in textual order.
std::vector<std::size_t> root_components(const std::vector<ComponentView> &list) {
    std::vector<std::size_t> root;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].addition_ == 0) {
            root.push_back(i);
        }
    }
    return root;
}

void put_bitmap(const std::vector<bool> &bitmap, BitWriter &out) {
    for (const bool bit : bitmap) {
        out.put(bit ? 1 : 0, 1);
    }
}
} // namespace

// The presence bit of each OPTIONAL or DEFAULT component among `indexes`.
void PerEncoder::presence_bits(const std::vector<ComponentView> &list, const std::vector<Term> &values,
                               const std::vector<std::size_t> &indexes, BitWriter &out) {
    for (const auto i : indexes) {
        if (list[i].presence_ != 0) {
            out.put(absent(list[i], values[i], false) ? 0 : 1, 1);
        }
    }
}

// The components among `indexes` that are not left out, in that order.
void PerEncoder::present_components(const std::vector<ComponentView> &list, const std::vector<Term> &values,
                                    const std::vector<std::size_t> &indexes, BitWriter &out) {
    for (const auto i : indexes) {
        if (!absent(list[i], values[i], false)) {
            frames_.back().current_ = i;
            encode(list[i].node_, values[i], out);
        }
    }
}

void PerEncoder::sequence(const NodeView &node, const Term &value, BitWriter &out) {
    const auto list = components_of(node);
    frames_.push_back({node.index(), fields(list, value)});
    // A copy: nested SEQUENCEs push frames, which may move this one.
    const auto values = frames_.back().values_;
    const auto slots = additions(list, values);
    const bool extended = std::ranges::find(slots.bitmap_, true) != slots.bitmap_.end();
    if (atom_is(node.field(2), "true")) {
        out.put(extended ? 1 : 0, 1);
    }
    presence_bits(list, values, root_components(list), out);
    present_components(list, values, root_order(schema_, node, list), out);
    if (extended) {
        put_small_length(out, slots.starts_.size());
        put_bitmap(slots.bitmap_, out);
    }
    extension_additions(list, slots, out);
    frames_.pop_back();
}

// Each addition with a value as an open type, as the generated encoder writes them (whatever the bitmap says).
void PerEncoder::extension_additions(const std::vector<ComponentView> &list, const Additions &slots, BitWriter &out) {
    for (std::size_t slot = 0; slot < slots.starts_.size(); ++slot) {
        const auto first = slots.starts_[slot];
        if (absent_addition(list[first], slots.values_[slot])) {
            continue;
        }
        frames_.back().current_ = first;
        if (list[first].group_) {
            group_value(list, first, slots.values_[slot], out);
        } else {
            as_open_type(list[first].node_, slots.values_[slot], out);
        }
    }
}

// An addition group as an open type: a SEQUENCE of its components from {extaddgroup, V1, ...}.
void PerEncoder::group_value(const std::vector<ComponentView> &list, const std::size_t first, const Term &packed,
                             BitWriter &out) {
    std::vector<Term> values(list.size(), packed);
    std::vector<std::size_t> indexes;
    for (auto i = first; i < group_end(list, first); ++i) {
        values[i] = packed.tuple_element_unchecked(1 + i - first);
        indexes.push_back(i);
    }
    BitWriter inner(out.aligned());
    presence_bits(list, values, indexes, inner);
    present_components(list, values, indexes, inner);
    complete_open_type(inner, out);
}

// per_enc_open_type/2: the value's complete encoding (at least one octet) after its length.
void PerEncoder::as_open_type(const std::int64_t node, const Term &value, BitWriter &out) {
    BitWriter inner(out.aligned());
    encode(node, value, inner);
    complete_open_type(inner, out);
}

// complete/1 of an open type's encoding (padded, one zero octet when empty), then its length and octets.
void PerEncoder::complete_open_type(BitWriter &inner, BitWriter &out) {
    inner.pad_to_octet();
    if (inner.bits() == 0) {
        inner.put(0, 8);
    }
    put_string(out, inner.bytes(), inner.bits() / 8, 8, {}, StringKind::octets);
}

// enc_sof_length/3: an unconstrained count (no fragments), a constrained one, or none for a fixed size.
void PerEncoder::sequence_length(const Term &bounds, const std::size_t count, BitWriter &out) {
    const auto size = size_range(bounds);
    const bool in_root =
        count >= static_cast<std::size_t>(size.lower_) && count <= static_cast<std::size_t>(size.upper_);
    const auto root = [&] {
        if (!in_root) {
            erlang_error(abi::v1::ErrorReason::if_clause);
        }
        put_constrained(out, count - static_cast<std::size_t>(size.lower_), size.upper_ - size.lower_ + 1);
    };
    switch (size.form_) {
    case SizeRange::Form::none:
        put_unconstrained_length(out, count);
        return;
    case SizeRange::Form::fixed:
    case SizeRange::Form::range:
        root();
        return;
    case SizeRange::Form::extensible:
        out.put(in_root ? 0 : 1, 1);
        if (in_root) {
            root();
        } else {
            put_unconstrained_length(out, count);
        }
        return;
    }
}

// The elements in order; asn1ct ignores the table of an open type element, which takes the octets of its value.
void PerEncoder::elements(const std::int64_t element, const Term &list, BitWriter &out) {
    const bool open = content_of(schema_, schema_.node(element)).kind() == Kind::open_type;
    for (auto rest = list; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (open) {
            std::vector<std::byte> bytes;
            append_iolist(bytes, rest.head_unchecked());
            octets_with_length(bytes.empty() ? std::vector{std::byte{0}} : bytes, out);
        } else {
            encode(element, rest.head_unchecked(), out);
        }
    }
}

void PerEncoder::collection(const NodeView &node, const Term &value, BitWriter &out) {
    if (!value.is_list()) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    std::size_t count = 0;
    for (auto rest = value; rest.is_cons(); rest = rest.tail_unchecked()) {
        ++count;
    }
    if (size_range(node.field(1)).form_ == SizeRange::Form::none && count >= FRAGMENT) {
        fragmented_elements(node.small_field(0), value, count, out);
        return;
    }
    sequence_length(node.field(1), count, out);
    elements(node.small_field(0), value, out);
}

// encode_fragmented_sof/3: segments of 64K, 48K, 32K or 16K elements after their counts, then the rest after its
// length.
void PerEncoder::fragmented_elements(const std::int64_t element, const Term &list, std::size_t count, BitWriter &out) {
    auto rest = list;
    const auto take = [&](const std::size_t items) {
        std::vector<Term> taken;
        for (std::size_t i = 0; i < items; ++i, rest = rest.tail_unchecked()) {
            taken.push_back(rest.head_unchecked());
        }
        elements(element, builder_.list(taken), out);
    };
    for (std::size_t n = 4; n > 0;) {
        if (count >= n * FRAGMENT) {
            out.align();
            out.put(0xC0U | n, 8);
            take(n * FRAGMENT);
            count -= n * FRAGMENT;
        } else {
            --n;
        }
    }
    put_unconstrained_length(out, count);
    take(count);
}

// The alternative's index (a constrained number in the root, a normally small number with the extension bit for
// an addition, whose value is an open type).
namespace {
// The alternative a CHOICE value names: its node, whether it is in the root, its index among the root alternatives
// or the additions, and how many root alternatives there are.
struct Chosen {
    std::int64_t node_ = 0;
    bool root_ = true;
    std::size_t index_ = 0;
    std::size_t roots_ = 0;
};

std::optional<Chosen> chosen(const NodeView &node, const Term &name) {
    const auto alternatives = node.field(0);
    std::optional<Chosen> found;
    std::size_t counts[2] = {0, 0};
    for (std::size_t i = 0; i < alternatives.tuple_size_unchecked(); ++i) {
        const auto alternative = alternatives.tuple_element_unchecked(i);
        const bool root = alternative.tuple_element_unchecked(2).small_integer_unchecked() == 0;
        if (!found && need(alternative.tuple_element_unchecked(0).exactly_equal(name))) {
            found =
                Chosen{alternative.tuple_element_unchecked(1).small_integer_unchecked(), root, counts[root ? 0 : 1], 0};
        }
        ++counts[root ? 0 : 1];
    }
    if (found) {
        found->roots_ = counts[0];
    }
    return found;
}
} // namespace

void PerEncoder::choice(const NodeView &node, const Term &value, BitWriter &out) {
    if (!value.is_tuple() || value.tuple_size_unchecked() != 2) {
        erlang_error(abi::v1::ErrorReason::badmatch, &value);
    }
    const auto alternative = chosen(node, value.tuple_element_unchecked(0));
    if (!alternative) {
        erlang_error(abi::v1::ErrorReason::if_clause);
    }
    if (atom_is(node.field(1), "true")) {
        out.put(alternative->root_ ? 0 : 1, 1);
    }
    if (alternative->root_) {
        put_constrained(out, alternative->index_, alternative->roots_);
        encode(alternative->node_, value.tuple_element_unchecked(1), out);
    } else {
        put_normally_small(out, alternative->index_);
        as_open_type(alternative->node_, value.tuple_element_unchecked(1), out);
    }
}

// An open type: the encoding of the value its table selects, or the octets given.
void PerEncoder::open_type(const NodeView &node, const Term &value, BitWriter &out) {
    if (node.field(0).is_tuple()) {
        table_value(node, value, out);
        return;
    }
    std::vector<std::byte> bytes;
    append_iolist(bytes, value);
    if (bytes.empty()) {
        bytes.push_back(std::byte{0});
    }
    octets_with_length(bytes, out);
}

// An extensible set takes the octets of an unknown object's value as {asn1_OPENTYPE, Bytes}, and under
// legacy_erlang_types alone (legacy_open_type_to_binary/1); the generated object function has no clause for
// anything else.
void PerEncoder::unknown_object(const Term &value, BitWriter &out) {
    const bool wrapped = value.is_tuple() && value.tuple_size_unchecked() == 2 &&
                         atom_is(value.tuple_element_unchecked(0), "asn1_OPENTYPE") &&
                         value.tuple_element_unchecked(1).is_binary();
    const bool legacy = schema_.flag(FLAG_LEGACY_TYPES) && (value.is_binary() || value.is_list());
    if (!wrapped && !legacy) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    std::vector<std::byte> bytes;
    append_iolist(bytes, wrapped ? value.tuple_element_unchecked(1) : value);
    octets_with_length(bytes, out);
}

void PerEncoder::table_value(const NodeView &node, const Term &value, BitWriter &out) {
    const auto table = node.field(0);
    const auto levels = static_cast<std::size_t>(table.tuple_element_unchecked(0).small_integer_unchecked());
    const auto key = relation_key(schema_, frames_, levels, table.tuple_element_unchecked(1), schema_.flag(FLAG_MAPS));
    const auto entry = key ? need(table.tuple_element_unchecked(2).map_find(*key)) : std::nullopt;
    if (entry && entry->is_integer()) {
        as_open_type(entry->small_integer_unchecked(), value, out);
        return;
    }
    if (atom_is(table.tuple_element_unchecked(3), "true")) {
        unknown_object(value, out);
        return;
    }
    plain_exit(builder_.tuple({builder_.atom("Type not compatible with table constraint"),
                               builder_.tuple({builder_.atom("component"), table.tuple_element_unchecked(4)}),
                               builder_.tuple({builder_.atom("value"), value}),
                               builder_.tuple({builder_.atom("unique_name_and_value"), builder_.atom("_")})}));
}
} // namespace clause::runtime::builtins::asn1
