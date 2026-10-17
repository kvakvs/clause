#include "floats.hpp"
#include <cmath>

namespace clause::runtime::detail {
namespace {
// Integer comparison against the exact truncation preserves neighbors beyond binary64 precision.
int mixed(const Integer &integer, double real) {
    const auto whole = float_integer(real);
    if (integer != whole) {
        return integer < whole ? -1 : 1;
    }
    const double fraction = real - std::trunc(real);
    return fraction > 0 ? -1 : static_cast<int>(fraction < 0);
}

// Exact float identity retains the sign of zero while ordinary numeric order treats both zeros alike.
int reals(double left, double right, bool exact) {
    if (exact && left == 0 && right == 0 && std::signbit(left) != std::signbit(right)) {
        return std::signbit(left) ? -1 : 1;
    }
    return left < right ? -1 : static_cast<int>(left > right);
}

// Keep arbitrary precision reads outside the float path and preserve exact integer comparison.
int integers(const Term &left, const Term &right) {
    const auto lhs = integer_read(left).value();
    const auto rhs = integer_read(right).value();
    return lhs < rhs ? -1 : static_cast<int>(lhs > rhs);
}
} // namespace

TermResult<int> numeric_order(const Term &left, const Term &right, bool exact) {
    if (left.is_integer() && right.is_integer()) {
        return integers(left, right);
    }
    if (left.is_float() && right.is_float()) {
        return reals(left.float_value_unchecked(), right.float_value_unchecked(), exact);
    }
    if (exact) {
        return left.is_integer() ? -1 : 1;
    }
    return left.is_integer() ? mixed(integer_read(left).value(), right.float_value().value())
                             : -mixed(integer_read(right).value(), left.float_value().value());
}
} // namespace clause::runtime::detail
