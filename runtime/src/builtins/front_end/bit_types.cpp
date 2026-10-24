#include "bit_types.hpp"
#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include <array>

// erl_bits:set_bit_type/2 (plan 21 steps LIB31, LIB32): type lists merged in order and erl_bits' defaults applied, with
// its error reasons.
namespace clause::runtime::builtins::front_end {
namespace {
using abi::v1::BitType;

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

bool is_node(const Term &term, std::string_view tag, std::size_t size) {
    return term.is_tuple() && term.tuple_size_unchecked() == size && is_atom(term.tuple_element_unchecked(0), tag);
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
        } else if (is_node(type, "unit", 2)) {
            if (const auto unit = unit_record(type.tuple_element_unchecked(1))) {
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

// check_size_unit_1/2: a UTF segment has no unit, and no size but undefined.
void check_utf(const SpecifierReader &reader, const Term &size, bool unit) {
    const auto undefined_size = (is_node(size, "atom", 3) || is_node(size, "value", 3)) &&
                                is_atom(size.tuple_element_unchecked(2), "undefined");
    if ((!is_atom(size, "default") && !undefined_size) || unit) {
        bit_type_error(reader.atom("utf_bittype_size_or_unit"_atom));
    }
}
} // namespace

BitTypeSpec settle_bit_type(ProcessContext &context, const BitSpecifiers &written) {
    const auto &size = written.size;
    const SpecifierReader reader(context);
    const auto specifiers = reader.read(written.types);
    BitTypeSpec spec;
    spec.type = bit_type_of(specifiers.type.value_or("integer"));
    spec.signed_value = specifiers.sign == std::optional<std::string_view>{"signed"};
    spec.endian = specifiers.endian.value_or("big");
    if (is_utf_type(spec.type)) {
        check_utf(reader, size, specifiers.unit.has_value());
        return spec;
    }
    const auto binary = spec.type == BitType::binary;
    const auto sized = !is_atom(size, "default");
    if (!sized && specifiers.unit && !binary) {
        bit_type_error(reader.atom("bittype_unit"_atom));
    }
    spec.unit = specifiers.unit ? static_cast<unsigned>(*specifiers.unit) : binary ? 8U : 1U;
    if (!sized && !binary) {
        spec.default_size = spec.type == BitType::floating ? 64 : 8;
    }
    return spec;
}
} // namespace clause::runtime::builtins::front_end
