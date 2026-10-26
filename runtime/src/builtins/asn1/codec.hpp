#pragma once
// Added for ASN.1 sources (plan 21 step LIB84): the BER/DER encoder and decoder over a schema, and the value
// conversions they share.
#include "../../terms/integers.hpp"
#include "asn1.hpp"
#include <deque>
#include <optional>

namespace clause::runtime::builtins::asn1 {
using Tags = std::span<const std::uint64_t>;

// A component of a SEQUENCE or SET node: {Name, Node, Presence, Default, Addition, Root2}.
struct ComponentView {
    Term name_;
    std::int64_t node_ = 0;
    // 0 required, 1 optional, 2 default.
    std::int64_t presence_ = 0;
    Term default_;
    std::int64_t addition_ = 0;
    bool root2_ = false;
};

std::vector<ComponentView> components_of(const NodeView &node);

// A SEQUENCE or SET being encoded or decoded: its node and its component values so far, which component relation
// constraints read their keys from.
struct Frame {
    std::int64_t node_ = 0;
    std::vector<Term> values_;
    // The component being encoded or decoded; a component relation path through it continues in the next frame.
    std::size_t current_ = SIZE_MAX;
};

// The value a component relation names: `levels` frames above the innermost one, then `path` through records.
std::optional<Term> relation_key(const Schema &schema, const std::vector<Frame> &frames, std::size_t levels,
                                 const Term &path, bool maps);

// Signed big-endian two's complement integers (X.690 8.3).
Term integer_from_bytes(Builder &builder, std::span<const std::byte> bytes);
std::vector<std::byte> integer_bytes(const detail::Integer &value);
detail::Integer integer_of(const Term &term);

// OTP's REAL representations (asn1rtt_real_common): decode_real/1 and encode_real/1.
Term decode_real(Builder &builder, std::span<const std::byte> bytes);
std::vector<std::byte> encode_real(Builder &builder, const Term &value);

class Decoder {
  public:
    Decoder(Builder &builder, const Schema &schema) : builder_(builder), schema_(schema) {}

    // Decode a node's value from data matched against `tags` (the tags at the place of use).
    Term decode(std::int64_t node, const Data &data, Tags tags);
    Term top(std::int64_t node, const Data &data);

  private:
    using Method = Term (Decoder::*)(const NodeView &, const Data &, Tags);
    Term boolean(const NodeView &node, const Data &data, Tags tags);
    Term integer(const NodeView &node, const Data &data, Tags tags);
    Term enumerated(const NodeView &node, const Data &data, Tags tags);
    Term null(const NodeView &node, const Data &data, Tags tags);
    Term bit_string(const NodeView &node, const Data &data, Tags tags);
    Term octet_string(const NodeView &node, const Data &data, Tags tags);
    Term object_identifier(const NodeView &node, const Data &data, Tags tags);
    Term relative_oid(const NodeView &node, const Data &data, Tags tags);
    Term real(const NodeView &node, const Data &data, Tags tags);
    Term restricted_string(const NodeView &node, const Data &data, Tags tags);
    Term simple_string(const NodeView &node, const Data &data, Tags tags);
    Term sequence(const NodeView &node, const Data &data, Tags tags);
    Term set(const NodeView &node, const Data &data, Tags tags);
    Term collection(const NodeView &node, const Data &data, Tags tags);
    Term choice(const NodeView &node, const Data &data, Tags tags);
    Term open_type(const NodeView &node, const Data &data, Tags tags);
    Term reference(const NodeView &node, const Data &data, Tags tags);

    std::span<const std::byte> primitive(const Data &data, Tags tags);
    std::vector<std::byte> collected(const Data &data, Tags tags);
    Term bits(const NodeView &node, std::span<const std::byte> bytes);
    void check_size(const Term &bounds, std::size_t size);
    Term unnamed_bits(std::size_t unused, std::span<const std::byte> body, std::size_t size);
    Term named_bits(const Term &named, std::span<const std::byte> body, std::size_t size);

