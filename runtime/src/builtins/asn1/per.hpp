#pragma once
// Added for PER (plan 21 step LIB84): the aligned (per) and unaligned (uper) packed encoding rules over a schema, as
// asn1ct's generated PER code and asn1rtt_per/asn1rtt_uper/asn1rtt_per_common encode and decode (X.691).
#include "codec.hpp"
#include <cstddef>

namespace clause::runtime::builtins::asn1 {
// Bits appended most significant first; align() pads with zero bits to the next octet of the whole encoding, as
// asn1rtt_per:complete/1 resolves its `align` markers.
class BitWriter {
  public:
    explicit BitWriter(const bool aligned) : aligned_(aligned) {}

    void put(std::uint64_t value, std::size_t bits);
    void put_integer(const detail::Integer &value, std::size_t bits);
    void put_bits(std::span<const std::byte> bytes, std::size_t bits);

    void put_bytes(std::span<const std::byte> bytes) { put_bits(bytes, bytes.size() * 8); }

    // Pads to an octet boundary in aligned PER; nothing in unaligned PER.
    void align();
    void pad_to_octet();

    bool aligned() const { return aligned_; }

    std::size_t bits() const { return bits_; }

    const std::vector<std::byte> &bytes() const { return bytes_; }

  private:
    void put_bit(bool bit);

    // The encoding so far; the last octet holds bits_ % 8 valid bits.
    std::vector<std::byte> bytes_;
    std::size_t bits_ = 0;
    bool aligned_ = false;
};

// Reads bits most significant first; align() skips to the next octet of the buffer, as the generated decoders
// align on the bits left in their binary. Running out of bits fails as the generated match does: badmatch on the
// bits from where the read began, or case_clause on those from where an enclosing case began (CaseScope).
class BitReader {
  public:
    BitReader(std::span<const std::byte> bytes, std::size_t bits, const bool aligned, Builder *builder = nullptr)
        : bytes_(bytes), end_(bits), aligned_(aligned), builder_(builder) {}

    std::uint64_t get(std::size_t bits);
    detail::Integer get_integer(std::size_t bits);
    // `bits` bits as a bitstring's bytes (the last one padded with zero bits).
    std::vector<std::byte> get_bits(std::size_t bits);
    void align();

    bool aligned() const { return aligned_; }

    std::size_t remaining() const { return end_ - at_; }

    // The bits not read yet, as a bitstring term.
    Term rest(Builder &builder) const;

    // Where an enclosing case began, else NO_CASE.
    static constexpr std::size_t NO_CASE = SIZE_MAX;
    std::size_t case_at_ = NO_CASE;
    // The enclosing case is a function's clauses: function_clause without a payload.
    bool function_clauses_ = false;

    std::size_t position() const { return at_; }

    // Fail as a generated case on the bits from `from`.
    [[noreturn]] void fail_case(std::size_t from) const;

  private:
    bool get_bit();
    void need(std::size_t bits) const;
    Term bits_from(std::size_t from) const;

    std::span<const std::byte> bytes_;
    std::size_t at_ = 0;
    std::size_t end_ = 0;
    bool aligned_ = false;
    // Where the padding before the next read began, else NO_CASE.
    std::size_t read_start_ = NO_CASE;
    // Builds the failure payloads; none for readers of decoded strings.
    Builder *builder_ = nullptr;
};

// A `case` of the generated decoder: running out of bits inside it fails with case_clause on the bits it began at,
// or (a runtime function's clauses) with function_clause.
class CaseScope {
  public:
    explicit CaseScope(BitReader &in, const bool clauses = false)
        : in_(in), saved_(in.case_at_), saved_clauses_(in.function_clauses_) {
        in.case_at_ = in.position();
        in.function_clauses_ = clauses;
    }

    ~CaseScope() {
        in_.case_at_ = saved_;
        in_.function_clauses_ = saved_clauses_;
    }

    CaseScope(const CaseScope &) = delete;
    CaseScope &operator=(const CaseScope &) = delete;

