#include "../semantic/capabilities.hpp"
#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include "source_locations.hpp"
#include <clause/abi/builtins.hpp>
#include <clause/abi/equality.hpp>
#include <clause/abi/frames.hpp>
#include <clause/abi/term.hpp>
#include <llvm/IR/Module.h>
#include <utility>

namespace clause::codegen {
namespace {
enum class Source : std::uint8_t { list, binary, map };

struct Generator {
    // One generator of a qualifier and the kind of input it walks.
    const ast::Qualifier *part;
    Source source;
    bool strict;
    // Frame slots: the rest of a list or bitstring input, or a map's entries cell (MapOperation::entries) with its
    // next position and its size, and the map itself for error reports (OPT0.13).
    llvm::Value *slot = nullptr;
    llvm::Value *original = nullptr;
    llvm::Value *position = nullptr;
    llvm::Value *size = nullptr;
    // What this step reads: the rest of a list or bitstring, or a map and its position.
    llvm::Value *input = nullptr;
    llvm::Value *map = nullptr;
};

// A new block of the function being lowered.
llvm::BasicBlock *block(const ExpressionLowering &state, const char *name) {
    return llvm::BasicBlock::Create(state.entry.getContext(), name, &state.entry);
}

// Loop state lives in frame slots, so no SSA value crosses an iteration or an Erlang call.
llvm::Value *load(const ExpressionLowering &state, llvm::Value *slot, const char *name) {
    return state.builder.CreateAlignedLoad(state.word, slot, llvm::Align(state.word->getBitWidth() / 8), name);
}

void store(const ExpressionLowering &state, llvm::Value *value, llvm::Value *slot) {
    state.builder.CreateAlignedStore(value, slot, llvm::Align(state.word->getBitWidth() / 8));
}

// The expression's value, already lowered by the walk.
llvm::Value *value_of(const ExpressionLowering &state, const ast::ExprId &id) {
    return state.values.at(&state.module.syntax->expression(id));
}

// Compare two immediate words (empty list, small integers) without a runtime service.
llvm::Value *compare(const ExpressionLowering &state, const llvm::CmpInst::Predicate predicate, llvm::Value *left,
                     llvm::Value *right) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, predicate, left, right));
}

// Whether a type test (is_map, is_bitstring) holds for `value`.
llvm::Value *holds(ExpressionLowering &state, abi::v1::ImmediateOperation test, llvm::Value *value) {
    return lower_exact(state, lower_immediate(state, test, value), lower_atom(state, ast::Atom{U"true"}));
}

// Continue in a new block when `test` holds; otherwise branch to `otherwise`.
void require(ExpressionLowering &state, llvm::Value *test, llvm::BasicBlock *otherwise) {
    auto *next = block(state, "generator.test");
    state.builder.CreateCondBr(test, next, otherwise);
    state.builder.SetInsertPoint(next);
}

// The bridge index of erlang:self/0, which a loop head enters when the time slice is spent (OPT0.7): entering any
// function with no reductions left suspends the process, and on resumption self/0 just runs. erlang:yield/0 would
// spend the new slice at once and yield again on the next iteration.
constexpr auto YIELD = abi::v1::find_bridge_builtin("erlang", "self", 0).value_or(0);
static_assert(abi::v1::find_bridge_builtin("erlang", "self", 0));

llvm::Value *state_field(ExpressionLowering &state, abi::v1::StateField field) {
    return state.builder.CreateConstInBoundsGEP1_64(state.word, state.entry.getArg(0), static_cast<unsigned>(field));
}

// Continue in `next` after `taken`'s code where `condition` holds, directly elsewhere.
template <class Taken> void when(ExpressionLowering &state, llvm::Value *condition, const char *name, Taken taken) {
    auto *then = block(state, name);
    auto *next = block(state, "loop.continue");
    state.builder.CreateCondBr(condition, then, next);
    state.builder.SetInsertPoint(then);
    taken();
    state.builder.CreateBr(next);
    state.builder.SetInsertPoint(next);
}

// The kind of input a generator walks.
Source source(const ast::Qualifier &part) {
    if (std::holds_alternative<ast::BinaryGenerator>(part.value)) {
        return Source::binary;
    }
    return std::holds_alternative<ast::MapGenerator>(part.value) ? Source::map : Source::list;
}
} // namespace