    // Constructed types (ber_constructed.cpp).
    Term components(const NodeView &node, const std::vector<ComponentView> &list, Data rest);
    void decode_postponed(const std::vector<ComponentView> &list, const std::vector<std::optional<Data>> &raw);
    void leftover(const NodeView &node, bool root2, const Data &rest);
    std::vector<ComponentView> set_components(const NodeView &node);
    Term component(const ComponentView &item, Data &rest, bool optional, std::optional<Data> &raw);
    std::optional<std::pair<Data, std::vector<std::uint64_t>>> take(const ComponentView &item, Data &rest,
                                                                    bool optional);
    Data skip_additions(const std::vector<ComponentView> &list, std::size_t from, const Data &rest);
    bool starts_with(std::int64_t node, const Tlv &tlv);
    bool starts_untagged(std::int64_t node, const Tlv &tlv);
    std::optional<Term> alternative(const NodeView &node, const Tlv &tlv);
    bool postponed(const ComponentView &item);
    Term record(const NodeView &node, const std::vector<ComponentView> &list, const std::vector<Term> &values);
    Data open_type_raw(const Data &data, Tags tags);
    Term table_value(const NodeView &node, const Data &raw);
    Term undecoded(const Data &raw);
    std::vector<const Tlv *> set_order(const std::vector<ComponentView> &list, const Data &content);

    Builder &builder_;
    const Schema &schema_;
    std::vector<Frame> frames_;
    // TLVs decoded again from open type binaries, and the TLVs of SETs in component order; deques keep addresses.
    std::deque<Tlv> owned_;
    std::deque<std::vector<Tlv>> owned_lists_;
};

// get_size_constraint/1 of asn1ct: none, an exact size, or {Min, Max} (no upper bound for MAX).
struct SizeLimit {
    bool present_ = false;
    bool exact_ = false;
    std::int64_t lower_ = 0;
    std::optional<std::int64_t> upper_;
};

class Encoder {
  public:
    Encoder(Builder &builder, const Schema &schema) : builder_(builder), schema_(schema) {}

    // Append the encoding of `value` as node `node` with `tags` (the tags at the place of use).
    void encode(std::int64_t node, const Term &value, std::span<const Tag> tags, std::vector<std::byte> &out);
    std::vector<std::byte> top(std::int64_t node, const Term &value);

  private:
    using Method = void (Encoder::*)(const NodeView &, const Term &, std::vector<std::byte> &);
    void boolean(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void integer(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void enumerated(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void null(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void bit_string(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void octets(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void object_identifier(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void relative_oid(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void real(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void wide_character(const Term &character, std::size_t width, std::vector<std::byte> &out);
    void restricted_string(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void sequence(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void collection(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void choice(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void open_type(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void reference(const NodeView &node, const Term &value, std::vector<std::byte> &out);

    std::int64_t named_position(const NodeView &node, const Term &item);
    std::vector<Term> record_fields(const std::vector<ComponentView> &list, const Term &value);
    std::vector<Term> map_fields(const std::vector<ComponentView> &list, const Term &value);
    std::vector<Term> fields(const NodeView &node, const std::vector<ComponentView> &list, const Term &value);
    bool omitted(const ComponentView &component, const Term &value);
    bool der_default(std::int64_t node, const Term &value, const Term &fallback);
    void table_value(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void unknown_object_value(const Term &value, std::vector<std::byte> &out);
    void sorted_components(std::vector<std::vector<std::byte>> &parts, std::vector<std::byte> &out);
    // BIT STRING values under legacy_erlang_types (legacy.cpp).
    void legacy_bits(const NodeView &node, const Term &value, std::vector<std::byte> &out);
    void legacy_compact(const SizeLimit &limit, std::size_t unused, std::vector<std::byte> bytes,
                        std::vector<std::byte> &out);
    void legacy_list(const SizeLimit &limit, std::vector<int> bits, std::vector<std::byte> &out);
    void legacy_named(const NodeView &node, const SizeLimit &limit, const Term &value, std::vector<std::byte> &out);

    Builder &builder_;
    const Schema &schema_;
    std::vector<Frame> frames_;
    // The node whose tags are being written, for the constructed bit of its innermost tag.
    bool constructed_ = false;
};

// The bytes of an iolist (iolist_to_binary/1), badarg for anything else.
void append_iolist(std::vector<std::byte> &out, const Term &term);
// Append the tag and length octets of a TLV in front of content appended at `start`.
void wrap(std::vector<std::byte> &out, std::size_t start, std::span<const Tag> tags, bool constructed);
// asn1rtt_ber's collect_parts/1 over the TLVs of a constructed string.
std::vector<std::byte> collect_parts(std::span<const Tlv> parts);
// Bit `index` of a bitstring's bytes, most significant first.
bool bit_at(std::span<const std::byte> bytes, std::size_t index);
} // namespace clause::runtime::builtins::asn1
