#include "lowering_state.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/equality.hpp>
#include <clause/abi/term.hpp>
#include <initializer_list>
#include <llvm/Transforms/Utils/SSAUpdater.h>
#include <utility>

// Inline forms of immediate operations (OPT0.2B): type tests, comparisons, tuple and cons access, boolean operators
// and bitwise and division arithmetic on small integers. Every form that can meet an operand it does not handle keeps
// the runtime service as its slow tail.
namespace clause::codegen {
namespace {
using Op = abi::v1::ImmediateOperation;
using abi::v1::HeaderKind;

llvm::Value *word(ExpressionLowering &state, const std::uint64_t value) {
    return llvm::ConstantInt::get(state.word, value);
}

llvm::Value *compare(ExpressionLowering &state, const llvm::CmpInst::Predicate predicate, llvm::Value *left,
                     llvm::Value *right) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, predicate, left, right));
}

llvm::Value *word_is(ExpressionLowering &state, llvm::Value *value, const std::uint64_t constant) {
    return compare(state, llvm::CmpInst::ICMP_EQ, value, word(state, constant));
}

// i1: the bits of `mask` in the word are `tag`.
llvm::Value *tagged(ExpressionLowering &state, llvm::Value *value, const std::uint64_t mask, const std::uint64_t tag) {
    return word_is(state, state.builder.CreateAnd(value, word(state, mask)), tag);
}

llvm::Value *small(ExpressionLowering &state, llvm::Value *value) {
    return tagged(state, value, abi::v1::small_integer_tag, abi::v1::small_integer_tag);
}

llvm::Value *both_small(ExpressionLowering &state, llvm::Value *left, llvm::Value *right) {
    return small(state, state.builder.CreateAnd(left, right));
}

// The true or false word of an i1.
llvm::Value *boolean(ExpressionLowering &state, llvm::Value *truth) {
    return state.builder.CreateSelect(truth, word(state, abi::v1::true_atom), word(state, abi::v1::false_atom));
}

llvm::Value *is_boolean(ExpressionLowering &state, llvm::Value *value) {
    return state.builder.CreateOr(word_is(state, value, abi::v1::true_atom),
                                  word_is(state, value, abi::v1::false_atom));
}

// Branch on `condition`: `fast` builds the value where it holds and `slow` elsewhere, each in its own blocks; the
// builder continues after both, at the value LLVM's SSA updater joins (no PHI operand access, as lowering_integers).
template <class Fast, class Slow>
llvm::Value *split(ExpressionLowering &state, llvm::Type *type, llvm::Value *condition, Fast fast, Slow slow) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *fast_block = llvm::BasicBlock::Create(context, "inline.direct", &state.entry);
    auto *slow_block = llvm::BasicBlock::Create(context, "inline.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "inline.join", &state.entry);
    builder.CreateCondBr(condition, fast_block, slow_block);
    builder.SetInsertPoint(fast_block);
    auto *fast_value = fast();
    auto *fast_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(slow_block);
    auto *slow_value = slow();
    auto *slow_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    auto *boundary = builder.CreateUnreachable();
    llvm::SmallVector<llvm::PHINode *, 2> phis;
    llvm::SSAUpdater updater(&phis);
    updater.Initialize(type, "inline.value");
    updater.AddAvailableValue(fast_end, fast_value);
    updater.AddAvailableValue(slow_end, slow_value);
    auto *value = updater.GetValueInMiddleOfBlock(join);
    for (auto *phi : phis) {
        phi->setDebugLoc(builder.getCurrentDebugLocation());
    }
    boundary->eraseFromParent();
    return value;
}

// The header word of a boxed word, read only where a boxed tag test dominates. A constant word is an immediate, so
// its tag test never passes: no pointer is formed for it.
llvm::Value *header(ExpressionLowering &state, llvm::Value *value) {
    if (llvm::isa<llvm::Constant>(value)) {
        return llvm::PoisonValue::get(state.word);
    }
    auto &builder = state.builder;
    auto *cell = builder.CreateIntToPtr(builder.CreateSub(value, word(state, abi::v1::boxed_tag)), builder.getPtrTy(),
                                        "proven.cell");
    return builder.CreateAlignedLoad(state.word, cell, llvm::Align(state.word->getBitWidth() / 8), "inline.header");
}

