#include "../semantic/capabilities.hpp"
#include "lowering_state.hpp"
#include <algorithm>
#include <clause/abi/maps.hpp>
#include <clause/abi/term.hpp>
#include <optional>

// Map fold loops (OPT1.7, docs/funs.md#inlined-funs): maps:fold/3 called with a known anonymous fun becomes a loop in
// the calling function over the map's entries, with the fun inlined (inline funs, OPT1.8). A fun that returns its
// accumulator updated, `fun(K, V, Acc) -> Acc#{...} end`, updates it in place from the second step on (OPT3.15). The
// results and error reasons are those of Clause's maps module; only stack traces differ.
namespace clause::codegen {
namespace {
struct MapLoop {
    // The fun inlined and the resolved maps:fold/3, kept for the paths that make the real call.
    const semantic::FunEntry *lambda;
    semantic::FunctionRef callee;
    // The fun argument's value; null for a fun written in the call or sunk, which is built only where it is needed.
    llvm::Value *fun = nullptr;
    // Frame slots: the map, its entries cell, the next entry's word index (a small integer), the accumulator, whether
    // it is a map only this loop holds (an atom) and the result.
    llvm::Value *map = nullptr;
    llvm::Value *entries = nullptr;
    llvm::Value *index = nullptr;
    llvm::Value *accumulator = nullptr;
    llvm::Value *owned = nullptr;
    llvm::Value *result = nullptr;
    // Where the loop leaves with the result in its slot.
    llvm::BasicBlock *done = nullptr;
};

llvm::BasicBlock *block(const ExpressionLowering &state, const char *name) {
    return llvm::BasicBlock::Create(state.entry.getContext(), name, &state.entry);
}

llvm::Value *word(const ExpressionLowering &state, std::uint64_t value) {
    return llvm::ConstantInt::get(state.word, value);
}

// An i1 comparing two words, inserted as an instruction (IRBuilder's ICmp helpers trip a clang-analyzer false
// positive in LLVM's operand accessors).
llvm::Value *compare(ExpressionLowering &state, llvm::CmpInst::Predicate predicate, llvm::Value *left,
                     llvm::Value *right) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, predicate, left, right));
}

// Loop state lives in frame slots, so no SSA value crosses an iteration or an Erlang call.
llvm::Value *load(ExpressionLowering &state, llvm::Value *slot, const char *name) {
    auto *value = state.builder.CreateAlignedLoad(state.word, slot, llvm::Align(state.word->getBitWidth() / 8), name);
    mark_term(state, value);
    return value;
}

void store(const ExpressionLowering &state, llvm::Value *value, llvm::Value *slot) {
    state.builder.CreateAlignedStore(value, slot, llvm::Align(state.word->getBitWidth() / 8));
}

llvm::Value *fun_value(ExpressionLowering &state, const MapLoop &loop) {
    return loop.fun ? loop.fun : lower_fun(state, *loop.lambda->expression);
}

// Leave the loop with the real maps:fold/3 call, which handles iterators and raises its own errors.
void leave_by_call(ExpressionLowering &state, const MapLoop &loop) {
    const std::array values{fun_value(state, loop), load(state, loop.accumulator, "fold.accumulator"),
                            load(state, loop.map, "fold.map")};
    store(state, lower_named_call(state, loop.callee, values), loop.result);
    state.builder.CreateBr(loop.done);
}

// Clause's own maps:fold/3 when the call names it; none otherwise.
std::optional<semantic::FunctionRef> fold_callee(const ExpressionLowering &state, const ast::Expression &expression,
                                                 const ast::CallExpression &call) {
    const auto callee = state.inferred.callees.find(&expression);
    if (callee == state.inferred.callees.end() || state.function.services.contains(&expression) ||
        !callee->second.module->library_ || callee->second.module->name != U"maps" || call.arguments.size() != 3) {
        return std::nullopt;
    }
    const auto &key = callee->second.function->key;
    return key.name == U"fold" && key.arity == 3 ? std::optional{callee->second} : std::nullopt;
}

// The definition a variable expression makes, when the function records one there.
std::optional<semantic::BindingId> definition(const ExpressionLowering &state, const ast::Expression &variable) {
    for (const auto &binding : state.function.bindings) {
        if (binding.use == semantic::BindingUse::definition &&
            &state.module.syntax->expression(binding.expression) == &variable) {
            return binding.identity;
        }
    }
    return std::nullopt;
}

