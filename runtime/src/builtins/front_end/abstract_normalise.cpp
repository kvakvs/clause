#include "../../terms/bitstrings.hpp"
#include "../../terms/floats.hpp"
#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include "abstract.hpp"
#include "bit_types.hpp"
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
    Field string_field(const Term &chars, const BitSpecifiers &specification) {
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
    Field settle(const BitSpecifiers &specification) {
        const auto spec = settle_bit_type(context_, specification);
        Field settled;
        settled.type = spec.type;
        settled.signed_value = spec.signed_value;
        const auto native_little = std::endian::native == std::endian::little;
        settled.little = spec.endian == "little" || (spec.endian == "native" && native_little);
        settled.unit = spec.unit.value_or(1);
        if (is_utf_type(spec.type)) {
            return settled;
        }
        const auto binary = spec.type == BitType::binary;
        if (is_atom(specification.size, "default")) {
            settled.all = binary;
            settled.size = terms_.integer(spec.default_size.value_or(0));
            return settled;
        }
        const auto size = value(specification.size);
        settled.all = binary && is_atom(size, "all");
        settled.size = settled.all ? terms_.integer(0) : size;
        return settled;
    }

    // eval_exp_field/6 for each value of a field; any failure is badarg.
    void build(detail::BitWriter &writer, const Field &settled) const {
        for (const auto &one : settled.values) {
            const detail::BitSegment segment{settled.type, settled.little, settled.signed_value, settled.all,
                                             false,        settled.unit,   settled.size,         one};
            if (!settled.all && !settled.size.is_integer() && !is_utf_type(settled.type)) {
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
