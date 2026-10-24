#include "../semantic/symbols.hpp"
#include "lowering_state.hpp"
#include "module_atoms.hpp"
#include <algorithm>
#include <llvm/IR/Module.h>

namespace clause::codegen {
namespace {
// Name this function in stack traces (abi::v1::FrameDescriptor): module descriptor, module and function name
// atom slots, and arity.
llvm::Constant *frame_descriptor(ExpressionLowering &state) {
    auto &output = *state.entry.getParent();
    const auto module = utf8(state.module.name);
    auto *descriptor = output.getNamedGlobal(semantic::encode_symbol({module, "", 0}) + ".descriptor");
    auto *type = llvm::StructType::get(state.builder.getPtrTy(), state.word, state.word, state.word);
    const auto &name = state.lambda ? state.lambda->function : state.function.key.name;
    auto *data = llvm::ConstantStruct::get(type, descriptor, atom_slot(output, module), atom_slot(output, utf8(name)),
                                           llvm::ConstantInt::get(state.word, frame_arity(state)));
    const auto &symbol = state.lambda ? state.lambda->symbol : state.function.symbol;
    return new llvm::GlobalVariable(output, type, true, llvm::GlobalValue::PrivateLinkage, data, "frame." + symbol);
}
} // namespace

std::size_t frame_arity(const ExpressionLowering &state) {
    return state.lambda ? state.lambda->arity + state.lambda->captures.size() : state.function.key.arity;
}

FunctionRoots begin_roots(ExpressionLowering &state) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto marker = output.getOrInsertFunction(
        FRAME_MARKER,
        llvm::FunctionType::get(builder.getPtrTy(), {builder.getPtrTy(), state.word, builder.getPtrTy()}, false));
    auto *buffer = builder.CreateCall(
        marker, {state.entry.getArg(0), llvm::ConstantInt::get(state.word, 1), frame_descriptor(state)}, "roots");
    return {buffer, state.word, frame_arity(state)};
}

llvm::Value *root_slot(ExpressionLowering &state) {
    auto &roots = *state.roots;
    auto *slot = state.builder.CreateGEP(roots.word, roots.buffer, llvm::ConstantInt::get(roots.word, roots.next++),
                                         "root.slot");
    roots.capacity = std::max(roots.capacity, roots.next);
    return slot;
}

void root_value(ExpressionLowering &state, llvm::Value *value) {
    auto *slot = root_slot(state);
    state.builder.CreateAlignedStore(value, slot, llvm::Align(state.word->getBitWidth() / 8));
}

void mark_term(ExpressionLowering &state, llvm::Value *value) {
    // A module literal never moves, so it needs no root (literal heap).
    if (state.literal_values.contains(value)) {
        return;
    }
    if (auto *instruction = llvm::dyn_cast<llvm::Instruction>(value)) {
        instruction->setMetadata(TERM_METADATA, llvm::MDNode::get(state.entry.getContext(), {}));
    }
}

void root_arguments(ExpressionLowering &state) {
    // The argument slots are the first frame slots. They are not stored on entry (OPT1.2): frame lowering keeps an
    // argument in its own slot only where it crosses a call or safepoint.
    auto &roots = *state.roots;
    roots.next = roots.arguments;
    roots.capacity = std::max(roots.capacity, roots.next);
}

void reset_candidate_roots(ExpressionLowering &state) {
    auto &roots = *state.roots;
    if (roots.next > roots.arguments) {
        auto *start =
            state.builder.CreateGEP(roots.word, roots.buffer, llvm::ConstantInt::get(roots.word, roots.arguments));
        tag_memory(state.builder.CreateMemSet(start, state.builder.getInt8(0),
                                              (roots.next - roots.arguments) * (roots.word->getBitWidth() / 8),
                                              llvm::Align(roots.word->getBitWidth() / 8)),
                   MemoryKind::frame);
    }
    roots.next = roots.arguments;
}

void finish_roots(ExpressionLowering &state) {
    auto &roots = *state.roots;
    roots.buffer->setArgOperand(1, llvm::ConstantInt::get(roots.word, std::max(roots.capacity, std::size_t{1})));
}
} // namespace clause::codegen
