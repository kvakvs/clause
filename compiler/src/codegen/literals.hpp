#pragma once
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <string>

namespace clause::codegen {
// Module literals (literal heap): every compound literal of a module is one term of the module's encoded literals
// (abi/literals.hpp), which each runtime decodes into its literal heap at registration; generated code reads a
// literal's word from the module's literal cache. Equal literals share an index.

// The index of the literal `encoded` (one encoded term) among the module's literals.
std::size_t literal_index(llvm::Module &output, const std::string &encoded);
// The module's literal cache, one word per literal; a placeholder until finish_literals defines it.
llvm::GlobalVariable *literal_cache(llvm::Module &output, llvm::IntegerType *word);
// Give the module descriptor its encoded literals and its literal cache; a module without literals keeps none.
void finish_literals(llvm::Module &output, llvm::IntegerType *word, const std::string &prefix);
} // namespace clause::codegen
