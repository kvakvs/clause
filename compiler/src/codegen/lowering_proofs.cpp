#include "literals.hpp"
#include "lowering_state.hpp"
#include <clause/abi/equality.hpp>
#include <clause/abi/term.hpp>

namespace clause::codegen {
llvm::Value *cell_address(ExpressionLowering &state, llvm::Value *value, const unsigned tag) {
    if (auto *constant = llvm::dyn_cast<llvm::Constant>(value)) {
        if (auto *literal = literal_address(constant, tag)) {
            return literal;
        }
    }
    auto &builder = state.builder;
    return builder.CreateIntToPtr(builder.CreateSub(value, llvm::ConstantInt::get(state.word, tag)), builder.getPtrTy(),
                                  "proven.cell");
}

namespace {
llvm::Value *cell(ExpressionLowering &state, llvm::Value *value, const unsigned tag) {
    return cell_address(state, value, tag);
}

// Word `index` of the cell a proven word points at, rooted like a service output: kept in a term slot, so
// lower_frames treats it as a term and a collection rewrites it.
llvm::Value *word_at(ExpressionLowering &state, llvm::Value *value, const unsigned tag, const std::size_t index) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *slot = builder.CreateGEP(state.word, cell(state, value, tag), llvm::ConstantInt::get(state.word, index));
    auto *word = builder.CreateAlignedLoad(state.word, slot, align, "proven.word");
    mark_term(state, word);
    return word;
}
} // namespace

std::optional<Known> known_expression(const ExpressionLowering &state, const ast::Expression &expression) {
    if (!state.proofs) {
        return {};
    }
    const auto found = state.inferred.expressions.find(&expression);
    return found == state.inferred.expressions.end() ? std::nullopt : std::optional{Known{found->second.type}};
}

std::optional<Known> known_argument(const ExpressionLowering &state, const std::size_t index) {
    if (!state.proofs) {
        return {};
    }
    const auto found = state.inferred.inputs.find(&state.function);
    if (found == state.inferred.inputs.end() || index >= found->second.size()) {
        return {};
    }
    return Known{found->second[index]};
}

std::optional<SmallRange> small_expression(const ExpressionLowering &state, const ast::ExprId &id) {
    const auto known = known_expression(state, state.module.syntax->expression(id));
    return known ? state.proofs->small(known->fact) : std::nullopt;
}

llvm::Value *inline_cons_test(ExpressionLowering &state, llvm::Value *value) {
    auto &builder = state.builder;
    auto *primary = builder.CreateAnd(value, llvm::ConstantInt::get(state.word, abi::v1::primary_mask));
    return builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, primary,
                                                llvm::ConstantInt::get(state.word, abi::v1::list_tag)));
}

void inline_tuple_shape(ExpressionLowering &state, llvm::Value *value, const std::size_t arity,
                        llvm::BasicBlock *success, llvm::BasicBlock *mismatch) {
    auto &builder = state.builder;
    const auto equal = [&](llvm::Value *left, const std::uint64_t right) {
        return builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, left,
                                                    llvm::ConstantInt::get(state.word, right)));
    };
    if (arity == 0) {
        builder.CreateCondBr(equal(value, abi::v1::empty_tuple), success, mismatch);
        return;
    }
    auto *header = llvm::BasicBlock::Create(state.entry.getContext(), "match.tuple.header", &state.entry);
    builder.CreateCondBr(
        equal(builder.CreateAnd(value, llvm::ConstantInt::get(state.word, abi::v1::primary_mask)), abi::v1::boxed_tag),
        header, mismatch);
    builder.SetInsertPoint(header);
    // A tuple header is its element count past the kind bits, with kind 0.
    auto *word = builder.CreateAlignedLoad(state.word, cell(state, value, abi::v1::boxed_tag),
                                           llvm::Align(state.word->getBitWidth() / 8), "tuple.header");
    builder.CreateCondBr(equal(word, std::uint64_t{arity} << abi::v1::header_count_shift), success, mismatch);
}

llvm::Value *inline_cons_word(ExpressionLowering &state, llvm::Value *value, const std::size_t index) {
    return word_at(state, value, abi::v1::list_tag, index);
}

llvm::Value *inline_tuple_element(ExpressionLowering &state, llvm::Value *value, const std::size_t index) {
    return word_at(state, value, abi::v1::boxed_tag, index + 1);
}
} // namespace clause::codegen
