#pragma once
#include <llvm/IR/Function.h>

namespace clause::codegen {
// Batched loop reductions (OPT1.14): keep a body's reduction count in a register. Every load and store of the context's
// FrameState::reductions word becomes an SSA value, read once at the end of `entry`; the word is written before
// each call, whose services spend and test it, and read back after one that returns.
void keep_reductions_in_register(llvm::Function &body, llvm::BasicBlock &entry, llvm::IntegerType &word);
} // namespace clause::codegen
