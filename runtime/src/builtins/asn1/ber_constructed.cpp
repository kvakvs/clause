#include "codec.hpp"
#include <algorithm>

// Added for ASN.1 sources (plan 21 step LIB84): BER decoding of SEQUENCE, SET, SEQUENCE OF, SET OF, CHOICE and open
// types as asn1ct_constructed_ber_bin_v2 generates it: mandatory components are taken in order, optional ones by
// their first tag, extension additions are optional, unknown additions before a second root are skipped, and an open
// type under a component relation is decoded with its object's type once the rest of its SEQUENCE is.
namespace clause::runtime::builtins::asn1 {
namespace {
Term novalue(Builder &builder) { return builder.atom("asn1_NOVALUE"); }

Data rest_of(const Data &list) { return {Data::Shape::list, nullptr, {}, list.list_.subspan(1)}; }

[[noreturn]] void bad_match(Builder &builder, const Data &data) {
    const auto term = data.term(builder);
    erlang_error(abi::v1::ErrorReason::badmatch, &term);
}

bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The Erlang reason of a failure, for errors caught and wrapped again.
Term failure_reason(Builder &builder, const BuiltinFailure &failure) {
    using abi::v1::ErrorReason;
    const auto payload = failure.payload != 0 ? need(Term::from_word(failure.payload, builder.context())) : Term();
    switch (failure.reason) {
    case ErrorReason::raised_exit:
    case ErrorReason::raised_error:
        return payload;
    case ErrorReason::badmatch:
        return builder.tuple({builder.atom("badmatch"), payload});
    case ErrorReason::case_clause:
        return builder.tuple({builder.atom("case_clause"), payload});
    case ErrorReason::function_clause:
        return builder.atom("function_clause");
    default:
        return builder.atom("badarg");
    }
}

[[noreturn]] void not_compatible(Builder &builder, const Term &reason) {
    plain_exit(builder.tuple({builder.atom("Type not compatible with table constraint"), reason}));
}
} // namespace

Term Decoder::sequence(const NodeView &node, const Data &data, const Tags tags) {
    return components(node, components_of(node), match_tags(builder_, data, tags));
}

// asn1ct sorts the components of a SET by tag under DER unless one is an untagged CHOICE, and then decodes them into
// the record in that order (its fields no longer in textual order).
std::vector<ComponentView> Decoder::set_components(const NodeView &node) {
    auto list = components_of(node);
    if (!schema_.flag(FLAG_DER)) {
        return list;
    }
    std::vector<std::pair<std::pair<std::uint32_t, std::uint64_t>, ComponentView>> keyed;
    for (const auto &item : list) {
        const auto tags = schema_.node(item.node_).tags();
        if (tags.empty()) {
            return list;
        }
        keyed.emplace_back(std::pair{tags.front().class_, tags.front().number_}, item);
    }
    std::ranges::stable_sort(keyed, [](const auto &a, const auto &b) { return a.first < b.first; });
    list.clear();
    for (const auto &[key, item] : keyed) {
        list.push_back(item);
    }
    return list;
}

Term Decoder::set(const NodeView &node, const Data &data, const Tags tags) {
    const auto content = match_tags(builder_, data, tags);
    if (content.shape_ != Data::Shape::list) {
        const auto generator = builder_.tuple({builder_.atom("bad_generator"), content.term(builder_)});
        erlang_error(abi::v1::ErrorReason::raised_error, &generator);
    }
    const auto list = set_components(node);
    const auto order = set_order(list, content);
    std::vector<Tlv> sorted;
    sorted.reserve(order.size());
    for (const auto *tlv : order) {
        sorted.push_back(*tlv);
    }
    owned_lists_.push_back(std::move(sorted));
    return components(node, list, {Data::Shape::list, nullptr, {}, owned_lists_.back()});
}

// The TLVs of a SET in the order of the components their tags belong to; unknown ones last.
std::vector<const Tlv *> Decoder::set_order(const std::vector<ComponentView> &list, const Data &content) {
    std::vector<std::pair<std::size_t, const Tlv *>> positioned;
    positioned.reserve(content.list_.size());
    for (const auto &tlv : content.list_) {
        const auto found = std::ranges::find_if(list, [&](const auto &item) { return starts_with(item.node_, tlv); });
        positioned.emplace_back(static_cast<std::size_t>(found - list.begin()), &tlv);
    }
    std::ranges::stable_sort(positioned, [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<const Tlv *> result;
    result.reserve(positioned.size());
    for (const auto &[position, tlv] : positioned) {
        result.push_back(tlv);
    }
    return result;
}

// The components of a SEQUENCE or SET from its TLVs (asn1ct's gen_dec_sequence_call2).
Term Decoder::components(const NodeView &node, const std::vector<ComponentView> &list, Data rest) {
    frames_.push_back({node.index(), {}});
    std::vector<std::optional<Data>> raw(list.size());
    bool root2 = false;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].root2_ && !root2) {
            root2 = true;
            rest = skip_additions(list, i, rest);
        }
        const bool optional = list[i].presence_ != 0 || list[i].addition_ != 0;
        frames_.back().current_ = i;
        // Decoded first: the component's own decoding pushes frames, which may move this one.
        const auto value = component(list[i], rest, optional, raw[i]);
        frames_.back().values_.push_back(value);
    }
    decode_postponed(list, raw);
    leftover(node, root2, rest);
    const auto values = std::move(frames_.back().values_);
    frames_.pop_back();
    return record(node, list, values);
}

