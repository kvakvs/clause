#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <clause/abi/equality.hpp>
#include <llvm/IR/Module.h>

namespace clause::codegen {
// Semantic rejection targets the entire enclosing guard; ordinary bodies use one shared badarg exit.
llvm::BasicBlock *bad_argument_exit(ExpressionLowering &state, llvm::Value *payload) {
    if (state.rejection) {
        return state.rejection;
    }
    if (payload) {
        auto *saved = state.builder.GetInsertBlock();
        auto *failure = llvm::BasicBlock::Create(state.entry.getContext(), "body.boolean.badarg", &state.entry);
        state.builder.SetInsertPoint(failure);
        raise_reason(state, abi::v1::ErrorReason::badarg_value, payload);
        state.builder.SetInsertPoint(saved);
        return failure;
    }
    if (!state.bad_argument) {
        auto *saved = state.builder.GetInsertBlock();
        state.bad_argument = llvm::BasicBlock::Create(state.entry.getContext(), "body.badarg", &state.entry);
        state.builder.SetInsertPoint(state.bad_argument);
        raise_reason(state, abi::v1::ErrorReason::badarg);
        state.builder.SetInsertPoint(saved);
    }
    return state.bad_argument;
}

llvm::Value *lower_immediate(ExpressionLowering &state, abi::v1::ImmediateOperation operation, llvm::Value *left,
                             llvm::Value *right) {
    if (auto *value = lower_inline_immediate(state, operation, left, right)) {
        return value;
    }
    return lower_immediate_service(state, operation, left, right);
}

llvm::Value *lower_immediate_service(ExpressionLowering &state, abi::v1::ImmediateOperation operation,
                                     llvm::Value *left, llvm::Value *right) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *slot = root_slot(state);
    auto service = output.getOrInsertFunction(
        services::symbol<services::Immediate>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(),
                                {builder.getPtrTy(), builder.getInt8Ty(), state.word, state.word, builder.getPtrTy()},
                                false));
    auto *outcome = builder.CreateCall(service,
                                       {state.entry.getArg(0), builder.getInt8(static_cast<std::uint8_t>(operation)),
                                        left, right ? right : llvm::ConstantInt::get(state.word, 0), slot},
                                       "service.outcome");
    if ((operation >= abi::v1::ImmediateOperation::add && operation < abi::v1::ImmediateOperation::absolute) ||
        operation == abi::v1::ImmediateOperation::divide) {
        return checked_arithmetic(state, {outcome, slot});
    }
    auto *rejection =
        bad_argument_exit(state, operation == abi::v1::ImmediateOperation::boolean_check ? left : nullptr);
    return checked_value(state, {outcome, slot}, rejection);
}

llvm::Value *lower_display(ExpressionLowering &state, llvm::Value *value) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *slot = root_slot(state);
    auto service = output.getOrInsertFunction(
        services::symbol<services::Display>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(), {builder.getPtrTy(), state.word, builder.getPtrTy()}, false));
    builder.CreateCall(service, {state.entry.getArg(0), value, slot}, "display.outcome");
    // Display has no semantic rejection: every failure is already in the checked channel.
    propagate_failure(state);
    return builder.CreateAlignedLoad(state.word, slot, llvm::Align(state.word->getBitWidth() / 8), "display.value");
}

llvm::Value *lower_halt(ExpressionLowering &state, llvm::Value *status) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto service = output.getOrInsertFunction(
        services::symbol<services::Halt>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(), {builder.getPtrTy(), state.word}, false));
    // halt/0 is halt(0): pass the small-integer encoding of zero.
    auto *code = status ? status : llvm::ConstantInt::get(state.word, abi::v1::small_integer_tag);
    builder.CreateCall(service, {state.entry.getArg(0), code}, "halt.outcome");
    // The service always records a halt, badarg or infrastructure failure, so this check always unwinds.
    propagate_failure(state);
    return llvm::ConstantInt::get(state.word, abi::v1::empty_list);
}

llvm::Value *lower_raise(ExpressionLowering &state, const std::u32string_view name, llvm::Value *reason) {
    using abi::v1::ErrorReason;
    const auto id = name == U"exit"    ? ErrorReason::raised_exit
                    : name == U"throw" ? ErrorReason::raised_throw
                                       : ErrorReason::raised_error;
    auto &output = *state.entry.getParent();
    auto service = output.getOrInsertFunction(
        services::symbol<services::Raise>(output.getTargetTriple()),
        llvm::FunctionType::get(state.builder.getInt8Ty(),
                                {state.builder.getPtrTy(), state.builder.getInt8Ty(), state.word}, false));
    state.builder.CreateCall(
        service, {state.entry.getArg(0), state.builder.getInt8(static_cast<std::uint8_t>(id)), reason}, "raise");
    // The service always records the exception or an infrastructure failure, so this check always unwinds.
    propagate_failure(state);
    return llvm::ConstantInt::get(state.word, abi::v1::empty_list);
}

llvm::Value *lower_catch(ExpressionLowering &state) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *slot = root_slot(state);
    auto service = output.getOrInsertFunction(
        services::symbol<services::Catch>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(), {builder.getPtrTy(), builder.getPtrTy()}, false));
    builder.CreateCall(service, {state.entry.getArg(0), slot}, "catch.outcome");
    // A caught exception clears the channel; anything else is still pending and leaves through this check.
    propagate_failure(state);
    return builder.CreateAlignedLoad(state.word, slot, llvm::Align(state.word->getBitWidth() / 8), "catch.value");
}

Exception lower_exception(ExpressionLowering &state) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *class_slot = root_slot(state);
    auto *reason_slot = root_slot(state);
    auto *stack_slot = root_slot(state);
    auto *ptr = builder.getPtrTy();
    auto service =
        output.getOrInsertFunction(services::symbol<services::Exception>(output.getTargetTriple()),
                                   llvm::FunctionType::get(builder.getInt8Ty(), {ptr, ptr, ptr, ptr}, false));
    builder.CreateCall(service, {state.entry.getArg(0), class_slot, reason_slot, stack_slot}, "exception.outcome");
    // A caught exception clears the channel; anything else is still pending and leaves through this check.
    propagate_failure(state);
    const llvm::Align align(state.word->getBitWidth() / 8);
    return {builder.CreateAlignedLoad(state.word, class_slot, align, "exception.class"),
            builder.CreateAlignedLoad(state.word, reason_slot, align, "exception.reason"),
            builder.CreateAlignedLoad(state.word, stack_slot, align, "exception.stack")};
}

llvm::Value *checked_value(ExpressionLowering &state, const ServiceOutput result, llvm::BasicBlock *rejection) {
    auto &builder = state.builder;
    propagate_failure(state);
    auto *success = llvm::BasicBlock::Create(state.entry.getContext(), "service.success", &state.entry);
    auto *test = builder.Insert(
        llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, result.outcome, builder.getInt8(0)));
    builder.CreateCondBr(test, success, rejection);
    builder.SetInsertPoint(success);
    return builder.CreateAlignedLoad(state.word, result.slot, llvm::Align(state.word->getBitWidth() / 8),
                                     "service.value");
}
} // namespace clause::codegen
