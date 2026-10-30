#pragma once
#include "../semantic/types/inference.hpp"
#include "proofs.hpp"
#include <llvm/IR/IRBuilder.h>
#include <map>

namespace clause::codegen {
struct LambdaBindings {
    // Inline funs (OPT1.8): each variable of a function bound once to an anonymous fun (`F = fun ... end`), with that
    // fun.
    std::map<semantic::BindingId, const semantic::FunEntry *> funs;
    // Sunk bound funs: the matches `F = fun ... end` whose value is unused and whose variable no fun captures and
    // only calls read; their closure is built only where a read needs it.
    std::map<const ast::Expression *, semantic::BindingId> sunk;
};

// The variables of `function` whose fun calls may lower the fun's clauses in place: bound once to an anonymous fun
// that is read once or is small (inline funs, OPT1.8).
LambdaBindings lambda_bindings(const semantic::Module &module, const semantic::Function &function);
struct FloatLoop;
// The float loops of a module's functions, planned before any is lowered (float loops across calls, OPT1.11).
using FloatLoops = std::map<const semantic::Function *, FloatLoop>;

struct LoweringContext {
    // What lowering of one module shares: non-null `proofs` let inferred facts and shape tests replace checks (step
    // 59), non-null `lambdas` inline known funs (OPT1.7, OPT1.8), non-null `float_loops` give each function its float
    // loop and let calls enter the float loops of other functions (OPT1.11).
    const Proofs *proofs = nullptr;
    const LambdaBindings *lambdas = nullptr;
    const FloatLoops *float_loops = nullptr;
};

// Plan the float loops of the module's live functions and declare their variants (float loops across calls, OPT1.11).
FloatLoops plan_float_loops(llvm::Module &output, const semantic::Module &module, llvm::IntegerType *word,
                            const semantic::types::Inference &inferred, const Proofs *proofs);
// Emit source-ordered candidates and a single exhaustion exit without trusting specifications.
void lower_function(llvm::IRBuilder<> &builder, llvm::Function &entry, const semantic::Module &module,
                    const semantic::Function &function, llvm::IntegerType *word,
                    const semantic::types::Inference &inferred, LoweringContext context = {});
// Emit the code of an anonymous fun: its clauses over its arguments, with its captured values after them.
void lower_lambda(llvm::IRBuilder<> &builder, llvm::Function &entry, const semantic::Module &module,
                  const semantic::FunEntry &lambda, llvm::IntegerType *word, const semantic::types::Inference &inferred,
                  LoweringContext context = {});
} // namespace clause::codegen
