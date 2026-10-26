#include "per.hpp"
#include <algorithm>

// Added for PER (plan 21 step LIB84): SEQUENCE, SET, SEQUENCE OF, CHOICE and open types as asn1ct_constructed_per
// decodes them: extension bit, presence bitmap, root components, then the extension additions' open types.
namespace clause::runtime::builtins::asn1 {
namespace {
bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

constexpr std::size_t FRAGMENT = 16384;

Term novalue(Builder &builder) { return builder.atom("asn1_NOVALUE"); }

// The value of a component the encoding leaves out: its default, else asn1_NOVALUE.
Term absent_value(Builder &builder, const ComponentView &item) {
    return item.presence_ == 2 ? item.default_ : novalue(builder);
}

// Root components in decoding order: textual, or by tag in a SET (as the encoder orders them).
std::vector<std::size_t> decoding_order(const Schema &schema, const NodeView &node,
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

bool table_open_type(const Schema &schema, const ComponentView &item) {
    const auto node = content_of(schema, schema.node(item.node_));
    return node.kind() == Kind::open_type && node.field(0).is_tuple();
}
} // namespace

Term PerDecoder::sequence(const NodeView &node, BitReader &in) { return components(node, in); }

Term PerDecoder::components(const NodeView &node, BitReader &in) {
    const auto list = components_of(node);
    frames_.push_back({node.index(), std::vector<Term>(list.size(), novalue(builder_))});
    const bool extended = atom_is(node.field(2), "true") && in.get(1) != 0;
    std::vector<std::optional<std::vector<std::byte>>> raw(list.size());
    root_components(list, in, raw);
    if (extended) {
        extension_components(list, in);
    } else {
        for (const auto first : addition_starts(list)) {
            absent_addition(list, first);
        }
    }
    decode_postponed(list, raw);
    const auto values = std::move(frames_.back().values_);
    frames_.pop_back();
    return record(node, list, values);
}

// The presence bitmap of the OPTIONAL and DEFAULT root components (textual order), then those present.
void PerDecoder::root_components(const std::vector<ComponentView> &list, BitReader &in,
                                 std::vector<std::optional<std::vector<std::byte>>> &raw) {
    std::vector<bool> present(list.size(), true);
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].addition_ == 0 && list[i].presence_ != 0) {
            present[i] = in.get(1) != 0;
        }
    }
    for (const auto i : decoding_order(schema_, schema_.node(frames_.back().node_), list)) {
        frames_.back().current_ = i;
        const auto value = component(list[i], present[i], in, raw[i]);
        frames_.back().values_[i] = value;
    }
}

Term PerDecoder::component(const ComponentView &item, const bool present, BitReader &in,
                           std::optional<std::vector<std::byte>> &raw) {
    if (!present) {
        return absent_value(builder_, item);
    }
    if (table_open_type(schema_, item)) {
        raw = open_octets(in);
        return novalue(builder_);
    }
    return decode(item.node_, in);
}

// The additions' bitmap, the known additions present, and the unknown ones skipped (asn1rtt_per:skipextensions/3).
void PerDecoder::extension_components(const std::vector<ComponentView> &list, BitReader &in) {
    const auto count = get_small_length(builder_, in);
    const auto bitmap = in.get_bits(count);
    const auto starts = addition_starts(list);
    for (std::size_t slot = 0; slot < starts.size(); ++slot) {
        if (slot < count && bit_at(bitmap, slot)) {
            addition(list, starts[slot], open_octets(in));
        } else {
            absent_addition(list, starts[slot]);
        }
    }
    for (auto slot = starts.size(); slot < count; ++slot) {
        if (bit_at(bitmap, slot)) {
            skip_open_type(builder_, in);
        }
    }
}

// A present addition decoded from its open type: one component, or an addition group's components.
void PerDecoder::addition(const std::vector<ComponentView> &list, const std::size_t first,
                          const std::span<const std::byte> bytes) {
    BitReader inner(bytes, bytes.size() * 8, aligned_, &builder_);
    if (!list[first].group_) {
        frames_.back().current_ = first;
        const auto value = decode(list[first].node_, inner);
        frames_.back().values_[first] = value;
        return;
    }
    std::size_t end = first;
    while (end < list.size() && list[end].addition_ == list[first].addition_) {
        ++end;
    }
    std::vector<bool> present(end - first, true);
    for (auto i = first; i < end; ++i) {
        if (list[i].presence_ != 0) {
            present[i - first] = inner.get(1) != 0;
        }
    }
    for (auto i = first; i < end; ++i) {
        frames_.back().current_ = i;
        const auto value = present[i - first] ? decode(list[i].node_, inner) : absent_value(builder_, list[i]);
        frames_.back().values_[i] = value;
    }
}

void PerDecoder::absent_addition(const std::vector<ComponentView> &list, const std::size_t first) {
    for (auto i = first; i < list.size() && list[i].addition_ == list[first].addition_; ++i) {
        frames_.back().values_[i] = absent_value(builder_, list[i]);
    }
}

// Open types decoded once all components are known: their table may key on a later component.
void PerDecoder::decode_postponed(const std::vector<ComponentView> &list,
                                  const std::vector<std::optional<std::vector<std::byte>>> &raw) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (raw[i]) {
            const auto value = table_value(content_of(schema_, schema_.node(list[i].node_)), *raw[i]);
            frames_.back().values_[i] = value;
        }
    }
}

