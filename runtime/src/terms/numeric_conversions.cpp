#include "floats.hpp"
#include <cmath>

namespace clause::runtime::detail {
Integer float_integer(double value) {
    int exponent = 0;
    const double fraction = std::frexp(std::abs(value), &exponent);
    Integer magnitude{static_cast<std::uint64_t>(std::ldexp(fraction, 53))};
    if (exponent >= 53) {
        magnitude <<= static_cast<unsigned>(exponent - 53);
    } else {
        magnitude >>= static_cast<unsigned>(53 - exponent);
    }
    return std::signbit(value) ? -magnitude : magnitude;
}

TermResult<double> integer_float(const Integer &value) {
    const auto bits = integer_bits(value);
    if (bits > 1024) {
        return std::unexpected(TermError::out_of_range);
    }
    const Integer magnitude = value < 0 ? -value : value;
    const auto shift = bits > 53 ? bits - 53 : 0;
    Integer top = magnitude >> shift;
    if (shift != 0) {
        const Integer remainder = magnitude - (top << shift);
        const Integer halfway = Integer{1} << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (top & 1) != 0)) {
            ++top;
        }
    }
    const double result = std::ldexp(top.convert_to<double>(), static_cast<int>(shift));
    if (!std::isfinite(result)) {
        return std::unexpected(TermError::out_of_range);
    }
    return value < 0 ? -result : result;
}

TermResult<double> number_float(const Term &value) {
    if (value.is_float()) {
        return value.float_value_unchecked();
    }
    return integer_read(value).and_then(integer_float);
}
} // namespace clause::runtime::detail