// Every iteration starts at a loop head. It collects only when the heap wants a collection (FrameState::slow,
// OPT0.1): the heap may move there, so lower_frames reloads values read after it. Then it spends a reduction.
void loop_safepoint(ExpressionLowering &state) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *slow =
        builder.CreateAlignedLoad(state.word, state_field(state, abi::v1::StateField::slow), align, "loop.slow");
    auto *collect = builder.Insert(
        llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_NE,
                              builder.CreateAnd(slow, llvm::ConstantInt::get(state.word, abi::v1::slow_collect)),
                              llvm::ConstantInt::get(state.word, 0)));
    when(state, collect, "loop.collect", [&] {
        auto &output = *state.entry.getParent();
        auto service =
            output.getOrInsertFunction(services::symbol<services::Safepoint>(output.getTargetTriple()),
                                       llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()}, false));
        builder.CreateCall(service, {state.entry.getArg(0)});
    });
    loop_reduction(state);
}

// Spend one reduction; with none left, yield by entering erlang:self/0 (OPT0.7), itself a safepoint.
void loop_reduction(ExpressionLowering &state) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *field = state_field(state, abi::v1::StateField::reductions);
    auto *reductions = builder.CreateAlignedLoad(state.word, field, align, "loop.reductions");
    auto *spent = builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, reductions,
                                                       llvm::ConstantInt::get(state.word, 0)));
    auto *yield = block(state, "loop.yield");
    auto *spend = block(state, "loop.spend");
    auto *next = block(state, "loop.body");
    builder.CreateCondBr(spent, yield, spend);
    builder.SetInsertPoint(yield);
    lower_builtin(state, YIELD, {});
    builder.CreateBr(next);
    builder.SetInsertPoint(spend);
    builder.CreateAlignedStore(builder.CreateSub(reductions, llvm::ConstantInt::get(state.word, 1)), field, align);
    builder.CreateBr(next);
    builder.SetInsertPoint(next);
}

namespace {
// Before a map generator's loop: its input must be a map (else bad_generator); it starts at position 0.
void start_map(ExpressionLowering &state, Generator &generator, llvm::Value *input) {
    auto *bad = block(state, "generator.bad_map");
    require(state, holds(state, abi::v1::ImmediateOperation::is_map, input), bad);
    auto *ready = state.builder.GetInsertBlock();
    state.builder.SetInsertPoint(bad);
    raise_reason(state, abi::v1::ErrorReason::bad_generator, input);
    state.builder.SetInsertPoint(ready);
    generator.original = root_slot(state);
    store(state, input, generator.original);
    store(state, lower_map_operation(state, abi::v1::MapOperation::entries, std::array{input}), generator.slot);
    generator.size = root_slot(state);
    store(state, lower_map_operation(state, abi::v1::MapOperation::size, std::array{input}), generator.size);
    generator.position = root_slot(state);
    store(state, lower_integer(state, "0"), generator.position);
}

// Before the loop: keep the generator's input in its slot.
Generator start(ExpressionLowering &state, const ast::Qualifier &part) {
    Generator generator{&part, source(part), semantic::strict_generator(part), root_slot(state)};
    const auto &syntax = state.module.syntax->expression(semantic::qualifier_expression(part));
    auto *input = state.values.at(&syntax);
    store(state, input, generator.slot);
    if (generator.source == Source::map) {
        start_map(state, generator, input);
    }
    return generator;
}

// At the loop head: read this step's input.
void read(const ExpressionLowering &state, Generator &generator) {
    if (generator.source == Source::map) {
        generator.map = load(state, generator.slot, "generator.map");
        generator.input = load(state, generator.position, "generator.position");
    } else {
        generator.input = load(state, generator.slot, "generator.input");
    }
}

// Match `value` against one pattern of the generator, continuing in a new block.
void match(ExpressionLowering &state, const ast::PatternSyntaxId &pattern, llvm::Value *value,
           llvm::BasicBlock *mismatch) {
    auto *matched = block(state, "generator.matched");
    const auto plan = body_pattern_plan(state, semantic::pattern_root(*state.module.syntax, pattern));
    lower_match_plan(state, plan, std::array{value}, matched, mismatch);
    state.builder.SetInsertPoint(matched);
}

// The key and value at the map generator's position, read from its entries cell (OPT0.13): words 1 + 2i and 2 + 2i.
std::array<llvm::Value *, 2> entry(ExpressionLowering &state, const Generator &generator) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *cell = cell_address(state, generator.map, abi::v1::boxed_tag);
    auto *index = builder.CreateShl(builder.CreateAShr(generator.input, 4), 1);
    const auto word = [&](unsigned offset, const char *name) {
        auto *at =
            builder.CreateGEP(state.word, cell, builder.CreateAdd(index, llvm::ConstantInt::get(state.word, offset)));
        auto *loaded = builder.CreateAlignedLoad(state.word, at, align, name);
        mark_term(state, loaded);
        return static_cast<llvm::Value *>(loaded);
    };
    return {word(1, "generator.key"), word(2, "generator.value")};
}

