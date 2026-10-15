#include "lowering_roots.hpp"
#include "lowering_state.hpp"
#include <clause/abi/term.hpp>
#include <clause/abi/v1.hpp>
#include <cstdint>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>

// Inline float arithmetic (OPT0.15): + - * / of floats, or of a float and a small integer (and / of two small
// integers), computed in registers into a bump-allocated float cell on 64-bit targets; a non-finite result, a zero
// divisor, other operands or a full heap take the immediate service, which raises badarith or promotes.
namespace clause::codegen {
namespace {
using Op = abi::v1::ImmediateOperation;

// The header of a float cell on a 64-bit target: one payload word.
constexpr std::uint64_t FLOAT_HEADER =
    (std::uint64_t{1} << abi::v1::header_count_shift) |
    (static_cast<std::uint64_t>(abi::v1::HeaderKind::floating) << abi::v1::header_kind_shift);

// A constant float cell that operands which are no boxed word read instead of their own, so no load ever depends
// on a branch: its header passes the float test, its value is never selected.
llvm::Value *stand_in(ExpressionLowering &state) {
    auto &module = *state.entry.getParent();
    constexpr auto name = "clause.float.stand_in";
    if (auto *existing = module.getNamedGlobal(name)) {
        return existing;
    }
    auto *type = llvm::ArrayType::get(state.word, 2);
    auto *value = llvm::ConstantArray::get(
        type, {llvm::ConstantInt::get(state.word, FLOAT_HEADER), llvm::ConstantInt::get(state.word, 0)});
    return new llvm::GlobalVariable(module, type, true, llvm::GlobalValue::PrivateLinkage, value, name);
}

// An i1 comparing a word to a constant.
llvm::Value *word_equal(ExpressionLowering &state, llvm::Value *left, const std::uint64_t right) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, left,
                                                      llvm::ConstantInt::get(state.word, right)));
}

struct FloatOperand {
    // The operand is a small integer; it is a small integer or a float; its value as a double.
    llvm::Value *small;
    llvm::Value *number;
    llvm::Value *value;
};

// Read an operand as a double when it is a small integer or a float cell.
FloatOperand operand(ExpressionLowering &state, llvm::Value *word) {
    auto &builder = state.builder;
    const llvm::Align align(8);
    auto *small = word_equal(state, builder.CreateAnd(word, abi::v1::small_integer_tag), abi::v1::small_integer_tag);
    auto *boxed = word_equal(state, builder.CreateAnd(word, abi::v1::primary_mask), abi::v1::boxed_tag);
    auto *cell = builder.CreateSelect(boxed, cell_address(state, word, abi::v1::boxed_tag), stand_in(state));
    auto *header = builder.CreateAlignedLoad(state.word, cell, align, "float.header");
    auto *number = builder.CreateOr(small, builder.CreateAnd(boxed, word_equal(state, header, FLOAT_HEADER)));
    auto *stored = builder.CreateAlignedLoad(builder.getDoubleTy(), builder.CreateConstGEP1_64(state.word, cell, 1),
                                             align, "float.value");
    auto *converted = builder.CreateSIToFP(builder.CreateAShr(word, 4), builder.getDoubleTy());
    return {small, number, builder.CreateSelect(small, converted, stored)};
}

// The double result of the operation.
llvm::Value *calculate(ExpressionLowering &state, const Op operation, llvm::Value *left, llvm::Value *right) {
    auto &builder = state.builder;
    switch (operation) {
    case Op::add:
        return builder.CreateFAdd(left, right);
    case Op::subtract:
        return builder.CreateFSub(left, right);
    case Op::multiply:
        return builder.CreateFMul(left, right);
    default:
        return builder.CreateFDiv(left, right);
    }
}

// An i1 that holds when the inline form applies: numbers, a float among them (any numbers for /), a nonzero
// divisor, a finite result.
llvm::Value *applies(ExpressionLowering &state, const Op operation, const FloatOperand &left, const FloatOperand &right,
                     llvm::Value *result) {
    auto &builder = state.builder;
    auto *numbers = builder.CreateAnd(left.number, right.number);
    auto *zero = llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
    auto *operands = operation == Op::divide ? builder.CreateFCmpONE(right.value, zero)
                                             : builder.CreateNot(builder.CreateAnd(left.small, right.small));
    auto *magnitude = builder.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, result);
    auto *finite = builder.CreateFCmpONE(magnitude, llvm::ConstantFP::getInfinity(builder.getDoubleTy()));
    return builder.CreateAnd(builder.CreateAnd(numbers, operands), finite);
}
} // namespace

llvm::Value *inline_float_operation(ExpressionLowering &state, const Op operation, llvm::Value *left,
                                    llvm::Value *right, const llvm::function_ref<llvm::Value *()> service) {
    const bool supported =
        operation == Op::add || operation == Op::subtract || operation == Op::multiply || operation == Op::divide;
    if (state.word->getBitWidth() != 64 || !supported) {
        return service();
    }
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const auto a = operand(state, left);
    const auto b = operand(state, right);
    auto *result = calculate(state, operation, a.value, b.value);
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, 2));
    auto *direct = llvm::BasicBlock::Create(context, "float.inline", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "float.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "float.join", &state.entry);
    builder.CreateCondBr(builder.CreateAnd(applies(state, operation, a, b, result), room.fits), direct, fallback);
    builder.SetInsertPoint(direct);
    const llvm::Align align(8);
    builder.CreateAlignedStore(llvm::ConstantInt::get(state.word, FLOAT_HEADER), room.top, align);
    builder.CreateAlignedStore(result, builder.CreateConstGEP1_64(state.word, room.top, 1), align);
    heap_commit(state, room);
    auto *made = builder.CreateOr(builder.CreatePtrToInt(room.top, state.word),
                                  llvm::ConstantInt::get(state.word, abi::v1::boxed_tag));
    mark_term(state, made);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *serviced = service();
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {direct, made}, {fallback_end, serviced});
}
} // namespace clause::codegen
