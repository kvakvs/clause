#pragma once
#include "lowering_state.hpp"

namespace clause::codegen {
// Binary loops (OPT1.4C): a function that matches one parameter as a binary in every clause and passes the binary's
// rest back to itself in tail calls gets a variant that keeps one sub-binary cell for the whole loop.

// How the first parameter that qualifies is read: no clause head compares it with a binding, and some rest it binds
// is passed back as that parameter of a self tail call; none when no parameter does.
std::optional<LoopReads> binary_loop_reads(const ExpressionLowering &state, std::size_t clauses,
                                           const std::set<const ast::Expression *> &tails);
// Drop the variant's invariant loads (SSA binary cursors, OPT1.4F) when it calls another Erlang function: frame
// lowering may then merge it into one function with code that writes its cell.
void keep_invariants_alone(llvm::Function &variant);
// The head plan of every clause of the function being lowered; empty when one has none.
std::vector<semantic::MatchPlan> head_plans(const ExpressionLowering &state, std::size_t clauses);
// Declare a private loop variant (binary or float loops) of the function being lowered, named `suffix` after it, with
// its arity and a source scope like its own.
llvm::Function *declare_loop_variant(const ExpressionLowering &state, std::string_view suffix);
} // namespace clause::codegen
