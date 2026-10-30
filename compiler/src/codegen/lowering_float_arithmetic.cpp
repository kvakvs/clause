#include "lowering_roots.hpp"
#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/builtins.hpp>
#include <clause/abi/term.hpp>
#include <clause/abi/v1.hpp>
#include <cstdint>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>
#include <span>
#include <vector>

// Inline float arithmetic (OPT0.15): + - * / of floats, or of a float and a small integer (and / of two small
// integers), computed in registers into a bump-allocated float cell on 64-bit targets; a non-finite result, a zero
// divisor, other operands or a full heap take the immediate service, which raises badarith or promotes. Inline math
// computes the common math functions of numbers the same way, with the builtin as the fallback.
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

// Read an operand as a double when it is a small integer or a float cell; a float whose double lowering holds is read
// from its register (raw floats, OPT1.2).
FloatOperand operand(ExpressionLowering &state, llvm::Value *word) {
    auto &builder = state.builder;
    if (const auto raw = state.raw_floats.find(word); raw != state.raw_floats.end()) {
        return {builder.getFalse(), builder.getTrue(), raw->second};
    }
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

// An i1 that holds when a double is finite (neither infinite nor NaN).
llvm::Value *finite(ExpressionLowering &state, llvm::Value *result) {
    auto &builder = state.builder;
    auto *magnitude = builder.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, result);
    return builder.CreateFCmpONE(magnitude, llvm::ConstantFP::getInfinity(builder.getDoubleTy()));
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
    return builder.CreateAnd(builder.CreateAnd(numbers, operands), finite(state, result));
}

// A float result and whether the inline form applies to it.
struct FloatResult {
    llvm::Value *applies;
    llvm::Value *value;
};

// A new float cell holding the result where it applies and the heap has room; `service` elsewhere.
llvm::Value *boxed_or_service(ExpressionLowering &state, const FloatResult result,
                              const llvm::function_ref<llvm::Value *()> service) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, 2));
    auto *direct = llvm::BasicBlock::Create(context, "float.inline", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "float.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "float.join", &state.entry);
    builder.CreateCondBr(builder.CreateAnd(result.applies, room.fits), direct, fallback);
    builder.SetInsertPoint(direct);
    const llvm::Align align(8);
    builder.CreateAlignedStore(llvm::ConstantInt::get(state.word, FLOAT_HEADER), room.top, align);
    builder.CreateAlignedStore(result.value, builder.CreateConstGEP1_64(state.word, room.top, 1), align);
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

// The double of a word known to be a float cell, read where the builder is (raw floats, OPT1.2).
llvm::Value *float_value(ExpressionLowering &state, llvm::Value *word) {
    auto *cell = cell_address(state, word, abi::v1::boxed_tag);
    return state.builder.CreateAlignedLoad(state.builder.getDoubleTy(),
                                           state.builder.CreateConstGEP1_64(state.word, cell, 1), llvm::Align(8),
                                           "float.raw");
}

// The result of an operation whose every success is a float (raw floats, OPT1.2): the inline double where it applies,
// else the double of the float cell the service returns, kept in a register; float_box makes its cell.
llvm::Value *raw_or_service(ExpressionLowering &state, const FloatResult result,
                            const llvm::function_ref<llvm::Value *()> service) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *direct = llvm::BasicBlock::Create(context, "float.direct", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "float.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "float.join", &state.entry);
    builder.CreateCondBr(result.applies, direct, fallback);
    builder.SetInsertPoint(direct);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *serviced = float_value(state, service());
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    auto *raw = join_values(state, builder.getDoubleTy(), {direct, result.value}, {fallback_end, serviced});
    return float_box(state, raw);
}

// A float cell made by the float service from the double's IEEE bytes in network order (CLAUSE_float_v1).
llvm::Value *serviced_box(ExpressionLowering &state, llvm::Value *raw) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *bits = builder.CreateBitCast(raw, state.word);
    if (output.getDataLayout().isLittleEndian()) {
        bits = builder.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, bits);
    }
    // One word holds the bytes, then the result: lower_frames makes it a raw slot.
    auto *buffer = builder.CreateAlloca(state.word, nullptr, "float.bytes");
    buffer->setAlignment(llvm::Align(8));
    builder.CreateAlignedStore(bits, buffer, llvm::Align(8));
    auto service = output.getOrInsertFunction(
        services::symbol<services::Float>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(),
                                {builder.getPtrTy(), builder.getPtrTy(), state.word, builder.getPtrTy()}, false));
    auto *outcome = builder.CreateCall(
        service, {state.entry.getArg(0), buffer, llvm::ConstantInt::get(state.word, 8), buffer}, "float.box.outcome");
    auto *value = checked_value(state, {outcome, buffer}, bad_arithmetic_exit(state));
    mark_term(state, value);
    return value;
}

