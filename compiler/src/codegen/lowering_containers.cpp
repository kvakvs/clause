#include "literals.hpp"
#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <array>
#include <clause/abi/equality.hpp>
#include <clause/abi/frames.hpp>
#include <clause/abi/term.hpp>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>
#include <vector>

namespace clause::codegen {
namespace {
// Large constructors use bounded runtime scratch roots, keeping source width off the native stack.
llvm::Value *construct_service(ExpressionLowering &state, abi::v1::ContainerConstruction operation,
                               const std::span<llvm::Value *const> values) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *arguments = builder.CreateGEP(state.word, state.roots->buffer,
                                        llvm::ConstantInt::get(state.word, state.roots->next), "container.arguments");
    for (auto *value : values) {
        root_value(state, value);
    }
    auto *slot = root_slot(state);
    auto service = output.getOrInsertFunction(
        services::symbol<services::Construct>(output.getTargetTriple()),
        llvm::FunctionType::get(
            builder.getInt8Ty(),
            {builder.getPtrTy(), builder.getInt8Ty(), builder.getPtrTy(), state.word, builder.getPtrTy()}, false));
    auto *outcome = builder.CreateCall(service,
                                       {state.entry.getArg(0), builder.getInt8(static_cast<std::uint8_t>(operation)),
                                        arguments, llvm::ConstantInt::get(state.word, values.size()), slot},
                                       "container.outcome");
    return checked_value(state, {outcome, slot}, bad_argument_exit(state));
}

// Containers of at most this many words are built inline (OPT0.3); larger ones only through the service.
constexpr std::size_t INLINE_WORDS = 64;

// The words a tuple or a list of `values` (its tail last) takes on the heap.
std::size_t container_words(abi::v1::ContainerConstruction operation, std::size_t values) {
    return operation == abi::v1::ContainerConstruction::tuple ? values + 1 : 2 * (values - 1);
}

// Write a tuple or the cons cells of a list at `top`; its term word: the address tagged boxed or list.
llvm::Value *write_cells(ExpressionLowering &state, abi::v1::ContainerConstruction operation, llvm::Value *top,
                         const std::span<llvm::Value *const> values) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    const auto at = [&](std::size_t index) { return builder.CreateConstGEP1_64(state.word, top, index); };
    auto *address = builder.CreatePtrToInt(top, state.word);
    if (operation == abi::v1::ContainerConstruction::tuple) {
        builder.CreateAlignedStore(llvm::ConstantInt::get(state.word, values.size() << abi::v1::header_count_shift),
                                   at(0), align);
        for (std::size_t i = 0; i < values.size(); ++i) {
            builder.CreateAlignedStore(values[i], at(i + 1), align);
        }
        return builder.CreateOr(address, llvm::ConstantInt::get(state.word, abi::v1::boxed_tag));
    }
    const auto cells = values.size() - 1;
    for (std::size_t i = 0; i < cells; ++i) {
        builder.CreateAlignedStore(values[i], at(2 * i), align);
        auto *tail = i + 1 < cells ? builder.CreateOr(builder.CreatePtrToInt(at(2 * i + 2), state.word),
                                                      llvm::ConstantInt::get(state.word, abi::v1::list_tag))
                                   : values[cells];
        builder.CreateAlignedStore(tail, at(2 * i + 1), align);
    }
    return builder.CreateOr(address, llvm::ConstantInt::get(state.word, abi::v1::list_tag));
}

// Bump-allocate a small tuple or list in the heap block's free words that FrameState shows (OPT0.3); the service
// builds it when they do not suffice. The term is rooted like a service output, and always a new cell.
llvm::Value *construct_fresh(ExpressionLowering &state, abi::v1::ContainerConstruction operation,
                             const std::span<llvm::Value *const> values) {
    const auto words = container_words(operation, values.size());
    if (words > INLINE_WORDS) {
        return construct_service(state, operation, values);
    }
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const llvm::Align align(state.word->getBitWidth() / 8);
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, words));
    auto *direct = llvm::BasicBlock::Create(context, "heap.inline", &state.entry);
    auto *service = llvm::BasicBlock::Create(context, "heap.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "heap.join", &state.entry);
    auto *branch = builder.CreateCondBr(room.fits, direct, service);
    builder.SetInsertPoint(direct);
    auto *built = write_cells(state, operation, room.top, values);
    heap_commit(state, room);
    mark_term(state, built);
    builder.CreateBr(join);
    builder.SetInsertPoint(service);
    auto *serviced = construct_service(state, operation, values);
    auto *service_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    auto *word = join_values(state, state.word, {direct, built}, {service_end, serviced});
    // A cell whose word nothing reads once matches read its words is dropped (allocation sinking, OPT1.13).
    mark_cell(branch, word);
    // A list of several cells is known by its first cell only, whose tail word exists on the inline path alone.
    const bool tuple = operation == abi::v1::ContainerConstruction::tuple;
    if (tuple || values.size() == 2) {
        record_construction(state, word, tuple, values);
    }
    return word;
}