// Whether the definition is read exactly once, at `base`, and captured by no fun.
bool read_only_at(const ExpressionLowering &state, semantic::BindingId identity, const ast::Expression &base) {
    std::size_t reads = 0;
    bool at_base = false;
    for (const auto &binding : state.function.bindings) {
        if (binding.identity == identity && binding.use != semantic::BindingUse::definition) {
            ++reads;
            at_base = &state.module.syntax->expression(binding.expression) == &base;
        }
    }
    const auto captured = std::ranges::any_of(state.function.captures, [&](const auto &entry) {
        return std::ranges::find(entry.second, identity) != entry.second.end();
    });
    return reads == 1 && at_base && !captured;
}

// The map update a clause returns as the new accumulator: its last body expression updates the clause's accumulator
// parameter, a variable read nowhere else and captured by no fun; null otherwise.
const ast::Expression *accumulator_update(const ExpressionLowering &state, const ast::FunctionClause &clause) {
    const auto &syntax = *state.module.syntax;
    const auto &last = syntax.expression(semantic::ungroup(syntax, clause.body.back()));
    const auto *update = std::get_if<ast::MapExpression>(&last.value);
    const auto &parameter =
        syntax.expression(semantic::ungroup(syntax, semantic::pattern_root(syntax, clause.arguments.back())));
    const auto identity =
        std::holds_alternative<ast::Variable>(parameter.value) ? definition(state, parameter) : std::nullopt;
    if (!update || !update->base || !identity) {
        return nullptr;
    }
    return read_only_at(state, *identity, syntax.expression(semantic::ungroup(syntax, *update->base))) ? &last
                                                                                                       : nullptr;
}

// The accumulator updates of every clause, when each clause returns one; a fold loop then owns each step's result.
std::vector<const ast::Expression *> accumulator_updates(const ExpressionLowering &state,
                                                         const semantic::FunEntry &lambda) {
    std::vector<const ast::Expression *> updates;
    for (const auto &clause : *semantic::fun_clauses(lambda.expression->value)) {
        const auto *update = accumulator_update(state, clause);
        if (!update) {
            return {};
        }
        updates.push_back(update);
    }
    return updates;
}

// The loop's slots, filled with the call's arguments.
MapLoop begin(ExpressionLowering &state, const KnownLambda &known, semantic::FunctionRef callee,
              const ast::CallExpression &call) {
    const auto &syntax = *state.module.syntax;
    const auto value = [&](const ast::ExprId &id) { return state.values.at(&syntax.expression(id)); };
    MapLoop loop{known.lambda, callee};
    loop.fun = known.unlowered ? nullptr : value(call.arguments.front());
    loop.map = root_slot(state);
    store(state, value(call.arguments[2]), loop.map);
    loop.accumulator = root_slot(state);
    store(state, value(call.arguments[1]), loop.accumulator);
    loop.entries = root_slot(state);
    loop.index = root_slot(state);
    store(state, word(state, abi::v1::small_integer_tag), loop.index);
    loop.owned = fold_owned_slot(state);
    loop.result = root_slot(state);
    loop.done = block(state, "fold.done");
    return loop;
}

// A map continues with its entries cell in its slot; anything else (an iterator, a bad argument) leaves by the call.
void read_entries(ExpressionLowering &state, const MapLoop &loop, llvm::Value *map) {
    auto *other = block(state, "fold.other");
    inline_map_shape(state, map, other);
    store(state, lower_map_operation(state, abi::v1::MapOperation::entries, std::array{map}), loop.entries);
    auto *after = state.builder.GetInsertBlock();
    state.builder.SetInsertPoint(other);
    leave_by_call(state, loop);
    state.builder.SetInsertPoint(after);
}

struct Entry {
    llvm::Value *key;
    llvm::Value *value;
};

