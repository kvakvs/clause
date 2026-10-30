#include "../semantic/capabilities.hpp"
#include "../semantic/funs.hpp"
#include "lowering_state.hpp"
#include <algorithm>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Module.h>
#include <set>
#include <stdexcept>

// Leaf inlining (OPT1.9, docs/specialization.md#leaf-inlining): at O2 a call of a small function of the same module
// that calls no Erlang code but other such leaves lowers the callee's clauses in place, with the caller's facts about
// the arguments; a call costs a frame push and a return through it, often more than the leaf's own work.
namespace clause::codegen {
namespace {
// Leaves nest at most this deep: a leaf may call leaves that call only call-free leaves (nested leaves).
constexpr std::size_t LEAF_DEPTH = 3;
// A leaf of at most this many expressions, its nested leaves included, is about the size of its call: inlining it
// spends no budget (leaf budget).
constexpr std::size_t FREE_LEAF = 12;

// Whether an expression may be part of a leaf: no code of its own (funs, comprehensions), no waiting or catching,
// and no transfer to Erlang code.
bool leaf_value(const ast::ExprValue &value) {
    return !semantic::fun_value(value) && !semantic::comprehension_qualifiers(value) &&
           !std::holds_alternative<ast::ReceiveExpression>(value) &&
           !std::holds_alternative<ast::TryExpression>(value) && !std::holds_alternative<ast::CatchExpression>(value) &&
           !std::holds_alternative<ast::MaybeExpression>(value);
}

// Whether a call in a leaf is a service that keeps the stack observable as it was: no apply, no raise (its stack
// trace names the leaf) and no process_info (current_function).
bool leaf_service(const semantic::Function &function, const ast::Expression &expression) {
    const auto service = function.services.find(&expression);
    if (service == function.services.end()) {
        return false;
    }
    const auto &resolution = service->second;
    return !resolution.apply() && resolution.operation != abi::v1::ImmediateOperation::raise &&
           resolution.identity.name != U"process_info";
}

// The live function of this module a local call (`f()` or `?MODULE:f()`) names; null for every other call.
const semantic::FunctionRef *local_callee(const ExpressionLowering &state, const ast::Expression &expression) {
    const auto callee = state.inferred.callees.find(&expression);
    if (callee == state.inferred.callees.end() || callee->second.module != &state.module ||
        !callee->second.function->live_) {
        return nullptr;
    }
    return &callee->second;
}

struct LeafWalk {
    // The lowering whose inference resolves calls, and the leaves being measured, outermost first (nested leaves).
    const ExpressionLowering &state;
    std::vector<const semantic::Function *> path;
};

std::optional<std::size_t> leaf_size(LeafWalk &walk, const semantic::Function &function);

// The expressions a call in leaf `function` adds: none for a service, a nested leaf's size; none (nullopt) when the
// call keeps `function` from being a leaf.
std::optional<std::size_t> call_size(LeafWalk &walk, const semantic::Function &function,
                                     const ast::Expression &expression) {
    if (leaf_service(function, expression)) {
        return 0;
    }
    const auto *callee = local_callee(walk.state, expression);
    return callee ? leaf_size(walk, *callee->function) : std::nullopt;
}

// The expressions of `function`'s clauses, those of the leaves it calls counted in, while all of them are leaf
// values and leaf calls and they stay within LEAF_BUDGET; none otherwise.
std::optional<std::size_t> clauses_size(LeafWalk &walk, const semantic::Function &function) {
    const auto &module = walk.state.module;
    auto pending = semantic::function_roots(std::get<ast::Function>(module.syntax->form(function.form).value));
    std::size_t size = 0;
    while (!pending.empty()) {
        const auto &expression = module.syntax->expression(pending.back());
        pending.pop_back();
        if (!leaf_value(expression.value)) {
            return std::nullopt;
        }
        const auto added = std::holds_alternative<ast::CallExpression>(expression.value)
                               ? call_size(walk, function, expression)
                               : std::optional<std::size_t>{0};
        if (!added) {
            return std::nullopt;
        }
        size += 1 + *added;
        if (size > LEAF_BUDGET) {
            return std::nullopt;
        }
        const auto children = semantic::expression_children(module, expression);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return size;
}

// The size of `function` as a leaf called at the end of the walk's path; none when it is no leaf there: nested too
// deep, recursive or too large.
std::optional<std::size_t> leaf_size(LeafWalk &walk, const semantic::Function &function) {
    if (walk.path.size() == LEAF_DEPTH || std::ranges::contains(walk.path, &function)) {
        return std::nullopt;
    }
    walk.path.push_back(&function);
    const auto size = clauses_size(walk, function);
    walk.path.pop_back();
    return size;
}

struct LeafCallee {
    // The leaf a call names and its size, its nested leaves counted in.
    const semantic::FunctionRef *callee;
    std::size_t size;
};

// The leaf a call names, where leaves are inlined (O2 gives a budget, outside binary and float loops); none for every
// other call.
std::optional<LeafCallee> leaf_callee(const ExpressionLowering &state, const ast::Expression &expression) {
    if (!state.leaf_budget || state.binary_loop || state.float_loop) {
        return std::nullopt;
    }
    const auto *callee = local_callee(state, expression);
    if (!callee) {
        return std::nullopt;
    }
    LeafWalk walk{state, {}};
    const auto size = leaf_size(walk, *callee->function);
    return size ? std::optional{LeafCallee{callee, *size}} : std::nullopt;
}

// Whether the function's leaf budget pays for a leaf of `size` expressions, which then spends it; a free leaf costs
// nothing (leaf budget).
bool afford(const ExpressionLowering &state, const std::size_t size) {
    if (size <= FREE_LEAF) {
        return true;
    }
    if (*state.leaf_budget < size) {
        return false;
    }
    *state.leaf_budget -= size;
    return true;
}

struct LeafSite {
    // The leaf called, the index of its reads, the argument values and what the caller knows about each.
    const semantic::Function &callee;
    const BindingReads &reads;
    std::vector<llvm::Value *> arguments;
    std::vector<std::optional<Known>> known;
    // What the site paid for the leaves the leaf calls, which their sites spend (nested leaves).
    std::size_t *budget;
};

// A lowering of the leaf's clause `index` that shares the caller's builder, frame slots, error exits and handler.
ExpressionLowering clause_state(const ExpressionLowering &state, const LeafSite &site, const std::size_t index) {
    ExpressionLowering inner{state.builder, state.entry, state.module, site.callee, state.inferred,
                             state.word,    {},          &site.reads,  index};
    inner.roots = state.roots;
    inner.failure = state.failure;
    inner.handler = state.handler;
    inner.bad_argument = state.bad_argument;
    inner.bad_arithmetic = state.bad_arithmetic;
    inner.system_limit = state.system_limit;
    inner.atom_owner = state.atom_owner;
    inner.proofs = state.proofs;
    inner.raw_floats = state.raw_floats;
    inner.float_loops = state.float_loops;
    inner.atom_literals = state.atom_literals;
    inner.literal_values = state.literal_values;
    inner.inline_depth = state.inline_depth + 1;
    inner.leaf_budget = site.budget;
    // Matches in the leaf read the words of the caller's constructions (OPT1.13).
    inner.constructions = state.constructions;
    inner.joins = state.joins;
    return inner;
}

// Take back the error exits and the value facts a leaf clause's lowering made.
void resume_caller(ExpressionLowering &state, ExpressionLowering &inner) {
    state.failure = inner.failure;
    state.bad_argument = inner.bad_argument;
    state.bad_arithmetic = inner.bad_arithmetic;
    state.system_limit = inner.system_limit;
    state.raw_floats.merge(inner.raw_floats);
    state.atom_literals.merge(inner.atom_literals);
    state.literal_values.merge(inner.literal_values);
    state.constructions.merge(inner.constructions);
    state.joins.merge(inner.joins);
}

// Lower clause `index` of the leaf against the arguments: its head and guard go to `mismatch` when they fail; its
// body's value.
llvm::Value *leaf_clause(ExpressionLowering &inner, const LeafSite &site, const ast::FunctionClause &clause,
                         llvm::BasicBlock *mismatch) {
    auto &context = inner.entry.getContext();
    const auto plan =
        semantic::make_match_plan(inner.module, site.callee,
                                  [](const Diagnostic &diagnostic) { throw std::invalid_argument(render(diagnostic)); },
                                  {.clause = inner.clause, .word_bits = inner.word->getBitWidth()});
    if (!plan) {
        throw std::invalid_argument("lowering: unavailable leaf head plan");
    }
    auto *body = llvm::BasicBlock::Create(context, "leaf.body", &inner.entry);
    auto *guard = clause.guard ? llvm::BasicBlock::Create(context, "leaf.guard", &inner.entry) : body;
    lower_match_plan(inner, *plan, site.arguments, guard, mismatch, site.known);
    if (clause.guard) {
        inner.builder.SetInsertPoint(guard);
        lower_guard(inner, *clause.guard, {.success = body, .rejection = mismatch});
    }
    inner.builder.SetInsertPoint(body);
    llvm::Value *value = nullptr;
    for (const auto &expression : clause.body) {
        value = lower_body(inner, expression);
    }
    return value;
}

struct InlineMark {
    // Where the leaf's code begins (the caller's block and its last instruction then, the function's last block)
    // and the call's location, which the leaf's locations are inlined at.
    llvm::BasicBlock *block;
    llvm::Instruction *last;
    llvm::BasicBlock *last_block;
    llvm::DebugLoc site;
};

InlineMark mark_inlining(const ExpressionLowering &state) {
    auto *block = state.builder.GetInsertBlock();
    return {block, block->empty() ? nullptr : &block->back(), &state.entry.back(),
            state.builder.getCurrentDebugLocation()};
}

// A location of the leaf's code once it is inlined at `site`, as LLVM's inliner would place it: the leaf's own lines,
// in the caller's scope while lowering, move to the leaf's scope; those of leaves inlined into it keep their scope,
// inlined at their relocated call (nested leaves).
llvm::DILocation *inlined(const llvm::DILocation *location, llvm::DISubprogram *leaf, llvm::DILocation *site) {
    auto &context = location->getContext();
    if (const auto *call = location->getInlinedAt()) {
        return llvm::DILocation::get(context, location->getLine(), location->getColumn(), location->getScope(),
                                     inlined(call, leaf, site));
    }
    llvm::DIScope *scope = leaf;
    if (const auto *file = llvm::dyn_cast<llvm::DILexicalBlockFile>(location->getScope())) {
        scope = llvm::DILexicalBlockFile::get(context, leaf, file->getFile(), 0);
    }
    return llvm::DILocation::get(context, location->getLine(), location->getColumn(), scope, site);
}

// Locate one instruction of the leaf in the leaf's own scope, inlined at the call.
void inline_location(llvm::Instruction &instruction, llvm::DISubprogram *leaf, const llvm::DebugLoc &site) {
    if (const auto *location = instruction.getDebugLoc().get()) {
        instruction.setDebugLoc(inlined(location, leaf, site.get()));
    }
}

// Relocate the instructions of `range`: those of an error exit the caller shares belong to the call, whichever code
// made it; the leaf's own are inlined at it.
template <typename Instructions>
void relocate(Instructions &&range, const bool shared, llvm::DISubprogram *leaf, const llvm::DebugLoc &site) {
    for (auto &instruction : range) {
        if (shared) {
            instruction.setDebugLoc(site);
        } else {
            inline_location(instruction, leaf, site);
        }
    }
}

// Debuggers show an inlined leaf as an inlined frame at its own lines: relocate what its lowering emitted since
// `mark`, then go on at the call's location.
void inline_locations(ExpressionLowering &state, const InlineMark &mark, const semantic::Function &callee) {
    const auto *function = state.entry.getParent()->getFunction(callee.symbol);
    auto *leaf = function ? function->getSubprogram() : nullptr;
    state.builder.SetCurrentDebugLocation(mark.site);
    if (!leaf || !mark.site || !state.entry.getSubprogram()) {
        return;
    }
    auto first = mark.last ? std::next(mark.last->getIterator()) : mark.block->begin();
    relocate(llvm::make_range(first, mark.block->end()), false, leaf, mark.site);
    const std::set<const llvm::BasicBlock *> shared{state.failure, state.bad_argument, state.bad_arithmetic,
                                                    state.system_limit};
    for (auto &block : llvm::make_range(std::next(mark.last_block->getIterator()), state.entry.end())) {
        relocate(block, shared.contains(&block), leaf, mark.site);
    }
}

// Lower every clause of the leaf in turn, each failing to the next; the values they complete with.
std::vector<BlockValue> inline_clauses(ExpressionLowering &state, const LeafSite &site, llvm::BasicBlock *join) {
    std::vector<BlockValue> incoming;
    const auto &clauses = std::get<ast::Function>(state.module.syntax->form(site.callee.form).value).clauses;
    for (std::size_t index = 0; index < clauses.size(); ++index) {
        auto *next = llvm::BasicBlock::Create(state.entry.getContext(), "leaf.next", &state.entry);
        auto inner = clause_state(state, site, index);
        auto *value = leaf_clause(inner, site, clauses[index], next);
        incoming.emplace_back(state.builder.GetInsertBlock(), value);
        state.builder.CreateBr(join);
        resume_caller(state, inner);
        state.builder.SetInsertPoint(next);
    }
    return incoming;
}
} // namespace

llvm::Value *lower_leaf_call(ExpressionLowering &state, const ast::Expression &expression,
                             const ast::CallExpression &call) {
    const auto leaf = leaf_callee(state, expression);
    if (!leaf || !afford(state, leaf->size)) {
        return nullptr;
    }
    const auto &syntax = *state.module.syntax;
    const auto &callee = *leaf->callee;
    const auto reads = read_bindings(state.module, *callee.function);
    auto nested = leaf->size;
    LeafSite site{*callee.function, reads, {}, {}, &nested};
    for (const auto &argument : call.arguments) {
        const auto &value = syntax.expression(argument);
        site.arguments.push_back(state.values.at(&value));
        site.known.push_back(known_expression(state, value));
    }
    const auto mark = mark_inlining(state);
    auto *join = llvm::BasicBlock::Create(state.entry.getContext(), "leaf.join", &state.entry);
    auto incoming = inline_clauses(state, site, join);
    inline_locations(state, mark, site.callee);
    auto *mismatch = state.builder.GetInsertBlock();
    if (mismatch->use_empty()) {
        state.builder.ClearInsertionPoint();
        mismatch->eraseFromParent();
    } else {
        // No clause matched: calling the function raises function_clause with its own frame on top. Its clauses reject
        // the same arguments, so the call never returns and the join has only the clauses' values.
        lower_named_call(state, callee, site.arguments);
        state.builder.CreateUnreachable();
    }
    return join_inlined(state, join, incoming);
}
} // namespace clause::codegen
