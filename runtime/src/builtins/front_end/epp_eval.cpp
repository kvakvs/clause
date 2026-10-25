#include "eval.hpp"
#include <array>

// epp's -if and -elif conditions in C++ (plan 21 step LIB34C, docs/builtins.md#epp): the guard expression
// erl_eval:exprs/3 would evaluate with no bindings and a local function handler raising badarg, so epp does not link
// erl_eval. Any error makes the condition false, as epp catches it.
namespace clause::runtime::builtins::front_end {
namespace {
struct ExprRole;
using ExprArg = Named<ExprRole>;

// clause_epp:eval_if(Expr): the value of a -if condition, false when it raises.
Term eval_if(ProcessContext &context, const ExprArg &expr) {
    try {
        return eval::GuardEval(context, nullptr).value(expr.term);
    } catch (...) {
        if (!eval::evaluation_failure(std::current_exception())) {
            throw;
        }
    }
    return string::Terms(context).atom("false"_atom);
}

constexpr std::array EPP_BUILTINS{
    typed_entry<eval_if>("clause_epp", "eval_if"),
};
} // namespace
} // namespace clause::runtime::builtins::front_end

namespace clause::runtime {
std::span<const BuiltinEntry> epp_builtins() noexcept { return builtins::front_end::EPP_BUILTINS; }
} // namespace clause::runtime