// Open types decoded once all components are known, as their table constraints may refer to later ones.
void Decoder::decode_postponed(const std::vector<ComponentView> &list, const std::vector<std::optional<Data>> &raw) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (raw[i]) {
            const auto value = table_value(schema_.node(list[i].node_), *raw[i]);
            frames_.back().values_[i] = value;
        }
    }
}

// TLVs left after the components are an error, unless the type is extensible without components after the extensions.
void Decoder::leftover(const NodeView &node, const bool root2, const Data &rest) {
    const bool extensible = atom_is(node.field(2), "true");
    if (!(extensible && !root2) && !(rest.shape_ == Data::Shape::list && rest.list_.empty())) {
        asn1_exit(builder_, builder_.tuple({builder_.atom("unexpected"), rest.term(builder_)}));
    }
}

Term Decoder::component(const ComponentView &item, Data &rest, const bool optional, std::optional<Data> &raw) {
    auto taken = take(item, rest, optional);
    if (!taken) {
        return item.presence_ == 2 ? item.default_ : novalue(builder_);
    }
    if (postponed(item)) {
        raw = open_type_raw(taken->first, taken->second);
        return novalue(builder_);
    }
    return decode(item.node_, taken->first, taken->second);
}

// The TLV of a component and the tags left to match on it: a mandatory one is the next TLV, an optional one is the
// next TLV only when its tag starts the component's tags (any tag of an untagged CHOICE, anything for an untagged
// open type).
std::optional<std::pair<Data, std::vector<std::uint64_t>>> Decoder::take(const ComponentView &item, Data &rest,
                                                                         const bool optional) {
    auto tags = decoded_tags(schema_.node(item.node_).tags());
    if (!optional) {
        if (rest.shape_ != Data::Shape::list || rest.list_.empty()) {
            bad_match(builder_, rest);
        }
        const auto head = Data::of(rest.list_.front());
        rest = rest_of(rest);
        return std::pair{head, std::move(tags)};
    }
    if (rest.shape_ != Data::Shape::list || rest.list_.empty()) {
        return std::nullopt;
    }
    const auto &head = rest.list_.front();
    if (!tags.empty()) {
        if (head.tag_ != tags.front()) {
            return std::nullopt;
        }
        rest = rest_of(rest);
        return std::pair{Data::content(head), std::vector<std::uint64_t>(tags.begin() + 1, tags.end())};
    }
    if (!starts_untagged(item.node_, head)) {
        return std::nullopt;
    }
    rest = rest_of(rest);
    return std::pair{Data::of(head), std::vector<std::uint64_t>{}};
}

