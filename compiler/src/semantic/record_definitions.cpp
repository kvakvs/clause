#include "record_definitions.hpp"
#include "../preprocessor/expression.hpp"
#include "../preprocessor/value.hpp"
#include "capabilities.hpp"
#include <array>
#include <clause/abi/external_term.hpp>
#include <clause/compiler/source.hpp>

// Folding native record defaults and encoding definitions (plan 21 step LIB8B): the compiler keeps each record's
// field list as External Term Format bytes in its RecordDescriptor, so the runtime can answer get_definition/2.
namespace clause::semantic {
namespace {
// The spelling evaluate_operator takes for an operator OTP folds; empty for one it does not (++, --, andalso,
// orelse, !).
std::u32string_view operator_name(ast::BinaryOperator operation) {
    using enum ast::BinaryOperator;
    static constexpr std::array<std::pair<ast::BinaryOperator, std::u32string_view>, 22> NAMES{{
        {equal, U"=="},
        {not_equal, U"/="},
        {less_equal, U"=<"},
        {less, U"<"},
        {greater_equal, U">="},
        {greater, U">"},
        {exact_equal, U"=:="},
        {exact_not_equal, U"=/="},
        {add, U"+"},
        {subtract, U"-"},
        {bit_or, U"bor"},
        {bit_xor, U"bxor"},
        {shift_left, U"bsl"},
        {shift_right, U"bsr"},
        {logical_or, U"or"},
        {logical_xor, U"xor"},
        {multiply, U"*"},
        {divide, U"/"},
        {integer_divide, U"div"},
        {remainder, U"rem"},
        {bit_and, U"band"},
        {logical_and, U"and"},
    }};
    const auto found = std::ranges::find(NAMES, operation, &std::pair<ast::BinaryOperator, std::u32string_view>::first);
    return found == NAMES.end() ? std::u32string_view{} : found->second;
}

std::u32string_view operator_name(ast::UnaryOperator operation) {
    using enum ast::UnaryOperator;
    switch (operation) {
    case positive:
        return U"+";
    case negative:
        return U"-";
    case bit_not:
        return U"bnot";
    case logical_not:
        return U"not";
    }
    return {};
}

// How a string segment of a binary default encodes its characters.
struct StringEncoding {
    std::u32string_view type = U"integer";
    bool little = false;
};

// Apply one modifier of a string segment; false for one a literal string segment cannot have.
bool apply(StringEncoding &encoding, const ast::BinaryModifier &modifier) {
    static constexpr std::array<std::u32string_view, 4> TYPES{U"integer", U"utf8", U"utf16", U"utf32"};
    const auto &name = modifier.name.name;
    if (modifier.parameter) {
        return false;
    }
    if (const auto type = std::ranges::find(TYPES, name); type != TYPES.end()) {
        encoding.type = *type;
        return true;
    }
    if (name == U"little" || name == U"big") {
        encoding.little = name == U"little";
        return true;
    }
    return name == U"unsigned" || name == U"signed";
}

// The encoding a string segment's modifiers ask for; none for modifiers a literal string segment cannot have.
std::optional<StringEncoding> string_encoding(const ast::BinarySegment &segment) {
    StringEncoding encoding;
    for (const auto &modifier : segment.modifiers.value_or(std::vector<ast::BinaryModifier>{})) {
        if (!apply(encoding, modifier)) {
            return std::nullopt;
        }
    }
    return encoding;
}

void append_byte(Value &bits, unsigned byte) {
    for (int bit = 7; bit >= 0; --bit) {
        bits.bits.push_back(((byte >> static_cast<unsigned>(bit)) & 1U) != 0);
    }
}

// One UTF-16 or UTF-32 code unit: its value and its width in bytes.
struct CodeUnit {
    std::uint32_t value;
    unsigned width;
};

// A code unit in the segment's byte order.
void append_unit(Value &bits, CodeUnit unit, bool little) {
    for (unsigned i = 0; i < unit.width; ++i) {
        const auto shift = 8 * (little ? i : unit.width - 1 - i);
        append_byte(bits, (unit.value >> shift) & 0xFFU);
    }
}

// One character of a string segment.
void append_char(Value &bits, char32_t code, const StringEncoding &encoding) {
    if (encoding.type == U"integer") {
        append_byte(bits, static_cast<unsigned>(code) & 0xFFU);
    } else if (encoding.type == U"utf8") {
        for (const auto byte : utf8(std::u32string(1, code))) {
            append_byte(bits, static_cast<unsigned char>(byte));
        }
    } else if (encoding.type == U"utf32") {
        append_unit(bits, {code, 4}, encoding.little);
    } else if (code < 0x10000) {
        append_unit(bits, {code, 2}, encoding.little);
    } else {
        const auto offset = static_cast<std::uint32_t>(code) - 0x10000;
        append_unit(bits, {0xD800 + (offset >> 10U), 2}, encoding.little);
        append_unit(bits, {0xDC00 + (offset & 0x3FFU), 2}, encoding.little);
    }
}

// Folds one default expression; EvaluationFailure for anything OTP would not fold.
class Folder final {
  public:
    explicit Folder(const ast::Module &syntax) : syntax_(syntax) {}

