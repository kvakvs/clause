#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/literals.hpp>
#include <clause/abi/term.hpp>
#include <llvm-c/Core.h>
#include <llvm/ADT/APInt.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/SSAUpdater.h>
#include <vector>

namespace clause::codegen {
namespace {
using Op = abi::v1::ImmediateOperation;

// The word operation of each arithmetic operator with an inline small-integer path.
std::optional<llvm::Instruction::BinaryOps> arithmetic(const Op operation) {
    switch (operation) {
    case Op::add:
        return llvm::Instruction::Add;
    case Op::subtract:
        return llvm::Instruction::Sub;
    case Op::multiply:
        return llvm::Instruction::Mul;
    default:
        return {};
    }
}

// Reject noninteger tags before decoding, independently of inferred facts or source specifications.
llvm::Value *compare(ExpressionLowering &state, const llvm::CmpInst::Predicate operation, llvm::Value *left,
                     llvm::Value *right) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, operation, left, right));
}

// Check both immediate tags before any payload decode is reachable.
llvm::Value *small(ExpressionLowering &state, llvm::Value *value) {
    auto &builder = state.builder;
    return compare(state, llvm::CmpInst::ICMP_EQ, builder.CreateAnd(value, llvm::ConstantInt::get(state.word, 15)),
                   llvm::ConstantInt::get(state.word, 15));
}

struct Arithmetic {
    // Carry the tagged result and whether it stayed a small integer.
    llvm::Value *value;
    llvm::Value *valid;
};

// One overflow-checked word operation: the result and an i1 that is set when the signed operation overflowed. The
// fields are read through LLVM-C, whose out-of-line builder clang-analyzer does not misread as an ArrayBound error.
std::array<llvm::Value *, 2> with_overflow(ExpressionLowering &state, const llvm::Intrinsic::ID intrinsic,
                                           llvm::Value *left, llvm::Value *right) {
    auto *pair = llvm::wrap(state.builder.CreateBinaryIntrinsic(intrinsic, left, right, nullptr, "integer.checked"));
    auto *builder = llvm::wrap(&state.builder);
    return {llvm::unwrap(LLVMBuildExtractValue(builder, pair, 0, "integer.result")),
            llvm::unwrap(LLVMBuildExtractValue(builder, pair, 1, "integer.overflow"))};
}

// Add, subtract or multiply the tagged words (OPT1.3): with both tags 0xF, `a + (b ^ 15)`, `a - (b ^ 15)` and
// `(a >> 4) * (b ^ 15)` (or 15) are the tagged results, and the word overflows exactly when the result leaves the
// small payload range, so the overflow flag of one instruction is the whole check.
Arithmetic tagged(ExpressionLowering &state, const llvm::Instruction::BinaryOps operation,
                  const std::array<llvm::Value *, 2> operands) {
    auto &builder = state.builder;
    auto *tag = llvm::ConstantInt::get(state.word, 15);
    auto *rhs = builder.CreateXor(operands[1], tag);
    if (operation == llvm::Instruction::Mul) {
        auto *lhs = builder.CreateAShr(operands[0], llvm::ConstantInt::get(state.word, 4));
        const auto [product, overflow] = with_overflow(state, llvm::Intrinsic::smul_with_overflow, lhs, rhs);
        return {builder.CreateOr(product, tag), builder.CreateNot(overflow)};
    }
    const auto intrinsic =
        operation == llvm::Instruction::Add ? llvm::Intrinsic::sadd_with_overflow : llvm::Intrinsic::ssub_with_overflow;
    const auto [result, overflow] = with_overflow(state, intrinsic, operands[0], rhs);
    return {result, builder.CreateNot(overflow)};
}

