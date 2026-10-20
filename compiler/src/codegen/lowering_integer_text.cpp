#include "lowering_state.hpp"
#include <clause/abi/builtins.hpp>
#include <clause/abi/equality.hpp>
#include <clause/abi/term.hpp>

// Inline integer text (OPT3.8): integer_to_list/1 of a small integer writes its digit cells in generated code, and
// list_to_integer/1 of a short proper list of decimal digits reads it in place, as OTP's JIT does for the common
// cases; any other argument, or a full heap block, makes the builtin call, which also raises every error.
namespace clause::codegen {
namespace {
constexpr auto INTEGER_TO_LIST = abi::v1::find_bridge_builtin("erlang", "integer_to_list", 1).value_or(0);
static_assert(abi::v1::find_bridge_builtin("erlang", "integer_to_list", 1));
constexpr auto LIST_TO_INTEGER = abi::v1::find_bridge_builtin("erlang", "list_to_integer", 1).value_or(0);
static_assert(abi::v1::find_bridge_builtin("erlang", "list_to_integer", 1));

constexpr std::uint64_t DECIMAL = 10;
// The tag bits of a small integer, and the word step between consecutive small integers.
constexpr std::uint64_t TAG = abi::v1::small_integer_tag;
constexpr std::uint64_t STEP = std::uint64_t{1} << abi::v1::small_integer_bits;

llvm::Value *word(const ExpressionLowering &state, std::uint64_t value) {
    return llvm::ConstantInt::get(state.word, value);
}

// The small-integer word of a character.
llvm::Value *character(const ExpressionLowering &state, char value) {
    return word(state, static_cast<std::uint64_t>(value) << abi::v1::small_integer_bits | TAG);
}

llvm::Value *compare(ExpressionLowering &state, llvm::CmpInst::Predicate predicate, llvm::Value *left,
                     llvm::Value *right) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, predicate, left, right));
}

llvm::BasicBlock *block(const ExpressionLowering &state, const char *name) {
    return llvm::BasicBlock::Create(state.entry.getContext(), name, &state.entry);
}

// Digits a list may hold for list_to_integer/1 to read inline: one fewer than the largest small integer has, so
// every such list is a small integer.
std::uint64_t inline_digits(unsigned word_bits) {
    auto largest = (std::uint64_t{1} << (word_bits - abi::v1::small_integer_bits - 1)) - 1;
    std::uint64_t digits = 0;
    for (; largest != 0; largest /= DECIMAL) {
        ++digits;
    }
    return digits - 1;
}

// The decimal digits of a non-negative magnitude, at least one.
llvm::Value *count_digits(ExpressionLowering &state, llvm::Value *magnitude) {
    auto &builder = state.builder;
    auto *before = builder.GetInsertBlock();
    auto *loop = block(state, "itl.count");
    auto *done = block(state, "itl.counted");
    builder.CreateBr(loop);
    builder.SetInsertPoint(loop);
    auto *rest = loop_placeholder(state, "itl.rest");
    auto *digits = loop_placeholder(state, "itl.digits");
    auto *next = builder.CreateAdd(digits, word(state, 1));
    auto *shorter = builder.CreateUDiv(rest, word(state, DECIMAL));
    builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_EQ, shorter, word(state, 0)), done, loop);
    close_loop(rest, loop, {before, magnitude}, {loop, shorter});
    close_loop(digits, loop, {before, word(state, 0)}, {loop, next});
    builder.SetInsertPoint(done);
    return next;
}

struct Digits {
    // The magnitude written, the cells (one per character, the sign's first), whether a minus sign leads and the
    // last cell's tail.
    llvm::Value *magnitude;
    llvm::Value *cells;
    llvm::Value *negative;
    llvm::Value *tail;
};

