#pragma once
// Added for ASN.1 sources (plan 21 step LIB84): the codec behind clau's generated ASN.1 modules (docs/asn1.md#schema).
// A module's schema term describes its types; the encoders and decoders here walk it with OTP's BER/DER semantics,
// result shapes and error terms (asn1rtt_ber and the code asn1ct generates).
#include "../support.hpp"
#include <cstdint>
#include <span>
#include <string_view>
#include <terms.hpp>
#include <vector>

namespace clause::runtime::builtins::asn1 {
// Node kinds, numbered as the compiler's asn1::NodeKind (compiler/src/asn1/model.hpp).
enum class Kind : std::uint8_t {
    boolean,
    integer,
    enumerated,
    null,
    bit_string,
    octet_string,
    object_identifier,
    relative_oid,
    real,
    restricted_string,
    utc_time,
    generalized_time,
    object_descriptor,
    sequence,
    set,
    sequence_of,
    set_of,
    choice,
    open_type,
    reference,
};

// The schema's Flags bits (compiler/src/asn1/schema.cpp).
inline constexpr std::int64_t FLAG_DER = 1;
inline constexpr std::int64_t FLAG_MAPS = 2;
inline constexpr std::int64_t FLAG_LEGACY_TYPES = 4;
inline constexpr std::int64_t FLAG_LEGACY_BIT_STRING = 8;
inline constexpr std::int64_t FLAG_COMPACT_BIT_STRING = 16;
inline constexpr std::int64_t FLAG_UNDEC_REST = 32;
// Added for PER: uper's no_final_padding.
inline constexpr std::int64_t FLAG_NO_FINAL_PADDING = 64;

// A tag: its class (universal 0, application 1, context 2, private 3) and number.
struct Tag {
    std::uint32_t class_ = 0;
    std::uint64_t number_ = 0;

    // The tag as OTP's BER decoder compares it: (Class bsl 16) bor Number.
    std::uint64_t decoded() const { return (static_cast<std::uint64_t>(class_) << 16U) | number_; }
};

// One node of a schema: {Kind, Tags, Fields...} (docs/asn1.md#schema).
class NodeView {
  public:
    NodeView(const Term &tuple, const std::int64_t index) : tuple_(tuple), index_(index) {}

    // The node's number in the schema.
    std::int64_t index() const { return index_; }

    Kind kind() const { return static_cast<Kind>(tuple_.tuple_element_unchecked(0).small_integer_unchecked()); }

    std::vector<Tag> tags() const;

    Term field(const std::size_t index) const { return tuple_.tuple_element_unchecked(2 + index); }

    std::int64_t small_field(const std::size_t index) const { return field(index).small_integer_unchecked(); }

  private:
    Term tuple_;
    std::int64_t index_ = 0;
};

// The schema term of a generated module: {asn1_schema, 1, Rule, Flags, #{Type => Node}, {Nodes...}}.
class Schema {
  public:
    // Reads the term; badarg when it is not a schema.
    explicit Schema(const Term &term);

    NodeView node(std::int64_t index) const;
    // The node of a type name; exit({error, {asn1, {undefined_type, Name}}}) as asn1ct's dispatchers for others.
    std::int64_t root(ProcessContext &context, const Term &name) const;

    bool flag(const std::int64_t bit) const { return (flags_ & bit) != 0; }

    std::string_view rule() const { return rule_; }

  private:
    Term nodes_;
    Term types_;
    std::int64_t flags_ = 0;
    std::string_view rule_;
};

// Builds terms of the calling process.
class Builder {
  public:
    explicit Builder(ProcessContext &context) : context_(context), factory_(context) {}

    ProcessContext &context() const { return context_; }

    Term atom(std::string_view name);
    Term integer(std::int64_t value);
    Term tuple(std::initializer_list<Term> elements);
    Term tuple(std::span<const Term> elements);
    Term list(std::span<const Term> elements);
    Term binary(std::span<const std::byte> bytes);
    Term bitstring(std::span<const std::byte> bytes, std::size_t bits);
    Term string(std::span<const std::byte> bytes);

    TermFactory &factory() { return factory_; }

  private:
    ProcessContext &context_;
    TermFactory factory_;
};

// exit({error, {asn1, Reason}}), as asn1rtt_ber and asn1ct's generated code raise their errors.
[[noreturn]] void asn1_exit(Builder &builder, const Term &reason);
// exit(Reason) with no wrapping (table constraint failures).
[[noreturn]] void plain_exit(const Term &reason);
// error(Kind) with a payload, as a failed match in generated code (badmatch, case_clause), or without one
// (function_clause, badarg).
[[noreturn]] void erlang_error(abi::v1::ErrorReason reason, const Term *payload = nullptr);

// One BER TLV of the input, as asn1rt_nif:decode_ber_tlv/1 decodes it: {Tag, Value}, Value a binary for a primitive
// encoding and a list of TLVs for a constructed one (definite or indefinite length).
struct Tlv {
    std::uint64_t tag_ = 0;
    bool constructed_ = false;
    std::span<const std::byte> value_;
    std::vector<Tlv> children_;
};

// asn1rt_nif's decode errors: {Reason, Position} with Reason invalid_tag, invalid_length, invalid_value or unknown.
struct TlvError {
    std::string_view reason_;
    std::size_t position_ = 0;
};

// Decode one TLV from the start of `input`; returns it and the length it takes. Throws TlvError.
std::pair<Tlv, std::size_t> parse_tlv(std::span<const std::byte> input);
// The TLV as the Erlang term the NIF builds.
Term tlv_term(Builder &builder, const Tlv &tlv);
// The TLV encoded again with definite lengths, as asn1rt_nif:encode_ber_tlv/1 (ber_encode/1).
void append_tlv(std::vector<std::byte> &out, const Tlv &tlv);

// What the generated decoders pass around: a whole TLV, the binary of a primitive one, or the TLVs of a
// constructed one.
struct Data {
    enum class Shape : std::uint8_t { tlv, bytes, list };
    Shape shape_ = Shape::bytes;
    const Tlv *tlv_ = nullptr;
    std::span<const std::byte> bytes_;
    std::span<const Tlv> list_;

    static Data of(const Tlv &tlv) { return {Shape::tlv, &tlv, {}, {}}; }

    static Data content(const Tlv &tlv);
    Term term(Builder &builder) const;
};

// asn1rtt_ber:match_tags/2: strips the expected tags, failing with wrong_tag or function_clause.
Data match_tags(Builder &builder, const Data &data, std::span<const std::uint64_t> tags);
// The tags of a node as the decoder compares them.
std::vector<std::uint64_t> decoded_tags(const std::vector<Tag> &tags);
} // namespace clause::runtime::builtins::asn1
