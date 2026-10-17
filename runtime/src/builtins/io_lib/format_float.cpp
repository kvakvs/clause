#include "../float_text.hpp"
#include "format.hpp"
#include <array>
#include <cmath>

// The ~e, ~f and ~g controls (plan 21 step LIB4C): io_lib_format's float_e/3, float_f/3 and fwrite_g/5, which round
// the 21 significant digits of float_to_list/1 by their first dropped digit.
namespace clause::runtime::builtins::io_lib {
namespace {
// float_data/1: the digits of float_to_list(Value) and its exponent plus one.
struct Digits {
    std::string digits;
    std::int64_t exponent = 0;
};

Digits float_data(double value) {
    const auto text = *float_text(value, FloatFormat{});
    const auto mark = text.find('e');
    Digits out;
    for (const char c : text.substr(0, mark)) {
        if (c >= '0' && c <= '9') {
            out.digits.push_back(c);
        }
    }
    out.exponent = std::stoll(text.substr(mark + 1)) + 1;
    return out;
}

// float_man/3: `whole` digits, '.', `decimals` digits, rounded up by the next digit; the carry out of the first.
std::pair<std::string, bool> mantissa(std::string_view digits, std::size_t whole, std::size_t decimals) {
    if (digits.size() < whole) {
        return {std::string(digits) + std::string(whole - digits.size(), '0') + "." + std::string(decimals, '0'),
                false};
    }
    const auto rest = digits.substr(whole);
    auto text = std::string(digits.substr(0, whole)) + "." + std::string(rest.substr(0, decimals));
    text.append(decimals > rest.size() ? decimals - rest.size() : 0, '0');
    bool carry = rest.size() > decimals && rest[decimals] >= '5';
    for (auto it = text.rbegin(); carry && it != text.rend(); ++it) {
        if (*it == '.') {
            continue;
        }
        carry = *it == '9';
        *it = carry ? '0' : static_cast<char>(*it + 1);
    }
    return {text, carry};
}

// float_exp/1: the exponent, always signed.
std::string exponent_text(std::int64_t exponent) {
    return exponent >= 0 ? "e+" + std::to_string(exponent) : "e" + std::to_string(exponent);
}

// signbit/1: "-" for negative values and -0.0.
std::string sign_of(double value) { return std::signbit(value) ? "-" : ""; }
} // namespace

std::optional<std::string> float_e(const FloatField &field) {
    const auto [value, precision] = field;
    if (precision < 2) {
        return std::nullopt;
    }
    const auto data = float_data(value);
    auto [text, carry] = mantissa(data.digits, 1, static_cast<std::size_t>(precision - 1));
    if (carry) {
        return sign_of(value) + "1" + text.substr(1) + exponent_text(data.exponent);
    }
    return sign_of(value) + text + exponent_text(data.exponent - 1);
}

std::optional<std::string> float_f(const FloatField &field) {
    const auto [value, precision] = field;
    if (precision < 1) {
        return std::nullopt;
    }
    auto data = float_data(value);
    if (data.exponent <= 0) {
        data.digits.insert(0, static_cast<std::size_t>(1 - data.exponent), '0');
        data.exponent = 1;
    }
    auto [text, carry] =
        mantissa(data.digits, static_cast<std::size_t>(data.exponent), static_cast<std::size_t>(precision));
    return sign_of(value) + (carry ? "1" : "") + text;
}

FloatChoice float_g(const FloatField &field) {
    const auto [value, precision] = field;
    static constexpr std::array<double, 6> BOUNDS{1.0e-1, 1.0e0, 1.0e1, 1.0e2, 1.0e3, 1.0e4};
    const auto magnitude = std::abs(value);
    std::optional<std::int64_t> exponent;
    for (std::size_t i = 0; i < BOUNDS.size() && !exponent; ++i) {
        if (magnitude < BOUNDS[i]) {
            exponent = static_cast<std::int64_t>(i) - 2;
        }
    }
    if ((precision <= 1 && exponent == -1) || (exponent && precision - 1 > *exponent && *exponent >= -1)) {
        return {.fixed = true, .precision = precision - 1 - *exponent};
    }
    return {.fixed = false, .precision = precision <= 1 ? 2 : precision};
}
} // namespace clause::runtime::builtins::io_lib