// Write the digit cells from the last to the first at `top`, each cell's tail the next one, the last's `tail`.
void write_digits(ExpressionLowering &state, const Digits &digits, llvm::Value *top) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *before = builder.GetInsertBlock();
    auto *loop = block(state, "itl.write");
    auto *done = block(state, "itl.written");
    auto *first = builder.CreateZExt(digits.negative, state.word);
    auto *last = builder.CreateSub(digits.cells, word(state, 1));
    builder.CreateBr(loop);
    builder.SetInsertPoint(loop);
    auto *index = loop_placeholder(state, "itl.index");
    auto *rest = loop_placeholder(state, "itl.rest");
    auto *tail = loop_placeholder(state, "itl.tail");
    auto *cell = builder.CreateGEP(state.word, top, builder.CreateShl(index, 1));
    auto *digit = builder.CreateURem(rest, word(state, DECIMAL));
    builder.CreateAlignedStore(
        builder.CreateAdd(builder.CreateShl(digit, abi::v1::small_integer_bits), character(state, '0')), cell, align);
    builder.CreateAlignedStore(tail, builder.CreateConstGEP1_64(state.word, cell, 1), align);
    auto *list = builder.CreateOr(builder.CreatePtrToInt(cell, state.word), word(state, abi::v1::list_tag));
    auto *previous = builder.CreateSub(index, word(state, 1));
    auto *shorter = builder.CreateUDiv(rest, word(state, DECIMAL));
    builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_EQ, index, first), done, loop);
    close_loop(index, loop, {before, last}, {loop, previous});
    close_loop(rest, loop, {before, digits.magnitude}, {loop, shorter});
    close_loop(tail, loop, {before, digits.tail}, {loop, list});
    // A negative number's first cell holds the sign; its tail is the first digit's cell, the loop's last.
    auto *sign = block(state, "itl.sign");
    auto *signed_done = block(state, "itl.signed");
    builder.SetInsertPoint(done);
    builder.CreateCondBr(digits.negative, sign, signed_done);
    builder.SetInsertPoint(sign);
    builder.CreateAlignedStore(character(state, '-'), top, align);
    builder.CreateAlignedStore(list, builder.CreateConstGEP1_64(state.word, top, 1), align);
    builder.CreateBr(signed_done);
    builder.SetInsertPoint(signed_done);
}

struct DigitList {
    // A small-integer word whose digits are written, and the last digit cell's tail.
    llvm::Value *value;
    llvm::Value *tail;
};

// The digit list of a small-integer word onto its tail where the heap block has room; `slow` otherwise.
llvm::Value *small_digits(ExpressionLowering &state, DigitList digits, llvm::BasicBlock *slow) {
    auto &builder = state.builder;
    auto *number = builder.CreateAShr(digits.value, abi::v1::small_integer_bits);
    auto *negative = compare(state, llvm::CmpInst::ICMP_SLT, number, word(state, 0));
    auto *magnitude = builder.CreateSelect(negative, builder.CreateNeg(number), number);
    auto *cells = builder.CreateAdd(count_digits(state, magnitude), builder.CreateZExt(negative, state.word));
    const auto room = heap_room(state, builder.CreateShl(cells, 1));
    auto *fill = block(state, "itl.fill");
    builder.CreateCondBr(room.fits, fill, slow);
    builder.SetInsertPoint(fill);
    write_digits(state, {magnitude, cells, negative, digits.tail}, room.top);
    heap_commit(state, room);
    auto *list = builder.CreateOr(builder.CreatePtrToInt(room.top, state.word), word(state, abi::v1::list_tag));
    mark_term(state, list);
    return list;
}

llvm::Value *is_small(ExpressionLowering &state, llvm::Value *value) {
    return compare(state, llvm::CmpInst::ICMP_EQ, state.builder.CreateAnd(value, word(state, TAG)), word(state, TAG));
}