// Word `index` (a word value) of the cell a boxed or list word points at, rooted like a service output.
llvm::Value *cell_word(ExpressionLowering &state, llvm::Value *value, const unsigned tag, llvm::Value *index) {
    if (llvm::isa<llvm::Constant>(value)) {
        return llvm::PoisonValue::get(state.word);
    }
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *cell = builder.CreateIntToPtr(builder.CreateSub(value, word(state, tag)), builder.getPtrTy(), "proven.cell");
    auto *loaded =
        builder.CreateAlignedLoad(state.word, builder.CreateGEP(state.word, cell, index), align, "proven.word");
    builder.CreateAlignedStore(loaded, root_slot(state), align);
    return loaded;
}

// i1: a header of one of `kinds`.
llvm::Value *header_kind(ExpressionLowering &state, llvm::Value *header_word, std::initializer_list<HeaderKind> kinds) {
    auto &builder = state.builder;
    auto *kind = builder.CreateAnd(builder.CreateLShr(header_word, word(state, abi::v1::header_kind_shift)),
                                   word(state, abi::v1::header_kind_mask));
    llvm::Value *any = builder.getFalse();
    for (const auto expected : kinds) {
        any = builder.CreateOr(any, word_is(state, kind, static_cast<std::uint64_t>(expected)));
    }
    return any;
}

// i1: a boxed word whose header has one of `kinds`.
llvm::Value *boxed(ExpressionLowering &state, llvm::Value *value, std::initializer_list<HeaderKind> kinds) {
    return split(
        state, state.builder.getInt1Ty(), tagged(state, value, abi::v1::primary_mask, abi::v1::boxed_tag),
        [&] { return header_kind(state, header(state, value), kinds); }, [&] { return state.builder.getFalse(); });
}

// --- Type tests: an i1 for every operand, no slow tail ---

using TypeTest = llvm::Value *(*)(ExpressionLowering &, llvm::Value *);

llvm::Value *atom_test(ExpressionLowering &state, llvm::Value *value) {
    return tagged(state, value, (std::uint64_t{1} << abi::v1::atom_tag_bits) - 1, abi::v1::atom_tag);
}

llvm::Value *integer_test(ExpressionLowering &state, llvm::Value *value) {
    return state.builder.CreateOr(small(state, value), boxed(state, value, {HeaderKind::bignum}));
}

llvm::Value *number_test(ExpressionLowering &state, llvm::Value *value) {
    return state.builder.CreateOr(small(state, value), boxed(state, value, {HeaderKind::bignum, HeaderKind::floating}));
}

llvm::Value *tuple_test(ExpressionLowering &state, llvm::Value *value) {
    return state.builder.CreateOr(word_is(state, value, abi::v1::empty_tuple),
                                  boxed(state, value, {HeaderKind::tuple}));
}

llvm::Value *list_test(ExpressionLowering &state, llvm::Value *value) {
    return state.builder.CreateOr(word_is(state, value, abi::v1::empty_list),
                                  tagged(state, value, abi::v1::primary_mask, abi::v1::list_tag));
}

llvm::Value *bitstring_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::refc_binary, HeaderKind::heap_binary});
}

llvm::Value *float_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::floating});
}

llvm::Value *map_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::map, HeaderKind::tree_map});
}

llvm::Value *pid_test(ExpressionLowering &state, llvm::Value *value) {
    return tagged(state, value, abi::v1::immediate_tag_mask, abi::v1::pid_tag);
}

llvm::Value *port_test(ExpressionLowering &state, llvm::Value *value) {
    return tagged(state, value, abi::v1::immediate_tag_mask, abi::v1::port_tag);
}

llvm::Value *reference_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::reference});
}

llvm::Value *function_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::fun_closure});
}

llvm::Value *native_record_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::native_record});
}

constexpr std::array<std::pair<Op, TypeTest>, 14> TYPE_TESTS{{
    {Op::is_atom, atom_test},
    {Op::is_integer, integer_test},
    {Op::is_number, number_test},
    {Op::is_boolean, is_boolean},
    {Op::is_tuple, tuple_test},
    {Op::is_list, list_test},
    {Op::is_bitstring, bitstring_test},
    {Op::is_float, float_test},
    {Op::is_map, map_test},
    {Op::is_pid, pid_test},
    {Op::is_port, port_test},
    {Op::is_reference, reference_test},
    {Op::is_function, function_test},
    {Op::is_native_record, native_record_test},
}};

// --- Operations with a slow tail: the inline value where `condition` holds, the service's elsewhere ---

struct Operands {
    // The operation and its operands, passed to the service on the slow tail.
    Op operation;
    llvm::Value *left;
    llvm::Value *right;
};