// Whether a node's encoding can start with the TLV: by its outermost tag, or as an untagged node.
bool Decoder::starts_with(const std::int64_t node, const Tlv &tlv) {
    const auto tags = decoded_tags(schema_.node(node).tags());
    return tags.empty() ? starts_untagged(node, tlv) : tags.front() == tlv.tag_;
}

// Whether an untagged node (a CHOICE or open type) can start with the TLV.
bool Decoder::starts_untagged(const std::int64_t node, const Tlv &tlv) {
    auto view = schema_.node(node);
    while (view.kind() == Kind::reference) {
        view = schema_.node(view.small_field(0));
    }
    if (view.kind() == Kind::open_type) {
        return true;
    }
    if (view.kind() != Kind::choice) {
        return false;
    }
    const auto alternatives = view.field(0);
    for (std::size_t i = 0; i < alternatives.tuple_size_unchecked(); ++i) {
        const auto alternative =
            alternatives.tuple_element_unchecked(i).tuple_element_unchecked(1).small_integer_unchecked();
        if (starts_with(alternative, tlv)) {
            return true;
        }
    }
    return false;
}

// asn1ct's skip_ExtensionAdditions: unknown TLVs up to one tagged as a second-root component, from the first one to
// the first mandatory one.
Data Decoder::skip_additions(const std::vector<ComponentView> &list, const std::size_t from, const Data &rest) {
    std::vector<std::uint64_t> root2;
    for (auto i = from; i < list.size(); ++i) {
        const auto tags = decoded_tags(schema_.node(list[i].node_).tags());
        if (!tags.empty()) {
            root2.push_back(tags.front());
        }
        if (list[i].presence_ == 0) {
            break;
        }
    }
    auto result = rest;
    while (result.shape_ == Data::Shape::list && !result.list_.empty() &&
           std::ranges::find(root2, result.list_.front().tag_) == root2.end()) {
        result = rest_of(result);
    }
    return result;
}

// An open type whose component relation is resolved in its own SEQUENCE waits for the rest of it.
bool Decoder::postponed(const ComponentView &item) {
    auto view = schema_.node(item.node_);
    while (view.kind() == Kind::reference) {
        view = schema_.node(view.small_field(0));
    }
    return view.kind() == Kind::open_type && view.field(0).is_tuple() &&
           view.field(0).tuple_element_unchecked(0).small_integer_unchecked() == 0;
}

