#include "lowering_state.hpp"
#include <array>
#include <clause/abi/builtins.hpp>
#include <clause/abi/equality.hpp>
#include <clause/abi/term.hpp>
#include <llvm/Transforms/Utils/SSAUpdater.h>

// Inline appends: `Left ++ Right` copies a short proper Left onto Right in generated code, bump-allocated like an
// inline list (OPT0.3); a longer or improper Left, or a full heap block, goes to the erlang:'++'/2 builtin.
namespace clause::codegen {
llvm::Instruction *loop_placeholder(ExpressionLowering &state, const char *name) {
    return llvm::cast<llvm::Instruction>(state.builder.CreateFreeze(llvm::PoisonValue::get(state.word), name));
}

llvm::Value *close_loop(llvm::Instruction *value, llvm::BasicBlock *head, BlockValue entry, BlockValue latch) {
    llvm::SSAUpdater updater;
    updater.Initialize(value->getType(), value->getName());
    updater.AddAvailableValue(entry.first, entry.second);
    updater.AddAvailableValue(latch.first, latch.second);
    auto *joined = updater.GetValueInMiddleOfBlock(head);
    value->replaceAllUsesWith(joined);
    value->eraseFromParent();
    return joined;
}

namespace {
// Cells of Left copied inline; a longer Left goes to the builtin, which works in portions.
constexpr std::uint64_t INLINE_APPEND = 32;

constexpr auto APPEND = abi::v1::find_bridge_builtin("erlang", "++", 2).value_or(0);
static_assert(abi::v1::find_bridge_builtin("erlang", "++", 2));

struct Append {
    // The operands of `Left ++ Right`.
    llvm::Value *left;
    llvm::Value *right;
};

struct Counted {
    // The cells of Left walked (at most INLINE_APPEND) and the word after the last: [] for a short proper list.
    llvm::Value *cells;
    llvm::Value *end;
};

// An integer comparison of two words, inserted at the builder.
llvm::Value *compare(ExpressionLowering &state, llvm::CmpInst::Predicate predicate, llvm::Value *left,
                     llvm::Value *right) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, predicate, left, right));
}

// Walk Left's spine while it is a cons cell, up to INLINE_APPEND cells.
Counted count_cells(ExpressionLowering &state, llvm::Value *left) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *word = state.word;
    auto *before = builder.GetInsertBlock();
    auto *loop = llvm::BasicBlock::Create(context, "append.count", &state.entry);
    auto *step = llvm::BasicBlock::Create(context, "append.step", &state.entry);
    auto *done = llvm::BasicBlock::Create(context, "append.counted", &state.entry);
    builder.CreateBr(loop);
    builder.SetInsertPoint(loop);
    auto *cell = loop_placeholder(state, "append.cell");
    auto *cells = loop_placeholder(state, "append.cells");
    auto *cons = compare(state, llvm::CmpInst::ICMP_EQ, builder.CreateAnd(cell, abi::v1::primary_mask),
                         llvm::ConstantInt::get(word, abi::v1::list_tag));
    auto *short_enough = compare(state, llvm::CmpInst::ICMP_ULT, cells, llvm::ConstantInt::get(word, INLINE_APPEND));
    builder.CreateCondBr(builder.CreateAnd(cons, short_enough), step, done);
    builder.SetInsertPoint(step);
    auto *tail = inline_cons_word(state, cell, 1);
    auto *counted = builder.CreateAdd(cells, llvm::ConstantInt::get(word, 1));
    builder.CreateBr(loop);
    builder.SetInsertPoint(done);
    // `done` leaves the loop head, so the walk ends with the head's values.
    auto *end = close_loop(cell, loop, {before, left}, {step, tail});
    return {close_loop(cells, loop, {before, llvm::ConstantInt::get(word, 0)}, {step, counted}), end};
}

