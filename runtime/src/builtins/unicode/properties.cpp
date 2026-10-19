#include "../typed.hpp"
#include "code_points.hpp"
#include "tables.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>
#include <vector>

// The table queries of Clause's private clause_unicode module (plan step 41O1, docs/builtins.md#unicode-util):
// library/stdlib/unicode_util.erl answers OTP's unicode_util functions over them. Each takes a code point, which
// unicode_util has already checked; anything else is badarg.
namespace clause::runtime::builtins {
namespace {
namespace tables = unicode_tables;
using unicode::category_term;

// The atom `name`.
// A small integer term.
Term small_term(std::int64_t value) { return need(Term::from_word(need(encode_integer(value)))); }

// The code point argument; badarg outside 0..16#10FFFF.
char32_t code_point(std::int64_t value) {
    if (value < 0 || value > 0x10FFFF) {
        bad_argument();
    }
    return static_cast<char32_t>(value);
}

// The combining class of a code point.
std::uint32_t combining_class(char32_t code) { return tables::lookup(tables::class_ranges(), code, 0); }

// [{CCC, CP}] of a run of points().
Term class_pairs(ProcessContext &context, std::uint32_t offset, std::uint8_t size) {
    std::vector<Term> pairs;
    for (const auto code : tables::points().subspan(offset, size)) {
        const std::array parts{small_term(combining_class(code)), small_term(code)};
        pairs.push_back(need(TermFactory(context).tuple(parts)));
    }
    return need(TermFactory(context).list(pairs));
}

// A run of points() as OTP's gc(): one code point, or a list of them.
Term run_term(ProcessContext &context, std::uint32_t offset, std::uint8_t size) {
    if (size == 1) {
        return small_term(tables::points()[offset]);
    }
    std::vector<Term> codes;
    for (const auto code : tables::points().subspan(offset, size)) {
        codes.push_back(small_term(code));
    }
    return need(TermFactory(context).list(codes));
}

// The compatibility decomposition: [] or {Tag, [{CCC, CP}]}.
Term compat_term(ProcessContext &context, const tables::Decomposition &entry) {
    if (entry.compat_tag == 0) {
        return need(TermFactory(context).nil());
    }
    const std::array parts{atom(context, tables::compat_tags()[entry.compat_tag - 1]),
                           class_pairs(context, entry.compat, entry.compat_size)};
    return need(TermFactory(context).tuple(parts));
}

// table(CP): {CCC, Canon, Compat, Category}, as OTP's unicode_table/1 with the category resolved.
TermResult<Term> table(ProcessContext &context, std::int64_t value) {
    const auto code = code_point(value);
    const auto entries = tables::decompositions();
    const auto found = std::ranges::lower_bound(entries, code, {}, &tables::Decomposition::code_point);
    const bool decomposes = found != entries.end() && found->code_point == code;
    const auto empty = need(TermFactory(context).nil());
    const std::array parts{small_term(combining_class(code)),
                           decomposes ? class_pairs(context, found->canon, found->canon_size) : empty,
                           decomposes ? compat_term(context, *found) : empty, category_term(context, code)};
    return TermFactory(context).tuple(parts);
}

// class(CP): the canonical combining class.
Term class_of(ProcessContext &, std::int64_t value) { return small_term(combining_class(code_point(value))); }

// cases(CP): {Upper, Lower, Title, Fold}, each a code point or a list.
TermResult<Term> case_of(ProcessContext &context, std::int64_t value) {
    const auto code = code_point(value);
    const auto entries = tables::cases();
    const auto found = std::ranges::lower_bound(entries, code, {}, &tables::CaseMapping::code_point);
    if (found == entries.end() || found->code_point != code) {
        const std::array same{small_term(code), small_term(code), small_term(code), small_term(code)};
        return TermFactory(context).tuple(same);
    }
    std::array<Term, 4> parts{};
    for (std::size_t index = 0; index < parts.size(); ++index) {
        parts[index] = run_term(context, found->offsets[index], found->sizes[index]);
    }
    return TermFactory(context).tuple(parts);
}

// flags(CP): the property bits of unicode_util.erl.
Term flags(ProcessContext &, std::int64_t value) {
    return small_term(tables::lookup(tables::flag_ranges(), code_point(value), 0));
}

// The second code point of compose_pair/2, apart from the first.
struct Second {
    std::int64_t value;
};
} // namespace

// A small integer as compose_pair/2's second code point.
template <> struct Argument<Second> {
    static TermResult<std::optional<Second>> convert(const Term &term) {
        return Argument<std::int64_t>::convert(term).transform(
            [](std::optional<std::int64_t> value) { return value ? std::optional{Second{*value}} : std::nullopt; });
    }
};

namespace {
// compose_pair(A, B): the canonical composition of the pair, or false.
Term compose_pair(ProcessContext &context, std::int64_t first, const Second &second) {
    const tables::Composition key{code_point(first), code_point(second.value), 0};
    const auto entries = tables::compositions();
    const auto order = [](const tables::Composition &left, const tables::Composition &right) {
        return std::pair(left.first, left.second) < std::pair(right.first, right.second);
    };
    const auto found = std::ranges::lower_bound(entries, key, order);
    if (found == entries.end() || found->first != key.first || found->second != key.second) {
        return atom(context, "false"_atom);
    }
    return small_term(found->composed);
}

// unicode_util:spec_version(): {Major, Minor}.
TermResult<Term> spec_version(ProcessContext &context) {
    const auto [major, minor] = tables::spec_version();
    const std::array parts{small_term(major), small_term(minor)};
    return TermFactory(context).tuple(parts);
}

// unicode_util:pattern_whitespace(): [[13, 10] | the single pattern whitespace code points].
TermResult<Term> pattern_whitespace(ProcessContext &context) {
    const std::array crlf{small_term('\r'), small_term('\n')};
    std::vector<Term> items{need(TermFactory(context).list(crlf))};
    for (const auto code : tables::pattern_whitespace()) {
        items.push_back(small_term(code));
    }
    return TermFactory(context).list(items);
}

constexpr std::array UNICODE_TABLE_BUILTINS{
    typed_entry<table>("clause_unicode", "table"),
    typed_entry<class_of>("clause_unicode", "class"),
    typed_entry<case_of>("clause_unicode", "cases"),
    typed_entry<flags>("clause_unicode", "flags"),
    typed_entry<compose_pair>("clause_unicode", "compose_pair"),
    typed_entry<spec_version>("unicode_util", "spec_version"),
    typed_entry<pattern_whitespace>("unicode_util", "pattern_whitespace"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> unicode_table_builtins() noexcept { return builtins::UNICODE_TABLE_BUILTINS; }
} // namespace clause::runtime
