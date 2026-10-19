#include "../../terms/bitstrings.hpp"
#include "../../terms/floats.hpp"
#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include "abstract.hpp"
#include <array>
#include <bit>
#include <clause/abi/immediate_services.hpp>
#include <clause/runtime/code_server.hpp>
#include <terms.hpp>

// normalise/1 of erl_parse (plan 21 step LIB31): an abstract literal as the term it stands for, binaries built
// segment by segment as eval_bits:expr_grp/3 builds them.
namespace clause::runtime::builtins::front_end::abstract {
namespace {
using abi::v1::BitType;

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

Term element(const Term &tuple, std::size_t index) { return tuple.tuple_element_unchecked(index); }

// A tuple of `size` elements tagged `tag`.
bool is_node(const Term &term, std::string_view tag, std::size_t size) {
    return term.is_tuple() && term.tuple_size_unchecked() == size && is_atom(element(term, 0), tag);
}

// The elements of a proper list; function_clause for an improper one, as OTP's recursions raise.
std::vector<Term> proper_elements(const Term &list) {
    std::vector<Term> items;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        lists::no_clause();
    }
    return items;
}

// erl_bits' #bittype{} while the specifiers merge: each field as written, absent while undefined.
struct Specifiers {
    std::optional<std::string_view> type;
    std::optional<std::int64_t> unit;
    std::optional<std::string_view> sign;
    std::optional<std::string_view> endian;
};

// The type names of erl_bits:type_to_record/1: the type each stands for and the unit it implies.
struct TypeName {
    std::string_view name;
    std::string_view type;
    std::optional<std::int64_t> unit;
};

constexpr std::array<TypeName, 9> TYPE_NAMES{{
    {"integer", "integer", std::nullopt},
    {"utf8", "utf8", std::nullopt},
    {"utf16", "utf16", std::nullopt},
    {"utf32", "utf32", std::nullopt},
    {"float", "float", std::nullopt},
    {"binary", "binary", std::nullopt},
    {"bytes", "binary", 8},
    {"bitstring", "binary", 1},
    {"bits", "binary", 1},
}};

constexpr std::array<std::pair<std::string_view, BitType>, 6> BIT_TYPES{{
    {"integer", BitType::integer},
    {"utf8", BitType::utf8},
    {"utf16", BitType::utf16},
    {"utf32", BitType::utf32},
    {"float", BitType::floating},
    {"binary", BitType::binary},
}};

BitType bit_type_of(std::string_view name) {
    for (const auto &[spelling, type] : BIT_TYPES) {
        if (spelling == name) {
            return type;
        }
    }
    return BitType::integer;
}

// error(Reason) as make_bit_type/4 raises erl_bits' errors.
[[noreturn]] void bit_type_error(const Term &reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = reason.word()};
}

// erl_bits:set_bit/1: the specifiers of a type list merged in order, with erl_bits' error reasons.
class SpecifierReader final {
  public:
    explicit SpecifierReader(ProcessContext &context) : terms_(context) {}

