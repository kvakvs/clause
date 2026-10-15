#include "literals.hpp"
#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/term.hpp>
#include <llvm/ADT/APInt.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/SSAUpdater.h>
#include <vector>

namespace clause::codegen {
namespace {
using Op = abi::v1::ImmediateOperation;

// Twice the target width holds every exact add/subtract/product of two small payloads.
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
    // Carry the checked decoded result and its combined word/payload overflow proof.
    llvm::Value *value;
    llvm::Value *valid;
};

// Compute without wrap in a double-width integer, then prove payload bounds before truncation and encoding.
Arithmetic calculate(ExpressionLowering &state, const llvm::Instruction::BinaryOps operation, llvm::Value *left,
                     llvm::Value *right) {
    auto &builder = state.builder;
    auto *wide = builder.getIntNTy(state.word->getBitWidth() * 2);
    auto *shift = llvm::ConstantInt::get(state.word, 4);
    auto *lhs = builder.CreateSExt(builder.CreateAShr(left, shift), wide);
    auto *rhs = builder.CreateSExt(builder.CreateAShr(right, shift), wide);
    auto *number = builder.CreateBinOp(operation, lhs, rhs, "integer.checked");
    const auto minimum =
        state.word->getBitWidth() == 32 ? abi::v1::IntegerEncoding<32>::minimum : abi::v1::IntegerEncoding<64>::minimum;
    const auto maximum = -minimum - 1;
    auto *fits =
        builder.CreateAnd(compare(state, llvm::CmpInst::ICMP_SGE, number, llvm::ConstantInt::getSigned(wide, minimum)),
                          compare(state, llvm::CmpInst::ICMP_SLE, number, llvm::ConstantInt::getSigned(wide, maximum)));
    return {number, fits};
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

// Integer literals of at most this many limbs become literal-area cells (OPT0.4); larger ones (and those the ERTS size
// limit rejects) keep the service.
constexpr std::size_t LITERAL_LIMBS = 64;

// An integer literal as a constant (OPT0.4): a small one as its immediate; a bignum as a cell of the module literal
// area, a header, the sign word (1 when negative) and the magnitude in target-word limbs, least significant first.
// Null when it stays with the service.
llvm::Constant *integer_literal(ExpressionLowering &state, const std::string_view decimal) {
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
    std::vector<llvm::Constant *> words{
        llvm::ConstantInt::get(
            state.word, ((limbs + 1) << abi::v1::header_count_shift) |
                            (static_cast<std::uint64_t>(abi::v1::HeaderKind::bignum) << abi::v1::header_kind_shift)),
        llvm::ConstantInt::get(state.word, parsed.isNegative() ? 1 : 0)};
    for (unsigned limb = 0; limb < limbs; ++limb) {
        words.push_back(llvm::ConstantInt::get(
            state.word,
            magnitude.extractBitsAsZExtValue(std::min(width, magnitude.getBitWidth() - limb * width), limb * width)));
    }
    return literal_cell(*state.entry.getParent(), state.word, words, abi::v1::boxed_tag);
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
    if (!checked) {
        return lower_immediate(state, operation, left, right);
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
    if (auto *tags = unproven_tags(state, left, right, proofs)) {
        builder.CreateCondBr(tags, attempt, fallback);
    } else {
        builder.CreateBr(attempt);
    }
    builder.SetInsertPoint(attempt);
    const auto result = calculate(state, *checked, left, right);
    builder.CreateCondBr(result.valid, fast, fallback);
    builder.SetInsertPoint(fast);
    auto *payload = builder.CreateTrunc(result.value, state.word);
    auto *encoded = builder.CreateOr(builder.CreateShl(payload, llvm::ConstantInt::get(state.word, 4)),
                                     llvm::ConstantInt::get(state.word, 15));
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *promoted = lower_immediate(state, operation, left, right);
    auto *slow = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return joined(state, fast, encoded, slow, promoted);
}
} // namespace clause::codegen
