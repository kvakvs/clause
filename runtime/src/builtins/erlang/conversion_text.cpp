#include "conversion_text.hpp"
#include "../float_text.hpp"
#include "../text.hpp"
#include <algorithm>
#include <charconv>
#include <clause/abi/equality.hpp>
#include <cmath>

namespace clause::runtime::builtins {
namespace {
using detail::Integer;

// The value of a digit character of base 36 (either letter case); 36 for any other character.
unsigned digit_of(unsigned char character) {
    if (character >= '0' && character <= '9') {
        return static_cast<unsigned>(character - '0');
    }
    if (character >= 'a' && character <= 'z') {
        return static_cast<unsigned>(character - 'a' + 10);
    }
    return character >= 'A' && character <= 'Z' ? static_cast<unsigned>(character - 'A' + 10) : 36;
}

// Accumulates digits in machine-word chunks before folding them into the multiprecision value.
class DigitReader {
  public:
    explicit DigitReader(unsigned base) : base_(base) {}

    // Add one digit value.
    void add(unsigned digit) {
        part_ = part_ * base_ + digit;
        scale_ *= base_;
        ++count_;
        if (scale_ > (std::uint64_t{1} << 63) / base_) {
            flush();
        }
    }

    // The value read so far; none when no digit was read.
    std::optional<Integer> value() {
        flush();
        return count_ ? std::optional{value_} : std::nullopt;
    }

  private:
    // Fold the pending chunk into the value.
    void flush() {
        value_ = value_ * Integer(scale_) + Integer(part_);
        part_ = 0;
        scale_ = 1;
    }

