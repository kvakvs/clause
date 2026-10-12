#include "../semantic/match_plan.hpp"
#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <array>
#include <clause/abi/maps.hpp>
#include <clause/abi/term.hpp>
#include <llvm/IR/Module.h>
#include <utility>

namespace clause::codegen {
namespace {
using Op = abi::v1::MapOperation;

// Semantic map errors carry their checked offending term; root cleanup follows the shared error transport.
llvm::BasicBlock *rejection(ExpressionLowering &state, const ServiceOutput result) {
    if (state.rejection) {
        return state.rejection;
    }
    auto &builder = state.builder;
    auto *saved = builder.GetInsertBlock();
    auto &context = state.entry.getContext();
    auto *dispatch = llvm::BasicBlock::Create(context, "map.rejection", &state.entry);
    auto *bad_map = llvm::BasicBlock::Create(context, "map.badmap", &state.entry);
    auto *bad_key = llvm::BasicBlock::Create(context, "map.badkey", &state.entry);
    builder.SetInsertPoint(dispatch);
    auto *payload = builder.CreateAlignedLoad(state.word, result.slot, llvm::Align(state.word->getBitWidth() / 8));
    auto *test =
        builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, result.outcome,
                                             builder.getInt8(static_cast<std::uint8_t>(abi::v1::MapOutcome::bad_map))));
    builder.CreateCondBr(test, bad_map, bad_key);
    builder.SetInsertPoint(bad_map);
    raise_reason(state, abi::v1::ErrorReason::badmap, payload);
    builder.SetInsertPoint(bad_key);
    raise_reason(state, abi::v1::ErrorReason::badkey, payload);
    builder.SetInsertPoint(saved);
    return dispatch;
}

// Marshal source values into runtime-owned scratch roots, avoiding source-width native stack allocations.
llvm::Value *service(ExpressionLowering &state, Op operation, const std::span<llvm::Value *const> values) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *arguments = builder.CreateGEP(state.word, state.roots->buffer,
                                        llvm::ConstantInt::get(state.word, state.roots->next), "map.arguments");
    for (auto *value : values) {
        root_value(state, value);
    }
    auto *slot = root_slot(state);
    auto callee = output.getOrInsertFunction(
        services::symbol<services::Map>(output.getTargetTriple()),
        llvm::FunctionType::get(
            builder.getInt8Ty(),
            {builder.getPtrTy(), builder.getInt8Ty(), builder.getPtrTy(), state.word, builder.getPtrTy()}, false));
    auto *outcome = builder.CreateCall(callee,
                                       {state.entry.getArg(0), builder.getInt8(static_cast<std::uint8_t>(operation)),
                                        arguments, llvm::ConstantInt::get(state.word, values.size()), slot},
                                       "map.outcome");
    const ServiceOutput result{outcome, slot};
    return checked_value(state, result, rejection(state, result));
}
} // namespace

llvm::Value *lower_map_operation(ExpressionLowering &state, const Op operation,
                                 const std::span<llvm::Value *const> values) {
    return service(state, operation, values);
}

llvm::Value *lower_map(ExpressionLowering &state, const ast::MapExpression &map, bool owned) {
    std::vector<llvm::Value *> values;
    values.reserve(1 + 3 * map.fields.size());
    const auto read = [&](const ast::ExprId &id) { return state.values.at(&state.module.syntax->expression(id)); };
    if (map.base) {
        values.push_back(read(*map.base));
    }
    for (const auto &field : map.fields) {
        values.push_back(read(field.key));
        values.push_back(read(field.value));
        if (map.base) {
            values.push_back(llvm::ConstantInt::get(
                state.word, *abi::v1::IntegerEncoding<32>::encode(field.kind == ast::MapFieldKind::exact ? 1 : 0)));
        }
    }
    if (!map.base) {
        return service(state, Op::make, values);
    }
    return service(state, owned ? Op::update_owned : Op::update, values);
}

llvm::Value *lower_map_query(ExpressionLowering &state, const abi::v1::ImmediateOperation operation, llvm::Value *left,
                             llvm::Value *right) {
    using Query = abi::v1::ImmediateOperation;
    switch (operation) {
    case Query::map_size:
        return service(state, Op::size, std::array{left});
    case Query::map_get:
        return service(state, Op::get, std::array{right, left});
    case Query::is_map_key:
        return service(state, Op::contains, std::array{right, left});
    default:
        return nullptr;
    }
}

llvm::Value *lower_map_pattern(ExpressionLowering &state, const semantic::MatchNode &node, llvm::Value *input,
                               llvm::BasicBlock *mismatch) {
    auto *saved = std::exchange(state.rejection, mismatch);
    llvm::Value *value = nullptr;
    if (node.operation == semantic::MatchOperation::map_shape) {
        value = service(state, Op::test, std::array{input});
    } else {
        auto *key = lower_body(state, analyzed(node.key));
        value = service(state, Op::get, std::array{input, key});
    }
    state.rejection = saved;
    return value;
}
} // namespace clause::codegen