// A tuple or list: a module literal when it holds only immediates (literal heap), else a new cell.
llvm::Value *construct(ExpressionLowering &state, abi::v1::ContainerConstruction operation,
                       const std::span<llvm::Value *const> values) {
    if (operation == abi::v1::ContainerConstruction::reverse) {
        return construct_service(state, operation, values);
    }
    if (auto *literal = immediate_container(state, operation == abi::v1::ContainerConstruction::tuple, values)) {
        return literal;
    }
    return construct_fresh(state, operation, values);
}

// Values already exist in SSA after the common iterative expression walk visits every child.
std::vector<llvm::Value *> children(ExpressionLowering &state, const std::span<const ast::ExprId> ids) {
    std::vector<llvm::Value *> result;
    result.reserve(ids.size() + 1);
    for (const auto &id : ids) {
        result.push_back(state.values.at(&state.module.syntax->expression(id)));
    }
    return result;
}

// Strings are lists of decoded Unicode integers; no separate runtime string representation exists.
llvm::Value *string(ExpressionLowering &state, const ast::StringLiteral &literal) {
    if (literal.value.empty()) {
        return llvm::ConstantInt::get(state.word, abi::v1::empty_list);
    }
    std::vector<llvm::Value *> values;
    values.reserve(literal.value.size() + 1);
    for (const auto character : literal.value) {
        const auto encoded = *abi::v1::IntegerEncoding<32>::encode(static_cast<std::int64_t>(character));
        values.push_back(llvm::ConstantInt::get(state.word, encoded));
    }
    values.push_back(llvm::ConstantInt::get(state.word, abi::v1::empty_list));
    return construct(state, abi::v1::ContainerConstruction::list, values);
}
} // namespace

HeapRoom heap_room(ExpressionLowering &state, llvm::Value *words) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    const auto field = [&](abi::v1::StateField at) {
        return builder.CreateConstInBoundsGEP1_64(state.word, state.entry.getArg(0), static_cast<unsigned>(at));
    };
    auto *top = tag_memory(
        builder.CreateAlignedLoad(builder.getPtrTy(), field(abi::v1::StateField::heap_top), align, "heap.top"),
        MemoryKind::state);
    auto *limit = tag_memory(
        builder.CreateAlignedLoad(builder.getPtrTy(), field(abi::v1::StateField::heap_limit), align, "heap.limit"),
        MemoryKind::state);
    auto *end = builder.CreateGEP(state.word, top, words, "heap.end");
    auto *fits = builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_ULE,
                                                      builder.CreatePtrToInt(end, state.word),
                                                      builder.CreatePtrToInt(limit, state.word)));
    return {top, end, fits};
}

void heap_commit(ExpressionLowering &state, const HeapRoom &room) {
    tag_memory(state.builder.CreateAlignedStore(
                   room.end,
                   state.builder.CreateConstInBoundsGEP1_64(state.word, state.entry.getArg(0),
                                                            static_cast<unsigned>(abi::v1::StateField::heap_top)),
                   llvm::Align(state.word->getBitWidth() / 8)),
               MemoryKind::state);
}

