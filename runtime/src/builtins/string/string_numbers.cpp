#include "../../terms/integers.hpp"
#include "../lists/list_walk.hpp"
#include "search.hpp"
#include <array>
#include <charconv>
#include <cmath>

// string's number parsers (plan 21 step LIB4): list_to_integer/1 and list_to_float/1 as erts answers them, and
// to_integer/1 and to_float/1 over take/2 as string.erl composes them.
namespace clause::runtime::builtins::string {
namespace {
using detail::Integer;
using lists::no_clause;

Term error_tuple(const Terms &terms, std::string_view reason) {
    const std::array items{terms.atom("error"), terms.atom(reason)};
    return terms.tuple(items);
}

// A list element that is the character `c`.
bool starts_with(const Term &list, char c) { return list.is_cons() && is_char(list.head_unchecked(), c); }

bool digit_head(const Term &list) {
    if (!list.is_cons()) {
        return false;
    }
    const auto code = small_of(list.head_unchecked());
    return code && *code >= '0' && *code <= '9';
}

// The decimal digits at the start of a list, as text, and the list after them.
std::pair<std::string, Term> digits_of(Term list) {
    std::string digits;
    while (digit_head(list)) {
        digits.push_back(static_cast<char>(*small_of(list.head_unchecked())));
        list = list.tail_unchecked();
    }
    return {digits, list};
}

// A list argument of the erts parsers: none gives {error, not_a_list}.
std::optional<Term> list_argument(const Term &term) { return term.is_list() ? std::optional{term} : std::nullopt; }

// list_to_integer(String): {Integer, Rest}, or {error, no_integer | not_a_list | system_limit}.
Term list_to_integer1(ProcessContext &context, const Term &string) {
    const Terms terms(context);
    if (!list_argument(string)) {
        return error_tuple(terms, "not_a_list");
    }
    Term rest = string;
    const bool negative = starts_with(rest, '-');
    if (negative || starts_with(rest, '+')) {
        rest = rest.tail_unchecked();
    }
    auto [digits, after] = digits_of(rest);
    if (digits.empty()) {
        return error_tuple(terms, "no_integer");
    }
    if (digits.size() > detail::integer_decimal_limit) {
        return error_tuple(terms, "system_limit");
    }
    Integer value = detail::integer_digits(digits);
    if (detail::integer_bits(value) > detail::integer_bit_limit) {
        return error_tuple(terms, "system_limit");
    }
    if (negative) {
        value = -value;
    }
    const std::array items{need(detail::IntegerAccess::make(context.heap(), value)), after};
    return terms.tuple(items);
}

// An optional sign at the start of a list, as text, and the list after it.
std::pair<std::string, Term> sign_of(Term list) {
    std::string sign;
    if (starts_with(list, '-') || starts_with(list, '+')) {
        sign.push_back(static_cast<char>(*small_of(list.head_unchecked())));
        list = list.tail_unchecked();
    }
    return {sign, list};
}

// An exponent (e or E, a sign, digits) at the start of a list, as text, and the list after it; empty text and the
// same list without one (an e without digits is not consumed).
std::pair<std::string, Term> exponent_of(const Term &list) {
    if (!starts_with(list, 'e') && !starts_with(list, 'E')) {
        return {"", list};
    }
    const auto [sign, after_sign] = sign_of(list.tail_unchecked());
    const auto [power, after_power] = digits_of(after_sign);
    if (power.empty()) {
        return {"", list};
    }
    return {"e" + sign + power, after_power};
}

// The text of a float at the start of a list, Erlang's syntax with a comma allowed as the point, and the list after
// it; none without a float.
std::optional<std::pair<std::string, Term>> float_text_of(const Term &list) {
    const auto [sign, after_sign] = sign_of(list);
    const auto [whole, after_whole] = digits_of(after_sign);
    if (whole.empty() || !(starts_with(after_whole, '.') || starts_with(after_whole, ','))) {
        return std::nullopt;
    }
    const auto [fraction, after_fraction] = digits_of(after_whole.tail_unchecked());
    if (fraction.empty()) {
        return std::nullopt;
    }
    const auto [exponent, rest] = exponent_of(after_fraction);
    return std::pair{sign + whole + "." + fraction + exponent, rest};
}

// list_to_float(String): {Float, Rest}, or {error, no_float | not_a_list}.
Term list_to_float1(ProcessContext &context, const Term &string) {
    const Terms terms(context);
    if (!list_argument(string)) {
        return error_tuple(terms, "not_a_list");
    }
    const auto parsed = float_text_of(string);
    if (!parsed) {
        return error_tuple(terms, "no_float");
    }
    double value = 0;
    const std::string_view text = parsed->first;
    const auto digits = text.starts_with('+') ? text.substr(1) : text;
    std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (!std::isfinite(value)) {
        bad_argument();
    }
    const std::array items{need(TermFactory(context).floating(value)), parsed->second};
    return terms.tuple(items);
}

// erlang:length/1 of a proper list.
std::size_t list_length(const Term &list) {
    const auto items = proper_list(list);
    if (!items) {
        bad_argument();
    }
    return items->size();
}

// The characters to_number parsed (List) and the chardata after them (Tail).
struct Taken {
    Term list;
    Term tail;
};

// to_number: the parsed {Number, Rest} with the rest of the original string, by bytes for a binary, by Rest ++ Tail
// for a list.
Term to_number(const Terms &terms, const TextArg &string, const Term &parsed, const Taken &taken) {
    const auto number = need(parsed.tuple_element(0));
    const auto rest = need(parsed.tuple_element(1));
    if (string.term.is_binary()) {
        const auto skip = list_length(taken.list) - list_length(rest);
        const std::array items{number, terms.slice(string.term, skip, binary_bytes(string.term).size() - skip)};
        return terms.tuple(items);
    }
    const auto items_rest = proper_list(rest);
    if (!items_rest) {
        bad_argument();
    }
    const std::array items{number, terms.list(*items_rest, taken.tail)};
    return terms.tuple(items);
}

// to_integer/1 and to_float/1: the characters a number may use taken from the front, parsed, and the rest.
template <Term (*Parse)(ProcessContext &, const Term &), bool Float>
Term to_number_of(ProcessContext &context, const TextArg &string) {
    const Terms terms(context);
    try {
        const auto chars = terms.characters_to_list(terms.binary(
            std::as_bytes(std::span(Float ? std::string_view("+-0123456789eE.,") : std::string_view("+-0123456789")))));
        const auto taken = take_leading(context, string.term, {chars});
        if (is_empty(taken.head)) {
            return error_tuple(terms, Float ? "no_float" : "no_integer");
        }
        const auto list = terms.characters_to_list(taken.head);
        auto parsed = Parse(context, list);
        if (need(parsed.tuple_element(0)).is_atom()) {
            return parsed;
        }
        return to_number(terms, string, parsed, {.list = list, .tail = taken.tail});
    } catch (const BuiltinFailure &failure) {
        if (failure.term) {
            throw;
        }
        return error_tuple(terms, "badarg");
    }
}

constexpr std::array STRING_NUMBER_BUILTINS{
    typed_entry<list_to_integer1>("string", "list_to_integer"),
    typed_entry<list_to_float1>("string", "list_to_float"),
    typed_entry<to_number_of<list_to_integer1, false>>("string", "to_integer"),
    typed_entry<to_number_of<list_to_float1, true>>("string", "to_float"),
};
} // namespace
} // namespace clause::runtime::builtins::string

namespace clause::runtime {
std::span<const BuiltinEntry> string_number_builtins() noexcept { return builtins::string::STRING_NUMBER_BUILTINS; }
} // namespace clause::runtime