    Specifiers read(const Term &types) const {
        Specifiers merged;
        if (is_atom(types, "default")) {
            return merged;
        }
        auto rest = types;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            merge(merged, record_of(rest.head_unchecked()));
        }
        if (!rest.is_nil()) {
            lists::no_clause();
        }
        return merged;
    }

    Term atom(const AtomName name) const { return terms_.atom(name); }

  private:
    // type_to_record/1: one specifier, {undefined_bittype, Type} for anything else.
    Specifiers record_of(const Term &type) const {
        if (type.is_atom()) {
            if (const auto known = atom_record(type.atom_spelling_unchecked())) {
                return *known;
            }
        } else if (type.is_tuple() && type.tuple_size_unchecked() == 2 && is_atom(element(type, 0), "unit")) {
            if (const auto unit = unit_record(element(type, 1))) {
                return *unit;
            }
        }
        bit_type_error(terms_.tuple(std::array{atom("undefined_bittype"_atom), type}));
    }

    // {unit, undefined} sets nothing; {unit, N} needs 1 =< N =< 256.
    static std::optional<Specifiers> unit_record(const Term &unit) {
        if (is_atom(unit, "undefined")) {
            return Specifiers{};
        }
        const auto value = string::small_of(unit);
        if (!value || *value <= 0 || *value > 256) {
            return std::nullopt;
        }
        return Specifiers{.type = std::nullopt, .unit = *value, .sign = std::nullopt, .endian = std::nullopt};
    }

    static std::optional<Specifiers> atom_record(std::string_view name) {
        for (const auto &row : TYPE_NAMES) {
            if (row.name == name) {
                return Specifiers{.type = row.type, .unit = row.unit, .sign = std::nullopt, .endian = std::nullopt};
            }
        }
        if (name == "big" || name == "little" || name == "native") {
            return Specifiers{.type = std::nullopt, .unit = std::nullopt, .sign = std::nullopt, .endian = name};
        }
        if (name == "signed" || name == "unsigned") {
            return Specifiers{.type = std::nullopt, .unit = std::nullopt, .sign = name, .endian = std::nullopt};
        }
        return std::nullopt;
    }

    // merge_bittype/2: endianness, sign, type and unit, a field set twice differently being a mismatch.
    void merge(Specifiers &merged, const Specifiers &added) const {
        merge_field(merged.endian, added.endian, "endianness");
        merge_field(merged.sign, added.sign, "sign");
        merge_field(merged.type, added.type, "type");
        merge_field(merged.unit, added.unit, "unit");
    }

    template <typename Value>
    void merge_field(std::optional<Value> &merged, const std::optional<Value> &added, std::string_view what) const {
        if (!added) {
            return;
        }
        if (merged && *merged != *added) {
            const std::array reason{atom("bittype_mismatch"_atom), term_of(*added), term_of(*merged), chars(what)};
            bit_type_error(terms_.tuple(reason));
        }
        merged = added;
    }

    Term term_of(AtomName name) const { return atom(name); }

    Term term_of(std::int64_t value) const { return terms_.integer(value); }

    Term chars(std::string_view text) const {
        std::vector<Term> codes;
        codes.reserve(text.size());
        for (const auto code : text) {
            codes.push_back(terms_.integer(code));
        }
        return terms_.list(codes);
    }

    string::Terms terms_;
};

bool is_utf(BitType type) { return type == BitType::utf8 || type == BitType::utf16 || type == BitType::utf32; }

// The size and type specifiers of a bin_element as written.
struct Specification {
    Term size;
    Term types;
};

// One evaluated bin_element: its value(s), size and settled specifiers.
struct Field {
    std::vector<Term> values;
    BitType type = BitType::integer;
    unsigned unit = 1;
    bool signed_value = false;
    bool little = false;
    bool all = false;
    Term size;
};

class Normaliser final {
  public:
    explicit Normaliser(ProcessContext &context) : context_(context), terms_(context) {}

    Term value(const Term &node) {
        if (const auto leaf = literal(node)) {
            return *leaf;
        }
        if (is_node(node, "cons", 4)) {
            return list(node);
        }
        if (is_node(node, "tuple", 3)) {
            return tuple(node);
        }
        if (is_node(node, "map", 3)) {
            return map(node);
        }
        if (is_node(node, "bin", 3)) {
            return bits(element(node, 2));
        }
        if (is_node(node, "fun", 3)) {
            return fun(node);
        }
        return signed_literal(node);
    }

