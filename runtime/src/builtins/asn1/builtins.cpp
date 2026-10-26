#include "../typed.hpp"
#include "codec.hpp"

// Added for ASN.1 sources (plan 21 step LIB84): the builtins of clause_asn1 that clau's generated ASN.1 modules call
// (docs/builtins.md#asn1): encode/3 and decode/3 over the module's schema term.
namespace clause::runtime::builtins::asn1 {
namespace {
struct SchemaRole;
struct TypeRole;
struct ValueRole;

// The bytes of an iolist argument, as enif_inspect_iolist_as_binary takes them; badarg for anything else.
std::vector<std::byte> input_bytes(const Term &data) {
    if (data.is_binary()) {
        return need(data.binary_bytes());
    }
    std::vector<std::byte> bytes;
    auto rest = data;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto head = rest.head_unchecked();
        const auto byte = small(head.word());
        if (byte && *byte >= 0 && *byte <= 255) {
            bytes.push_back(static_cast<std::byte>(*byte));
        } else if (head.is_binary() || head.is_list()) {
            const auto inner = input_bytes(head);
            bytes.insert(bytes.end(), inner.begin(), inner.end());
        } else {
            bad_argument();
        }
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return bytes;
}

// encode(Schema, Type, Value): the encoding of Value as Type.
Term encode(ProcessContext &context, const Named<SchemaRole> &schema_term, const Named<TypeRole> &type,
            const Named<ValueRole> &value) {
    const Schema schema(schema_term.term);
    Builder builder(context);
    const auto root = schema.root(context, type.term);
    Encoder encoder(builder, schema);
    const auto bytes = encoder.top(root, value.term);
    return builder.binary(bytes);
}

// decode(Schema, Type, Bytes): {Value, Rest}; the TLV is split off before the type is looked up, as
// ber_decode_nif/1 runs before asn1ct's decode_disp/2.
Term decode(ProcessContext &context, const Named<SchemaRole> &schema_term, const Named<TypeRole> &type,
            const Named<ValueRole> &data) {
    const Schema schema(schema_term.term);
    Builder builder(context);
    const auto bytes = input_bytes(data.term);
    std::pair<Tlv, std::size_t> parsed;
    try {
        parsed = parse_tlv(bytes);
    } catch (const TlvError &error) {
        asn1_exit(builder, builder.tuple({builder.atom(error.reason_),
                                          builder.integer(static_cast<std::int64_t>(error.position_))}));
    }
    const auto root = schema.root(context, type.term);
    Decoder decoder(builder, schema);
    const auto value = decoder.top(root, Data::of(parsed.first));
    const auto rest = std::span(bytes).subspan(parsed.second);
    return builder.tuple({value, builder.binary(rest)});
}

constexpr std::array ASN1_BUILTINS{
    typed_entry<encode>("clause_asn1", "encode"),
    typed_entry<decode>("clause_asn1", "decode"),
};
} // namespace
} // namespace clause::runtime::builtins::asn1

namespace clause::runtime {
std::span<const BuiltinEntry> asn1_builtins() noexcept { return builtins::asn1::ASN1_BUILTINS; }
} // namespace clause::runtime