// Copy the counted cons cells of Left to `top`, the last one's tail Right; the copy's list word.
llvm::Value *copy_cells(ExpressionLowering &state, const Append &append, const Counted &counted, llvm::Value *top) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *word = state.word;
    const llvm::Align align(word->getBitWidth() / 8);
    auto *before = builder.GetInsertBlock();
    auto *loop = llvm::BasicBlock::Create(context, "append.copy", &state.entry);
    auto *done = llvm::BasicBlock::Create(context, "append.copied", &state.entry);
    builder.CreateBr(loop);
    builder.SetInsertPoint(loop);
    auto *index = loop_placeholder(state, "append.index");
    auto *source = loop_placeholder(state, "append.source");
    auto *target = builder.CreateGEP(word, top, builder.CreateShl(index, 1));
    builder.CreateAlignedStore(inline_cons_word(state, source, 0), target, align);
    auto *next = builder.CreateAdd(index, llvm::ConstantInt::get(word, 1));
    auto *last = compare(state, llvm::CmpInst::ICMP_EQ, next, counted.cells);
    auto *following = builder.CreateOr(builder.CreatePtrToInt(builder.CreateConstGEP1_64(word, target, 2), word),
                                       llvm::ConstantInt::get(word, abi::v1::list_tag));
    builder.CreateAlignedStore(builder.CreateSelect(last, append.right, following),
                               builder.CreateConstGEP1_64(word, target, 1), align);
    auto *rest = inline_cons_word(state, source, 1);
    builder.CreateCondBr(last, done, loop);
    close_loop(index, loop, {before, llvm::ConstantInt::get(word, 0)}, {loop, next});
    close_loop(source, loop, {before, append.left}, {loop, rest});
    builder.SetInsertPoint(done);
    auto *copy = builder.CreateOr(builder.CreatePtrToInt(top, word), llvm::ConstantInt::get(word, abi::v1::list_tag));
    mark_term(state, copy);
    return copy;
}

// The inline result of a proper Left of the counted cells: Right itself for [], else the copy; none (the builtin's
// branch taken) when the heap block lacks room.
llvm::Value *direct_append(ExpressionLowering &state, const Append &append, const Counted &counted,
                           llvm::BasicBlock *builtin) {
    auto *cells = counted.cells;
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    auto *empty = builder.GetInsertBlock();
    auto *direct = llvm::BasicBlock::Create(context, "append.inline", &state.entry);
    auto *room_block = llvm::BasicBlock::Create(context, "append.room", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "append.made", &state.entry);
    builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_EQ, cells, llvm::ConstantInt::get(state.word, 0)), join,
                         direct);
    builder.SetInsertPoint(direct);
    const auto room = heap_room(state, builder.CreateShl(cells, 1));
    builder.CreateCondBr(room.fits, room_block, builtin);
    builder.SetInsertPoint(room_block);
    auto *copy = copy_cells(state, append, counted, room.top);
    heap_commit(state, room);
    auto *copied = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {empty, append.right}, {copied, copy});
}
} // namespace

llvm::Value *lower_append(ExpressionLowering &state, llvm::Value *left, llvm::Value *right) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const Append append{left, right};
    const auto counted = count_cells(state, left);
    auto *sized = llvm::BasicBlock::Create(context, "append.sized", &state.entry);
    auto *builtin = llvm::BasicBlock::Create(context, "append.builtin", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "append.join", &state.entry);
    // Only a proper list ends in [] within the walk; [] ++ Right is Right.
    auto *proper =
        compare(state, llvm::CmpInst::ICMP_EQ, counted.end, llvm::ConstantInt::get(state.word, abi::v1::empty_list));
    builder.CreateCondBr(proper, sized, builtin);
    builder.SetInsertPoint(sized);
    auto *made = direct_append(state, append, counted, builtin);
    auto *made_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(builtin);
    auto *called = lower_builtin(state, APPEND, std::array{left, right});
    auto *called_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    auto *result = join_values(state, state.word, {made_end, made}, {called_end, called});
    mark_term(state, result);
    return result;
}
} // namespace clause::codegen