    Value fold(const ast::ExprId &id) const {
        return std::visit([this](const auto &node) { return this->node(node); }, syntax_.expression(id).value);
    }

  private:
    Value node(const ast::IntegerLiteral &literal) const { return integer(decimal_number(literal.value.decimal)); }

    Value node(const ast::CharacterLiteral &literal) const {
        return integer(BigInt(static_cast<std::uint32_t>(literal.value)));
    }

    Value node(const ast::FloatLiteral &literal) const { return floating(literal.value); }

    Value node(const ast::Atom &literal) const { return atom(literal.name); }

    Value node(const ast::Group &group) const { return fold(group.expression); }

    Value node(const ast::StringLiteral &literal) const {
        std::vector<Value> codes;
        for (const auto code : literal.value) {
            codes.push_back(integer(BigInt(static_cast<std::uint32_t>(code))));
        }
        return list(std::move(codes));
    }

    Value node(const ast::Tuple &tuple) const {
        Value value;
        value.kind = ValueKind::tuple;
        for (const auto &element : tuple.elements) {
            value.elements.push_back(fold(element));
        }
        return value;
    }

    // [E1, E2 | Tail], a tail that is itself a list joining the elements.
    Value node(const ast::List &literal) const {
        std::vector<Value> elements;
        elements.reserve(literal.elements.size());
        for (const auto &element : literal.elements) {
            elements.push_back(fold(element));
        }
        auto value = list(std::move(elements));
        if (!literal.tail) {
            return value;
        }
        auto tail = fold(*literal.tail);
        if (tail.kind == ValueKind::list || tail.kind == ValueKind::nil) {
            value.elements.insert(value.elements.end(), tail.elements.begin(), tail.elements.end());
            value.tail = tail.tail;
        } else {
            value.tail = std::make_shared<Value>(std::move(tail));
        }
        value.kind = value.elements.empty() && !value.tail ? ValueKind::nil : ValueKind::list;
        return value;
    }

    // #{K => V, ...}: a later equal key replaces an earlier one.
    Value node(const ast::MapExpression &map) const {
        if (map.base) {
            throw EvaluationFailure();
        }
        Value value;
        value.kind = ValueKind::map;
        for (const auto &field : map.fields) {
            auto key = fold(field.key);
            auto item = fold(field.value);
            put(value, std::move(key), std::move(item));
        }
        return value;
    }

    static void put(Value &map, Value key, Value item) {
        for (std::size_t i = 0; i < map.elements.size(); i += 2) {
            if (compare(map.elements[i], key, true) == 0) {
                map.elements[i + 1] = std::move(item);
                return;
            }
        }
        map.elements.push_back(std::move(key));
        map.elements.push_back(std::move(item));
    }

    // A binary of string segments without sizes.
    Value node(const ast::Bitstring &binary) const {
        Value value;
        value.kind = ValueKind::bits;
        for (const auto &segment : binary.segments) {
            const auto *text =
                std::get_if<ast::StringLiteral>(&syntax_.expression(ungroup(syntax_, segment.value)).value);
            const auto encoding = string_encoding(segment);
            if (!text || segment.size || !encoding) {
                throw EvaluationFailure();
            }
            for (const auto code : text->value) {
                append_char(value, code, *encoding);
            }
        }
        return value;
    }