    unsigned base_;
    // The value of the folded digits, the pending chunk, its scale and the digit count.
    Integer value_ = 0;
    std::uint64_t part_ = 0;
    std::uint64_t scale_ = 1;
    std::size_t count_ = 0;
};

// Digits OTP reads into a small integer before switching to its multiprecision path.
std::size_t small_digits(unsigned base) {
    std::size_t digits = 0;
    for (std::uint64_t power = base; power < (std::uint64_t{1} << 59); power *= base) {
        ++digits;
    }
    return digits;
}

// OTP's size limits for its multiprecision path, checked before the digits: system_limit past them.
bool too_many_digits(std::span<const unsigned char> digits, unsigned base) {
    const auto small = small_digits(base);
    const bool multiprecision =
        digits.size() > small &&
        std::ranges::all_of(digits.first(small + 1), [&](unsigned char c) { return digit_of(c) < base; });
    return multiprecision && (digits.size() > 4'194'304 || (digits.size() > 1'262'611 && base >= 10));
}

// Why a text is not an integer.
enum class ParseError : std::uint8_t { badarg, system_limit };

// The digits of `text` after its optional sign, with OTP's leading-zero and size rules.
std::expected<Integer, ParseError> parse_digits(std::span<const unsigned char> text, unsigned base) {
    const auto zeros =
        static_cast<std::size_t>(std::ranges::find_if(text, [](auto c) { return c != '0'; }) - text.begin());
    const auto digits = text.subspan(zeros);
    if (too_many_digits(digits, base)) {
        return std::unexpected(ParseError::system_limit);
    }
    DigitReader reader(base);
    for (const auto character : digits) {
        if (digit_of(character) >= base) {
            return std::unexpected(ParseError::badarg);
        }
        reader.add(digit_of(character));
    }
    if (zeros == 0 && digits.empty()) {
        return std::unexpected(ParseError::badarg);
    }
    return reader.value().value_or(Integer(0));
}

// An optional sign and at least one digit of `base`.
std::expected<Integer, ParseError> parse_integer(std::span<const unsigned char> text, unsigned base) {
    const bool signed_text = !text.empty() && (text.front() == '-' || text.front() == '+');
    auto value = parse_digits(signed_text ? text.subspan(1) : text, base);
    if (value && signed_text && text.front() == '-') {
        *value = -*value;
    }
    return value;
}

// Apply the atom option compact or short; false for any other atom.
bool float_flag(std::string_view name, FloatFormat &format) {
    if (name == "compact") {
        format.compact = true;
        return true;
    }
    if (name == "short") {
        format.kind = FloatFormat::Kind::shortest;
        return true;
    }
    return false;
}

// Apply one float_to_list/2 option; false for anything OTP rejects.
bool float_option(const Term &option, FloatFormat &format) {
    if (option.is_atom()) {
        const auto name = option.atom_spelling();
        return name && float_flag(*name, format);
    }
    if (!option.is_tuple() || option.tuple_size().value_or(0) != 2) {
        return false;
    }
    const auto kind = option.tuple_element(0).value().atom_spelling();
    const auto digits = small(option.tuple_element(1).value().word());
    if (!kind || !digits || (*kind != "decimals" && *kind != "scientific")) {
        return false;
    }
    format.kind = *kind == "decimals" ? FloatFormat::Kind::fixed : FloatFormat::Kind::scientific;
    format.decimals = *digits;
    return true;
}

// Apply a proper list of options in order; false when one is invalid or the list is improper.
bool float_options(Term options, FloatFormat &format) {
    while (options.is_cons()) {
        if (!float_option(next(options), format)) {
            return false;
        }
    }
    return options.is_nil();
}

// The length of the run of decimal digits at the start of `text`.
std::size_t digit_run(std::span<const unsigned char> text) {
    return static_cast<std::size_t>(std::ranges::find_if(text, [](auto c) { return c < '0' || c > '9'; }) -
                                    text.begin());
}

// Whether `text` is an optional float exponent: nothing, or e or E, an optional sign and digits.
bool exponent_syntax(std::span<const unsigned char> text) {
    if (text.empty()) {
        return true;
    }
    if (text.front() != 'e' && text.front() != 'E') {
        return false;
    }
    auto digits = text.subspan(1);
    if (!digits.empty() && (digits.front() == '+' || digits.front() == '-')) {
        digits = digits.subspan(1);
    }
    return !digits.empty() && digit_run(digits) == digits.size();
}

// Whether `text` is OTP's float syntax: digits '.' digits, then an optional exponent.
bool float_syntax(std::span<const unsigned char> text) {
    const auto whole = digit_run(text);
    if (whole == 0 || whole == text.size() || text[whole] != '.') {
        return false;
    }
    const auto rest = text.subspan(whole + 1);
    const auto fraction = digit_run(rest);
    return fraction != 0 && exponent_syntax(rest.subspan(fraction));
}

// Whether float text has a negative exponent.
bool negative_exponent(std::string_view text) {
    const auto mark = text.find_first_of("eE");
    return mark != std::string_view::npos && text[mark + 1] == '-';
}

// The value of unsigned float text: badarg for other text or an exponent past the range, zero for an underflow
// (as with strtod).
double magnitude(std::span<const unsigned char> text) {
    if (!float_syntax(text)) {
        bad_argument();
    }
    const std::string digits(text.begin(), text.end());
    double value = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (parsed.ec == std::errc::result_out_of_range) {
        if (!negative_exponent(digits)) {
            bad_argument();
        }
        return 0;
    }
    if (!std::isfinite(value)) {
        bad_argument();
    }
    return value;
}
} // namespace

TermResult<Term> char_list(ProcessContext &context, std::u32string_view codes) {
    std::vector<Term> items;
    items.reserve(codes.size());
    for (const auto code : codes) {
        items.push_back(*Term::from_word(*abi::v1::NativeIntegerEncoding::encode(code)));
    }
    return TermFactory(context).list(items);
}

TermResult<Term> byte_list(ProcessContext &context, std::string_view text) {
    std::u32string codes(text.size(), 0);
    std::ranges::transform(text, codes.begin(), [](char c) { return static_cast<unsigned char>(c); });
    return char_list(context, codes);
}

Term next(Term &list) {
    auto head = list.head().value();
    list = list.tail().value();
    return head;
}

std::expected<std::string, NameError> atom_name(Term list) {
    std::string text;
    for (std::size_t count = 0; !list.is_nil(); ++count) {
        if (!list.is_cons()) {
            return std::unexpected(NameError::badarg);
        }
        if (count == MAX_ATOM_CHARACTERS) {
            return std::unexpected(NameError::too_long);
        }
        const auto code = small(next(list).word());
        if (!code || !unicode_character(*code)) {
            return std::unexpected(NameError::badarg);
        }
        encode(static_cast<char32_t>(*code), text);
    }
    return text;
}

void name_failure(NameError error) {
    throw BuiltinFailure{error == NameError::too_long ? abi::v1::ErrorReason::system_limit
                                                      : abi::v1::ErrorReason::badarg};
}

unsigned base_of(std::int64_t base) {
    if (base < 2 || base > 36) {
        bad_argument();
    }
    return static_cast<unsigned>(base);
}

std::optional<std::vector<unsigned char>> byte_codes(Term list) {
    std::vector<unsigned char> codes;
    while (list.is_cons()) {
        const auto code = small(next(list).word());
        if (!code || *code < 0 || *code > 255) {
            return std::nullopt;
        }
        codes.push_back(static_cast<unsigned char>(*code));
    }
    return list.is_nil() ? std::optional{std::move(codes)} : std::nullopt;
}

TermResult<Term> integer_term(ProcessContext &context, std::span<const unsigned char> text, unsigned base) {
    const auto value = parse_integer(text, base);
    if (!value || detail::integer_bits(*value) > detail::integer_bit_limit) {
        throw BuiltinFailure{value.error_or(ParseError::system_limit) == ParseError::badarg
                                 ? abi::v1::ErrorReason::badarg
                                 : abi::v1::ErrorReason::system_limit};
    }
    return detail::IntegerAccess::make(context.heap(), *value);
}

std::string float_with_options(double number, const Term &options) {
    FloatFormat format;
    const auto text = float_options(options, format) ? float_text(number, format) : std::nullopt;
    if (!text) {
        bad_argument();
    }
    return *text;
}

double float_of_text(std::span<const unsigned char> text) {
    const bool sign = !text.empty() && (text.front() == '-' || text.front() == '+');
    const auto value = magnitude(sign ? text.subspan(1) : text);
    return sign && text.front() == '-' ? -value : value;
}
} // namespace clause::runtime::builtins