template <class Fast>
llvm::Value *with_service(ExpressionLowering &state, const Operands &in, llvm::Value *condition, Fast fast) {
    return split(state, state.word, condition, fast,
                 [&] { return lower_immediate_service(state, in.operation, in.left, in.right); });
}

// ==, /= of two immediates are word comparisons; ordering of two small integers compares their words.
llvm::Value *comparison(ExpressionLowering &state, const Operands &in) {
    using P = llvm::CmpInst::Predicate;
    constexpr std::array predicates{P::ICMP_EQ,  P::ICMP_NE,  P::ICMP_EQ,  P::ICMP_NE,
                                    P::ICMP_SLT, P::ICMP_SLE, P::ICMP_SGT, P::ICMP_SGE};
    const auto predicate = predicates.at(static_cast<unsigned>(in.operation));
    if (in.operation <= Op::exact_not_equal) {
        auto *same = lower_exact(state, in.left, in.right);
        return boolean(state, in.operation == Op::exact_equal ? same : state.builder.CreateNot(same));
    }
    auto *condition = in.operation <= Op::not_equal ? tagged(state, state.builder.CreateAnd(in.left, in.right),
                                                             abi::v1::primary_mask, abi::v1::primary_mask)
                                                    : both_small(state, in.left, in.right);
    return with_service(state, in, condition,
                        [&] { return boolean(state, compare(state, predicate, in.left, in.right)); });
}

// element(N, T) for a small N in 1..tuple_size(T).
llvm::Value *element(ExpressionLowering &state, const Operands &in) {
    auto &builder = state.builder;
    auto *index = builder.CreateAShr(in.left, word(state, abi::v1::small_integer_bits));
    auto *valid = split(
        state, builder.getInt1Ty(),
        builder.CreateAnd(small(state, in.left), tagged(state, in.right, abi::v1::primary_mask, abi::v1::boxed_tag)),
        [&] {
            auto *tuple = header(state, in.right);
            auto *arity = builder.CreateLShr(tuple, word(state, abi::v1::header_count_shift));
            return builder.CreateAnd(
                header_kind(state, tuple, {HeaderKind::tuple}),
                compare(state, llvm::CmpInst::ICMP_ULT, builder.CreateSub(index, word(state, 1)), arity));
        },
        [&] { return builder.getFalse(); });
    return with_service(state, in, valid, [&] { return cell_word(state, in.right, abi::v1::boxed_tag, index); });
}

// tuple_size(T) of a boxed tuple.
llvm::Value *tuple_size(ExpressionLowering &state, const Operands &in) {
    auto &builder = state.builder;
    return with_service(state, in, boxed(state, in.left, {HeaderKind::tuple}), [&] {
        auto *arity = builder.CreateLShr(header(state, in.left), word(state, abi::v1::header_count_shift));
        return builder.CreateOr(builder.CreateShl(arity, word(state, abi::v1::small_integer_bits)),
                                word(state, abi::v1::small_integer_tag));
    });
}

// hd/1 and tl/1 of a cons cell.
llvm::Value *cons_word(ExpressionLowering &state, const Operands &in) {
    const auto index = in.operation == Op::hd ? 0U : 1U;
    return with_service(state, in, tagged(state, in.left, abi::v1::primary_mask, abi::v1::list_tag),
                        [&] { return cell_word(state, in.left, abi::v1::list_tag, word(state, index)); });
}

// not/1, and/or/xor of booleans, and the boolean check of a lazy operator's left operand.
llvm::Value *logical(ExpressionLowering &state, const Operands &in) {
    auto &builder = state.builder;
    const bool unary = in.operation == Op::logical_not || in.operation == Op::boolean_check;
    auto *condition =
        unary ? is_boolean(state, in.left) : builder.CreateAnd(is_boolean(state, in.left), is_boolean(state, in.right));
    return with_service(state, in, condition, [&]() -> llvm::Value * {
        auto *left = word_is(state, in.left, abi::v1::true_atom);
        switch (in.operation) {
        case Op::boolean_check:
            return in.left;
        case Op::logical_not:
            return boolean(state, builder.CreateNot(left));
        default:
            break;
        }
        auto *right = word_is(state, in.right, abi::v1::true_atom);
        constexpr std::array operators{llvm::Instruction::And, llvm::Instruction::Or, llvm::Instruction::Xor};
        const auto at = static_cast<unsigned>(in.operation) - static_cast<unsigned>(Op::logical_and);
        return boolean(state, builder.CreateBinOp(operators.at(at), left, right));
    });
}

