#include "alias_tags.hpp"
#include <array>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/MDBuilder.h>
#include <vector>

namespace clause::codegen {
llvm::Instruction *tag_memory(llvm::Instruction *access, const MemoryKind kind) {
    // Scalar types never alias their siblings, only their ancestors; MDBuilder uniques the nodes per context.
    static constexpr std::array NAMES{"clause.state", "clause.frame", "clause.sub_binary", "clause.direct_binary",
                                      "clause.term"};
    llvm::MDBuilder builder(access->getContext());
    auto *root = builder.createTBAARoot("clause.memory");
    const auto binary = kind == MemoryKind::sub_binary || kind == MemoryKind::direct_binary;
    auto *parent =
        binary ? builder.createTBAAScalarTypeNode(NAMES.at(static_cast<std::size_t>(MemoryKind::term)), root) : root;
    auto *type = builder.createTBAAScalarTypeNode(NAMES.at(static_cast<std::size_t>(kind)), parent);
    access->setMetadata(llvm::LLVMContext::MD_tbaa, builder.createTBAAStructTagNode(type, type, 0));
    return access;
}

llvm::LoadInst *invariant_load(llvm::LoadInst *load) {
    load->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(load->getContext(), {}));
    return load;
}

namespace {
// Tag the untagged loads `pointer` addresses as term memory; queue the offsets taken from it (OPT1.10).
void tag_uses(llvm::Instruction &pointer, std::vector<llvm::Instruction *> &pointers) {
    for (auto *user : pointer.users()) {
        if (llvm::isa<llvm::GetElementPtrInst>(user)) {
            pointers.push_back(llvm::cast<llvm::Instruction>(user));
        } else if (auto *load = llvm::dyn_cast<llvm::LoadInst>(user);
                   load && !load->hasMetadata(llvm::LLVMContext::MD_tbaa)) {
            tag_memory(load, MemoryKind::term);
        }
    }
}
} // namespace

void tag_term_loads(llvm::Function &function) {
    // Lowering forms a pointer from a word only for a term's cell (`proven.cell`) or a binary's data: follow each such
    // pointer through its offsets to the loads it addresses (users, not operands: a load's only operand is its
    // address).
    std::vector<llvm::Instruction *> pointers;
    for (auto &instruction : llvm::instructions(function)) {
        if (llvm::isa<llvm::IntToPtrInst>(instruction)) {
            pointers.push_back(&instruction);
        }
    }
    while (!pointers.empty()) {
        auto *pointer = pointers.back();
        pointers.pop_back();
        tag_uses(*pointer, pointers);
    }
}
} // namespace clause::codegen