// The digits of a small integer onto its tail, else `other`'s value.
llvm::Value *digits_or(ExpressionLowering &state, DigitList digits, llvm::function_ref<llvm::Value *()> other) {
    auto &builder = state.builder;
    auto *small = block(state, "itl.small");
    auto *slow = block(state, "itl.builtin");
    auto *join = block(state, "itl.join");
    builder.CreateCondBr(is_small(state, digits.value), small, slow);
    builder.SetInsertPoint(small);
    auto *list = small_digits(state, digits, slow);
    auto *made = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(slow);
    auto *called = other();
    auto *called_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {made, list}, {called_end, called});
}

// A small integer itself, which the append taking it writes as digits (digit prefixes); else the builtin's list.
llvm::Value *deferred_digits(ExpressionLowering &state, llvm::Value *value, llvm::function_ref<llvm::Value *()> call) {
    auto &builder = state.builder;
    auto *small = builder.GetInsertBlock();
    auto *slow = block(state, "itl.builtin");
    auto *join = block(state, "itl.deferred");
    builder.CreateCondBr(is_small(state, value), join, slow);
    builder.SetInsertPoint(slow);
    auto *called = call();
    auto *called_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {small, value}, {called_end, called});
}

struct Reading {
    // The value of the digits read so far, their count and the list word left.
    llvm::Value *value;
    llvm::Value *digits;
    llvm::Value *rest;
};

// Read decimal digit cells from `list` while there are at most `limit`; the first other word ends the walk.
Reading read_digits(ExpressionLowering &state, llvm::Value *list, std::uint64_t limit) {
    auto &builder = state.builder;
    auto *before = builder.GetInsertBlock();
    auto *loop = block(state, "lti.read");
    auto *cell = block(state, "lti.cell");
    auto *step = block(state, "lti.digit");
    auto *done = block(state, "lti.end");
    builder.CreateBr(loop);
    builder.SetInsertPoint(loop);
    auto *rest = loop_placeholder(state, "lti.rest");
    auto *value = loop_placeholder(state, "lti.value");
    auto *digits = loop_placeholder(state, "lti.digits");
    auto *cons = compare(state, llvm::CmpInst::ICMP_EQ, builder.CreateAnd(rest, word(state, abi::v1::primary_mask)),
                         word(state, abi::v1::list_tag));
    auto *room = compare(state, llvm::CmpInst::ICMP_ULT, digits, word(state, limit));
    builder.CreateCondBr(builder.CreateAnd(cons, room), cell, done);
    builder.SetInsertPoint(cell);
    // A digit's word less the word of '0' is a multiple of the step below ten steps; anything else ends the walk.
    auto *offset = builder.CreateSub(inline_cons_word(state, rest, 0), character(state, '0'));
    auto *aligned =
        compare(state, llvm::CmpInst::ICMP_EQ, builder.CreateAnd(offset, word(state, STEP - 1)), word(state, 0));
    auto *below = compare(state, llvm::CmpInst::ICMP_ULT, offset, word(state, DECIMAL * STEP));
    builder.CreateCondBr(builder.CreateAnd(aligned, below), step, done);
    builder.SetInsertPoint(step);
    auto *next = builder.CreateAdd(builder.CreateMul(value, word(state, DECIMAL)),
                                   builder.CreateLShr(offset, abi::v1::small_integer_bits));
    auto *tail = inline_cons_word(state, rest, 1);
    auto *counted = builder.CreateAdd(digits, word(state, 1));
    builder.CreateBr(loop);
    builder.SetInsertPoint(done);
    return {close_loop(value, loop, {before, word(state, 0)}, {step, next}),
            close_loop(digits, loop, {before, word(state, 0)}, {step, counted}),
            close_loop(rest, loop, {before, list}, {step, tail})};
}

