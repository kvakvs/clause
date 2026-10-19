#pragma once
#include "lowering_state.hpp"

namespace clause::codegen {
// Float loops (OPT1.2): a function whose float parameters are passed new float values by its own tail calls gets a
// variant taking those parameters as raw doubles, so the loop makes no float cell per step.

// The tail positions of a function's clauses: the calls that become tail transfers and every tail expression.
struct TailPositions {
    const std::set<const ast::Expression *> &calls;
    const std::set<const ast::Expression *> &leaves;
};

// The float parameters and their head bindings, when some qualify: a parameter passed in a register that every head
// binds to a variable, that some self tail call passes a float-only expression, and whose value is read only as an
// arithmetic or comparison operand, as the same parameter of a self tail call, or within the result or a tail call of
// another function, so a step needs a cell of it only where the loop ends.
std::optional<FloatLoop> float_loop_parameters(const ExpressionLowering &state, std::size_t clauses,
                                               const TailPositions &tails);
} // namespace clause::codegen
