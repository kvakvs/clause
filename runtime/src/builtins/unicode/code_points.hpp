#pragma once
#include "../support.hpp"
#include <cstdint>
#include <string_view>
#include <utility>

// unicode_util:cp/1 and gc/1 over chardata terms (plan 21 step LIB4, docs/builtins.md#unicode-util), in OTP's result
// shapes: [CodePoint | Rest] or [Cluster | Rest] with Rest the chardata left as OTP leaves it (nested lists opened
// as far as OTP opens them, binaries kept as sub-binaries), [] at the end, {error, Rest} at invalid UTF-8.
namespace clause::runtime::builtins::unicode {
// unicode_util:cp(Chardata): the term OTP returns; function_clause for data that is not chardata.
Term cp_term(ProcessContext &context, const Term &data);

// unicode_util:gc(Chardata): the term OTP returns; {badarg, _} errors as OTP raises them.
Term gc_term(ProcessContext &context, const Term &data);

// The head and tail of a non-empty cp/gc result; the caller has checked it is a cons.
struct Next {
    Term item;
    Term rest;
};

Next split(const Term &result);

// The property bits of unicode_util.erl for a code point (0 outside the tables).
std::uint32_t flags_of(char32_t code);

// The general category of a code point as OTP's {Group, Name} names, and as that tuple.
std::pair<std::string_view, std::string_view> category_names(char32_t code);
Term category_term(ProcessContext &context, char32_t code);

// The case mappings of unicode_util:get_case/1, in its field order.
enum class Case : std::uint8_t { upper, lower, title, fold };

// One case of a code point: a code point or a list of them, as unicode_util:get_case/1's fields.
Term case_term(ProcessContext &context, char32_t code, Case which);

// unicode_util:uppercase/1, lowercase/1, titlecase/1 or casefold/1: [Mapped | Rest] with the first code point's case
// mapping, [] at the end; error({badarg, Rest}) at invalid UTF-8.
Term mapped_first(ProcessContext &context, const Term &data, Case which);
} // namespace clause::runtime::builtins::unicode