// A bitstring element is the prefix its pattern matches; a skip pattern reads it without testing values.
llvm::Value *take_bits(ExpressionLowering &state, const Generator &generator, const bool skip,
                       llvm::BasicBlock *mismatch) {
    auto *matched = block(state, "generator.matched");
    const auto mode = skip ? semantic::GeneratorPattern::skip : semantic::GeneratorPattern::element;
    const auto pattern = semantic::generator_patterns(*generator.part).front();
    const auto plan = body_pattern_plan(state, semantic::pattern_root(*state.module.syntax, pattern), mode);
    const auto values = lower_match_plan(state, plan, std::array{generator.input}, matched, mismatch);
    state.builder.SetInsertPoint(matched);
    return values.at(analyzed(plan.rest));
}

// A map element is the key and value at the position, unless the map is exhausted.
llvm::Value *take_entry(ExpressionLowering &state, const Generator &generator, const bool skip,
                        llvm::BasicBlock *mismatch) {
    auto *size = load(state, generator.size, "generator.size");
    require(state, compare(state, llvm::CmpInst::ICMP_NE, generator.input, size), mismatch);
    if (!skip) {
        const auto patterns = semantic::generator_patterns(*generator.part);
        const auto [key, value] = entry(state, generator);
        match(state, patterns[0], key, mismatch);
        match(state, patterns[1], value, mismatch);
    }
    return lower_operation(state, abi::v1::ImmediateOperation::add, generator.input, lower_integer(state, "1"));
}

// Take the head of a list's cons cell inline, matching it unless `skip`; return the tail. Anything but a cons
// mismatches, as the inspection service did before cells were read inline for unproven inputs too.
llvm::Value *take_cell(ExpressionLowering &state, const Generator &generator, const bool skip,
                       llvm::BasicBlock *mismatch) {
    require(state, inline_cons_test(state, generator.input), mismatch);
    auto *rest = inline_cons_word(state, generator.input, 1);
    if (!skip) {
        match(state, semantic::generator_patterns(*generator.part).front(), inline_cons_word(state, generator.input, 0),
              mismatch);
    }
    return rest;
}

// Take one element, matching it unless `skip`; return what the input continues with, or go to `mismatch`.
llvm::Value *take(ExpressionLowering &state, const Generator &generator, const bool skip, llvm::BasicBlock *mismatch) {
    if (generator.source == Source::binary) {
        return take_bits(state, generator, skip, mismatch);
    }
    if (generator.source == Source::map) {
        return take_entry(state, generator, skip, mismatch);
    }
    return take_cell(state, generator, skip, mismatch);
}

// Take an element of every generator in step (matching only the strict ones when `skip_relaxed`) and advance them
// all; continue in the current block or go to `rejected`.
void take_all(ExpressionLowering &state, const std::vector<Generator> &generators, const bool skip_relaxed,
              llvm::BasicBlock *rejected) {
    std::vector<llvm::Value *> rests;
    rests.reserve(generators.size());
    for (const auto &generator : generators) {
        rests.push_back(take(state, generator, skip_relaxed && !generator.strict, rejected));
    }
    for (std::size_t i = 0; i < generators.size(); ++i) {
        const auto &generator = generators[i];
        store(state, rests[i], generator.source == Source::map ? generator.position : generator.slot);
    }
}

// Whether a generator has nothing left: an empty list, the end of a map, an empty bitstring for a strict
// generator and any bitstring (too short for the pattern) for a relaxed one.
llvm::Value *exhausted(ExpressionLowering &state, const Generator &generator) {
    if (generator.source == Source::list) {
        return compare(state, llvm::CmpInst::ICMP_EQ, generator.input,
                       llvm::ConstantInt::get(state.word, abi::v1::empty_list));
    }
    if (generator.source == Source::map) {
        return compare(state, llvm::CmpInst::ICMP_EQ, generator.input, load(state, generator.size, "generator.size"));
    }
    if (!generator.strict) {
        return holds(state, abi::v1::ImmediateOperation::is_bitstring, generator.input);
    }
    return lower_exact(state, generator.input, lower_bits_operation(state, abi::v1::BitOperation::make, {}));
}