// Share LLVM's SSA formation utility with lazy joins, preserving locations on its inserted PHI.
llvm::Value *joined(ExpressionLowering &state, llvm::BasicBlock *fast, llvm::Value *encoded, llvm::BasicBlock *slow,
                    llvm::Value *promoted) {
    auto &builder = state.builder;
    auto *boundary = builder.CreateUnreachable();
    llvm::SmallVector<llvm::PHINode *, 2> phis;
    llvm::SSAUpdater updater(&phis);
    updater.Initialize(state.word, "integer.value");
    updater.AddAvailableValue(fast, encoded);
    updater.AddAvailableValue(slow, promoted);
    auto *value = updater.GetValueInMiddleOfBlock(builder.GetInsertBlock());
    for (auto *phi : phis) {
        phi->setDebugLoc(builder.getCurrentDebugLocation());
    }
    boundary->eraseFromParent();
    return value;
}

// One bound of an arithmetic result on a pair of proven operand bounds; none when it leaves int64.
std::optional<std::int64_t> bound(const llvm::Instruction::BinaryOps operation,
                                  const std::array<std::int64_t, 2> &operands) {
    bool overflow = false;
    const llvm::APInt lhs(64, static_cast<std::uint64_t>(operands[0]), true);
    const llvm::APInt rhs(64, static_cast<std::uint64_t>(operands[1]), true);
    const auto result = operation == llvm::Instruction::Add   ? lhs.sadd_ov(rhs, overflow)
                        : operation == llvm::Instruction::Sub ? lhs.ssub_ov(rhs, overflow)
                                                              : lhs.smul_ov(rhs, overflow);
    return overflow ? std::nullopt : std::optional{result.getSExtValue()};
}

// Whether every result of the operation on the proven operand ranges is a small integer of the target.
bool fits(const ExpressionLowering &state, const llvm::Instruction::BinaryOps operation, const SmallRange &left,
          const SmallRange &right) {
    const std::array corners{bound(operation, {left.low, right.low}), bound(operation, {left.low, right.high}),
                             bound(operation, {left.high, right.low}), bound(operation, {left.high, right.high})};
    const auto minimum =
        state.word->getBitWidth() == 32 ? abi::v1::IntegerEncoding<32>::minimum : abi::v1::IntegerEncoding<64>::minimum;
    return std::ranges::all_of(
        corners, [minimum](const auto &corner) { return corner && *corner >= minimum && *corner <= -minimum - 1; });
}

// Two proven small operands whose every result is small: decode, compute and encode in the word, with no check.
llvm::Value *direct(ExpressionLowering &state, const llvm::Instruction::BinaryOps operation, llvm::Value *left,
                    llvm::Value *right) {
    auto &builder = state.builder;
    auto *shift = llvm::ConstantInt::get(state.word, 4);
    auto *number = builder.CreateBinOp(operation, builder.CreateAShr(left, shift), builder.CreateAShr(right, shift),
                                       "integer.proven");
    return builder.CreateOr(builder.CreateShl(number, shift), llvm::ConstantInt::get(state.word, 15));
}

// The tag test of the operands not proven small integers; null when both are proven.
llvm::Value *unproven_tags(ExpressionLowering &state, llvm::Value *left, llvm::Value *right,
                           const OperandProofs &proofs) {
    llvm::Value *left_tag = proofs.left ? nullptr : small(state, left);
    llvm::Value *right_tag = proofs.right ? nullptr : small(state, right);
    if (left_tag && right_tag) {
        return state.builder.CreateAnd(left_tag, right_tag);
    }
    return left_tag ? left_tag : right_tag;
}

// Whether an operation takes the float path at once: a division, or + - * with a float operand, which never takes
// the small-integer path (raw floats, OPT1.2).
bool float_first(const ExpressionLowering &state, const Op operation, llvm::Value *left, llvm::Value *right) {
    return operation == Op::divide ||
           (arithmetic(operation) && (state.raw_floats.contains(left) || state.raw_floats.contains(right)));
}

