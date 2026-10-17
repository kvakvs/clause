#include "../semantic/capabilities.hpp"
#include "../semantic/match_plan.hpp"
#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <clause/abi/maps.hpp>
#include <clause/abi/term.hpp>
#include <compare>
#include <llvm/IR/Module.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

// The exact-order rank of a key known at compile time (OPT0.3A): integer literals (numbers sort before atoms) and
// atom literals (by their characters); none for any other key.
struct KeyRank {
    bool atom;
    std::int64_t number;
    std::u32string name;

    auto operator<=>(const KeyRank &) const = default;
};

std::optional<KeyRank> key_rank(const ExpressionLowering &state, const ast::ExprId &key) {
    const auto &value = state.module.syntax->expression(semantic::ungroup(*state.module.syntax, key)).value;
    if (const auto *atom = std::get_if<ast::Atom>(&value)) {
        return KeyRank{true, 0, atom->name};
    }
    std::int64_t number = 0;
    if (const auto *character = std::get_if<ast::CharacterLiteral>(&value)) {
        return KeyRank{false, character->value, {}};
    }
    if (const auto *integer = std::get_if<ast::IntegerLiteral>(&value)) {
        const auto &digits = integer->value.decimal;
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), number);
        // Keys within 32-bit small integers rank the same on every target width.
        if (parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() && number >= -(1 << 27) &&
            number < (1 << 27)) {
            return KeyRank{false, number, {}};
        }
    }
    return std::nullopt;
}

// The fields of a map expression in exact key order with the last value of each key, when every key ranks at
// compile time; none otherwise.
std::optional<std::vector<std::size_t>> ordered_fields(const ExpressionLowering &state, const ast::MapExpression &map) {
    std::vector<std::pair<KeyRank, std::size_t>> ranks;
    ranks.reserve(map.fields.size());
    for (std::size_t i = 0; i < map.fields.size(); ++i) {
        auto rank = key_rank(state, map.fields[i].key);
        if (!rank) {
            return std::nullopt;
        }
        ranks.emplace_back(std::move(*rank), i);
    }
    std::ranges::stable_sort(ranks, {}, &std::pair<KeyRank, std::size_t>::first);
    std::vector<std::size_t> fields;
    for (std::size_t i = 0; i < ranks.size(); ++i) {
        if (i + 1 < ranks.size() && ranks[i + 1].first == ranks[i].first) {
            continue;
        }
        fields.push_back(ranks[i].second);
    }
    return fields;
}

// A map expression without a base whose keys rank at compile time, written inline in the heap block's free words
// (OPT0.3A): a classic map cell of its sorted key and value words. The service builds it when the heap has no room.
llvm::Value *inline_map(ExpressionLowering &state, std::span<llvm::Value *const> values,
                        const std::vector<std::size_t> &fields) {
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const llvm::Align align(state.word->getBitWidth() / 8);
    const auto words = 1 + 2 * fields.size();
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, words));
    auto *direct = llvm::BasicBlock::Create(context, "map.inline", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "map.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "map.join", &state.entry);
    builder.CreateCondBr(room.fits, direct, fallback);
    builder.SetInsertPoint(direct);
    builder.CreateAlignedStore(
        llvm::ConstantInt::get(
            state.word, ((words - 1) << abi::v1::header_count_shift) |
                            (static_cast<std::uint64_t>(abi::v1::HeaderKind::map) << abi::v1::header_kind_shift)),
        room.top, align);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        builder.CreateAlignedStore(values[2 * fields[i]], builder.CreateConstGEP1_64(state.word, room.top, 1 + 2 * i),
                                   align);
        builder.CreateAlignedStore(values[2 * fields[i] + 1],
                                   builder.CreateConstGEP1_64(state.word, room.top, 2 + 2 * i), align);
    }
    heap_commit(state, room);
    auto *built = builder.CreateOr(builder.CreatePtrToInt(room.top, state.word),
                                   llvm::ConstantInt::get(state.word, abi::v1::boxed_tag));
    mark_term(state, built);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *serviced = service(state, Op::make, values);
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {direct, built}, {fallback_end, serviced});
}

// A map expression without a base: inline when its keys rank at compile time and fit a classic map, else the service.
llvm::Value *new_map(ExpressionLowering &state, const ast::MapExpression &map, std::span<llvm::Value *const> values) {
    const auto fields = ordered_fields(state, map);
    if (fields && !fields->empty() && fields->size() <= 32) {
        return inline_map(state, values, *fields);
    }
    return service(state, Op::make, values);
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
        return new_map(state, map, values);
    }
    return inline_map_update(state, values,
                             [&] { return service(state, owned ? Op::update_owned : Op::update, values); });
}

llvm::Value *lower_map_query(ExpressionLowering &state, const abi::v1::ImmediateOperation operation, llvm::Value *left,
                             llvm::Value *right) {
    using Query = abi::v1::ImmediateOperation;
    switch (operation) {
    case Query::map_size:
        return inline_map_size(state, left, [&] { return service(state, Op::size, std::array{left}); });
    case Query::map_get:
        return inline_map_get(state, {right, left}, nullptr,
                              [&] { return service(state, Op::get, std::array{right, left}); });
    case Query::is_map_key:
        return inline_map_contains(state, {right, left},
                                   [&] { return service(state, Op::contains, std::array{right, left}); });
    default:
        return nullptr;
    }
}

llvm::Value *lower_map_pattern(ExpressionLowering &state, const semantic::MatchNode &node, llvm::Value *input,
                               llvm::BasicBlock *mismatch) {
    auto *saved = std::exchange(state.rejection, mismatch);
    llvm::Value *value = nullptr;
    if (node.operation == semantic::MatchOperation::map_shape) {
        value = inline_map_shape(state, input, mismatch) ? input : service(state, Op::test, std::array{input});
    } else {
        auto *key = lower_body(state, analyzed(node.key));
        value = inline_map_get(state, {input, key}, mismatch,
                               [&] { return service(state, Op::get, std::array{input, key}); });
    }
    state.rejection = saved;
    return value;
}
} // namespace clause::codegen