// What a zip's bad_generators error shows for one generator: its rest, or OTP's iterator for a map.
llvm::Value *remaining(ExpressionLowering &state, const Generator &generator) {
    if (generator.source != Source::map) {
        return generator.input;
    }
    return lower_map_operation(state, abi::v1::MapOperation::iterator,
                               std::array{load(state, generator.original, "generator.original"), generator.input});
}

// The element a lone strict generator rejected: a list head, a bitstring's rest or a map's {Key, Value}; null for
// a relaxed generator. Anything that is no list or bitstring goes to `bad` instead.
llvm::Value *rejected_element(ExpressionLowering &state, const Generator &generator, llvm::BasicBlock *bad) {
    if (!generator.strict) {
        return nullptr;
    }
    if (generator.source == Source::list) {
        return lower_inspection(state, abi::v1::ContainerInspection::cons_head, generator.input, 0, bad);
    }
    if (generator.source == Source::binary) {
        require(state, holds(state, abi::v1::ImmediateOperation::is_bitstring, generator.input), bad);
        return generator.input;
    }
    return lower_tuple(state, entry(state, generator));
}

// A lone generator that is not exhausted after a rejection: a strict one raises {badmatch, Element}, and an input
// that is no list or bitstring raises bad_generator.
void lone_failure(ExpressionLowering &state, const Generator &generator) {
    auto *bad = block(state, "generator.bad");
    if (auto *element = rejected_element(state, generator, bad)) {
        raise_reason(state, abi::v1::ErrorReason::badmatch, element);
    } else {
        state.builder.CreateBr(bad);
    }
    state.builder.SetInsertPoint(bad);
    raise_reason(state, abi::v1::ErrorReason::bad_generator,
                 generator.source == Source::map ? load(state, generator.original, "generator.original")
                                                 : generator.input);
}

// Leave the loop for `next` once every generator is exhausted; anything else fails.
void finish_loop(ExpressionLowering &state, const std::vector<Generator> &generators, llvm::BasicBlock *next) {
    auto *bad = block(state, "generator.failed");
    for (const auto &generator : generators) {
        require(state, exhausted(state, generator), bad);
    }
    state.builder.CreateBr(next);
    state.builder.SetInsertPoint(bad);
    if (generators.size() == 1) {
        lone_failure(state, generators.front());
        return;
    }
    std::vector<llvm::Value *> inputs;
    inputs.reserve(generators.size());
    for (const auto &generator : generators) {
        inputs.push_back(remaining(state, generator));
    }
    raise_reason(state, abi::v1::ErrorReason::bad_generators, lower_tuple(state, inputs));
}

struct LoopEdges {
    // Where a skipped step takes the next element, and where the qualifier continues once the loop ends.
    llvm::BasicBlock *head;
    llvm::BasicBlock *next;
};

// A rejected step is skipped when every relaxed generator still has an element and every strict one matches it
// (a lone strict generator cannot match again); otherwise the loop ends or fails.
void reject(ExpressionLowering &state, const std::vector<Generator> &generators, const LoopEdges edges) {
    auto *ending = block(state, "generator.ending");
    if (generators.size() == 1 && generators.front().strict) {
        state.builder.CreateBr(ending);
    } else {
        take_all(state, generators, true, ending);
        state.builder.CreateBr(edges.head);
    }
    state.builder.SetInsertPoint(ending);
    finish_loop(state, generators, edges.next);
}

// The elements one step adds: list templates in order, the bitstring template, or {Key, Value} per map field.
std::vector<llvm::Value *> produced(ExpressionLowering &state, const Comprehension &comprehension) {
    std::vector<llvm::Value *> values;
    if (const auto *map = std::get_if<ast::MapComprehension>(comprehension.syntax)) {
        values.reserve(map->templates.size());
        for (const auto &field : map->templates) {
            values.push_back(lower_tuple(state, std::array{value_of(state, field.key), value_of(state, field.value)}));
        }
        return values;
    }
    const auto templates = semantic::comprehension_templates(*comprehension.syntax);
    values.reserve(templates.size());
    for (const auto &item : templates) {
        values.push_back(value_of(state, item));
    }
    if (std::holds_alternative<ast::BinaryComprehension>(*comprehension.syntax)) {
        auto *bad = block(state, "comprehension.badarg");
        require(state, holds(state, abi::v1::ImmediateOperation::is_bitstring, values.front()), bad);
        auto *ready = state.builder.GetInsertBlock();
        state.builder.SetInsertPoint(bad);
        raise_reason(state, abi::v1::ErrorReason::badarg);
        state.builder.SetInsertPoint(ready);
    }
    return values;
}
} // namespace