    Value node(const ast::UnaryExpression &expression) const {
        return evaluate_operator(operator_name(expression.operation), {fold(expression.operand)});
    }

    Value node(const ast::BinaryExpression &expression) const {
        const auto name = operator_name(expression.operation);
        if (name.empty()) {
            throw EvaluationFailure();
        }
        return evaluate_operator(name, {fold(expression.left), fold(expression.right)});
    }

    // Variables, calls, funs, records, comprehensions and every other construct do not fold.
    template <typename Other> Value node(const Other & /*other*/) const { throw EvaluationFailure(); }

    const ast::Module &syntax_;
};

// The bytes of a Value's bitstring, its last byte holding the final bits in the high positions.
std::string bit_bytes(const Value &value) {
    std::string bytes((value.bits.size() + 7) / 8, '\0');
    for (std::size_t i = 0; i < value.bits.size(); ++i) {
        if (value.bits[i]) {
            bytes[i / 8] = static_cast<char>(static_cast<unsigned char>(bytes[i / 8]) | (0x80U >> (i % 8)));
        }
    }
    return bytes;
}

void encode(abi::external::Writer &out, const Value &value);

void encode_integer(abi::external::Writer &out, const BigInt &number) {
    if (number >= std::numeric_limits<std::int64_t>::min() && number <= std::numeric_limits<std::int64_t>::max()) {
        out.integer(number.convert_to<std::int64_t>());
        return;
    }
    std::string magnitude;
    for (BigInt rest = number < 0 ? BigInt(-number) : number; rest != 0; rest >>= 8) {
        magnitude.push_back(static_cast<char>(static_cast<unsigned>(rest & 0xFF)));
    }
    out.big_integer(number < 0, magnitude);
}

void encode_list(abi::external::Writer &out, const Value &value) {
    out.list(value.elements.size());
    for (const auto &element : value.elements) {
        encode(out, element);
    }
    if (value.tail) {
        encode(out, *value.tail);
    } else {
        out.nil();
    }
}

void encode_elements(abi::external::Writer &out, const Value &value) {
    for (const auto &element : value.elements) {
        encode(out, element);
    }
}

void encode(abi::external::Writer &out, const Value &value) {
    switch (value.kind) {
    case ValueKind::integer:
        return encode_integer(out, value.integer);
    case ValueKind::floating:
        return out.floating(value.real);
    case ValueKind::atom:
        return out.atom(utf8(value.text));
    case ValueKind::nil:
        return out.nil();
    case ValueKind::list:
        return encode_list(out, value);
    case ValueKind::tuple:
        out.tuple(value.elements.size());
        return encode_elements(out, value);
    case ValueKind::map:
        out.map(value.elements.size() / 2);
        return encode_elements(out, value);
    case ValueKind::bits:
        return out.binary(bit_bytes(value), value.bits.size());
    default:
        throw EvaluationFailure();
    }
}

// The folded value of a default; none when it does not fold.
std::optional<Value> folded(const ast::Module &syntax, const ast::ExprId &id) {
    try {
        return Folder(syntax).fold(id);
    } catch (const EvaluationFailure &) {
        return std::nullopt;
    } catch (const EvaluationLimit &) {
        return std::nullopt;
    }
}
} // namespace

bool default_folds(const ast::Module &syntax, const ast::ExprId &id) { return folded(syntax, id).has_value(); }

std::string record_definition_etf(const ast::Module &syntax, const RecordLayout &layout) {
    std::string bytes;
    abi::external::Writer out(bytes);
    out.version();
    out.list(layout.fields.size());
    for (const auto &field : layout.fields) {
        const auto value = field.default_value ? folded(syntax, *field.default_value) : std::nullopt;
        if (value) {
            out.tuple(2);
            out.atom(utf8(field.name.name));
            encode(out, *value);
        } else {
            out.atom(utf8(field.name.name));
        }
    }
    out.nil();
    return bytes;
}
} // namespace clause::semantic
