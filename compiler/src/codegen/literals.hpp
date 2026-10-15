#pragma once
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <span>

namespace clause::codegen {
// The module literal area (OPT0.4): compound literals laid out as heap cells in one constant global per module,
// built once instead of on every evaluation. Equal cells are shared.

// The term word of a cell of `words` (a header and its payload, or the two words of a cons cell) in the literal
// area of `output`, tagged boxed or list. Every word must be a constant: an immediate or another literal's word.
llvm::Constant *literal_cell(llvm::Module &output, llvm::IntegerType *word, std::span<llvm::Constant *const> words,
                             unsigned tag);
// The address of the cell a literal word names (its ptrtoint operand less the tag), or null for any other constant:
// generated code reads literal cells without converting a constant word back to a pointer.
llvm::Constant *literal_address(llvm::Constant *word, unsigned tag);
// Give the literal area its contents and its descriptor fields; a module without literals keeps none.
void finish_literals(llvm::Module &output, llvm::IntegerType *word, const std::string &prefix);
} // namespace clause::codegen
