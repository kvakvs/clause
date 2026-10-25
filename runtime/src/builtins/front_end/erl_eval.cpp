#include "../../terms/bitstrings.hpp"
#include "../string/string.hpp"
#include "../typed.hpp"
#include "bit_types.hpp"
#include "eval.hpp"
#include <array>
#include <bit>
#include <terms.hpp>

// erl_eval's bit syntax (plan 21 step LIB32, docs/builtins.md#erl-eval): clause_erl_eval's builtins settle the type
// specifiers of a bin_element, and build or take one segment, for library/stdlib/erl_eval.erl.
namespace clause::runtime::builtins::front_end {
namespace {
using abi::v1::BitType;
using string::Terms;

// The roles of the arguments (typed.hpp's Named).
struct SizeRole;
struct TypesRole;
struct ValueRole;
struct SpecRole;
using SizeArg = Named<SizeRole>;
using TypesArg = Named<TypesRole>;
using ValueArg = Named<ValueRole>;
using SpecArg = Named<SpecRole>;

constexpr std::array<std::pair<BitType, std::string_view>, 6> TYPE_ATOMS{{
    {BitType::integer, "integer"},
    {BitType::floating, "float"},
    {BitType::binary, "binary"},
    {BitType::utf8, "utf8"},
    {BitType::utf16, "utf16"},
    {BitType::utf32, "utf32"},
}};

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

std::string_view type_name(BitType type) {
    for (const auto &[known, name] : TYPE_ATOMS) {
        if (known == type) {
            return name;
        }
    }
    return "integer";
}

std::optional<BitType> type_of(const Term &name) {
    for (const auto &[type, spelling] : TYPE_ATOMS) {
        if (is_atom(name, spelling)) {
            return type;
        }
    }
    return std::nullopt;
}

// bit_type(Size, Types): {Size1, {Type, Unit, Sign, Endian}} as make_bit_type/4 settles a bin_element, Size1 being
// the default size (all, undefined, 8 or 64) when Size is default and Size itself otherwise; erl_bits' errors raise.
Term settled(ProcessContext &context, std::span<const Term, 2> size_and_types) {
    const auto &size = size_and_types[0];
    const auto spec = settle_bit_type(context, {.size = size, .types = size_and_types[1]});
    const Terms terms(context);
    auto size1 = size;
    if (is_atom(size, "default")) {
        size1 = is_utf_type(spec.type)         ? terms.atom("undefined"_atom)
                : spec.type == BitType::binary ? terms.atom("all"_atom)
                                               : terms.integer(spec.default_size.value_or(8));
    }
    const auto unit = spec.unit ? terms.integer(*spec.unit) : terms.atom("undefined"_atom);
    const std::array fields{terms.atom(type_name(spec.type)), unit,
                            terms.atom(spec.signed_value ? "signed" : "unsigned"), terms.atom(spec.endian)};
    const std::array result{size1, terms.tuple(fields)};
    return terms.tuple(result);
}

// One segment of an evaluated size and a {Type, Unit, Sign, Endian} specification; none for a malformed one.
std::optional<detail::BitSegment> segment_of(const Term &size, const Term &spec, const Term &value) {
    if (!spec.is_tuple() || spec.tuple_size_unchecked() != 4) {
        return std::nullopt;
    }
    const auto type = type_of(spec.tuple_element_unchecked(0));
    if (!type) {
        return std::nullopt;
    }
    const auto unit = string::small_of(spec.tuple_element_unchecked(1)).value_or(1);
    if (unit <= 0) {
        return std::nullopt;
    }
    const auto endian = spec.tuple_element_unchecked(3);
    const auto native_little = std::endian::native == std::endian::little;
    const auto little = is_atom(endian, "little") || (is_atom(endian, "native") && native_little);
    const auto all = *type == BitType::binary && is_atom(size, "all");
    return detail::BitSegment{*type, little, is_atom(spec.tuple_element_unchecked(2), "signed"),
                              all,   false,  static_cast<std::size_t>(unit),
                              size,  value};
}

// bit_segment(Value, Size, Spec): eval_exp_field/6, the segment as a bitstring, or the atom badarg or system_limit
// the construction fails with.
Term built(ProcessContext &context, const Term &value, std::span<const Term, 2> size_and_spec) {
    const Terms terms(context);
    const auto segment = segment_of(size_and_spec[0], size_and_spec[1], value);
    if (!segment) {
        return terms.atom("badarg"_atom);
    }
    detail::BitWriter writer;
    if (const auto built = detail::bit_construct(writer, *segment); !built) {
        const auto limit = built.error() == TermError::resource_limit || built.error() == TermError::out_of_range;
        return terms.atom(limit ? "system_limit" : "badarg");
    }
    return need(TermFactory(context).bitstring(writer.bytes, writer.length));
}

// bit_take(Bits, Size, Spec): get_value/6, {Value, Rest} for the segment at the start of Bits, or nomatch.
Term taken(ProcessContext &context, const Term &bits, std::span<const Term, 2> size_and_spec) {
    const Terms terms(context);
    const auto view = detail::bit_view(bits);
    const auto segment = segment_of(size_and_spec[0], size_and_spec[1], terms.nil());
    if (!view || !segment) {
        return terms.atom("nomatch"_atom);
    }
    const auto extracted = detail::bit_extract(context, bits, 0, *segment);
    if (!extracted) {
        return terms.atom("nomatch"_atom);
    }
    const auto rest =
        need(detail::BitAccess::slice(context.heap(), bits, extracted->cursor, view->length - extracted->cursor));
    const std::array result{extracted->value, rest};
    return terms.tuple(result);
}

Term bit_type(ProcessContext &context, const SizeArg &size, const TypesArg &types) {
    const std::array args{size.term, types.term};
    return settled(context, args);
}

Term bit_segment(ProcessContext &context, const ValueArg &value, const SizeArg &size, const SpecArg &spec) {
    const std::array args{size.term, spec.term};
    return built(context, value.term, args);
}

Term bit_take(ProcessContext &context, const ValueArg &bits, const SizeArg &size, const SpecArg &spec) {
    const std::array args{size.term, spec.term};
    return taken(context, bits.term, args);
}

constexpr std::array ERL_EVAL_BUILTINS{
    typed_entry<bit_type>("clause_erl_eval", "bit_type"),
    typed_entry<bit_segment>("clause_erl_eval", "bit_segment"),
    typed_entry<bit_take>("clause_erl_eval", "bit_take"),
};
} // namespace

// Added for the C++ erl_eval parts.
namespace eval {
Term bit_type(ProcessContext &context, std::span<const Term, 2> size_and_types) {
    return settled(context, size_and_types);
}

Term bit_segment(ProcessContext &context, const Term &value, std::span<const Term, 2> size_and_spec) {
    return built(context, value, size_and_spec);
}

Term bit_take(ProcessContext &context, const Term &bits, std::span<const Term, 2> size_and_spec) {
    return taken(context, bits, size_and_spec);
}
} // namespace eval
} // namespace clause::runtime::builtins::front_end

namespace clause::runtime {
std::span<const BuiltinEntry> erl_eval_builtins() noexcept { return builtins::front_end::ERL_EVAL_BUILTINS; }
} // namespace clause::runtime