// A math function computed inline (inline math): its bridge builtin, and the LLVM intrinsic of an exact IEEE
// operation or else the C library function the runtime builtin calls, so both give the same bits.
struct MathFunction {
    std::size_t builtin;
    llvm::Intrinsic::ID intrinsic;
    const char *library;
};

// The bridge index of a math builtin.
constexpr std::size_t math(const char *name, const std::size_t arity) {
    return abi::v1::find_bridge_builtin("math", name, arity).value_or(0);
}

constexpr auto LIBRARY = llvm::Intrinsic::not_intrinsic;

constexpr std::array MATH_FUNCTIONS{
    MathFunction{math("sqrt", 1), llvm::Intrinsic::sqrt, nullptr},
    MathFunction{math("floor", 1), llvm::Intrinsic::floor, nullptr},
    MathFunction{math("ceil", 1), llvm::Intrinsic::ceil, nullptr},
    MathFunction{math("exp", 1), LIBRARY, "exp"},
    MathFunction{math("log", 1), LIBRARY, "log"},
    MathFunction{math("log2", 1), LIBRARY, "log2"},
    MathFunction{math("log10", 1), LIBRARY, "log10"},
    MathFunction{math("sin", 1), LIBRARY, "sin"},
    MathFunction{math("cos", 1), LIBRARY, "cos"},
    MathFunction{math("pow", 2), LIBRARY, "pow"},
};

// The double result of a math function of `operands`.
llvm::Value *evaluate(ExpressionLowering &state, const MathFunction &function, std::span<llvm::Value *const> operands) {
    auto &builder = state.builder;
    if (function.intrinsic != LIBRARY) {
        return builder.CreateUnaryIntrinsic(function.intrinsic, operands[0]);
    }
    auto &module = *state.entry.getParent();
    const std::vector<llvm::Type *> parameters(operands.size(), builder.getDoubleTy());
    auto callee =
        module.getOrInsertFunction(function.library, llvm::FunctionType::get(builder.getDoubleTy(), parameters, false));
    auto *call = builder.CreateCall(callee, llvm::ArrayRef(operands.data(), operands.size()));
    // A library call stays one: LLVM must not replace it with its own expansion of the function.
    call->addFnAttr(llvm::Attribute::NoBuiltin);
    return call;
}
} // namespace

llvm::Value *inline_float_operation(ExpressionLowering &state, const Op operation, llvm::Value *left,
                                    llvm::Value *right, const llvm::function_ref<llvm::Value *()> service) {
    const bool supported =
        operation == Op::add || operation == Op::subtract || operation == Op::multiply || operation == Op::divide;
    if (state.word->getBitWidth() != 64 || !supported) {
        return service();
    }
    // Every success is a float when an operand is one or the operation divides (raw floats, OPT1.2).
    const bool floats = operation == Op::divide || state.raw_floats.contains(left) || state.raw_floats.contains(right);
    const auto a = operand(state, left);
    const auto b = operand(state, right);
    auto *result = calculate(state, operation, a.value, b.value);
    const FloatResult computed{applies(state, operation, a, b, result), result};
    return floats ? raw_or_service(state, computed, service) : boxed_or_service(state, computed, service);
}

llvm::Value *inline_math(ExpressionLowering &state, const std::size_t builtin,
                         const std::span<llvm::Value *const> values,
                         const llvm::function_ref<llvm::Value *()> service) {
    const auto function = std::ranges::find(MATH_FUNCTIONS, builtin, &MathFunction::builtin);
    if (state.word->getBitWidth() != 64 || function == MATH_FUNCTIONS.end()) {
        return nullptr;
    }
    auto &builder = state.builder;
    llvm::Value *numbers = builder.getTrue();
    std::vector<llvm::Value *> operands;
    for (auto *value : values) {
        const auto read = operand(state, value);
        numbers = builder.CreateAnd(numbers, read.number);
        operands.push_back(read.value);
    }
    auto *result = evaluate(state, *function, operands);
    return raw_or_service(state, {builder.CreateAnd(numbers, finite(state, result)), result}, service);
}

llvm::Value *float_box(ExpressionLowering &state, llvm::Value *raw) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, 2));
    auto *direct = llvm::BasicBlock::Create(context, "float.box", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "float.box.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "float.boxed", &state.entry);
    auto *branch = builder.CreateCondBr(room.fits, direct, fallback);
    builder.SetInsertPoint(direct);
    const llvm::Align align(8);
    builder.CreateAlignedStore(llvm::ConstantInt::get(state.word, FLOAT_HEADER), room.top, align);
    builder.CreateAlignedStore(raw, builder.CreateConstGEP1_64(state.word, room.top, 1), align);
    heap_commit(state, room);
    auto *made = builder.CreateOr(builder.CreatePtrToInt(room.top, state.word),
                                  llvm::ConstantInt::get(state.word, abi::v1::boxed_tag));
    mark_term(state, made);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *serviced = serviced_box(state, raw);
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    auto *word = join_values(state, state.word, {direct, made}, {fallback_end, serviced});
    mark_cell(branch, word);
    state.raw_floats.emplace(word, raw);
    return word;
}

