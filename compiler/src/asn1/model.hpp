#pragma once
// Added for the ASN.1 compiler (plan 21 step LIB84): the checked form of a compiled module, which the generator
// turns into an Erlang module (schema term, value functions) and an .hrl file.
#include "syntax.hpp"
#include <boost/multiprecision/cpp_int.hpp>
#include <map>
#include <optional>

namespace clause::asn1 {
using BigInt = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>, boost::multiprecision::et_off>;

// An Erlang term: ASN.1 values, DEFAULT settings and the schema.
struct Term {
    // A map holds its keys and values alternately in `items_`.
    enum class Kind : std::uint8_t { atom, integer, tuple, list, binary, bitstring, real, map };
    Kind kind_ = Kind::atom;
    // Atom name, integer or float digits, or the bytes of a binary or bitstring.
    std::string text_;
    // A bitstring's length in bits.
    std::uint64_t bits_ = 0;
    std::vector<Term> items_;

    static Term atom(std::string name) { return {Kind::atom, std::move(name), 0, {}}; }

    static Term integer(const BigInt &value) { return {Kind::integer, value.str(), 0, {}}; }

    static Term tuple(std::vector<Term> items) { return {Kind::tuple, {}, 0, std::move(items)}; }

    static Term list(std::vector<Term> items) { return {Kind::list, {}, 0, std::move(items)}; }

    static Term binary(std::string bytes) { return {Kind::binary, std::move(bytes), 0, {}}; }

    static Term map(std::vector<Term> items) { return {Kind::map, {}, 0, std::move(items)}; }

    static Term string(std::string_view text);
    bool operator==(const Term &other) const = default;
};

// `~w` text of a term, as asn1ct writes values into .hrl macros.
std::string print(const Term &term);
// An Erlang atom quoted where needed.
std::string atom_text(std::string_view name);

using NodeId = std::uint32_t;
constexpr NodeId NO_NODE = 0xffffffffU;

enum class NodeKind : std::uint8_t {
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
    // A complete encoding of another type: ANY, class type fields, an unresolvable table constraint.
    open_type,
    // A type still being checked (recursion): content from `target_`, tags from this node.
    reference,
};

struct TagValue {
    TagClass class_ = TagClass::context;
    std::uint64_t number_ = 0;
    bool operator==(const TagValue &) const = default;
};

// An effective PER constraint: bounds of a value or a size, and whether it is extensible.
struct Bounds {
    std::optional<BigInt> lower_;
    std::optional<BigInt> upper_;
    bool extensible_ = false;
    bool present_ = false;
};

// The objects an open type or a table-constrained field may come from, keyed by the value of the referenced
// component (`{@id}`), found `levels_up_` SEQUENCE/SET levels above the innermost enclosing one by `path_`.
struct Table {
    std::vector<std::pair<Term, NodeId>> entries_;
    bool extensible_ = false;
    std::uint32_t levels_up_ = 0;
    std::vector<std::string> path_;
    // The class fields of the open type and of the key (field names are kept without `&`), as asn1ct's table
    // errors name them.
    std::string field_;
    std::string key_;
};

struct Component {
    std::string name_;
    NodeId type_ = NO_NODE;
    Presence presence_ = Presence::required;
    std::optional<Term> default_;
    // 0 for root components, else the extension addition index (a [[ ]] group counts once).
    std::uint32_t addition_ = 0;
    // Set for root components after the second ellipsis.
    bool root2_ = false;
    // The textual position in a SET (decoding order is by tag), and the group's version.
    std::uint32_t textual_ = 0;
};

struct Node {
    NodeKind kind_ = NodeKind::null;
    std::vector<TagValue> tags_;
    // A reference's target; a SEQUENCE OF/SET OF element.
    NodeId target_ = NO_NODE;
    NodeId element_ = NO_NODE;
    // A restricted string's type name (IA5String, ...).
    std::string string_;
    // The runtime record name of a SEQUENCE/SET.
    std::string record_;
    // The named type a node stands for (empty for inline types); the parameterized type a SEQUENCE/SET instance
    // comes from (asn1ct's pname).
    std::string named_;
    std::string pname_;
    // Set on a type assignment that only names another type (`A ::= B`, `A ::= [0] M.B`): it has no records.
    bool alias_ = false;
    std::vector<Component> components_;
    bool extensible_ = false;
    // The extension markers written in a SEQUENCE, SET or CHOICE body (0, 1 or 2): asn1ct's records comment on them.
    std::uint8_t ellipses_ = 0;
    // Named numbers (INTEGER), named bits (BIT STRING), enumeration items (root items first, then additions).
    std::vector<std::pair<std::string, BigInt>> named_numbers_;
    std::uint32_t enumeration_root_ = 0;
    Bounds value_;
    Bounds size_;
    // A PER permitted alphabet of a restricted string, as code points; empty when not constrained.
    std::vector<std::uint32_t> alphabet_;
    std::shared_ptr<Table> table_;
};

// A line of an .hrl record: a field with its initial value, or (with an empty name) one of asn1ct's comments on the
// extension structure ("with extensions", ...).
struct RecordField {
    std::string name_;
    // "", "asn1_NOVALUE" or "asn1_DEFAULT".
    std::string initial_;
    std::string comment_;
};

struct RecordDefinition {
    std::string name_;
    std::vector<RecordField> fields_;
};

struct ValueDefinition {
    std::string name_;
    Term value_;
};

enum class EncodingRule : std::uint8_t { ber, per, uper };

// asn1ct options that change what the generated module does.
struct CompileOptions {
    EncodingRule rule_ = EncodingRule::ber;
    bool der_ = false;
    bool maps_ = false;
    bool undec_rest_ = false;
    bool no_ok_wrapper_ = false;
    bool legacy_erlang_types_ = false;
    // bit_string_format/0: bitstring (default), legacy (legacy_bit_string) or compact (compact_bit_string).
    std::string bit_string_format_ = "bitstring";
    std::string record_prefix_;
    std::string macro_prefix_;
    // The options as written, for info/0.
    std::vector<std::string> spelled_;
};

struct CompiledModule {
    std::string name_;
    std::vector<Node> nodes_;
    // The type assignments encode/2 and decode/2 accept, in definition order.
    std::vector<std::pair<std::string, NodeId>> types_;
    std::vector<RecordDefinition> records_;
    std::vector<ValueDefinition> values_;
};
} // namespace clause::asn1