Comprehension begin_comprehension(ExpressionLowering &state, const ast::ExprValue &syntax) {
    auto *accumulator = root_slot(state);
    store(state, llvm::ConstantInt::get(state.word, abi::v1::empty_list), accumulator);
    auto *end = block(state, "comprehension.end");
    return {.syntax = &syntax, .bindings = state.bindings, .accumulator = accumulator, .next = end, .end = end};
}

void lower_generators(ExpressionLowering &state, Comprehension &comprehension,
                      const ast::ComprehensionQualifier &qualifier) {
    locate_source(state.builder, *state.module.syntax,
                  std::visit([](const auto &value) -> const ast::NodeSource & { return value.source; }, qualifier));
    const auto parts = semantic::zipped(qualifier);
    std::vector<Generator> generators;
    generators.reserve(parts.size());
    for (const auto &part : parts) {
        generators.push_back(start(state, part));
    }
    auto *head = block(state, "generator.next");
    state.builder.CreateBr(head);
    state.builder.SetInsertPoint(head);
    loop_safepoint(state);
    for (auto &generator : generators) {
        read(state, generator);
    }
    const auto before = state.bindings;
    auto *rejected = block(state, "generator.rejected");
    take_all(state, generators, false, rejected);
    auto *body = state.builder.GetInsertBlock();
    auto matched = std::move(state.bindings);
    state.bindings = before;
    state.builder.SetInsertPoint(rejected);
    reject(state, generators, {.head = head, .next = comprehension.next});
    state.bindings = std::move(matched);
    state.builder.SetInsertPoint(body);
    comprehension.next = head;
}

void lower_filter(ExpressionLowering &state, const Comprehension &comprehension, const ast::ExprId &filter) {
    const auto &expression = state.module.syntax->expression(filter);
    auto *pass = block(state, "filter.pass");
    if (state.function.guard_filters.contains(&expression)) {
        const ast::GuardSyntax guard{{{{filter}, expression.source}}, expression.source};
        lower_guard(state, guard, {.success = pass, .rejection = comprehension.next});
    } else {
        locate_source(state.builder, *state.module.syntax, expression.source);
        auto *value = state.values.at(&expression);
        auto *other = block(state, "filter.other");
        state.builder.CreateCondBr(lower_exact(state, value, lower_atom(state, ast::Atom{U"true"})), pass, other);
        state.builder.SetInsertPoint(other);
        auto *bad = block(state, "filter.bad");
        state.builder.CreateCondBr(lower_exact(state, value, lower_atom(state, ast::Atom{U"false"})),
                                   comprehension.next, bad);
        state.builder.SetInsertPoint(bad);
        raise_reason(state, abi::v1::ErrorReason::bad_filter, value);
    }
    state.builder.SetInsertPoint(pass);
}

void lower_templates(ExpressionLowering &state, const Comprehension &comprehension) {
    const auto values = produced(state, comprehension);
    std::vector<llvm::Value *> cells;
    cells.reserve(values.size() + 1);
    cells.assign(values.rbegin(), values.rend());
    cells.push_back(load(state, comprehension.accumulator, "comprehension.accumulator"));
    store(state, lower_list(state, cells), comprehension.accumulator);
    state.builder.CreateBr(comprehension.next);
}

llvm::Value *finish_comprehension(ExpressionLowering &state, Comprehension &comprehension) {
    state.builder.SetInsertPoint(comprehension.end);
    state.bindings = std::move(comprehension.bindings);
    auto *result = lower_reverse(state, load(state, comprehension.accumulator, "comprehension.accumulator"));
    if (std::holds_alternative<ast::BinaryComprehension>(*comprehension.syntax)) {
        return lower_bits_operation(state, abi::v1::BitOperation::concat, std::array{result});
    }
    if (std::holds_alternative<ast::MapComprehension>(*comprehension.syntax)) {
        return lower_map_operation(state, abi::v1::MapOperation::from_list, std::array{result});
    }
    return result;
}
} // namespace clause::codegen
