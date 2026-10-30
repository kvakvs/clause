#include "lowering_state.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/equality.hpp>
#include <clause/abi/frames.hpp>
#include <clause/abi/term.hpp>
#include <initializer_list>
#include <llvm/IR/Module.h>
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
    return join_values(state, type, {fast_end, fast_value}, {slow_end, slow_value});
}

// The header word of a boxed word, read only where a boxed tag test dominates. An integer constant is an immediate,
// so its tag test never passes: no pointer is formed for it.
llvm::Value *header(ExpressionLowering &state, llvm::Value *value) {
    if (llvm::isa<llvm::ConstantInt>(value)) {
        return llvm::PoisonValue::get(state.word);
    }
    auto &builder = state.builder;
    auto *cell = cell_address(state, value, abi::v1::boxed_tag);
    return builder.CreateAlignedLoad(state.word, cell, llvm::Align(state.word->getBitWidth() / 8), "inline.header");
}

// Word `index` (a word value) of the cell a boxed or list word points at, rooted like a service output.
llvm::Value *cell_word(ExpressionLowering &state, llvm::Value *value, const unsigned tag, llvm::Value *index) {
    if (llvm::isa<llvm::ConstantInt>(value)) {
        return llvm::PoisonValue::get(state.word);
    }
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *cell = cell_address(state, value, tag);
    auto *loaded =
        builder.CreateAlignedLoad(state.word, builder.CreateGEP(state.word, cell, index), align, "proven.word");
    mark_term(state, loaded);
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
    return boxed(state, value, {HeaderKind::refc_binary, HeaderKind::heap_binary, HeaderKind::sub_binary});
}

llvm::Value *float_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::floating});
}

llvm::Value *map_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::map, HeaderKind::tree_map});
}

// A local pid word, or a cell of another node's pid (plan 30 step DIST1); ports alike.
llvm::Value *pid_test(ExpressionLowering &state, llvm::Value *value) {
    return state.builder.CreateOr(tagged(state, value, abi::v1::immediate_tag_mask, abi::v1::pid_tag),
                                  boxed(state, value, {HeaderKind::external_pid}));
}

llvm::Value *port_test(ExpressionLowering &state, llvm::Value *value) {
    return state.builder.CreateOr(tagged(state, value, abi::v1::immediate_tag_mask, abi::v1::port_tag),
                                  boxed(state, value, {HeaderKind::external_port}));
}

llvm::Value *reference_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::reference, HeaderKind::external_reference});
}

llvm::Value *function_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::fun_closure});
}

llvm::Value *native_record_test(ExpressionLowering &state, llvm::Value *value) {
    return boxed(state, value, {HeaderKind::native_record});
}

// The bit length of a word proven a bitstring: word 1 of a heap binary, word 2 of an off-heap view or sub-binary.
llvm::Value *bit_length(ExpressionLowering &state, llvm::Value *value) {
    auto &builder = state.builder;
    auto *inline_data = header_kind(state, header(state, value), {HeaderKind::heap_binary});
    auto *index = builder.CreateSelect(inline_data, word(state, 1), word(state, 2));
    return builder.CreateAlignedLoad(
        state.word, builder.CreateGEP(state.word, cell_address(state, value, abi::v1::boxed_tag), index),
        llvm::Align(state.word->getBitWidth() / 8), "binary.bits");
}

// A binary: a bitstring whose bit length is whole bytes.
llvm::Value *binary_test(ExpressionLowering &state, llvm::Value *value) {
    auto &builder = state.builder;
    return split(
        state, builder.getInt1Ty(), bitstring_test(state, value),
        [&] { return tagged(state, bit_length(state, value), 7, 0); }, [&] { return builder.getFalse(); });
}

