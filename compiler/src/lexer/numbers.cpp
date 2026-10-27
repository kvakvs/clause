#include "parsing/boost_parser.hpp"
#include "preprocessor/value.hpp"
#include <algorithm>
#include <charconv>
#include <clause/compiler/lexer.hpp>
#include <cmath>
#include <system_error>

namespace clause {
namespace {
namespace bp = boost::parser;

// Consume digits with optional single separators between them.
std::size_t digits(const std::u32string_view input, const bool letters) {
    constexpr auto digit = bp::char_(U'0', U'9');
    const auto extended = digit | bp::char_(U'a', U'z') | bp::char_(U'A', U'Z');
    auto current = input.begin();
    if (letters) {
        bp::prefix_parse(current, input.end(), bp::omit[extended >> *(-bp::lit(U'_') >> extended)]);
    } else {
        bp::prefix_parse(current, input.end(), bp::omit[digit >> *(-bp::lit(U'_') >> digit)]);
    }
    return static_cast<std::size_t>(current - input.begin());
}

// Convert a digit from Erlang's base-2 through base-36 alphabet.
unsigned digit_value(const char32_t value) {
    if (value >= U'a' && value <= U'z') {
        return static_cast<unsigned>(value - U'a') + 10;
    }
    if (value >= U'A' && value <= U'Z') {
        return static_cast<unsigned>(value - U'A') + 10;
    }
    return static_cast<unsigned>(value - U'0');
}

// Accumulate arbitrary precision using a small decimal representation.
void multiply_add(std::string &decimal, const unsigned base, unsigned carry) {
    for (auto digit = decimal.rbegin(); digit != decimal.rend(); ++digit) {
        const auto value = static_cast<unsigned>(*digit - '0') * base + carry;
        *digit = static_cast<char>('0' + value % 10);
        carry = value / 10;
    }
    while (carry != 0) {
        decimal.insert(decimal.begin(), static_cast<char>('0' + carry % 10));
        carry /= 10;
    }
}

// Copy decimal digits without separators and leading zeros; long literals need no arithmetic.
Integer decimal_digits(const std::u32string_view digits) {
    std::string decimal;
    decimal.reserve(digits.size());
    for (const auto value : digits) {
        if (value != U'_' && (value != U'0' || !decimal.empty())) {
            decimal.push_back(static_cast<char>(value));
        }
    }
    return {decimal.empty() ? std::string("0") : decimal};
}

// Parse a based integer without depending on the future Erlang runtime.
Integer integer(const std::u32string_view digits, const unsigned base) {
    if (base == 10) {
        return decimal_digits(digits);
    }
    std::string decimal = "0";
    for (const auto value : digits) {
        if (value == U'_') {
            continue;
        }
        const auto digit = digit_value(value);
        if (digit >= base) {
            throw std::invalid_argument("digit outside integer base");
        }
        multiply_add(decimal, base, digit);
    }
    return {decimal};
}

// Admit an integer token within OTP's integer size limit; its scanner reports a larger one as an illegal integer.
Integer sized_integer(Integer value) {
    if (!decimal_fits(value.decimal)) {
        throw std::out_of_range("illegal integer (more than 4194240 bits)");
    }
    return value;
}

// Whether an out-of-range decimal spelling is tiny rather than huge, from the order of its first significant digit.
bool underflows(const std::string_view text) {
    const auto mark = std::min(text.find_first_of("eE"), text.size());
    const auto mantissa = text.substr(0, mark);
    const auto point = static_cast<long long>(std::min(mantissa.find('.'), mantissa.size()));
    const auto first = static_cast<long long>(mantissa.find_first_not_of("0."));
    const auto order = point - first - (first < point ? 1 : 0);
    auto exponent = text.substr(std::min(mark + 1, text.size()));
    if (exponent.starts_with('+')) {
        exponent.remove_prefix(1);
    }
    long long power = 0;
    if (std::from_chars(exponent.data(), exponent.data() + exponent.size(), power).ec ==
        std::errc::result_out_of_range) {
        return exponent.starts_with('-');
    }
    return order + power < 0;
}

// Parse floating values independently of locale and C++ library; as in OTP, an underflow reads as 0.0 and an
// overflow is rejected.
double floating(const std::u32string_view spelling) {
    auto text = utf8(spelling);
    std::erase(text, '_');
    double value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc::result_out_of_range && underflows(text)) {
        return 0.0;
    }
    if (error != std::errc() || end != text.data() + text.size() || !std::isfinite(value)) {
        throw std::invalid_argument("invalid floating-point value");
    }
    return value;
}

// Follow OTP's based-float conversion: integer significand times a base power.
double based_value(std::u32string text, std::u32string exponent, const unsigned base) {
    if (base == 10) {
        return floating(text + exponent);
    }
    std::erase(text, U'_');
    while (text.starts_with(U"0") && text[1] != U'.') {
        text.erase(0, 1);
    }
    while (text.ends_with(U"0") && text[text.size() - 2] != U'.') {
        text.pop_back();
    }
    const auto fraction = text.size() - text.find(U'.') - 1;
    std::erase(text, U'.');
    const auto decimal = integer(text, base).decimal;
    const auto mantissa = floating(std::u32string(decimal.begin(), decimal.end()));
    std::erase(exponent, U'_');
    const auto power = exponent.empty() ? 0.0 : std::stod(utf8(std::u32string_view(exponent).substr(1)));
    const auto result = mantissa * std::pow(static_cast<double>(base), power - static_cast<double>(fraction));
    if (!std::isfinite(result)) {
        throw std::invalid_argument("based float exceeds finite range");
    }
    return result;
}
} // namespace