// The record of a SEQUENCE or SET; a map of the present components under the maps option.
Term Decoder::record(const NodeView &node, const std::vector<ComponentView> &list, const std::vector<Term> &values) {
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

Term Decoder::collection(const NodeView &node, const Data &data, const Tags tags) {
    const auto content = match_tags(builder_, data, tags);
    if (content.shape_ != Data::Shape::list) {
        const auto generator = builder_.tuple({builder_.atom("bad_generator"), content.term(builder_)});
        erlang_error(abi::v1::ErrorReason::raised_error, &generator);
    }
    const auto element = node.small_field(0);
    const auto element_tags = decoded_tags(schema_.node(element).tags());
    std::vector<Term> items;
    for (const auto &tlv : content.list_) {
        items.push_back(decode(element, Data::of(tlv), element_tags));
    }
    return builder_.list(items);
}

// The alternative the TLV's tag selects; an unknown one is {asn1_ExtAlt, Bytes} in an extensible CHOICE.
Term Decoder::choice(const NodeView &node, const Data &data, const Tags tags) {
    const auto content = match_tags(builder_, data, tags);
    auto selected = content;
    if (content.shape_ == Data::Shape::list && content.list_.size() == 1) {
        selected = Data::of(content.list_.front());
    }
    if (selected.shape_ == Data::Shape::tlv) {
        if (auto value = alternative(node, *selected.tlv_)) {
            return *value;
        }
    }
    if (atom_is(node.field(1), "true")) {
        return builder_.tuple({builder_.atom("asn1_ExtAlt"), undecoded(selected)});
    }
    asn1_exit(builder_, builder_.tuple({builder_.atom("invalid_choice_tag"), selected.term(builder_)}));
}

// {Name, Value} of the alternative the TLV starts, if any.
std::optional<Term> Decoder::alternative(const NodeView &node, const Tlv &tlv) {
    const auto alternatives = node.field(0);
    for (std::size_t i = 0; i < alternatives.tuple_size_unchecked(); ++i) {
        const auto alternative = alternatives.tuple_element_unchecked(i);
        const auto type = alternative.tuple_element_unchecked(1).small_integer_unchecked();
        const auto tags = decoded_tags(schema_.node(type).tags());
        if (!tags.empty() && tags.front() == tlv.tag_) {
            const Tags inner(tags.begin() + 1, tags.end());
            return builder_.tuple({alternative.tuple_element_unchecked(0), decode(type, Data::content(tlv), inner)});
        }
        if (tags.empty() && starts_untagged(type, tlv)) {
            return builder_.tuple({alternative.tuple_element_unchecked(0), decode(type, Data::of(tlv), {})});
        }
    }
    return std::nullopt;
}

// asn1rtt_ber:ber_encode/1 of what a decoder holds: a binary as it is, a TLV (alone in its list or not) encoded.
Term Decoder::undecoded(const Data &raw) {
    if (raw.shape_ == Data::Shape::bytes) {
        return builder_.binary(raw.bytes_);
    }
    std::vector<std::byte> bytes;
    if (raw.shape_ == Data::Shape::tlv) {
        append_tlv(bytes, *raw.tlv_);
    } else if (raw.list_.size() == 1) {
        append_tlv(bytes, raw.list_.front());
    } else {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    return builder_.binary(bytes);
}

// decode_open_type/2: the TLV inside the open type's tags, decoded from a primitive value when it is one.
Data Decoder::open_type_raw(const Data &data, const Tags tags) {
    const auto value = match_tags(builder_, data, tags);
    if (value.shape_ != Data::Shape::bytes) {
        return value;
    }
    try {
        owned_.push_back(parse_tlv(value.bytes_).first);
    } catch (const TlvError &error) {
        asn1_exit(builder_, builder_.tuple({builder_.atom(error.reason_),
                                            builder_.integer(static_cast<std::int64_t>(error.position_))}));
    }
    return Data::of(owned_.back());
}

Term Decoder::open_type(const NodeView &node, const Data &data, const Tags tags) {
    if (!node.field(0).is_tuple()) {
        // decode_open_type_as_binary/2.
        return undecoded(match_tags(builder_, data, tags));
    }
    return table_value(node, open_type_raw(data, tags));
}

// The open type's value decoded as the type its object gives for the key; {asn1_OPENTYPE, Bytes} for an unknown key
// in an extensible object set. Failures are wrapped as asn1ct's generated code wraps them.
Term Decoder::table_value(const NodeView &node, const Data &raw) {
    const auto table = node.field(0);
    const auto levels = static_cast<std::size_t>(table.tuple_element_unchecked(0).small_integer_unchecked());
    const auto key = relation_key(schema_, frames_, levels, table.tuple_element_unchecked(1), schema_.flag(FLAG_MAPS));
    const auto entry = key ? need(table.tuple_element_unchecked(2).map_find(*key)) : std::nullopt;
    if (entry && entry->is_integer()) {
        try {
            return top(entry->small_integer_unchecked(), raw);
        } catch (const BuiltinFailure &failure) {
            not_compatible(builder_, failure_reason(builder_, failure));
        }
    }
    if (entry) {
        not_compatible(builder_, builder_.atom("function_clause"));
    }
    if (atom_is(table.tuple_element_unchecked(3), "true")) {
        return builder_.tuple({builder_.atom("asn1_OPENTYPE"), undecoded(raw)});
    }
    const auto component = builder_.tuple({builder_.atom("component"), table.tuple_element_unchecked(4)});
    const auto value = builder_.tuple({builder_.atom("value"), raw.term(builder_)});
    const auto unique = builder_.tuple({builder_.atom("unique_name_and_value"), table.tuple_element_unchecked(5),
                                        key ? *key : builder_.atom("undefined")});
    not_compatible(builder_, builder_.tuple({component, value, unique}));
}

namespace {
NodeView content_node(const Schema &schema, NodeView node) {
    while (node.kind() == Kind::reference) {
        node = schema.node(node.small_field(0));
    }
    return node;
}

// A record field by component name: its tuple element or map value.
std::optional<Term> field_value(const Term &value, const Term &name, const std::size_t index, const bool maps) {
    if (maps) {
        return value.is_map() ? need(value.map_find(name)) : std::nullopt;
    }
    if (value.is_tuple() && value.tuple_size_unchecked() > index + 1) {
        return value.tuple_element_unchecked(index + 1);
    }
    return std::nullopt;
}
} // namespace

namespace {
// The state of one relation_key walk: the frame still followed (none once a value was read) and the step's node.
struct RelationWalk {
    const Schema &schema_;
    const std::vector<Frame> &frames_;
    bool maps_;
    std::size_t at_;
    const Frame *frame_;
    NodeView node_;
    std::optional<Term> value_;

    // The index and the component named `name` of the current SEQUENCE or SET.
    std::optional<std::pair<std::size_t, ComponentView>> find(const Term &name) {
        node_ = content_node(schema_, node_);
        if (node_.kind() != Kind::sequence && node_.kind() != Kind::set) {
            return std::nullopt;
        }
        const auto list = components_of(node_);
        const auto found =
            std::ranges::find_if(list, [&](const auto &item) { return need(item.name_.exactly_equal(name)); });
        if (found == list.end()) {
            return std::nullopt;
        }
        return std::pair{static_cast<std::size_t>(found - list.begin()), *found};
    }

    // Reads the component's value from the frame, or the field of the value read so far.
    bool read(const Term &name, const std::size_t index) {
        if (frame_ != nullptr) {
            if (index >= frame_->values_.size()) {
                return false;
            }
            value_ = frame_->values_[index];
            frame_ = nullptr;
            return true;
        }
        value_ = value_ ? field_value(*value_, name, index, maps_) : std::nullopt;
        return value_.has_value();
    }

    bool step(const Term &name) {
        const auto found = find(name);
        if (!found) {
            return false;
        }
        const auto &[index, item] = *found;
        if (frame_ != nullptr && index == frame_->current_ && at_ + 1 < frames_.size()) {
            frame_ = &frames_[++at_];
            node_ = schema_.node(frame_->node_);
            return true;
        }
        if (!read(name, index)) {
            return false;
        }
        node_ = schema_.node(item.node_);
        return true;
    }
};
} // namespace

// The path starts at the frame `levels` above the innermost one. A step naming the component a frame is working on
// continues in the next frame; any other step reads the component's value, then the fields of that record.
std::optional<Term> relation_key(const Schema &schema, const std::vector<Frame> &frames, const std::size_t levels,
                                 const Term &path, const bool maps) {
    if (levels >= frames.size()) {
        return std::nullopt;
    }
    const auto at = frames.size() - 1 - levels;
    RelationWalk walk{schema, frames, maps, at, &frames[at], schema.node(frames[at].node_), std::nullopt};
    for (auto step = path; step.is_cons(); step = step.tail_unchecked()) {
        if (!walk.step(step.head_unchecked())) {
            return std::nullopt;
        }
    }
    return walk.frame_ == nullptr ? walk.value_ : std::nullopt;
}
} // namespace clause::runtime::builtins::asn1