constexpr std::array<std::pair<Op, TypeTest>, 15> TYPE_TESTS{{
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
    {Op::is_binary, binary_test},
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

// bit_size/1 and byte_size/1 of a bitstring whose size is a small integer (inline bitstring sizes); a size past the
// small range, possible on 32-bit targets, and any other operand take the service.
llvm::Value *bitstring_size(ExpressionLowering &state, const Operands &in) {
    auto &builder = state.builder;
    const auto largest = (std::uint64_t{1} << (state.word->getBitWidth() - abi::v1::small_integer_bits - 1)) - 1;
    // A non-bitstring gets a size past the small range, so only the service handles it.
    auto *size = split(
        state, state.word, bitstring_test(state, in.left),
        [&] {
            auto *bits = bit_length(state, in.left);
            return in.operation == Op::byte_size ? builder.CreateLShr(builder.CreateAdd(bits, word(state, 7)), 3)
                                                 : bits;
        },
        [&] { return word(state, largest + 1); });
    return with_service(state, in, compare(state, llvm::CmpInst::ICMP_ULE, size, word(state, largest)), [&] {
        return builder.CreateOr(builder.CreateShl(size, word(state, abi::v1::small_integer_bits)),
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

// self/0: the context's pid word (OPT0.2D).
llvm::Value *self_word(ExpressionLowering &state, const Operands &) {
    auto *field = state.builder.CreateConstInBoundsGEP1_64(state.word, state.entry.getArg(0),
                                                           static_cast<unsigned>(abi::v1::StateField::self));
    return state.builder.CreateAlignedLoad(state.word, field, llvm::Align(state.word->getBitWidth() / 8), "self");
}

// bsl of a small integer by a small count below the payload width, computed at twice the width: inline while the
// result stays small (OPT0.2D). The count is clamped to zero for other operands, so no shift is poison.
llvm::Value *shift_left(ExpressionLowering &state, const Operands &in) {
    auto &builder = state.builder;
    const auto bits = state.word->getBitWidth();
    const auto payload_bits = bits - abi::v1::small_integer_bits;
    auto *wide = builder.getIntNTy(2 * bits);
    auto *right = decode(state, in.right);
    auto *usable = builder.CreateAnd(both_small(state, in.left, in.right),
                                     compare(state, llvm::CmpInst::ICMP_ULT, right, word(state, payload_bits)));
    auto *count = builder.CreateSelect(usable, right, word(state, 0));
    auto *shifted =
        builder.CreateShl(builder.CreateSExt(decode(state, in.left), wide), builder.CreateZExt(count, wide));
    const auto limit = std::int64_t{1} << (payload_bits - 1);
    auto *fits =
        builder.CreateAnd(compare(state, llvm::CmpInst::ICMP_SGE, shifted, llvm::ConstantInt::getSigned(wide, -limit)),
                          compare(state, llvm::CmpInst::ICMP_SLT, shifted, llvm::ConstantInt::getSigned(wide, limit)));
    return with_service(state, in, builder.CreateAnd(usable, fits),
                        [&] { return encode(state, builder.CreateTrunc(shifted, state.word)); });
}

using Inline = llvm::Value *(*)(ExpressionLowering &, const Operands &);

constexpr std::array<std::pair<Op, Inline>, 25> OPERATIONS{{
    {Op::exact_equal, comparison},
    {Op::exact_not_equal, comparison},
    {Op::equal, comparison},
    {Op::not_equal, comparison},
    {Op::less, comparison},
    {Op::less_equal, comparison},
    {Op::greater, comparison},
    {Op::greater_equal, comparison},
    {Op::element, element},
    {Op::tuple_size, tuple_size},
    {Op::hd, cons_word},
    {Op::tl, cons_word},
    {Op::logical_not, logical},
    {Op::logical_and, logical},
    {Op::logical_or, logical},
    {Op::logical_xor, logical},
    {Op::boolean_check, logical},
    {Op::bit_and, bitwise},
    {Op::bit_or, bitwise},
    {Op::bit_xor, bitwise},
    {Op::shift_right, division},
    {Op::shift_left, shift_left},
    {Op::self, self_word},
    {Op::bit_size, bitstring_size},
    {Op::byte_size, bitstring_size},
}};

constexpr std::array<std::pair<Op, Inline>, 2> DIVISIONS{{{Op::integer_divide, division}, {Op::remainder, division}}};

template <class Table> auto entry(const Table &table, const Op operation) {
    return std::ranges::find(table, operation, &Table::value_type::first);
}
} // namespace

llvm::Value *lower_setelement(ExpressionLowering &state, const std::size_t builtin,
                              const std::span<llvm::Value *const> values) {
    auto *index = values[0];
    auto *tuple = values[1];
    // An integer constant is an immediate: never a boxed tuple, so only the builtin applies.
    if (llvm::isa<llvm::ConstantInt>(tuple)) {
        return lower_builtin(state, builtin, values);
    }
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    const auto arity = [&] {
        return builder.CreateLShr(header(state, tuple), word(state, abi::v1::header_count_shift));
    };
    auto *position = decode(state, index);
    auto *shaped = split(
        state, builder.getInt1Ty(),
        builder.CreateAnd(small(state, index), tagged(state, tuple, abi::v1::primary_mask, abi::v1::boxed_tag)),
        [&] {
            auto *in_range =
                compare(state, llvm::CmpInst::ICMP_ULT, builder.CreateSub(position, word(state, 1)), arity());
            return builder.CreateAnd(header_kind(state, header(state, tuple), {HeaderKind::tuple}), in_range);
        },
        [&] { return builder.getFalse(); });
    auto *room = split(
        state, builder.getInt1Ty(), shaped,
        [&] { return heap_room(state, builder.CreateAdd(arity(), word(state, 1))).fits; },
        [&] { return builder.getFalse(); });
    return split(
        state, state.word, room,
        [&] {
            auto *words = builder.CreateAdd(arity(), word(state, 1));
            const auto target = heap_room(state, words);
            auto *source = cell_address(state, tuple, abi::v1::boxed_tag);
            builder.CreateMemCpy(target.top, align, source, align,
                                 builder.CreateMul(words, word(state, state.word->getBitWidth() / 8)));
            builder.CreateAlignedStore(values[2], builder.CreateGEP(state.word, target.top, position), align);
            heap_commit(state, target);
            auto *copy =
                builder.CreateOr(builder.CreatePtrToInt(target.top, state.word), word(state, abi::v1::boxed_tag));
            mark_term(state, copy);
            return copy;
        },
        [&] { return lower_builtin(state, builtin, values); });
}

llvm::Value *lower_make_tuple(ExpressionLowering &state, const std::size_t builtin,
                              const std::span<llvm::Value *const> values) {
    auto *size = values[0];
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *count = decode(state, size);
    // 1..64 elements: an unsigned count - 1 below 64.
    auto *valid =
        builder.CreateAnd(small(state, size), compare(state, llvm::CmpInst::ICMP_ULT,
                                                      builder.CreateSub(count, word(state, 1)), word(state, 64)));
    auto *room = split(
        state, builder.getInt1Ty(), valid,
        [&] { return heap_room(state, builder.CreateAdd(count, word(state, 1))).fits; },
        [&] { return builder.getFalse(); });
    return split(
        state, state.word, room,
        [&] {
            const auto target = heap_room(state, builder.CreateAdd(count, word(state, 1)));
            builder.CreateAlignedStore(builder.CreateShl(count, word(state, abi::v1::header_count_shift)), target.top,
                                       align);
            auto *fill = llvm::Intrinsic::getOrInsertDeclaration(state.entry.getParent(),
                                                                 llvm::Intrinsic::experimental_memset_pattern,
                                                                 {builder.getPtrTy(), state.word, state.word});
            builder.CreateCall(
                fill, {builder.CreateConstGEP1_64(state.word, target.top, 1), values[1], count, builder.getFalse()});
            heap_commit(state, target);
            auto *made =
                builder.CreateOr(builder.CreatePtrToInt(target.top, state.word), word(state, abi::v1::boxed_tag));
            mark_term(state, made);
            return made;
        },
        [&] { return lower_builtin(state, builtin, values); });
}

llvm::Value *inline_bitstring_test(ExpressionLowering &state, llvm::Value *value) {
    return bitstring_test(state, value);
}

namespace {
// A constant zero cell an inline binary's view reads the off-heap words from instead of its own, so no load depends
// on a branch: its offset is 0, its data address is never selected (SSA binary cursors, OPT1.4F).
llvm::Value *binary_stand_in(ExpressionLowering &state) {
    auto &module = *state.entry.getParent();
    constexpr auto name = "clause.binary.stand_in";
    if (auto *existing = module.getNamedGlobal(name)) {
        return existing;
    }
    auto *type = llvm::ArrayType::get(state.word, abi::v1::refc_data_word + 1);
    return new llvm::GlobalVariable(module, type, true, llvm::GlobalValue::PrivateLinkage,
                                    llvm::ConstantAggregateZero::get(type), name);
}

// The view of an inline or off-heap binary (never a sub-binary), read without branches; its words are direct binary
// memory. With `invariant`, the binary exists before the function runs, so its header, offset and data address are
// fixed for the whole run and may leave a loop (SSA binary cursors, OPT1.4F).
BinaryView direct_view(ExpressionLowering &state, llvm::Value *binary, const bool invariant) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *cell = cell_address(state, binary, abi::v1::boxed_tag);
    const auto at = [&](llvm::Value *base, unsigned index) {
        return builder.CreateConstGEP1_64(state.word, base, index);
    };
    const auto load = [&](llvm::Value *address, const char *name, bool fixed) {
        auto *loaded =
            tag_memory(builder.CreateAlignedLoad(state.word, address, align, name), MemoryKind::direct_binary);
        if (fixed && invariant) {
            loaded->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(state.entry.getContext(), {}));
        }
        return loaded;
    };
    auto *inline_data = header_kind(state, load(cell, "inline.header", true), {HeaderKind::heap_binary});
    auto *shared = builder.CreateSelect(inline_data, binary_stand_in(state), cell, "binary.shared");
    auto *offset = load(at(shared, abi::v1::refc_offset_word), "binary.offset", true);
    auto *data =
        builder.CreateSelect(inline_data, builder.CreatePtrToInt(at(cell, abi::v1::heap_binary_data_word), state.word),
                             load(at(shared, abi::v1::refc_data_word), "binary.data", true), "binary.data");
    auto *bits = load(
        builder.CreateSelect(inline_data, at(cell, abi::v1::heap_binary_bits_word), at(cell, abi::v1::refc_bits_word)),
        "binary.bits", false);
    return {bits, offset, data, binary, nullptr};
}
} // namespace

BinaryView sub_view(ExpressionLowering &state, llvm::Value *binary, const bool invariant) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *cell = cell_address(state, binary, abi::v1::boxed_tag);
    const auto load = [&](unsigned index, const char *name) {
        return tag_memory(
            builder.CreateAlignedLoad(state.word, builder.CreateConstGEP1_64(state.word, cell, index), align, name),
            MemoryKind::sub_binary);
    };
    auto *sub_offset = load(abi::v1::sub_offset_word, "sub.offset");
    auto *sub_bits = load(abi::v1::sub_bits_word, "sub.bits");
    auto *original = load(abi::v1::sub_original_word, "sub.original");
    if (invariant) {
        original->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(state.entry.getContext(), {}));
    }
    mark_term(state, original);
    const auto under = direct_view(state, original, invariant);
    return {sub_bits, builder.CreateAdd(under.offset, sub_offset), under.data, original, under.offset};
}

BinaryView inline_binary_view(ExpressionLowering &state, llvm::Value *binary) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *sub = header_kind(state, header(state, binary), {HeaderKind::sub_binary});
    auto *range = llvm::BasicBlock::Create(context, "binary.sub", &state.entry);
    auto *whole = llvm::BasicBlock::Create(context, "binary.whole", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "binary.resolved", &state.entry);
    builder.CreateCondBr(sub, range, whole);
    builder.SetInsertPoint(range);
    const auto ranged = sub_view(state, binary, false);
    auto *range_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(whole);
    const auto direct = direct_view(state, binary, false);
    auto *whole_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    const auto joined = [&](llvm::Value *from_range, llvm::Value *from_whole) {
        return join_values(state, state.word, {range_end, from_range}, {whole_end, from_whole});
    };
    auto *root = joined(ranged.original, binary);
    mark_term(state, root);
    return {joined(ranged.bits, direct.bits), joined(ranged.offset, direct.offset), joined(ranged.data, direct.data),
            root, joined(ranged.base, direct.offset)};
}

llvm::Value *join_values(ExpressionLowering &state, llvm::Type *type, const BlockValue first, const BlockValue second) {
    auto &builder = state.builder;
    auto *join = builder.GetInsertBlock();
    auto *boundary = builder.CreateUnreachable();
    llvm::SmallVector<llvm::PHINode *, 2> phis;
    llvm::SSAUpdater updater(&phis);
    updater.Initialize(type, "joined.value");
    updater.AddAvailableValue(first.first, first.second);
    updater.AddAvailableValue(second.first, second.second);
    auto *value = updater.GetValueInMiddleOfBlock(join);
    for (auto *phi : phis) {
        phi->setDebugLoc(builder.getCurrentDebugLocation());
    }
    boundary->eraseFromParent();
    return value;
}

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