llvm::Value *materialized(ExpressionLowering &state, llvm::Value *word) {
    // A missing operand (null) has no double either.
    const auto raw = state.raw_floats.find(word);
    // A float literal is boxed already (literal heap).
    return raw == state.raw_floats.end() || state.literal_values.contains(word) ? word : float_box(state, raw->second);
}

namespace {
// The true or false word of an i1.
llvm::Value *truth_word(ExpressionLowering &state, llvm::Value *truth) {
    return state.builder.CreateSelect(truth, llvm::ConstantInt::get(state.word, abi::v1::true_atom),
                                      llvm::ConstantInt::get(state.word, abi::v1::false_atom));
}

// The i1 of comparing two doubles: exactly equal floats have the same bits (0.0 and -0.0 differ), the other
// comparisons are numeric.
llvm::Value *compare_doubles(ExpressionLowering &state, const Op operation, llvm::Value *left, llvm::Value *right) {
    auto &builder = state.builder;
    if (operation <= Op::exact_not_equal) {
        return builder.Insert(llvm::CmpInst::Create(
            llvm::Instruction::ICmp, operation == Op::exact_equal ? llvm::CmpInst::ICMP_EQ : llvm::CmpInst::ICMP_NE,
            builder.CreateBitCast(left, state.word), builder.CreateBitCast(right, state.word)));
    }
    using P = llvm::CmpInst::Predicate;
    constexpr std::array predicates{P::FCMP_OEQ, P::FCMP_UNE, P::FCMP_OLT, P::FCMP_OLE, P::FCMP_OGT, P::FCMP_OGE};
    return builder.Insert(llvm::CmpInst::Create(llvm::Instruction::FCmp,
                                                predicates.at(static_cast<unsigned>(operation) - 2), left, right));
}

// The word of `result` where it applies, the service's elsewhere (OPT1.11).
llvm::Value *word_or_service(ExpressionLowering &state, const FloatResult result,
                             const llvm::function_ref<llvm::Value *()> service) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *direct = llvm::BasicBlock::Create(context, "float.direct", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "float.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "float.join", &state.entry);
    builder.CreateCondBr(result.applies, direct, fallback, llvm::MDBuilder(context).createLikelyBranchWeights());
    builder.SetInsertPoint(direct);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *serviced = service();
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {direct, result.value}, {fallback_end, serviced});
}

// The magnitude below which ERTS compares a float with a small integer as two doubles (MAX_LOSSLESS_FLOAT).
constexpr double LOSSLESS = static_cast<double>((std::int64_t{1} << 53) - 2);

// A raw float compared with a word that has no double in a register (OPT1.11). =:= and =/= hold only against a float
// cell of the same bits. The others compare doubles inline against a float cell, or against a small integer while the
// float is within the lossless range, as ERTS does; any other word takes the service.
llvm::Value *mixed_comparison(ExpressionLowering &state, const Op operation, llvm::Value *raw,
                              const FloatOperand &other, const bool raw_left,
                              const llvm::function_ref<llvm::Value *()> service) {
    auto &builder = state.builder;
    auto *left = raw_left ? raw : other.value;
    auto *right = raw_left ? other.value : raw;
    auto *is_float = builder.CreateAnd(other.number, builder.CreateNot(other.small));
    if (operation <= Op::exact_not_equal) {
        auto *same = builder.CreateAnd(is_float, compare_doubles(state, Op::exact_equal, left, right));
        return truth_word(state, operation == Op::exact_equal ? same : builder.CreateNot(same));
    }
    auto *lossless = builder.CreateFCmpOLT(builder.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, raw),
                                           llvm::ConstantFP::get(builder.getDoubleTy(), LOSSLESS));
    auto *applies = builder.CreateOr(is_float, builder.CreateAnd(other.small, lossless));
    // The comparison reads no memory, so it is computed before the branch.
    return word_or_service(state, {applies, truth_word(state, compare_doubles(state, operation, left, right))},
                           service);
}
} // namespace

llvm::Value *raw_comparison(ExpressionLowering &state, const Op operation, llvm::Value *left, llvm::Value *right,
                            const llvm::function_ref<llvm::Value *()> service) {
    const auto first = state.raw_floats.find(left);
    const auto second = state.raw_floats.find(right);
    const bool raw_left = first != state.raw_floats.end();
    const bool raw_right = second != state.raw_floats.end();
    if (operation > Op::greater_equal || (!raw_left && !raw_right)) {
        return nullptr;
    }
    if (raw_left && raw_right) {
        return truth_word(state, compare_doubles(state, operation, first->second, second->second));
    }
    // Inline float reads are 64-bit only.
    if (state.word->getBitWidth() != 64) {
        return nullptr;
    }
    return raw_left ? mixed_comparison(state, operation, first->second, operand(state, right), true, service)
                    : mixed_comparison(state, operation, second->second, operand(state, left), false, service);
}

