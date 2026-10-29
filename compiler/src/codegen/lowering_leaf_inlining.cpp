#include "../semantic/capabilities.hpp"
#include "../semantic/funs.hpp"
#include "lowering_state.hpp"
#include <algorithm>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Module.h>
#include <set>
#include <stdexcept>

// Leaf inlining (OPT1.9, docs/specialization.md#leaf-inlining): at O2 a call of a small function of the same module
// that calls no Erlang code lowers the callee's clauses in place, with the caller's facts about the arguments; a call
// costs a frame push and a return through it, often more than the leaf's own work.
namespace clause::codegen {
namespace {
// A leaf holds at most this many expressions over all its clauses.
constexpr std::size_t LEAF_EXPRESSIONS = 24;

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
bool leaf_call(const semantic::Function &function, const ast::Expression &expression) {
    const auto service = function.services.find(&expression);
    if (service == function.services.end()) {
        return false;
    }
    const auto &resolution = service->second;
    return !resolution.apply() && resolution.operation != abi::v1::ImmediateOperation::raise &&
           resolution.identity.name != U"process_info";
}

// Whether `function` is a leaf: at most LEAF_EXPRESSIONS expressions, all of them leaf values and leaf calls.
bool leaf(const semantic::Module &module, const semantic::Function &function) {
    const auto &form = std::get<ast::Function>(module.syntax->form(function.form).value);
    auto pending = semantic::function_roots(form);
    std::size_t count = 0;
    while (!pending.empty()) {
        const auto &expression = module.syntax->expression(pending.back());
        pending.pop_back();
        const bool call = std::holds_alternative<ast::CallExpression>(expression.value);
        if (++count > LEAF_EXPRESSIONS || !leaf_value(expression.value) || (call && !leaf_call(function, expression))) {
            return false;
        }
        const auto children = semantic::expression_children(module, expression);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return true;
}

// The leaf a call names: a local call of a live function of this module, made where leaves are inlined (O2, which
// sets `lambdas`, outside binary and float loops); null otherwise.
const semantic::FunctionRef *leaf_callee(const ExpressionLowering &state, const ast::Expression &expression,
                                         const ast::CallExpression &call) {
    const auto &syntax = *state.module.syntax;
    const auto callee = state.inferred.callees.find(&expression);
    if (!state.lambdas || state.binary_loop || state.float_loop || callee == state.inferred.callees.end() ||
        callee->second.module != &state.module || !callee->second.function->live_ ||
        !std::holds_alternative<ast::Atom>(syntax.expression(semantic::ungroup(syntax, call.target)).value)) {
        return nullptr;
    }
    return leaf(state.module, *callee->second.function) ? &callee->second : nullptr;
}

struct LeafSite {
    // The leaf called, the index of its reads, the argument values and what the caller knows about each.
    const semantic::Function &callee;
    const BindingReads &reads;
    std::vector<llvm::Value *> arguments;
    std::vector<std::optional<Known>> known;
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
    inner.atom_literals = state.atom_literals;
    inner.literal_values = state.literal_values;
    inner.inline_depth = state.inline_depth + 1;
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

// Locate one instruction of the leaf in the leaf's own scope, inlined at the call, as LLVM's inliner would. Nothing
// is inlined yet while lowering, so every location is the caller's own.
void inline_location(llvm::Instruction &instruction, llvm::DISubprogram *leaf, const llvm::DebugLoc &site) {
    const auto &location = instruction.getDebugLoc();
    if (!location) {
        return;
    }
    auto &context = instruction.getContext();
    llvm::DIScope *scope = leaf;
    if (const auto *file = llvm::dyn_cast<llvm::DILexicalBlockFile>(location.getScope())) {
        scope = llvm::DILexicalBlockFile::get(context, leaf, file->getFile(), 0);
    }
    instruction.setDebugLoc(llvm::DILocation::get(context, location.getLine(), location.getCol(), scope, site));
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
    const auto *callee = leaf_callee(state, expression, call);
    if (!callee) {
        return nullptr;
    }
    const auto &syntax = *state.module.syntax;
    const auto reads = read_bindings(state.module, *callee->function);
    LeafSite site{*callee->function, reads, {}, {}};
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
        // No clause matched: calling the function raises function_clause with its own frame on top.
        auto *value = lower_named_call(state, *callee, site.arguments);
        incoming.emplace_back(state.builder.GetInsertBlock(), value);
        state.builder.CreateBr(join);
    }
    return join_inlined(state, join, incoming);
}
} // namespace clause::codegen