void write_cons_tail(ExpressionLowering &state, llvm::Value *cell, llvm::Value *tail) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const llvm::Align align(state.word->getBitWidth() / 8);
    const auto field = [&](abi::v1::StateField at, const char *name) {
        auto *address =
            builder.CreateConstInBoundsGEP1_64(state.word, state.entry.getArg(0), static_cast<unsigned>(at));
        return builder.CreatePtrToInt(
            tag_memory(builder.CreateAlignedLoad(builder.getPtrTy(), address, align, name), MemoryKind::state),
            state.word);
    };
    auto *address = cell_address(state, cell, abi::v1::list_tag);
    auto *at = builder.CreatePtrToInt(address, state.word);
    const auto compare = [&](llvm::CmpInst::Predicate predicate, llvm::Value *right) {
        return builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, predicate, at, right));
    };
    auto *young = builder.CreateAnd(compare(llvm::CmpInst::ICMP_UGE, field(abi::v1::StateField::young, "heap.young")),
                                    compare(llvm::CmpInst::ICMP_ULT, field(abi::v1::StateField::heap_top, "heap.top")));
    auto *direct = llvm::BasicBlock::Create(context, "cons.tail.young", &state.entry);
    auto *service = llvm::BasicBlock::Create(context, "cons.tail.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "cons.tail.written", &state.entry);
    builder.CreateCondBr(young, direct, service, llvm::MDBuilder(context).createLikelyBranchWeights());
    builder.SetInsertPoint(direct);
    tag_memory(builder.CreateAlignedStore(tail, builder.CreateConstInBoundsGEP1_64(state.word, address, 1), align),
               MemoryKind::term);
    builder.CreateBr(join);
    builder.SetInsertPoint(service);
    auto &output = *state.entry.getParent();
    auto callee = output.getOrInsertFunction(
        services::symbol<services::ConsTail>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy(), state.word, state.word}, false));
    builder.CreateCall(callee, {state.entry.getArg(0), cell, tail});
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
}

void append_cell(ExpressionLowering &state, const ListBuild build, llvm::Value *value) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *empty = llvm::ConstantInt::get(state.word, abi::v1::empty_list);
    // A cell of immediates must not be a shared module literal: its tail changes.
    auto *cell = construct_fresh(state, abi::v1::ContainerConstruction::list,
                                 std::array{value, static_cast<llvm::Value *>(empty)});
    // The last cell is read after the new one is made: a collection there would have moved it.
    auto *last = builder.CreateAlignedLoad(state.word, build.last, align, "list.last");
    auto *first = llvm::BasicBlock::Create(context, "list.first", &state.entry);
    auto *later = llvm::BasicBlock::Create(context, "list.later", &state.entry);
    auto *linked = llvm::BasicBlock::Create(context, "list.linked", &state.entry);
    builder.CreateCondBr(
        builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, last, empty)), first,
        later);
    builder.SetInsertPoint(first);
    builder.CreateAlignedStore(cell, build.root, align);
    builder.CreateBr(linked);
    builder.SetInsertPoint(later);
    write_cons_tail(state, last, cell);
    builder.CreateBr(linked);
    builder.SetInsertPoint(linked);
    builder.CreateAlignedStore(cell, build.last, align);
}

llvm::Value *lower_tuple(ExpressionLowering &state, const std::span<llvm::Value *const> values) {
    return construct(state, abi::v1::ContainerConstruction::tuple, values);
}

llvm::Value *lower_list(ExpressionLowering &state, const std::span<llvm::Value *const> values) {
    return construct(state, abi::v1::ContainerConstruction::list, values);
}

llvm::Value *lower_reverse(ExpressionLowering &state, llvm::Value *list) {
    return construct(
        state, abi::v1::ContainerConstruction::reverse,
        std::array{list, static_cast<llvm::Value *>(llvm::ConstantInt::get(state.word, abi::v1::empty_list))});
}

llvm::Value *lower_container(ExpressionLowering &state, const ast::ExprValue &value) {
    if (const auto *tuple = std::get_if<ast::Tuple>(&value)) {
        if (tuple->elements.empty()) {
            return llvm::ConstantInt::get(state.word, abi::v1::empty_tuple);
        }
        return construct(state, abi::v1::ContainerConstruction::tuple, children(state, tuple->elements));
    }
    if (const auto *list = std::get_if<ast::List>(&value)) {
        if (list->elements.empty() && !list->tail) {
            return llvm::ConstantInt::get(state.word, abi::v1::empty_list);
        }
        auto values = children(state, list->elements);
        values.push_back(list->tail ? state.values.at(&state.module.syntax->expression(*list->tail))
                                    : llvm::ConstantInt::get(state.word, abi::v1::empty_list));
        return construct(state, abi::v1::ContainerConstruction::list, values);
    }
    if (const auto *literal = std::get_if<ast::StringLiteral>(&value)) {
        return string(state, *literal);
    }
    return nullptr;
}

llvm::Value *lower_inspection(ExpressionLowering &state, abi::v1::ContainerInspection operation, llvm::Value *value,
                              const std::size_t index, llvm::BasicBlock *mismatch) {
    return inline_inspection(state, operation, value, index, mismatch);
}
} // namespace clause::codegen