  private:
    // error({badarg, Node}).
    [[noreturn]] void bad(const Term &node) const {
        const std::array items{terms_.atom("badarg"_atom), node};
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = terms_.tuple(items).word()};
    }

    std::optional<Term> literal(const Term &node) const {
        for (const auto tag : {"char", "integer", "float", "atom", "string"}) {
            if (is_node(node, tag, 3)) {
                return element(node, 2);
            }
        }
        if (is_node(node, "nil", 2)) {
            return terms_.nil();
        }
        return std::nullopt;
    }

    // [normalise(H) | normalise(T)], walking the spine without recursion.
    Term list(const Term &node) {
        std::vector<Term> heads;
        auto rest = node;
        for (; is_node(rest, "cons", 4); rest = element(rest, 3)) {
            heads.push_back(value(element(rest, 2)));
        }
        return terms_.list(heads, value(rest));
    }

    Term tuple(const Term &node) {
        std::vector<Term> items;
        for (const auto &item : proper_elements(element(node, 2))) {
            items.push_back(value(item));
        }
        return terms_.tuple(items);
    }

    // Only => fields; another field is error({badarg, Map}).
    Term map(const Term &node) {
        std::vector<std::pair<Term, Term>> entries;
        for (const auto &field : proper_elements(element(node, 2))) {
            if (!is_node(field, "map_field_assoc", 4)) {
                bad(node);
            }
            auto key = value(element(field, 2));
            entries.emplace_back(key, value(element(field, 3)));
        }
        return need(TermFactory(context_).map(entries));
    }

    // fun M:F/A of {function, {atom, _, M}, {atom, _, F}, {integer, _, A}}.
    Term fun(const Term &node) {
        const auto function = element(node, 2);
        if (!is_node(function, "function", 4) || !is_node(element(function, 1), "atom", 3) ||
            !is_node(element(function, 2), "atom", 3) || !is_node(element(function, 3), "integer", 3)) {
            bad(node);
        }
        const auto arity = string::small_of(element(element(function, 3), 2));
        if (!arity || *arity < 0 || *arity > 255) {
            bad_argument();
        }
        const auto &definition = context_.code_server().external_fun(
            element(element(function, 1), 2), element(element(function, 2), 2), static_cast<std::size_t>(*arity));
        return need(TermFactory(context_).fun(definition, {}));
    }

    // {op, _, '+' | '-', {char | integer | float, _, N}}: N or -N.
    Term signed_literal(const Term &node) {
        if (!is_node(node, "op", 4)) {
            bad(node);
        }
        const auto operation = element(node, 2);
        const auto operand = element(node, 3);
        const auto number =
            is_node(operand, "char", 3) || is_node(operand, "integer", 3) || is_node(operand, "float", 3);
        if (!number || !(is_atom(operation, "+") || is_atom(operation, "-"))) {
            bad(node);
        }
        const auto magnitude = element(operand, 2);
        if (is_atom(operation, "+")) {
            return magnitude;
        }
        const auto zero = Term::from_word(*abi::v1::NativeIntegerEncoding::encode(0)).value();
        return need(detail::numeric_service(context_, abi::v1::ImmediateOperation::subtract, zero, magnitude));
    }

    // eval_bits:expr_grp/3: every field is evaluated before the binary is built.
    Term bits(const Term &elements) {
        std::vector<Field> fields;
        for (const auto &segment : proper_elements(elements)) {
            fields.push_back(field(segment));
        }
        detail::BitWriter writer;
        for (const auto &one : fields) {
            build(writer, one);
        }
        return need(TermFactory(context_).bitstring(writer.bytes, writer.length));
    }

    Field field(const Term &segment) {
        if (!is_node(segment, "bin_element", 5)) {
            lists::no_clause();
        }
        const auto expression = element(segment, 2);
        const auto size = element(segment, 3);
        const auto types = element(segment, 4);
        if (is_node(expression, "string", 3)) {
            return string_field(element(expression, 2), {size, types});
        }
        auto value = this->value(expression);
        auto settled = settle({size, types});
        settled.values.push_back(value);
        return settled;
    }

    // A string segment: one segment per character, Latin-1 bytes when its size and types are the defaults.
    Field string_field(const Term &chars, const Specification &specification) {
        const auto codes = proper_elements(chars);
        if (is_atom(specification.size, "default") && is_atom(specification.types, "default")) {
            Field latin1;
            latin1.size = terms_.integer(8);
            for (const auto &code : codes) {
                latin1.values.push_back(latin1_byte(code));
            }
            return latin1;
        }
        auto settled = settle(specification);
        settled.values = codes;
        if (codes.empty()) {
            // OTP still builds a segment of 0 to report a bad size or type.
            detail::BitWriter probe;
            settled.values.push_back(terms_.integer(0));
            build(probe, settled);
            settled.values.clear();
        }
        return settled;
    }

    // C band 16#FF.
    Term latin1_byte(const Term &code) const {
        const auto value = string::small_of(code);
        if (!value) {
            bad_argument();
        }
        return terms_.integer(*value & 0xFF);
    }

    // make_bit_type/4: the specifiers with erl_bits' defaults and checks, and the size evaluated.
    Field settle(const Specification &specification) {
        const auto &size = specification.size;
        const SpecifierReader reader(context_);
        const auto specifiers = reader.read(specification.types);
        auto settled = field_of(specifiers);
        const auto sized = !is_atom(size, "default");
        if (is_utf(settled.type)) {
            check_utf(reader, size, specifiers.unit.has_value());
            return settled;
        }
        if (!sized && specifiers.unit && settled.type != BitType::binary) {
            bit_type_error(reader.atom("bittype_unit"_atom));
        }
        const auto unit = specifiers.unit ? std::optional{static_cast<unsigned>(*specifiers.unit)} : std::nullopt;
        settle_size(settled, sized ? value(size) : Term{}, sized, unit);
        return settled;
    }

    // The type, signedness and endianness of merged specifiers, integer, unsigned and big by default.
    static Field field_of(const Specifiers &specifiers) {
        Field settled;
        settled.type = bit_type_of(specifiers.type.value_or("integer"));
        settled.signed_value = specifiers.sign == std::optional<std::string_view>{"signed"};
        const auto native_little = std::endian::native == std::endian::little;
        settled.little = specifiers.endian == std::optional<std::string_view>{"little"} ||
                         (specifiers.endian == std::optional<std::string_view>{"native"} && native_little);
        return settled;
    }

    // check_size_unit_1/2: a UTF segment has no unit, and no size but undefined.
    static void check_utf(const SpecifierReader &reader, const Term &size, bool unit) {
        const auto undefined_size =
            (is_node(size, "atom", 3) || is_node(size, "value", 3)) && is_atom(element(size, 2), "undefined");
        if ((!is_atom(size, "default") && !undefined_size) || unit) {
            bit_type_error(reader.atom("utf_bittype_size_or_unit"_atom));
        }
    }

    // The default size (8 for integers, 64 for floats, all for binaries) and unit (8 for binaries, else 1).
    void settle_size(Field &settled, const Term &size, bool sized, std::optional<unsigned> unit) const {
        const auto binary = settled.type == BitType::binary;
        settled.unit = unit.value_or(binary ? 8 : 1);
        if (sized) {
            settled.all = is_atom(size, "all") && binary;
            settled.size = settled.all ? terms_.integer(0) : size;
            return;
        }
        settled.all = binary;
        settled.unit = binary ? settled.unit : 1;
        settled.size = terms_.integer(settled.type == BitType::floating ? 64 : binary ? 0 : 8);
    }

    // eval_exp_field/6 for each value of a field; any failure is badarg.
    void build(detail::BitWriter &writer, const Field &settled) const {
        for (const auto &one : settled.values) {
            const detail::BitSegment segment{settled.type, settled.little, settled.signed_value, settled.all,
                                             false,        settled.unit,   settled.size,         one};
            if (!settled.all && !settled.size.is_integer() && !is_utf(settled.type)) {
                bad_argument();
            }
            if (!detail::bit_construct(writer, segment)) {
                bad_argument();
            }
        }
    }

    ProcessContext &context_;
    string::Terms terms_;
};
} // namespace

Term normalise(ProcessContext &context, const Term &node) { return Normaliser(context).value(node); }
} // namespace clause::runtime::builtins::front_end::abstract
