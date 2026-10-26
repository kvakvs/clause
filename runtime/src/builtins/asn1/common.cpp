#include "../../terms/integers.hpp"
#include "asn1.hpp"

// Added for ASN.1 sources (plan 21 step LIB84): the schema view, term building, errors and asn1rtt_ber:match_tags.
namespace clause::runtime::builtins::asn1 {
std::vector<Tag> NodeView::tags() const {
    std::vector<Tag> result;
    for (auto list = tuple_.tuple_element_unchecked(1); list.is_cons(); list = list.tail_unchecked()) {
        const auto encoded = static_cast<std::uint64_t>(list.head_unchecked().small_integer_unchecked());
        result.push_back({static_cast<std::uint32_t>(encoded & 3U), encoded >> 2U});
    }
    return result;
}

Schema::Schema(const Term &term) {
    const bool shaped = term.is_tuple() && term.tuple_size_unchecked() == 6 &&
                        term.tuple_element_unchecked(0).is_atom() &&
                        term.tuple_element_unchecked(0).atom_spelling_unchecked() == "asn1_schema" &&
                        term.tuple_element_unchecked(2).is_atom() && small(term.tuple_element_unchecked(3).word()) &&
                        term.tuple_element_unchecked(4).is_map() && term.tuple_element_unchecked(5).is_tuple();
    if (!shaped) {
        bad_argument();
    }
    rule_ = term.tuple_element_unchecked(2).atom_spelling_unchecked();
    flags_ = term.tuple_element_unchecked(3).small_integer_unchecked();
    types_ = term.tuple_element_unchecked(4);
    nodes_ = term.tuple_element_unchecked(5);
}

NodeView Schema::node(const std::int64_t index) const {
    return {nodes_.tuple_element_unchecked(static_cast<std::size_t>(index)), index};
}

std::int64_t Schema::root(ProcessContext &context, const Term &name) const {
    const auto found = need(types_.map_find(name));
    if (!found) {
        Builder builder(context);
        asn1_exit(builder, builder.tuple({builder.atom("undefined_type"), name}));
    }
    return found->small_integer_unchecked();
}

Term Builder::atom(const std::string_view name) { return need(factory_.atom(AtomName(name))); }

Term Builder::integer(const std::int64_t value) { return need(factory_.integer(value)); }

Term Builder::tuple(const std::initializer_list<Term> elements) {
    return need(factory_.tuple(std::span(elements.begin(), elements.size())));
}

Term Builder::tuple(const std::span<const Term> elements) { return need(factory_.tuple(elements)); }

Term Builder::list(const std::span<const Term> elements) { return need(factory_.list(elements)); }

Term Builder::binary(const std::span<const std::byte> bytes) { return need(factory_.binary(bytes)); }

Term Builder::bitstring(const std::span<const std::byte> bytes, const std::size_t bits) {
    return need(factory_.bitstring(bytes, bits));
}

// binary_to_list/1 of a string's bytes.
Term Builder::string(const std::span<const std::byte> bytes) {
    std::vector<Term> codes;
    codes.reserve(bytes.size());
    for (const auto byte : bytes) {
        codes.push_back(integer(static_cast<std::int64_t>(byte)));
    }
    return list(codes);
}

void asn1_exit(Builder &builder, const Term &reason) {
    const auto wrapped = builder.tuple({builder.atom("error"), builder.tuple({builder.atom("asn1"), reason})});
    plain_exit(wrapped);
}

void plain_exit(const Term &reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_exit, .payload = reason.word()};
}

void erlang_error(const abi::v1::ErrorReason reason, const Term *payload) {
    throw BuiltinFailure{.reason = reason, .payload = payload != nullptr ? payload->word() : 0};
}

Data Data::content(const Tlv &tlv) {
    if (tlv.constructed_) {
        return {Shape::list, nullptr, {}, tlv.children_};
    }
    return {Shape::bytes, nullptr, tlv.value_, {}};
}

Term Data::term(Builder &builder) const {
    if (shape_ == Shape::tlv) {
        return tlv_term(builder, *tlv_);
    }
    if (shape_ == Shape::bytes) {
        return builder.binary(bytes_);
    }
    std::vector<Term> items;
    for (const auto &tlv : list_) {
        items.push_back(tlv_term(builder, tlv));
    }
    return builder.list(items);
}

std::vector<std::uint64_t> decoded_tags(const std::vector<Tag> &tags) {
    std::vector<std::uint64_t> result;
    result.reserve(tags.size());
    for (const auto &tag : tags) {
        result.push_back(tag.decoded());
    }
    return result;
}

namespace {
[[noreturn]] void wrong_tag(Builder &builder, const Tlv &tlv, const std::uint64_t expected) {
    const auto got = tlv_term(builder, tlv);
    const auto tag = builder.integer(static_cast<std::int64_t>(tlv.tag_));
    const auto expected_term =
        builder.tuple({builder.atom("expected"), builder.integer(static_cast<std::int64_t>(expected))});
    const auto got_term = builder.tuple({builder.atom("got"), tag, got});
    asn1_exit(builder, builder.tuple({builder.atom("wrong_tag"), builder.tuple({expected_term, got_term})}));
}
} // namespace

namespace {
Data match_list_tags(Builder &builder, const Data &data, std::span<const std::uint64_t> tags);
} // namespace

// The clauses of asn1rtt_ber:match_tags/2, in order.
Data match_tags(Builder &builder, const Data &data, const std::span<const std::uint64_t> tags) {
    if (tags.empty()) {
        return data;
    }
    if (data.shape_ == Data::Shape::tlv) {
        if (data.tlv_->tag_ != tags.front()) {
            wrong_tag(builder, *data.tlv_, tags.front());
        }
        const auto inner = Data::content(*data.tlv_);
        return tags.size() == 1 ? inner : match_tags(builder, inner, tags.subspan(1));
    }
    if (data.shape_ == Data::Shape::list && !data.list_.empty() && data.list_.front().tag_ == tags.front()) {
        return match_list_tags(builder, data, tags);
    }
    erlang_error(abi::v1::ErrorReason::function_clause);
}

namespace {
// A list of TLVs whose first one has the outer tag: one TLV continues inside it; several match a last tag alone.
Data match_list_tags(Builder &builder, const Data &data, const std::span<const std::uint64_t> tags) {
    if (data.list_.size() == 1) {
        return match_tags(builder, Data::content(data.list_.front()), tags.subspan(1));
    }
    if (tags.size() != 1) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    return data;
}
} // namespace
} // namespace clause::runtime::builtins::asn1