  private:
    BitReader &in_;
    std::size_t saved_;
    bool saved_clauses_;
};

// asn1ct_imm:effective_constraint(bitstring, ...) of a SIZE bound: none, a fixed size, a range, or an extensible
// range; bounds of 64K and more are none.
struct SizeRange {
    enum class Form : std::uint8_t { none, fixed, range, extensible };
    Form form_ = Form::none;
    std::int64_t lower_ = 0;
    std::int64_t upper_ = 0;
};

SizeRange size_range(const Term &bounds);
// uper_num_bits/1: the bits a constrained whole number of `range` values takes in unaligned PER.
std::size_t uper_bits(const detail::Integer &range);

// Lengths and whole numbers (per_lengths.cpp).
void put_constrained(BitWriter &out, const detail::Integer &offset, const detail::Integer &range);
detail::Integer get_constrained(BitReader &in, const detail::Integer &range);
void put_unconstrained_length(BitWriter &out, std::size_t length);
std::size_t get_unconstrained_length(Builder &builder, BitReader &in, bool allow_zero);
void put_semi_constrained(BitWriter &out, const detail::Integer &offset);
detail::Integer get_semi_constrained(Builder &builder, BitReader &in);
void put_normally_small(BitWriter &out, std::uint64_t number);
std::uint64_t get_normally_small(Builder &builder, BitReader &in);
void put_small_length(BitWriter &out, std::size_t length);
std::size_t get_small_length(Builder &builder, BitReader &in);
void skip_open_type(Builder &builder, BitReader &in);
// A string's units after its length determinant: `unit` bits each, aligned as per_enc_length/6 aligns them.
enum class StringKind : std::uint8_t { octets, bits, characters };
void put_string(BitWriter &out, std::span<const std::byte> content, std::size_t count, std::size_t unit,
                const SizeRange &size, StringKind kind);
std::vector<std::byte> get_string(Builder &builder, BitReader &in, std::size_t unit, const SizeRange &size,
                                  StringKind kind, std::size_t &count);

class PerEncoder {
  public:
    PerEncoder(Builder &builder, const Schema &schema) : builder_(builder), schema_(schema) {}

    // The complete encoding: whole octets (at least one), or the bits alone under no_final_padding.
    BitWriter top(std::int64_t node, const Term &value);
    void encode(std::int64_t node, const Term &value, BitWriter &out);

  private:
    using Method = void (PerEncoder::*)(const NodeView &, const Term &, BitWriter &);
    void boolean(const NodeView &node, const Term &value, BitWriter &out);
    void integer(const NodeView &node, const Term &value, BitWriter &out);
    void enumerated(const NodeView &node, const Term &value, BitWriter &out);
    void null(const NodeView &node, const Term &value, BitWriter &out);
    void bit_string(const NodeView &node, const Term &value, BitWriter &out);
    void octet_string(const NodeView &node, const Term &value, BitWriter &out);
    void object_identifier(const NodeView &node, const Term &value, BitWriter &out);
    void relative_oid(const NodeView &node, const Term &value, BitWriter &out);
    void real(const NodeView &node, const Term &value, BitWriter &out);
    void restricted_string(const NodeView &node, const Term &value, BitWriter &out);
    void time(const NodeView &node, const Term &value, BitWriter &out);
    void descriptor(const NodeView &node, const Term &value, BitWriter &out);
    void sequence(const NodeView &node, const Term &value, BitWriter &out);
    void collection(const NodeView &node, const Term &value, BitWriter &out);
    void choice(const NodeView &node, const Term &value, BitWriter &out);
    void open_type(const NodeView &node, const Term &value, BitWriter &out);
    void reference(const NodeView &node, const Term &value, BitWriter &out);

    // Primitive helpers (per_encode.cpp).
    void whole_number(const Term &bounds, const Term &value, BitWriter &out);
    void extensible_number(const Term &bounds, const detail::Integer &value, BitWriter &out);
    void root_number(const Term &bounds, const detail::Integer &value, BitWriter &out);
    void bits_value(const NodeView &node, const Term &value, BitWriter &out);
    std::pair<std::vector<std::byte>, std::size_t> named_bits(const NodeView &node, const Term &value,
                                                              const SizeRange &size);
    void known_multiplier(const NodeView &node, const Term &value, std::uint64_t tag, BitWriter &out);
    std::vector<std::byte> characters(const Term &value, std::size_t unit, const std::vector<std::uint32_t> &table);
    void octets_with_length(std::span<const std::byte> content, BitWriter &out);

    // Constructed helpers (per_encode_constructed.cpp).
    std::vector<Term> fields(const std::vector<ComponentView> &list, const Term &value);
    std::vector<Term> record_fields(std::size_t count, const Term &value);
    bool bits_default(const NodeView &node, const Term &value, const Term &fallback);
    void unknown_object(const Term &value, BitWriter &out);
    bool absent(const ComponentView &component, const Term &value, bool extension);
    bool default_value(const ComponentView &component, const Term &value);

    // The extension additions as the generated encoder reads them: per addition (a component or a group) its value
    // and presence bit, and the index of its first component.
    struct Additions {
        std::vector<std::size_t> starts_;
        std::vector<Term> values_;
        std::vector<bool> bitmap_;
    };