// Unary minus (OPT1.11): a small integer's negation while it stays small (only the smallest one leaves the range),
// the inline float negation for other operands.
llvm::Value *negation(ExpressionLowering &state, llvm::Value *value,
                      const llvm::function_ref<llvm::Value *()> service) {
    if (state.raw_floats.contains(value)) {
        return inline_float_negation(state, value, service);
    }
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *attempt = llvm::BasicBlock::Create(context, "integer.negate", &state.entry);
    auto *fast = llvm::BasicBlock::Create(context, "integer.negate.success", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "integer.negate.fallback", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "integer.negate.join", &state.entry);
    auto *likely = llvm::MDBuilder(context).createLikelyBranchWeights();
    builder.CreateCondBr(small(state, value), attempt, fallback, likely);
    builder.SetInsertPoint(attempt);
    // The tagged zero minus the operand.
    const auto result = tagged(state, llvm::Instruction::Sub, {llvm::ConstantInt::get(state.word, 15), value});
    builder.CreateCondBr(result.valid, fast, fallback, likely);
    builder.SetInsertPoint(fast);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *promoted = inline_float_negation(state, value, service);
    auto *slow = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return joined(state, fast, result.value, slow, promoted);
}

// The float forms of an operation (raw floats OPT1.2, OPT1.11): arithmetic that takes the float path at once, type
// tests and comparisons of raw floats, float/1 and unary minus; null when none applies.
llvm::Value *float_operation(ExpressionLowering &state, const Op operation, llvm::Value *left, llvm::Value *right,
                             const llvm::function_ref<llvm::Value *()> service) {
    if (float_first(state, operation, left, right)) {
        return inline_float_operation(state, operation, left, right, service);
    }
    if (auto *tested = raw_float_test(state, operation, left)) {
        return tested;
    }
    if (operation == Op::to_float) {
        return inline_to_float(state, left, service);
    }
    if (operation == Op::negative) {
        return negation(state, left, service);
    }
    return raw_comparison(state, operation, left, right, service);
}

// Integer literals of at most this many limbs become module literals (OPT0.4, literal heap); larger ones (and those the
// ERTS size limit rejects) keep the service.
constexpr std::size_t LITERAL_LIMBS = 64;

// An integer literal: a small one as its immediate constant; a bignum as a module literal (literal heap). Null when it
// stays with the service.
llvm::Value *integer_literal(ExpressionLowering &state, const std::string_view decimal) {
    const auto width = state.word->getBitWidth();
    const llvm::APInt parsed(static_cast<unsigned>(decimal.size() * 4 + 8), llvm::StringRef(decimal), 10);
    const auto payload = width - abi::v1::small_integer_bits;
    if (parsed.getSignificantBits() <= payload) {
        const auto value = parsed.getSExtValue();
        const auto encoded =
            width == 32 ? *abi::v1::IntegerEncoding<32>::encode(value) : *abi::v1::IntegerEncoding<64>::encode(value);
        return llvm::ConstantInt::get(state.word, encoded);
    }
    const auto magnitude = parsed.abs();
    const auto limbs = (magnitude.getActiveBits() + width - 1) / width;
    if (limbs == 0 || limbs > LITERAL_LIMBS) {
        return nullptr;
    }
    std::string encoded;
    abi::literals::Writer(encoded).integer_bytes(literal_integer_bytes(parsed));
    return lower_literal(state, encoded);
}
} // namespace

llvm::BasicBlock *bad_arithmetic_exit(ExpressionLowering &state) {
    if (state.rejection) {
        return state.rejection;
    }
    if (!state.bad_arithmetic) {
        auto *saved = state.builder.GetInsertBlock();
        state.bad_arithmetic = llvm::BasicBlock::Create(state.entry.getContext(), "body.badarith", &state.entry);
        state.builder.SetInsertPoint(state.bad_arithmetic);
        raise_reason(state, abi::v1::ErrorReason::badarith);
        state.builder.SetInsertPoint(saved);
    }
    return state.bad_arithmetic;
}

