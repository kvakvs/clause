#include "alias_tags.hpp"
#include <array>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/MDBuilder.h>

namespace clause::codegen {
llvm::Instruction *tag_memory(llvm::Instruction *access, const MemoryKind kind) {
    // Scalar types under one root never alias each other; MDBuilder uniques the nodes per context.
    static constexpr std::array NAMES{"clause.state", "clause.frame", "clause.sub_binary", "clause.direct_binary"};
    llvm::MDBuilder builder(access->getContext());
    auto *type = builder.createTBAAScalarTypeNode(NAMES.at(static_cast<std::size_t>(kind)),
                                                  builder.createTBAARoot("clause.memory"));
    access->setMetadata(llvm::LLVMContext::MD_tbaa, builder.createTBAAStructTagNode(type, type, 0));
    return access;
}
} // namespace clause::codegen
