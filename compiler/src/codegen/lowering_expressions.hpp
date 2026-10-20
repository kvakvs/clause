#pragma once
#include "../semantic/types/inference.hpp"
#include "proofs.hpp"
#include <llvm/IR/IRBuilder.h>
#include <map>

namespace clause::codegen {
// Inline funs (OPT1.8): each variable of a function bound once to an anonymous fun (`F = fun ... end`), with that fun.
using LambdaBindings = std::map<semantic::BindingId, const semantic::FunEntry *>;

// The variables of `function` whose fun calls may lower the fun's clauses in place: bound once to an anonymous fun
// that is read once or is small (inline funs, OPT1.8).
LambdaBindings lambda_bindings(const semantic::Module &module, const semantic::Function &function);
// Emit source-ordered candidates and a single exhaustion exit without trusting specifications; non-null `proofs`
// let inferred facts and shape tests replace checks (step 59), non-null `lambdas` inline known funs (OPT1.7, OPT1.8).
void lower_function(llvm::IRBuilder<> &builder, llvm::Function &entry, const semantic::Module &module,
                    const semantic::Function &function, llvm::IntegerType *word,
                    const semantic::types::Inference &inferred, const Proofs *proofs = nullptr,
                    const LambdaBindings *lambdas = nullptr);
// Emit an anonymous fun's code: its clauses over its arguments, with its captured values after them.
void lower_lambda(llvm::IRBuilder<> &builder, llvm::Function &entry, const semantic::Module &module,
                  const semantic::FunEntry &lambda, llvm::IntegerType *word, const semantic::types::Inference &inferred,
                  const LambdaBindings *lambdas = nullptr);
} // namespace clause::codegen
