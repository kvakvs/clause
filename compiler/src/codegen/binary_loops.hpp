#pragma once
#include "lowering_state.hpp"

namespace clause::codegen {
// Binary loops (OPT1.4C): a function that matches one parameter as a binary in every clause and passes the binary's
// rest back to itself in tail calls gets a variant that keeps one sub-binary cell for the whole loop.

// The parameter that qualifies: no clause keeps the whole binary, and every rest it binds is read only as that
// parameter of a self tail call; none when no parameter does.
std::optional<std::size_t> binary_loop_parameter(const ExpressionLowering &state, std::size_t clauses,
                                                 const std::set<const ast::Expression *> &tails);
// The head plan of every clause of the function being lowered; empty when one has none.
std::vector<semantic::MatchPlan> head_plans(const ExpressionLowering &state, std::size_t clauses);
// Declare a private loop variant (binary or float loops) of the function being lowered, named `suffix` after it, with
// its arity and a source scope like its own.
llvm::Function *declare_loop_variant(const ExpressionLowering &state, std::string_view suffix);
} // namespace clause::codegen