// band, bor and bxor of two small integers keep the small tag in place.
llvm::Value *bitwise(ExpressionLowering &state, const Operands &in) {
    auto &builder = state.builder;
    return with_service(state, in, both_small(state, in.left, in.right), [&]() -> llvm::Value * {
        if (in.operation == Op::bit_and) {
            return builder.CreateAnd(in.left, in.right);
        }
        if (in.operation == Op::bit_or) {
            return builder.CreateOr(in.left, in.right);
        }
        return builder.CreateOr(builder.CreateXor(in.left, in.right), word(state, abi::v1::small_integer_tag));
    });
}

llvm::Value *encode(ExpressionLowering &state, llvm::Value *payload) {
    auto &builder = state.builder;
    return builder.CreateOr(builder.CreateShl(payload, word(state, abi::v1::small_integer_bits)),
                            word(state, abi::v1::small_integer_tag));
}

llvm::Value *decode(ExpressionLowering &state, llvm::Value *value) {
    return state.builder.CreateAShr(value, word(state, abi::v1::small_integer_bits));
}

// bsr by a small count below the payload width, div and rem by a nonzero small; div of the smallest small integer
// by -1 leaves the payload range and takes the service.
llvm::Value *division(ExpressionLowering &state, const Operands &in) {
    auto &builder = state.builder;
    auto *left = decode(state, in.left);
    auto *right = decode(state, in.right);
    const auto payload_bits = state.word->getBitWidth() - abi::v1::small_integer_bits;
    llvm::Value *condition = both_small(state, in.left, in.right);
    if (in.operation == Op::shift_right) {
        condition =
            builder.CreateAnd(condition, compare(state, llvm::CmpInst::ICMP_ULT, right, word(state, payload_bits)));
        return with_service(state, in, condition, [&] { return encode(state, builder.CreateAShr(left, right)); });
    }
    auto *minimum = llvm::ConstantInt::getSigned(state.word, -(std::int64_t{1} << (payload_bits - 1)));
    auto *overflow =
        builder.CreateAnd(compare(state, llvm::CmpInst::ICMP_EQ, left, minimum),
                          compare(state, llvm::CmpInst::ICMP_EQ, right, llvm::ConstantInt::getSigned(state.word, -1)));
    condition = builder.CreateAnd(condition, compare(state, llvm::CmpInst::ICMP_NE, right, word(state, 0)));
    condition = builder.CreateAnd(condition, builder.CreateNot(overflow));
    return with_service(state, in, condition, [&] {
        return encode(state, in.operation == Op::integer_divide ? builder.CreateSDiv(left, right)
                                                                : builder.CreateSRem(left, right));
    });
}

using Inline = llvm::Value *(*)(ExpressionLowering &, const Operands &);

constexpr std::array<std::pair<Op, Inline>, 21> OPERATIONS{{
    {Op::exact_equal, comparison}, {Op::exact_not_equal, comparison},
    {Op::equal, comparison},       {Op::not_equal, comparison},
    {Op::less, comparison},        {Op::less_equal, comparison},
    {Op::greater, comparison},     {Op::greater_equal, comparison},
    {Op::element, element},        {Op::tuple_size, tuple_size},
    {Op::hd, cons_word},           {Op::tl, cons_word},
    {Op::logical_not, logical},    {Op::logical_and, logical},
    {Op::logical_or, logical},     {Op::logical_xor, logical},
    {Op::boolean_check, logical},  {Op::bit_and, bitwise},
    {Op::bit_or, bitwise},         {Op::bit_xor, bitwise},
    {Op::shift_right, division},
}};

constexpr std::array<std::pair<Op, Inline>, 2> DIVISIONS{{{Op::integer_divide, division}, {Op::remainder, division}}};

template <class Table> auto entry(const Table &table, const Op operation) {
    return std::ranges::find(table, operation, &Table::value_type::first);
}
} // namespace

llvm::Value *lower_inline_immediate(ExpressionLowering &state, const abi::v1::ImmediateOperation operation,
                                    llvm::Value *left, llvm::Value *right) {
    if (const auto test = entry(TYPE_TESTS, operation); test != TYPE_TESTS.end()) {
        return boolean(state, test->second(state, left));
    }
    const Operands in{operation, left, right};
    if (const auto found = entry(OPERATIONS, operation); found != OPERATIONS.end()) {
        return found->second(state, in);
    }
    if (const auto found = entry(DIVISIONS, operation); found != DIVISIONS.end()) {
        return found->second(state, in);
    }
    return nullptr;
}
} // namespace clause::codegen