    Additions additions(const std::vector<ComponentView> &list, std::vector<Term> record);
    std::vector<Term> slot_values(const std::vector<ComponentView> &list, std::vector<Term> &record,
                                  const std::vector<std::size_t> &starts);
    Term packed_group(const std::vector<ComponentView> &list, std::size_t first, const std::vector<Term> &record);
    void presence_bits(const std::vector<ComponentView> &list, const std::vector<Term> &values,
                       const std::vector<std::size_t> &indexes, BitWriter &out);
    void present_components(const std::vector<ComponentView> &list, const std::vector<Term> &values,
                            const std::vector<std::size_t> &indexes, BitWriter &out);
    void extension_additions(const std::vector<ComponentView> &list, const Additions &slots, BitWriter &out);
    void complete_open_type(BitWriter &inner, BitWriter &out);
    void group_value(const std::vector<ComponentView> &list, std::size_t first, const Term &packed, BitWriter &out);
    void as_open_type(std::int64_t node, const Term &value, BitWriter &out);
    void sequence_length(const Term &bounds, std::size_t count, BitWriter &out);
    void elements(std::int64_t element, const Term &list, BitWriter &out);
    void fragmented_elements(std::int64_t element, const Term &list, std::size_t count, BitWriter &out);
    void table_value(const NodeView &node, const Term &value, BitWriter &out);

    Builder &builder_;
    const Schema &schema_;
    std::vector<Frame> frames_;
};

class PerDecoder {
  public:
    PerDecoder(Builder &builder, const Schema &schema)
        : builder_(builder), schema_(schema), aligned_(schema.rule() == "per") {}

    Term decode(std::int64_t node, BitReader &in);

  private:
    using Method = Term (PerDecoder::*)(const NodeView &, BitReader &);
    Term boolean(const NodeView &node, BitReader &in);
    Term integer(const NodeView &node, BitReader &in);
    Term enumerated(const NodeView &node, BitReader &in);
    Term null(const NodeView &node, BitReader &in);
    Term bit_string(const NodeView &node, BitReader &in);
    Term octet_string(const NodeView &node, BitReader &in);
    Term object_identifier(const NodeView &node, BitReader &in);
    Term relative_oid(const NodeView &node, BitReader &in);
    Term real(const NodeView &node, BitReader &in);
    Term restricted_string(const NodeView &node, BitReader &in);
    Term time(const NodeView &node, BitReader &in);
    Term descriptor(const NodeView &node, BitReader &in);
    Term sequence(const NodeView &node, BitReader &in);
    Term collection(const NodeView &node, BitReader &in);
    Term choice(const NodeView &node, BitReader &in);
    Term open_type(const NodeView &node, BitReader &in);
    Term reference(const NodeView &node, BitReader &in);

    // Primitive helpers (per_decode.cpp).
    Term whole_number(const Term &bounds, BitReader &in);
    Term root_number(const Term &bounds, BitReader &in);
    Term number(const detail::Integer &value);
    Term bits_value(const NodeView &node, std::vector<std::byte> bytes, std::size_t bits);
    Term named_bit_list(const NodeView &node, std::span<const std::byte> bytes, std::size_t bits);
    Term known_multiplier(const NodeView &node, std::uint64_t tag, BitReader &in);
    Term characters(std::span<const std::byte> bytes, std::size_t count, std::size_t unit,
                    const std::vector<std::uint32_t> &table);
    std::vector<std::byte> octets_with_length(BitReader &in);
    std::vector<std::byte> open_octets(BitReader &in);

    // Constructed helpers (per_decode_constructed.cpp).
    Term components(const NodeView &node, BitReader &in);
    Term component(const ComponentView &item, bool present, BitReader &in, std::optional<std::vector<std::byte>> &raw);
    void root_components(const std::vector<ComponentView> &list, BitReader &in,
                         std::vector<std::optional<std::vector<std::byte>>> &raw);
    void extension_components(const std::vector<ComponentView> &list, BitReader &in);
    void addition(const std::vector<ComponentView> &list, std::size_t first, std::span<const std::byte> bytes);
    void absent_addition(const std::vector<ComponentView> &list, std::size_t first);
    void decode_postponed(const std::vector<ComponentView> &list,
                          const std::vector<std::optional<std::vector<std::byte>>> &raw);
    Term record(const NodeView &node, const std::vector<ComponentView> &list, const std::vector<Term> &values);
    Term from_open_type(std::int64_t node, std::span<const std::byte> bytes);
    std::size_t sequence_length(const Term &bounds, BitReader &in);
    Term table_value(const NodeView &node, std::span<const std::byte> bytes);

    Builder &builder_;
    const Schema &schema_;
    std::vector<Frame> frames_;
    // Aligned PER (per) rather than unaligned (uper): open types are read with the same variant.
    bool aligned_ = false;
};

// The ExtensionAdditionGroup structure of a component list: the first component of each addition, in order.
std::vector<std::size_t> addition_starts(const std::vector<ComponentView> &list);
// The node an enumerated, integer or other node's content is in, following references.
NodeView content_of(const Schema &schema, NodeView node);
// The known-multiplier string's PER character table: none when characters encode as their code points.
std::vector<std::uint32_t> character_table(std::uint64_t tag, const NodeView &node, std::size_t unit);
std::size_t character_bits(const NodeView &node, std::uint64_t tag, bool aligned);
} // namespace clause::runtime::builtins::asn1