// The small integer a proper list of 1 to `limit` decimal digits spells, after an optional minus sign; `slow` for
// any other word.
llvm::Value *small_number(ExpressionLowering &state, llvm::Value *list, llvm::BasicBlock *slow) {
    auto &builder = state.builder;
    auto *head = block(state, "lti.head");
    auto *start = block(state, "lti.start");
    auto *cons = compare(state, llvm::CmpInst::ICMP_EQ, builder.CreateAnd(list, word(state, abi::v1::primary_mask)),
                         word(state, abi::v1::list_tag));
    builder.CreateCondBr(cons, head, slow);
    builder.SetInsertPoint(head);
    auto *negative = compare(state, llvm::CmpInst::ICMP_EQ, inline_cons_word(state, list, 0), character(state, '-'));
    auto *digits = builder.CreateSelect(negative, inline_cons_word(state, list, 1), list);
    builder.CreateBr(start);
    builder.SetInsertPoint(start);
    const auto read = read_digits(state, digits, inline_digits(state.word->getBitWidth()));
    auto *ended = compare(state, llvm::CmpInst::ICMP_EQ, read.rest, word(state, abi::v1::empty_list));
    auto *some = compare(state, llvm::CmpInst::ICMP_NE, read.digits, word(state, 0));
    auto *made = block(state, "lti.made");
    builder.CreateCondBr(builder.CreateAnd(ended, some), made, slow);
    builder.SetInsertPoint(made);
    auto *number = builder.CreateSelect(negative, builder.CreateNeg(read.value), read.value);
    return builder.CreateOr(builder.CreateShl(number, abi::v1::small_integer_bits), word(state, TAG));
}

llvm::Value *list_to_integer(ExpressionLowering &state, llvm::Value *list,
                             llvm::function_ref<llvm::Value *()> builtin) {
    auto &builder = state.builder;
    auto *slow = block(state, "lti.builtin");
    auto *join = block(state, "lti.join");
    auto *number = small_number(state, list, slow);
    auto *made = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(slow);
    auto *called = builtin();
    auto *called_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {made, number}, {called_end, called});
}
} // namespace

llvm::Value *inline_integer_text(ExpressionLowering &state, const ast::Expression &expression, std::size_t builtin,
                                 std::span<llvm::Value *const> values, llvm::function_ref<llvm::Value *()> call) {
    if (values.size() != 1 || llvm::isa<llvm::Constant>(values[0])) {
        return nullptr;
    }
    if (builtin == INTEGER_TO_LIST && state.digit_prefixes.contains(&expression)) {
        state.deferred_digits.insert(&expression);
        return deferred_digits(state, values[0], call);
    }
    if (builtin == INTEGER_TO_LIST) {
        return digits_or(state, {values[0], word(state, abi::v1::empty_list)}, call);
    }
    return builtin == LIST_TO_INTEGER ? list_to_integer(state, values[0], call) : nullptr;
}

llvm::Value *lower_digit_prefix(ExpressionLowering &state, const ast::Expression &left, llvm::Value *prefix,
                                llvm::Value *right) {
    if (!state.deferred_digits.contains(&left)) {
        return nullptr;
    }
    // The deferred small integer's digits onto Right; the builtin's list (a bignum's) appended as usual, and so are
    // a small integer's digits when the heap block has no room.
    return digits_or(state, {prefix, right}, [&] {
        auto *small = block(state, "itl.full");
        auto *list = block(state, "itl.list");
        auto *join = block(state, "itl.appended");
        state.builder.CreateCondBr(is_small(state, prefix), small, list);
        state.builder.SetInsertPoint(small);
        auto *digits = lower_append(state, lower_builtin(state, INTEGER_TO_LIST, std::array{prefix}), right);
        auto *small_end = state.builder.GetInsertBlock();
        state.builder.CreateBr(join);
        state.builder.SetInsertPoint(list);
        auto *appended = lower_append(state, prefix, right);
        auto *list_end = state.builder.GetInsertBlock();
        state.builder.CreateBr(join);
        state.builder.SetInsertPoint(join);
        return join_values(state, state.word, {small_end, digits}, {list_end, appended});
    });
}
} // namespace clause::codegen