namespace {
// Integer results beyond the size limit reject guards and raise system_limit in ordinary bodies.
llvm::BasicBlock *system_limit_exit(ExpressionLowering &state) {
    if (state.rejection) {
        return state.rejection;
    }
    if (!state.system_limit) {
        auto *saved = state.builder.GetInsertBlock();
        state.system_limit = llvm::BasicBlock::Create(state.entry.getContext(), "body.system_limit", &state.entry);
        state.builder.SetInsertPoint(state.system_limit);
        raise_reason(state, abi::v1::ErrorReason::system_limit);
        state.builder.SetInsertPoint(saved);
    }
    return state.system_limit;
}
} // namespace

llvm::Value *checked_arithmetic(ExpressionLowering &state, const ServiceOutput result) {
    auto &builder = state.builder;
    propagate_failure(state);
    auto *bad = bad_arithmetic_exit(state);
    auto *limit = system_limit_exit(state);
    auto *success = llvm::BasicBlock::Create(state.entry.getContext(), "service.success", &state.entry);
    auto *outcomes = builder.CreateSwitch(result.outcome, bad, 2);
    outcomes->addCase(builder.getInt8(static_cast<std::uint8_t>(abi::v1::ValueOutcome::success)), success);
    outcomes->addCase(builder.getInt8(static_cast<std::uint8_t>(abi::v1::ValueOutcome::system_limit)), limit);
    builder.SetInsertPoint(success);
    return builder.CreateAlignedLoad(state.word, result.slot, llvm::Align(state.word->getBitWidth() / 8),
                                     "service.value");
}

llvm::Value *lower_integer(ExpressionLowering &state, const std::string_view decimal) {
    if (auto *literal = integer_literal(state, decimal)) {
        return literal;
    }
    auto &output = *state.entry.getParent();
    auto &builder = state.builder;
    auto *bytes = llvm::ConstantDataArray::getString(output.getContext(), decimal, false);
    auto *text = new llvm::GlobalVariable(output, bytes->getType(), true, llvm::GlobalValue::PrivateLinkage, bytes,
                                          "integer.literal");
    auto *slot = root_slot(state);
    auto service = output.getOrInsertFunction(
        services::symbol<services::Integer>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(),
                                {builder.getPtrTy(), builder.getPtrTy(), state.word, builder.getPtrTy()}, false));
    auto *outcome = builder.CreateCall(
        service, {state.entry.getArg(0), text, llvm::ConstantInt::get(state.word, decimal.size()), slot},
        "integer.outcome");
    return checked_value(state, {outcome, slot}, bad_arithmetic_exit(state));
}

llvm::Value *lower_operation(ExpressionLowering &state, const Op operation, llvm::Value *left, llvm::Value *right,
                             const OperandProofs proofs) {
    if (auto *map = lower_map_query(state, operation, left, right)) {
        return map;
    }
    const auto checked = arithmetic(operation);
    const auto service = [&] {
        return lower_immediate(state, operation, materialized(state, left), materialized(state, right));
    };
    if (auto *floating = float_operation(state, operation, left, right, service)) {
        return floating;
    }
    if (!checked) {
        return service();
    }
    if (proofs.left && proofs.right && fits(state, *checked, *proofs.left, *proofs.right)) {
        return direct(state, *checked, left, right);
    }
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *attempt = llvm::BasicBlock::Create(context, "integer.small", &state.entry);
    auto *fast = llvm::BasicBlock::Create(context, "integer.small.success", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "integer.fallback", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "integer.join", &state.entry);
    // Small operands that stay small are the common case: registers and layout favour them (batched loop reductions).
    auto *likely = llvm::MDBuilder(context).createLikelyBranchWeights();
    if (auto *tags = unproven_tags(state, left, right, proofs)) {
        builder.CreateCondBr(tags, attempt, fallback, likely);
    } else {
        builder.CreateBr(attempt);
    }
    builder.SetInsertPoint(attempt);
    const auto result = tagged(state, *checked, {left, right});
    builder.CreateCondBr(result.valid, fast, fallback, likely);
    builder.SetInsertPoint(fast);
    auto *encoded = result.value;
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *promoted = inline_float_operation(state, operation, left, right, service);
    auto *slow = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return joined(state, fast, encoded, slow, promoted);
}
} // namespace clause::codegen