Term PerDecoder::record(const NodeView &node, const std::vector<ComponentView> &list, const std::vector<Term> &values) {
    if (!schema_.flag(FLAG_MAPS)) {
        std::vector<Term> fields{node.field(0)};
        fields.insert(fields.end(), values.begin(), values.end());
        return builder_.tuple(fields);
    }
    std::vector<std::pair<Term, Term>> entries;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!atom_is(values[i], "asn1_NOVALUE")) {
            entries.emplace_back(list[i].name_, values[i]);
        }
    }
    return need(builder_.factory().map(entries));
}

Term PerDecoder::from_open_type(const std::int64_t node, const std::span<const std::byte> bytes) {
    BitReader inner(bytes, bytes.size() * 8, aligned_, &builder_);
    return decode(node, inner);
}

Term PerDecoder::table_value(const NodeView &node, const std::span<const std::byte> bytes) {
    const auto table = node.field(0);
    const auto levels = static_cast<std::size_t>(table.tuple_element_unchecked(0).small_integer_unchecked());
    const auto key = relation_key(schema_, frames_, levels, table.tuple_element_unchecked(1), schema_.flag(FLAG_MAPS));
    const auto entry = key ? need(table.tuple_element_unchecked(2).map_find(*key)) : std::nullopt;
    if (entry && entry->is_integer()) {
        return from_open_type(entry->small_integer_unchecked(), bytes);
    }
    // An unknown object's value: {asn1_OPENTYPE, Bytes}, the octets alone under legacy_erlang_types.
    if (atom_is(table.tuple_element_unchecked(3), "true")) {
        const auto octets = builder_.binary(bytes);
        return schema_.flag(FLAG_LEGACY_TYPES) ? octets : builder_.tuple({builder_.atom("asn1_OPENTYPE"), octets});
    }
    plain_exit(builder_.tuple({builder_.atom("Type not compatible with table constraint"),
                               builder_.tuple({builder_.atom("component"), table.tuple_element_unchecked(4)}),
                               builder_.tuple({builder_.atom("value"), builder_.binary(bytes)}),
                               builder_.tuple({builder_.atom("unique_name_and_value"), table.tuple_element_unchecked(5),
                                               key ? *key : builder_.atom("undefined")})}));
}

// per_dec_length/3 of a SEQUENCE OF: a fixed count, a constrained or extensible one.
std::size_t PerDecoder::sequence_length(const Term &bounds, BitReader &in) {
    const auto size = size_range(bounds);
    const auto lower = static_cast<std::size_t>(size.lower_);
    const auto range = static_cast<std::size_t>(size.upper_ - size.lower_ + 1);
    switch (size.form_) {
    case SizeRange::Form::fixed:
        return lower;
    case SizeRange::Form::range:
        return lower + static_cast<std::size_t>(get_constrained(in, range));
    case SizeRange::Form::extensible:
        if (in.get(1) == 0) {
            return lower + static_cast<std::size_t>(get_constrained(in, range));
        }
        return get_unconstrained_length(builder_, in, true);
    case SizeRange::Form::none:
        break;
    }
    return get_unconstrained_length(builder_, in, true);
}

// Elements after a count; an unconstrained count of 16K or more is a fragment followed by another count.
Term PerDecoder::collection(const NodeView &node, BitReader &in) {
    const auto element = node.small_field(0);
    const bool fragmented = size_range(node.field(1)).form_ == SizeRange::Form::none;
    std::vector<Term> items;
    for (;;) {
        const auto count = sequence_length(node.field(1), in);
        for (std::size_t i = 0; i < count; ++i) {
            items.push_back(decode(element, in));
        }
        if (!fragmented || count < FRAGMENT) {
            return builder_.list(items);
        }
    }
}

// The alternative's index, its value; an unknown addition is {asn1_ExtAlt, Bytes}.
Term PerDecoder::choice(const NodeView &node, BitReader &in) {
    const auto alternatives = node.field(0);
    std::vector<Term> roots;
    std::vector<Term> additions;
    for (std::size_t i = 0; i < alternatives.tuple_size_unchecked(); ++i) {
        const auto alternative = alternatives.tuple_element_unchecked(i);
        (alternative.tuple_element_unchecked(2).small_integer_unchecked() == 0 ? roots : additions)
            .push_back(alternative);
    }
    if (atom_is(node.field(1), "true") && in.get(1) != 0) {
        const auto index = get_normally_small(builder_, in);
        const auto bytes = open_octets(in);
        if (index >= additions.size()) {
            return builder_.tuple({builder_.atom("asn1_ExtAlt"), builder_.binary(bytes)});
        }
        const auto chosen = additions[index];
        return builder_.tuple({chosen.tuple_element_unchecked(0),
                               from_open_type(chosen.tuple_element_unchecked(1).small_integer_unchecked(), bytes)});
    }
    const auto index = static_cast<std::size_t>(get_constrained(in, roots.size()));
    if (index >= roots.size()) {
        const auto value = builder_.integer(static_cast<std::int64_t>(index));
        erlang_error(abi::v1::ErrorReason::case_clause, &value);
    }
    const auto chosen = roots[index];
    return builder_.tuple(
        {chosen.tuple_element_unchecked(0), decode(chosen.tuple_element_unchecked(1).small_integer_unchecked(), in)});
}

// An open type alone: its octets.
Term PerDecoder::open_type(const NodeView & /*node*/, BitReader &in) { return builder_.binary(open_octets(in)); }

// per_dec_open_type/1: octets read as an unconstrained string.
std::vector<std::byte> PerDecoder::open_octets(BitReader &in) {
    std::size_t count = 0;
    return get_string(builder_, in, 8, {}, StringKind::octets, count);
}
} // namespace clause::runtime::builtins::asn1