Token Lexer::number() {
    const auto begin = cursor_;
    cursor_ += digits(rest(), false);
    try {
        if (rest().starts_with(U"#")) {
            return based_number(begin);
        }
        if (rest().size() > 1 && rest()[0] == U'.' && rest()[1] >= U'0' && rest()[1] <= U'9') {
            return floating_number(begin);
        }
        number_end();
        return token(TokenKind::integer,
                     sized_integer(integer(std::u32string_view(source_->text).substr(begin, cursor_ - begin), 10)),
                     begin, cursor_);
    } catch (const std::invalid_argument &error) {
        fail(DiagnosticCode::invalid_number, error.what(), begin);
    } catch (const std::out_of_range &error) {
        fail(DiagnosticCode::invalid_number, error.what(), begin);
    }
}

Token Lexer::based_number(const std::size_t begin) {
    const auto base_text = integer(std::u32string_view(source_->text).substr(begin, cursor_ - begin), 10).decimal;
    if (base_text.size() > 2) {
        throw std::invalid_argument("invalid integer base");
    }
    const auto base = static_cast<unsigned>(std::stoul(base_text));
    if (base < 2 || base > 36) {
        throw std::invalid_argument("integer base must be 2 through 36");
    }
    const auto value_begin = ++cursor_;
    cursor_ += digits(rest(), true);
    if (cursor_ == value_begin) {
        throw std::invalid_argument("missing based integer digits");
    }
    if (rest().size() > 1 && rest().front() == U'.' && rest()[1] < 128 && word_length(rest().substr(1)) != 0) {
        return based_float(begin, BasedMantissa{.begin = value_begin, .base = base});
    }
    number_end();
    return token(
        TokenKind::integer,
        sized_integer(integer(std::u32string_view(source_->text).substr(value_begin, cursor_ - value_begin), base)),
        begin, cursor_);
}

// Reject adjacent name characters as OTP does, rather than splitting bad
// numbers.
void Lexer::number_end() const {
    if (!rest().empty() && rest().front() < 128 && word_length(rest()) != 0) {
        throw std::invalid_argument("name character after number");
    }
}

// Consume a decimal exponent after the appropriate float marker.
std::u32string Lexer::exponent(const bool based) {
    if (based) {
        if (!rest().starts_with(U"#e") && !rest().starts_with(U"#E")) {
            return {};
        }
        ++cursor_;
    } else if (!rest().starts_with(U'e') && !rest().starts_with(U"E")) {
        return {};
    }
    const auto begin = cursor_++;
    if (rest().starts_with(U'+') || rest().starts_with(U"-")) {
        ++cursor_;
    }
    const auto count = digits(rest(), false);
    cursor_ += count;
    if (count == 0) {
        throw std::invalid_argument("missing exponent digits");
    }
    return source_->text.substr(begin, cursor_ - begin);
}

Token Lexer::based_float(const std::size_t begin, const BasedMantissa syntax) {
    ++cursor_;
    const auto count = digits(rest(), true);
    cursor_ += count;
    if (count == 0) {
        throw std::invalid_argument("missing fraction digits");
    }
    const auto mantissa = source_->text.substr(syntax.begin, cursor_ - syntax.begin);
    const auto power = exponent(true);
    number_end();
    return token(TokenKind::floating, based_value(mantissa, power, syntax.base), begin, cursor_);
}

Token Lexer::floating_number(const std::size_t begin) {
    ++cursor_;
    cursor_ += digits(rest(), false);
    static_cast<void>(exponent(false));
    number_end();
    return token(TokenKind::floating, floating(std::u32string_view(source_->text).substr(begin, cursor_ - begin)),
                 begin, cursor_);
}
} // namespace clause
