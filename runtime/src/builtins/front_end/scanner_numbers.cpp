#include "../erlang/conversion_text.hpp"
#include "scanner_impl.hpp"
#include <cmath>
#include <utility>

// erl_scan's numbers (plan 21 step LIB30): OTP's scan_number/7, scan_fraction/7, scan_exponent/7 and their based
// counterparts, with digit separators, OTP's error columns and its arithmetic for based floats.
namespace clause::runtime::builtins::front_end::scanning {
namespace {
bool is_exponent_mark(char32_t code) { return code == 'e' || code == 'E'; }

bool is_sign(char32_t code) { return code == '+' || code == '-'; }

// The value of a digit of any base up to 36.
unsigned digit_value(char digit) {
    return is_digit(static_cast<char32_t>(digit)) ? static_cast<unsigned>(digit - '0')
                                                  : static_cast<unsigned>((digit | 0x20) - 'a' + 10);
}

// OTP's base check: 2..36 written in decimal digits, leading zeros allowed.
std::optional<unsigned> base_of_digits(const std::string &digits) {
    const auto first = digits.find_first_not_of('0');
    if (first == std::string::npos || digits.size() - first > 2) {
        return std::nullopt;
    }
    const auto base = static_cast<unsigned>(std::stoul(digits.substr(first)));
    return base >= 2 && base <= 36 ? std::optional(base) : std::nullopt;
}

// trim_float_zeros/1 both ways: trailing zeros of the fraction and leading zeros, but no zero next to the point.
std::string trimmed_mantissa(std::string digits) {
    while (digits.size() > 1 && digits.back() == '0' && digits[digits.size() - 2] != '.') {
        digits.pop_back();
    }
    while (digits.size() > 1 && digits.front() == '0' && digits[1] != '.') {
        digits.erase(digits.begin());
    }
    return digits;
}

// A based float as OTP computes it: N * math:pow(B, Exp - D); badarith when a step overflows.
double based_float(unsigned base, const std::string &digits, std::int64_t exponent) {
    const auto mantissa = trimmed_mantissa(digits);
    const auto point = mantissa.find('.');
    double value = 0;
    for (const auto digit : mantissa) {
        if (digit != '.') {
            value = value * base + digit_value(digit);
        }
    }
    const auto fraction = static_cast<std::int64_t>(mantissa.size() - point - 1);
    const auto scale = std::pow(static_cast<double>(base), static_cast<double>(exponent - fraction));
    const auto result = value * scale;
    if (!std::isfinite(scale) || !std::isfinite(result)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    return result;
}

// list_to_integer/1 of a based float's decimal exponent; badarg for one without digits, as OTP raises.
std::int64_t exponent_value(const std::string &text) {
    const auto start = text.find_first_not_of("+-");
    if (start == std::string::npos) {
        bad_argument();
    }
    const auto digits = text.substr(start);
    constexpr std::int64_t LIMIT = 1'000'000'000;
    std::int64_t value = 0;
    for (const auto digit : digits) {
        value = std::min(LIMIT, value * 10 + (digit - '0'));
    }
    return text.front() == '-' ? -value : value;
}

std::optional<double> float_of_digits(const std::string &digits) {
    const std::vector<unsigned char> text(digits.begin(), digits.end());
    try {
        return float_of_text(text);
    } catch (const BuiltinFailure &) {
        return std::nullopt;
    }
}
} // namespace

// Digits of `base` and their `_` separators (one between two digits) from `index`, appended without separators.
std::size_t Scanner::separated_digits(std::size_t index, unsigned base, std::string &digits) const {
    for (;;) {
        const auto code = at(index);
        if (is_based_digit(code, base)) {
            digits.push_back(static_cast<char>(code));
            ++index;
            continue;
        }
        const auto previous = !digits.empty() && is_based_digit(static_cast<char32_t>(digits.back()), base);
        if (code != '_' || !previous || !is_based_digit(at(index + 1), base)) {
            return index;
        }
        ++index;
    }
}

// scan_number/7: an integer, a fraction, a base, or an error at a name character.
Step Scanner::number() {
    std::string digits;
    const auto index = separated_digits(pos_, 10, digits);
    const auto code = at(index);
    const auto columns = index - pos_;
    if (code == '.') {
        const auto next = at(index + 1);
        if (is_digit(next)) {
            return decimal_fraction(index + 2, digits + '.' + static_cast<char>(next));
        }
        if (is_namechar_macro(next)) {
            return illegal_number("float", columns, index);
        }
    } else if (code == '#') {
        const auto base = base_of_digits(digits);
        if (base) {
            return based(index + 1, *base);
        }
        auto failure = error(ErrorKind::base, {}, location_, location_.advanced(static_cast<std::int64_t>(columns)),
                             rest_at(index));
        failure.digits = digits;
        return fail(std::move(failure));
    } else if (is_namechar_macro(code)) {
        return illegal_number("integer", columns, index);
    }
    return integer_token(index, std::move(digits), 10);
}

// scan_fraction/7: digits after the point, then an exponent.
Step Scanner::decimal_fraction(std::size_t index, std::string digits) {
    index = separated_digits(index, 10, digits);
    const auto code = at(index);
    if (is_exponent_mark(code)) {
        return decimal_exponent(index + 1, digits + static_cast<char>(code));
    }
    if (is_namechar_macro(code)) {
        return illegal_number("float", index - pos_, index);
    }
    return float_end(index, digits);
}

// scan_exponent_sign/7 and scan_exponent/7.
Step Scanner::decimal_exponent(std::size_t index, std::string digits) {
    if (is_sign(at(index))) {
        digits.push_back(static_cast<char>(at(index)));
        ++index;
    }
    index = separated_digits(index, 10, digits);
    if (is_namechar_macro(at(index))) {
        return illegal_number("float", index - pos_, index);
    }
    return float_end(index, digits);
}

// float_end/7: list_to_float/1 of the text without separators.
Step Scanner::float_end(std::size_t index, const std::string &digits) {
    const auto number = float_of_digits(digits);
    if (!number) {
        return illegal_number("float", index - pos_, index);
    }
    emit(TokenKind::floating, index, {}).number = *number;
    advance(index, static_cast<std::int64_t>(index - pos_));
    return std::nullopt;
}

// scan_based_num/9: digits of the base after B#.
Step Scanner::based(std::size_t index, unsigned base) {
    const auto prefix = index;
    std::string digits;
    index = separated_digits(index, base, digits);
    const auto code = at(index);
    if (code == '.') {
        const auto next = at(index + 1);
        if (is_based_digit(next, base)) {
            return based_fraction(index + 2, base, digits + '.' + static_cast<char>(next));
        }
        if (is_namechar_macro(next)) {
            return illegal_number("float", index - pos_, index);
        }
    } else if (is_namechar_macro(code)) {
        return illegal_number("integer", index - pos_, index);
    }
    if (digits.empty()) {
        return illegal_number("integer", prefix - pos_, index);
    }
    return integer_token(index, std::move(digits), base);
}

// scan_based_fraction/9: fraction digits of the base, then #e and a decimal exponent.
Step Scanner::based_fraction(std::size_t index, unsigned base, std::string digits) {
    index = separated_digits(index, base, digits);
    const auto code = at(index);
    if (code == '#' && is_exponent_mark(at(index + 1))) {
        return based_exponent(index + 2, base, digits, index);
    }
    if (is_namechar_macro(code)) {
        return illegal_number("float", index - pos_, index);
    }
    return based_float_end(index, digits, base, {}, index);
}

// scan_based_exponent_sign/10 and scan_based_exponent/10; `mantissa` is where the #e starts.
Step Scanner::based_exponent(std::size_t index, unsigned base, const std::string &digits, std::size_t mantissa) {
    std::string exponent;
    if (is_sign(at(index))) {
        exponent.push_back(static_cast<char>(at(index)));
        ++index;
    }
    index = separated_digits(index, 10, exponent);
    return based_float_end(index, digits, base, "e" + exponent, mantissa);
}

// based_float_end/10: list_to_float/1 for base 10, OTP's arithmetic for other bases.
Step Scanner::based_float_end(std::size_t index, const std::string &digits, unsigned base, const std::string &exponent,
                              std::size_t mantissa) {
    double number = 0;
    if (base == 10) {
        const auto parsed = float_of_digits(digits + exponent);
        if (!parsed) {
            return illegal_number("float", mantissa - pos_, index);
        }
        number = *parsed;
    } else {
        number = based_float(base, digits, exponent.empty() ? 0 : exponent_value(exponent.substr(1)));
    }
    emit(TokenKind::floating, index, {}).number = number;
    advance(index, static_cast<std::int64_t>(index - pos_));
    return std::nullopt;
}

Step Scanner::integer_token(std::size_t end, std::string digits, unsigned base) {
    auto &token = emit(TokenKind::integer, end, {});
    token.digits = std::move(digits);
    token.base = base;
    advance(end, static_cast<std::int64_t>(end - pos_));
    return std::nullopt;
}

// {illegal, What} from the number's start to `columns` further, with the rest at `rest`.
Step Scanner::illegal_number(std::string what, std::size_t columns, std::size_t rest) const {
    return fail(error(ErrorKind::illegal, std::move(what), location_,
                      location_.advanced(static_cast<std::int64_t>(columns)), rest_at(rest)));
}
} // namespace clause::runtime::builtins::front_end::scanning
