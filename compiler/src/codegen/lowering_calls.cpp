#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <clause/abi/equality.hpp>
#include <clause/abi/frames.hpp>
#include <llvm/IR/Module.h>
#include <stdexcept>

namespace clause::codegen {
namespace {
// Reuse one terminal exit (or the enclosing catch handler); callers must inspect the channel before
// interpreting its invalid word.
llvm::BasicBlock *failure_exit(ExpressionLowering &state) {
    if (state.handler) {
        return state.handler;
    }
    if (!state.failure) {
        state.failure = llvm::BasicBlock::Create(state.entry.getContext(), "call.failure", &state.entry);
        llvm::IRBuilder<> exit_builder(state.failure);
        exit_builder.CreateRet(llvm::ConstantInt::get(state.word, 0));
    }
    return state.failure;
}

// Check the context channel before a result can feed another argument or body operation.
} // namespace

void propagate_failure(ExpressionLowering &state) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    // The context's FrameState mirrors CLAUSE_call_failed_v2 in its `failed` word (OPT0.1).
    auto *address = builder.CreateConstInBoundsGEP1_64(state.word, state.entry.getArg(0),
                                                       static_cast<std::uint64_t>(abi::v1::StateField::failed));
    auto *failed =
        builder.CreateAlignedLoad(state.word, address, llvm::Align(state.word->getBitWidth() / 8), "call.failed");
    auto *failure = failure_exit(state);
    auto *success = llvm::BasicBlock::Create(output.getContext(), "call.success", &state.entry);
    auto *check = builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_NE, failed,
                                                       llvm::ConstantInt::get(state.word, 0)));
    builder.CreateCondBr(check, failure, success);
    builder.SetInsertPoint(success);
}

namespace {
// Store `values` in a word-aligned borrowed array of their count (at least one).
llvm::Value *word_array(ExpressionLowering &state, std::span<llvm::Value *const> values) {
    auto &builder = state.builder;
    auto *count = llvm::ConstantInt::get(state.word, values.size());
    auto *array = builder.CreateAlloca(state.word, count, "call.arguments");
    const llvm::Align alignment(state.word->getBitWidth() / 8);
    array->setAlignment(alignment);
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto *slot = builder.CreateGEP(state.word, array, llvm::ConstantInt::get(state.word, i));
        builder.CreateAlignedStore(values[i], slot, alignment);
    }
    return array;
}

// Allocate a word-aligned borrowed argument array; zero arity passes an unused null pointer.
llvm::Value *arguments(ExpressionLowering &state, const ast::CallExpression &call) {
    if (call.arguments.empty()) {
        return llvm::ConstantPointerNull::get(state.builder.getPtrTy());
    }
    std::vector<llvm::Value *> values;
    values.reserve(call.arguments.size());
    for (const auto &id : call.arguments) {
        values.push_back(state.values.at(&state.module.syntax->expression(id)));
    }
    return word_array(state, values);
}

// Import only batch-resolved exported entries, using exactly the current generic ABI signature.
llvm::Function *callee_declaration(ExpressionLowering &state, const semantic::FunctionRef callee) {
    auto &output = *state.entry.getParent();
    if (callee.module != &state.module && !callee.function->exported) {
        throw std::invalid_argument("lowering: remote callee is not exported");
    }
    if (auto *existing = output.getFunction(callee.function->symbol)) {
        return existing;
    }
    if (callee.module == &state.module) {
        throw std::invalid_argument("lowering: missing local declaration");
    }
    auto *entry = llvm::Function::Create(state.entry.getFunctionType(), llvm::GlobalValue::ExternalLinkage,
                                         callee.function->symbol, output);
    entry->setCallingConv(llvm::CallingConv::C);
    entry->addFnAttr(ARITY_ATTRIBUTE, std::to_string(callee.function->key.arity));
    return entry;
}
} // namespace

llvm::Value *lower_call(ExpressionLowering &state, const ast::Expression &expression, const ast::CallExpression &call) {
    const auto callee = state.inferred.callees.at(&expression);
    // Consume the resolved identity and summary, never repeat Erlang name resolution here.
    const auto &summary = state.inferred.functions.at(callee.function);
    if (summary.inputs.size() != call.arguments.size()) {
        throw std::invalid_argument("lowering: inconsistent resolved call arity");
    }
    auto *target = callee_declaration(state, callee);
    auto *result = state.builder.CreateCall(target, {state.entry.getArg(0), arguments(state, call)}, "call.result");
    result->setCallingConv(llvm::CallingConv::C);
    if (state.tail_calls && state.tail_calls->contains(&expression)) {
        // The callee's result is this function's: lower_frames turns the return into a tail transfer.
        state.builder.CreateRet(result);
        state.builder.SetInsertPoint(llvm::BasicBlock::Create(state.entry.getContext(), "tail.dead", &state.entry));
        return llvm::ConstantInt::get(state.word, abi::v1::empty_list);
    }
    propagate_failure(state);
    return result;
}
} // namespace clause::codegen
