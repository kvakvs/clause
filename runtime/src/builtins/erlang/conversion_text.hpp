#pragma once
#include "../../terms/integers.hpp"
#include "../typed.hpp"
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Text rules the list and binary conversion builtins share: atom names, integer digits and float text (moved out of
// conversions.cpp for plan step 41D, docs/builtins.md#source-layout).
namespace clause::runtime::builtins {
// Atoms hold at most 255 characters.
constexpr std::size_t MAX_ATOM_CHARACTERS = 255;

// A proper list of the given character codes.
TermResult<Term> char_list(ProcessContext &context, std::u32string_view codes);
// A proper list of the bytes of `text`.
TermResult<Term> byte_list(ProcessContext &context, std::string_view text);
// The head of a cons, advancing `list` to its tail.
Term next(Term &list);

// Why a text is not an atom name: OTP checks the length before each character.
enum class NameError : std::uint8_t { badarg, too_long };
// The UTF-8 spelling of a proper list of at most 255 characters.
std::expected<std::string, NameError> atom_name(Term list);
// Throw the badarg or system_limit of a name error.
[[noreturn]] void name_failure(NameError error);

// A base argument: a small integer in 2..36, else badarg.
unsigned base_of(std::int64_t base);
// The characters of a proper list of bytes; none for anything else.
std::optional<std::vector<unsigned char>> byte_codes(Term list);
// The integer an optional sign and digits of `base` spell, as a term; badarg for other text, system_limit past the
// integer limit.
TermResult<Term> integer_term(ProcessContext &context, std::span<const unsigned char> text, unsigned base);

// The text of float_to_list/2 and float_to_binary/2 for a list of options; badarg for an invalid option.
std::string float_with_options(double number, const Term &options);
// The float of OTP's float text: an optional sign, digits, '.', digits and an optional exponent; badarg for other
// text or an infinite value, an underflow is zero.
double float_of_text(std::span<const unsigned char> text);
} // namespace clause::runtime::builtins
