#include "../terms/floats.hpp"
#include "typed.hpp"
#include <array>
#include <cmath>
#include <numbers>
#include <terms.hpp>

// The math builtins (plan step 41V, docs/builtins.md#math-module): OTP's math NIFs over the C library. Arguments are
// numbers (anything else is badarg; an integer too large for a float is badarith); a result that is not a finite
// float (a domain error or an overflow) is badarith, as in OTP.
namespace clause::runtime::builtins {
namespace {
// A number argument as a float.
double operand(const Term &value) {
    if (!value.is_number()) {
        bad_argument();
    }
    const auto converted = detail::number_float(value);
    if (!converted) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    return *converted;
}

// A finite result as a float term; NaN and infinities are badarith.
TermResult<Term> result(ProcessContext &context, double value) {
    if (!std::isfinite(value)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    return TermFactory(context).floating(value);
}

// math:F(X) for a C library function F.
template <double (*Function)(double)> TermResult<Term> unary(ProcessContext &context, const Term &x) {
    return result(context, Function(operand(x)));
}

// math:F(X, Y) for a C library function F.
template <double (*Function)(double, double)>
TermResult<Term> binary(ProcessContext &context, const Term &x, const Term &y) {
    return result(context, Function(operand(x), operand(y)));
}

// The C library functions, as plain function pointers (the std overloads cannot be template arguments).
double sin(double x) { return std::sin(x); }
double cos(double x) { return std::cos(x); }
double tan(double x) { return std::tan(x); }
double asin(double x) { return std::asin(x); }
double acos(double x) { return std::acos(x); }
double atan(double x) { return std::atan(x); }
double sinh(double x) { return std::sinh(x); }
double cosh(double x) { return std::cosh(x); }
double tanh(double x) { return std::tanh(x); }
double asinh(double x) { return std::asinh(x); }
double acosh(double x) { return std::acosh(x); }
double atanh(double x) { return std::atanh(x); }
double exp(double x) { return std::exp(x); }
double log(double x) { return std::log(x); }
double log2(double x) { return std::log2(x); }
double log10(double x) { return std::log10(x); }
double sqrt(double x) { return std::sqrt(x); }
double erf(double x) { return std::erf(x); }
double erfc(double x) { return std::erfc(x); }
double floor(double x) { return std::floor(x); }
double ceil(double x) { return std::ceil(x); }
double pow(double x, double y) { return std::pow(x, y); }
double atan2(double y, double x) { return std::atan2(y, x); }
double fmod(double x, double y) { return std::fmod(x, y); }

// math:pi() and math:tau().
TermResult<Term> pi(ProcessContext &context) { return TermFactory(context).floating(std::numbers::pi); }
TermResult<Term> tau(ProcessContext &context) { return TermFactory(context).floating(2 * std::numbers::pi); }

constexpr std::array MATH_BUILTINS{
    typed_entry<unary<sin>>("math", "sin"),        typed_entry<unary<cos>>("math", "cos"),
    typed_entry<unary<tan>>("math", "tan"),        typed_entry<unary<asin>>("math", "asin"),
    typed_entry<unary<acos>>("math", "acos"),      typed_entry<unary<atan>>("math", "atan"),
    typed_entry<unary<sinh>>("math", "sinh"),      typed_entry<unary<cosh>>("math", "cosh"),
    typed_entry<unary<tanh>>("math", "tanh"),      typed_entry<unary<asinh>>("math", "asinh"),
    typed_entry<unary<acosh>>("math", "acosh"),    typed_entry<unary<atanh>>("math", "atanh"),
    typed_entry<unary<exp>>("math", "exp"),        typed_entry<unary<log>>("math", "log"),
    typed_entry<unary<log2>>("math", "log2"),      typed_entry<unary<log10>>("math", "log10"),
    typed_entry<unary<sqrt>>("math", "sqrt"),      typed_entry<unary<erf>>("math", "erf"),
    typed_entry<unary<erfc>>("math", "erfc"),      typed_entry<unary<floor>>("math", "floor"),
    typed_entry<unary<ceil>>("math", "ceil"),      typed_entry<binary<pow>>("math", "pow"),
    typed_entry<binary<atan2>>("math", "atan2"),   typed_entry<binary<fmod>>("math", "fmod"),
    typed_entry<pi>("math", "pi"),                 typed_entry<tau>("math", "tau"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> math_builtins() noexcept { return builtins::MATH_BUILTINS; }
} // namespace clause::runtime
