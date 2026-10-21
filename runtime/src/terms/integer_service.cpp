#include "integers.hpp"
#include <clause/runtime/process_context.hpp>

namespace clause::runtime::detail {
using Op = abi::v1::ImmediateOperation;

TermResult<Term> integer_service(ProcessContext &context, Op operation, const Term &left, const Term &right) {
    if (operation == Op::multiply) {
        if (auto scaled = IntegerAccess::scale(context.heap(), left, right)) {
            return *scaled;
        }
    }
    const auto lhs = integer_read(left);
    if (!lhs) {
        return std::unexpected(lhs.error());
    }
    TermResult<Integer> result;
    if (operation >= Op::positive) {
        result = integer_unary(operation, *lhs);
    } else {
        const auto rhs = integer_read(right);
        if (!rhs) {
            return std::unexpected(rhs.error());
        }
        result = integer_binary(operation, *lhs, *rhs);
    }
    return result.and_then([&](const auto &value) { return IntegerAccess::make(context.heap(), value); });
}
} // namespace clause::runtime::detail
