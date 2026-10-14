#pragma once
#include "../typed.hpp"
#include <cstddef>
#include <optional>
#include <vector>

// Helpers of the lists module's builtins (plan step 41T, docs/builtins.md#lists-module): OTP's lists functions are
// Erlang, so an argument they do not accept raises function_clause, not badarg.
namespace clause::runtime::builtins::lists {
// Throw the function_clause a lists function without a matching clause raises.
[[noreturn]] inline void no_clause() { throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause}; }

// A count or position argument, which the builtin checks itself (a wrong one is function_clause, not badarg).
struct Count {
    // The argument as passed.
    Term term;

    // Its value: none for a non-integer or a negative integer; a positive big integer counts as SIZE_MAX.
    std::optional<std::size_t> natural() const;
};

// The elements of a proper list; function_clause for anything else.
std::vector<Term> proper(const Term &list);

// Whether `left` compares equal to (==) or before (<) `right` in term order.
bool equal(const Term &left, const Term &right);
bool before(const Term &left, const Term &right);
} // namespace clause::runtime::builtins::lists

namespace clause::runtime::builtins {
// Any term as a lists count; the builtin checks it.
template <> struct Argument<lists::Count> {
    static TermResult<std::optional<lists::Count>> convert(const Term &term) { return lists::Count{term}; }
};
} // namespace clause::runtime::builtins