// A loop head at `head` over the entries: the next key and value words, with the index stored past them; the block
// reached when none is left.
Entry iterate(ExpressionLowering &state, const MapLoop &loop, llvm::BasicBlock *head, llvm::BasicBlock *end) {
    auto &builder = state.builder;
    builder.CreateBr(head);
    builder.SetInsertPoint(head);
    // Like the list loops, a map loop collects only where it yields and in the calls of its fun.
    loop_reduction(state);
    auto *entries = load(state, loop.entries, "fold.entries");
    auto *index = load(state, loop.index, "fold.index");
    auto *at = builder.CreateLShr(index, word(state, abi::v1::small_integer_bits));
    auto *cell = cell_address(state, entries, abi::v1::boxed_tag);
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *header = builder.CreateAlignedLoad(state.word, cell, align, "fold.header");
    auto *words = builder.CreateLShr(header, word(state, abi::v1::header_count_shift));
    auto *step = block(state, "fold.step");
    builder.CreateCondBr(compare(state, llvm::CmpInst::ICMP_ULT, at, words), step, end);
    builder.SetInsertPoint(step);
    auto *key = builder.CreateAlignedLoad(
        state.word, builder.CreateGEP(state.word, cell, builder.CreateAdd(at, word(state, 1))), align, "fold.key");
    auto *value = builder.CreateAlignedLoad(
        state.word, builder.CreateGEP(state.word, cell, builder.CreateAdd(at, word(state, 2))), align, "fold.value");
    mark_term(state, key);
    mark_term(state, value);
    store(state, builder.CreateAdd(index, word(state, 2 << abi::v1::small_integer_bits)), loop.index);
    return {key, value};
}

void fold_step(ExpressionLowering &state, const MapLoop &loop, const Entry &entry) {
    const std::array arguments{entry.key, entry.value, load(state, loop.accumulator, "fold.accumulator")};
    store(state, apply_fold(state, *loop.lambda, arguments, loop.owned, [&] { return fun_value(state, loop); }),
          loop.accumulator);
}
} // namespace

llvm::Value *fold_owned_slot(ExpressionLowering &state) {
    auto *slot = root_slot(state);
    store(state, word(state, abi::v1::false_atom), slot);
    return slot;
}

llvm::Value *apply_fold(ExpressionLowering &state, const semantic::FunEntry &lambda,
                        std::span<llvm::Value *const> arguments, llvm::Value *owned,
                        llvm::function_ref<llvm::Value *()> fun) {
    const auto updates = accumulator_updates(state, lambda);
    for (const auto *update : updates) {
        state.owned_maps[update] = owned;
    }
    auto *accumulated = inline_lambda(state, lambda, arguments, fun);
    for (const auto *update : updates) {
        state.owned_maps.erase(update);
    }
    if (!updates.empty()) {
        store(state, word(state, abi::v1::true_atom), owned);
    }
    return accumulated;
}

llvm::Value *owned_map_update(ExpressionLowering &state, const ast::Expression &update) {
    if (state.function.owned_map_updates.contains(&update)) {
        return state.builder.getTrue();
    }
    const auto found = state.owned_maps.find(&update);
    if (found == state.owned_maps.end()) {
        return nullptr;
    }
    auto *owned = state.builder.CreateAlignedLoad(state.word, found->second, llvm::Align(state.word->getBitWidth() / 8),
                                                  "map.owned");
    return compare(state, llvm::CmpInst::ICMP_EQ, owned, word(state, abi::v1::true_atom));
}

const ast::Expression *sunk_map_fun(const ExpressionLowering &state, const ast::Expression &expression,
                                    const ast::CallExpression &call) {
    return fold_callee(state, expression, call) ? known_lambda(state, call.arguments.front(), 3).unlowered : nullptr;
}

llvm::Value *lower_map_fold(ExpressionLowering &state, const ast::Expression &expression,
                            const ast::CallExpression &call) {
    const auto callee = fold_callee(state, expression, call);
    const auto known = callee ? known_lambda(state, call.arguments.front(), 3) : KnownLambda{};
    auto *map = known.lambda ? state.values.at(&state.module.syntax->expression(call.arguments[2])) : nullptr;
    // An immediate is never a map: the real call raises.
    if (!map || llvm::isa<llvm::ConstantInt>(map)) {
        return nullptr;
    }
    const auto loop = begin(state, known, *callee, call);
    read_entries(state, loop, map);
    auto *head = block(state, "fold.next");
    auto *end = block(state, "fold.end");
    const auto entry = iterate(state, loop, head, end);
    fold_step(state, loop, entry);
    state.builder.CreateBr(head);
    state.builder.SetInsertPoint(end);
    store(state, load(state, loop.accumulator, "fold.accumulator"), loop.result);
    state.builder.CreateBr(loop.done);
    state.builder.SetInsertPoint(loop.done);
    return load(state, loop.result, "fold.result");
}
} // namespace clause::codegen
