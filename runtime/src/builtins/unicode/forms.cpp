#include "../text.hpp"
#include "../typed.hpp"
#include "code_points.hpp"
#include "normalize.hpp"
#include "tables.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

// unicode_util's chardata walks and unicode's normalization and property functions as builtins (plan 21 step LIB4B,
// docs/builtins.md#unicode-util): the term-level cp/1, gc/1, nf* forms and case mappings, and the loops OTP's
// unicode module runs over them.
namespace clause::runtime::builtins::unicode {
namespace {
// Property bits of flags_of (unicode_util.erl's macros).
constexpr std::uint32_t WHITESPACE = 2;
constexpr std::uint32_t OTHER_ID_START = 4;
constexpr std::uint32_t OTHER_ID_CONTINUE = 8;
constexpr std::uint32_t PATTERN_SYNTAX_LETTER = 16;

Term cp1(ProcessContext &context, const Term &data) { return cp_term(context, data); }

Term gc1(ProcessContext &context, const Term &data) { return gc_term(context, data); }

// unicode_util:nfd/1, nfkd/1, nfc/1 and nfkc/1.
template <Form F> Term normalized1(ProcessContext &context, const Term &data) {
    return normalized_term(context, data, F);
}

// unicode_util:uppercase/1, lowercase/1, titlecase/1 and casefold/1.
template <Case C> Term cased1(ProcessContext &context, const Term &data) { return mapped_first(context, data, C); }

// The code points of a cluster term appended.
void append_cluster(std::u32string &codes, const Term &cluster) {
    if (cluster.is_integer()) {
        codes.push_back(static_cast<char32_t>(*cluster.integer_value()));
        return;
    }
    for (Term rest = cluster; rest.is_cons(); rest = rest.tail_unchecked()) {
        codes.push_back(static_cast<char32_t>(*rest.head_unchecked().integer_value()));
    }
}

// The normalized text as a list of code points or a UTF-8 binary.
Term text_term(ProcessContext &context, const std::u32string &codes, bool binary) {
    TermFactory factory(context);
    if (binary) {
        std::string text;
        for (const auto code : codes) {
            encode(code, text);
        }
        return need(factory.binary(std::as_bytes(std::span(text))));
    }
    std::vector<Term> items;
    items.reserve(codes.size());
    for (const auto code : codes) {
        items.push_back(need(factory.integer(static_cast<std::int64_t>(code))));
    }
    return need(factory.list(items));
}

// unicode's characters_to_nf*_list and _binary loops: the normalized text; {error, Converted, Rest} at invalid UTF-8.
Term normalized_text(ProcessContext &context, const Term &data, Form form, bool binary) {
    std::u32string codes;
    Term rest = data;
    auto next = normalized_term(context, rest, form);
    while (next.is_cons()) {
        append_cluster(codes, next.head_unchecked());
        rest = next.tail_unchecked();
        next = normalized_term(context, rest, form);
    }
    auto converted = text_term(context, codes, binary);
    if (next.is_nil()) {
        return converted;
    }
    TermFactory factory(context);
    const std::array items{need(factory.atom("error")), converted, need(next.tuple_element(1))};
    return need(factory.tuple(items));
}

// Whether a failure is an Erlang error the unicode functions turn into badarg (OTP's catch error:_).
bool error_class(const BuiltinFailure &failure) {
    return !failure.term && failure.reason != abi::v1::ErrorReason::raised_exit &&
           failure.reason != abi::v1::ErrorReason::raised_throw;
}

// unicode:characters_to_nfc_list/1 and its siblings: badarg for anything the walk cannot read.
template <Form F, bool Binary> Term characters_to_form(ProcessContext &context, const Term &data) {
    try {
        return normalized_text(context, data, F, Binary);
    } catch (const BuiltinFailure &failure) {
        if (!error_class(failure)) {
            throw;
        }
        bad_argument();
    }
}

// The code point argument of the property functions; badarg for anything else.
char32_t code_point_of(const Term &term) {
    const auto value = term.is_integer() ? term.integer_value() : std::unexpected(TermError::wrong_type);
    if (!value || *value < 0 || *value > 0x10FFFF) {
        bad_argument();
    }
    return static_cast<char32_t>(*value);
}

bool has(char32_t code, std::uint32_t bit) { return (flags_of(code) & bit) != 0; }

bool ascii_letter(char32_t code) { return (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z'); }

// unicode:is_whitespace/1: OTP's low code points first, nothing else below 5001, then the table.
bool whitespace(char32_t code) {
    constexpr std::array<char32_t, 8> LOW{9, 10, 11, 12, 13, 32, 133, 160};
    // Kept sorted for the binary search (optimization step OPTM.1).
    static_assert(std::ranges::is_sorted(LOW));
    if (std::ranges::binary_search(LOW, code)) {
        return true;
    }
    return code > 5000 && has(code, WHITESPACE);
}

// unicode_util:is_letter_not_pattern_syntax/1.
bool letter_not_pattern_syntax(char32_t code) { return !has(code, PATTERN_SYNTAX_LETTER); }

// unicode:is_id_start/1 above ASCII: letters and letter numbers, Other_ID_Start, no pattern syntax.
bool id_start(char32_t code) {
    const auto [group, name] = category_names(code);
    if (group == "number" && name == "letter") {
        return true;
    }
    if (group == "letter") {
        return name != "modifier" || letter_not_pattern_syntax(code);
    }
    return has(code, OTHER_ID_START);
}

// unicode:is_id_continue/1 above ASCII for the categories it lists.
std::optional<bool> id_continue_listed(char32_t code, std::string_view group, std::string_view name) {
    if ((group == "punctuation" && name == "connector") || (group == "mark" && name != "enclosing")) {
        return true;
    }
    if (group == "number") {
        return name != "other" || has(code, OTHER_ID_CONTINUE);
    }
    if (group == "letter") {
        return name != "modifier" || letter_not_pattern_syntax(code);
    }
    return std::nullopt;
}

// unicode:is_id_continue/1 above ASCII: the listed categories, else Other_ID_Start or Other_ID_Continue.
bool id_continue(char32_t code) {
    const auto [group, name] = category_names(code);
    return id_continue_listed(code, group, name).value_or(has(code, OTHER_ID_START) || has(code, OTHER_ID_CONTINUE));
}

Term boolean(ProcessContext &context, bool value) { return need(TermFactory(context).boolean(value)); }

Term category1(ProcessContext &context, const Term &term) { return category_term(context, code_point_of(term)); }

Term is_whitespace1(ProcessContext &context, const Term &term) {
    return boolean(context, whitespace(code_point_of(term)));
}

Term is_id_start1(ProcessContext &context, const Term &term) {
    const auto code = code_point_of(term);
    return boolean(context, code < 128 ? ascii_letter(code) : id_start(code));
}

Term is_id_continue1(ProcessContext &context, const Term &term) {
    const auto code = code_point_of(term);
    const bool ascii = ascii_letter(code) || (code >= '0' && code <= '9') || code == '_';
    return boolean(context, code < 128 ? ascii : id_continue(code));
}

constexpr std::array UNICODE_FORM_BUILTINS{
    typed_entry<cp1>("unicode_util", "cp"),
    typed_entry<gc1>("unicode_util", "gc"),
    typed_entry<normalized1<Form::nfd>>("unicode_util", "nfd"),
    typed_entry<normalized1<Form::nfkd>>("unicode_util", "nfkd"),
    typed_entry<normalized1<Form::nfc>>("unicode_util", "nfc"),
    typed_entry<normalized1<Form::nfkc>>("unicode_util", "nfkc"),
    typed_entry<cased1<Case::upper>>("unicode_util", "uppercase"),
    typed_entry<cased1<Case::lower>>("unicode_util", "lowercase"),
    typed_entry<cased1<Case::title>>("unicode_util", "titlecase"),
    typed_entry<cased1<Case::fold>>("unicode_util", "casefold"),
    typed_entry<characters_to_form<Form::nfc, false>>("unicode", "characters_to_nfc_list"),
    typed_entry<characters_to_form<Form::nfc, true>>("unicode", "characters_to_nfc_binary"),
    typed_entry<characters_to_form<Form::nfd, false>>("unicode", "characters_to_nfd_list"),
    typed_entry<characters_to_form<Form::nfd, true>>("unicode", "characters_to_nfd_binary"),
    typed_entry<characters_to_form<Form::nfkc, false>>("unicode", "characters_to_nfkc_list"),
    typed_entry<characters_to_form<Form::nfkc, true>>("unicode", "characters_to_nfkc_binary"),
    typed_entry<characters_to_form<Form::nfkd, false>>("unicode", "characters_to_nfkd_list"),
    typed_entry<characters_to_form<Form::nfkd, true>>("unicode", "characters_to_nfkd_binary"),
    typed_entry<category1>("unicode", "category"),
    typed_entry<is_whitespace1>("unicode", "is_whitespace"),
    typed_entry<is_id_start1>("unicode", "is_id_start"),
    typed_entry<is_id_continue1>("unicode", "is_id_continue"),
};
} // namespace
} // namespace clause::runtime::builtins::unicode

namespace clause::runtime {
std::span<const BuiltinEntry> unicode_form_builtins() noexcept { return builtins::unicode::UNICODE_FORM_BUILTINS; }
} // namespace clause::runtime
