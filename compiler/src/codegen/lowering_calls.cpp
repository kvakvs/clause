#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <clause/abi/equality.hpp>
#include <clause/abi/frames.hpp>
#include <clause/abi/term.hpp>
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

// Enter `target` with the call's arguments as this function's result: lower_frames makes it a tail transfer.
void tail_enter(ExpressionLowering &state, llvm::Function *target, const ast::CallExpression &call) {
    auto *result = state.builder.CreateCall(target, {state.entry.getArg(0), arguments(state, call)}, "call.result");
    result->setCallingConv(llvm::CallingConv::C);
    state.builder.CreateRet(result);
}

// Store a binary loop rest's offset and length into the parameter's sub-binary cell (OPT1.4C).
void update_context(ExpressionLowering &state, llvm::Value *cell, const std::pair<llvm::Value *, llvm::Value *> &rest) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *address = builder.CreateIntToPtr(
        builder.CreateSub(cell, llvm::ConstantInt::get(state.word, abi::v1::boxed_tag)), builder.getPtrTy());
    builder.CreateAlignedStore(rest.first, builder.CreateConstGEP1_64(state.word, address, abi::v1::sub_offset_word),
                               align);
    builder.CreateAlignedStore(rest.second, builder.CreateConstGEP1_64(state.word, address, abi::v1::sub_bits_word),
                               align);
}

// A self tail call passing a binary loop's rest as the looping parameter (OPT1.4C): the variant updates its cell and
// loops; the function enters the variant with a cell it just made, else itself. False for any other call.
bool binary_loop_call(ExpressionLowering &state, const ast::Expression &expression, const ast::CallExpression &call,
                      llvm::Function *target) {
    auto *loop = state.binary_loop;
    if (!loop || !state.tail_calls || !state.tail_calls->contains(&expression) ||
        target != state.entry.getParent()->getFunction(state.function.symbol)) {
        return false;
    }
    auto *argument = state.values.at(&state.module.syntax->expression(call.arguments.at(loop->parameter)));
    if (loop->in_variant) {
        const auto found = loop->updates.find(argument);
        if (found == loop->updates.end()) {
            return false;
        }
        update_context(state, argument, found->second);
        tail_enter(state, loop->variant, call);
        return true;
    }
    const auto found = loop->fresh.find(argument);
    if (found == loop->fresh.end()) {
        return false;
    }
    auto *variant = llvm::BasicBlock::Create(state.entry.getContext(), "binary.loop", &state.entry);
    auto *generic = llvm::BasicBlock::Create(state.entry.getContext(), "binary.shared", &state.entry);
    state.builder.CreateCondBr(found->second, variant, generic);
    state.builder.SetInsertPoint(variant);
    tail_enter(state, loop->variant, call);
    state.builder.SetInsertPoint(generic);
    tail_enter(state, target, call);
    return true;
}

// The raw doubles of a call's float loop arguments, or none when one has no raw double (float loops, OPT1.2).
std::optional<std::vector<llvm::Value *>> raw_arguments(ExpressionLowering &state, const ast::CallExpression &call) {
    std::vector<llvm::Value *> raws;
    for (const auto parameter : state.float_loop->parameters) {
        const auto raw =
            state.raw_floats.find(state.values.at(&state.module.syntax->expression(call.arguments.at(parameter))));
        if (raw == state.raw_floats.end()) {
            return std::nullopt;
        }
        raws.push_back(raw->second);
    }
    return raws;
}

// A self tail call passing a raw double as every float parameter of a float loop (float loops, OPT1.2): the function
// and its variant both enter the variant with the bits of those doubles as the argument words, so their cells go
// unread and are dropped. False for any other call.
bool float_loop_call(ExpressionLowering &state, const ast::Expression &expression, const ast::CallExpression &call,
                     llvm::Function *target) {
    auto *loop = state.float_loop;
    if (!loop || !state.tail_calls || !state.tail_calls->contains(&expression) ||
        target != state.entry.getParent()->getFunction(state.function.symbol)) {
        return false;
    }
    const auto raws = raw_arguments(state, call);
    if (!raws) {
        return false;
    }
    const auto saved = state.values;
    for (std::size_t i = 0; i < raws->size(); ++i) {
        const auto parameter = loop->parameters[i];
        state.values.at(&state.module.syntax->expression(call.arguments.at(parameter))) =
            state.builder.CreateBitCast(raws->at(i), state.word, "float.bits");
    }
    tail_enter(state, loop->variant, call);
    state.values = saved;
    return true;
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
    if (binary_loop_call(state, expression, call, target) || float_loop_call(state, expression, call, target)) {
        state.builder.SetInsertPoint(llvm::BasicBlock::Create(state.entry.getContext(), "tail.dead", &state.entry));
        return llvm::ConstantInt::get(state.word, abi::v1::empty_list);
    }
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