FloatCell float_cell(ExpressionLowering &state, llvm::Value *word) {
    auto &builder = state.builder;
    // An immediate is never a float cell.
    if (llvm::isa<llvm::ConstantInt>(word)) {
        return {builder.getFalse(), llvm::ConstantFP::get(builder.getDoubleTy(), 0.0)};
    }
    const auto read = operand(state, word);
    return {builder.CreateAnd(read.number, builder.CreateNot(read.small)), read.value};
}

llvm::Value *raw_float_test(ExpressionLowering &state, const Op operation, llvm::Value *value) {
    const bool test = operation == Op::is_float || operation == Op::is_number;
    return test && state.raw_floats.contains(value) ? llvm::ConstantInt::get(state.word, abi::v1::true_atom) : nullptr;
}

llvm::Value *inline_to_float(ExpressionLowering &state, llvm::Value *value,
                             const llvm::function_ref<llvm::Value *()> service) {
    if (state.raw_floats.contains(value)) {
        return value;
    }
    if (state.word->getBitWidth() != 64) {
        return service();
    }
    const auto read = operand(state, value);
    return raw_or_service(state, {read.number, read.value}, service);
}

llvm::Value *inline_float_negation(ExpressionLowering &state, llvm::Value *value,
                                   const llvm::function_ref<llvm::Value *()> service) {
    auto &builder = state.builder;
    if (const auto raw = state.raw_floats.find(value); raw != state.raw_floats.end()) {
        return float_box(state, builder.CreateFNeg(raw->second));
    }
    if (state.word->getBitWidth() != 64) {
        return service();
    }
    const auto cell = float_cell(state, value);
    return boxed_or_service(state, {cell.is_float, builder.CreateFNeg(cell.value)}, service);
}

void define_float_box(ExpressionLowering &state) {
    auto &output = *state.entry.getParent();
    if (output.getFunction(FLOAT_BOX)) {
        return;
    }
    auto &context = output.getContext();
    auto *type = llvm::FunctionType::get(state.word,
                                         {llvm::PointerType::get(context, 0), llvm::Type::getDoubleTy(context)}, false);
    auto *box = llvm::Function::Create(type, llvm::GlobalValue::InternalLinkage, FLOAT_BOX, output);
    llvm::IRBuilder<> builder(llvm::BasicBlock::Create(context, "entry", box));
    ExpressionLowering helper{builder, *box, state.module, state.function, state.inferred, state.word, {}};
    auto *raw = box->getArg(1);
    const llvm::Align align(8);
    // One word holds the service's bytes in network order, then the cell's word.
    auto *buffer = builder.CreateAlloca(state.word, nullptr, "float.bytes");
    buffer->setAlignment(align);
    const auto room = heap_room(helper, llvm::ConstantInt::get(state.word, 2));
    auto *direct = llvm::BasicBlock::Create(context, "float.box", box);
    auto *fallback = llvm::BasicBlock::Create(context, "float.box.service", box);
    builder.CreateCondBr(room.fits, direct, fallback);
    builder.SetInsertPoint(direct);
    builder.CreateAlignedStore(llvm::ConstantInt::get(state.word, FLOAT_HEADER), room.top, align);
    builder.CreateAlignedStore(raw, builder.CreateConstGEP1_64(state.word, room.top, 1), align);
    heap_commit(helper, room);
    builder.CreateRet(builder.CreateOr(builder.CreatePtrToInt(room.top, state.word),
                                       llvm::ConstantInt::get(state.word, abi::v1::boxed_tag)));
    builder.SetInsertPoint(fallback);
    auto *bits = builder.CreateBitCast(raw, state.word);
    if (output.getDataLayout().isLittleEndian()) {
        bits = builder.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, bits);
    }
    builder.CreateAlignedStore(bits, buffer, align);
    auto service = output.getOrInsertFunction(
        services::symbol<services::Float>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(),
                                {builder.getPtrTy(), builder.getPtrTy(), state.word, builder.getPtrTy()}, false));
    auto *outcome =
        builder.CreateCall(service, {box->getArg(0), buffer, llvm::ConstantInt::get(state.word, 8), buffer});
    auto *made = builder.CreateAlignedLoad(state.word, buffer, align);
    auto *succeeded = builder.Insert(
        llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, outcome, builder.getInt8(0)));
    builder.CreateRet(builder.CreateSelect(succeeded, made, llvm::ConstantInt::get(state.word, 0)));
}
} // namespace clause::codegen
