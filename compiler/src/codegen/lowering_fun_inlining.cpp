#include "../semantic/capabilities.hpp"
#include "../semantic/funs.hpp"
#include "lowering_state.hpp"
#include <algorithm>
#include <llvm/Transforms/Utils/SSAUpdater.h>
#include <stdexcept>

// Inline funs (OPT1.8, docs/funs.md#inlined-funs): calls of an anonymous fun known where it is called lower the fun's
// clauses in place of the call; the fun is built only on the path where no clause matches.
namespace clause::codegen {
namespace {
// Inlined fun bodies nest at most this deep; deeper calls stay calls.
constexpr std::size_t MAX_INLINE_DEPTH = 4;
// A fun read more than once is inlined only when its clauses hold at most this many expressions.
constexpr std::size_t SMALL_LAMBDA = 40;

// Whether funs may be inlined here: at O2, outside binary and float loops (whose rests and parameters inlined reads
// would see unchanged), and not too deep.
bool inlining(const ExpressionLowering &state) {
    return state.lambdas && !state.binary_loop && !state.float_loop && state.inline_depth < MAX_INLINE_DEPTH;
}

// The entry of an anonymous fun that can be inlined: unnamed (a named fun reads itself) and with code of its own,
// which the path where no clause matches calls.
const semantic::FunEntry *inlinable(const semantic::Module &module, const ast::Expression &expression) {
    const auto *fun = std::get_if<ast::FunExpression>(&expression.value);
    if (!fun || fun->name) {
        return nullptr;
    }
    const auto &entry = semantic::fun_entry(module, expression);
    return entry.symbol.empty() || entry.self ? nullptr : &entry;
}

// Whether a fun's clauses hold at most SMALL_LAMBDA expressions.
bool small(const semantic::Module &module, const ast::Expression &fun) {
    std::vector<ast::ExprId> pending = semantic::expression_children(module, fun);
    std::size_t count = 0;
    while (!pending.empty() && count <= SMALL_LAMBDA) {
        const auto &expression = module.syntax->expression(pending.back());
        pending.pop_back();
        ++count;
        const auto children = semantic::expression_children(module, expression);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return count <= SMALL_LAMBDA;
}

struct BindingCounts {
    // The definition each variable expression makes, and the definitions and reads of each binding.
    std::map<const ast::Expression *, semantic::BindingId> defined;
    std::map<semantic::BindingId, std::size_t> definitions;
    std::map<semantic::BindingId, std::size_t> reads;
};

BindingCounts count_bindings(const ast::Module &syntax, const semantic::Function &function) {
    BindingCounts counts;
    for (const auto &binding : function.bindings) {
        if (binding.use == semantic::BindingUse::definition) {
            counts.defined.emplace(&syntax.expression(binding.expression), binding.identity);
            ++counts.definitions[binding.identity];
        } else if (binding.use == semantic::BindingUse::read) {
            ++counts.reads[binding.identity];
        }
    }
    return counts;
}

// Record `Variable = fun ... end` when the variable is bound only there and the fun is read once or small.
void bind_lambda(const semantic::Module &module, const ast::MatchExpression &match, const BindingCounts &counts,
                 LambdaBindings &result) {
    const auto &syntax = *module.syntax;
    const auto &fun = syntax.expression(semantic::ungroup(syntax, match.right));
    const auto defined = counts.defined.find(&syntax.expression(semantic::ungroup(syntax, match.left)));
    const auto *entry = inlinable(module, fun);
    if (!entry || defined == counts.defined.end() || counts.definitions.at(defined->second) != 1) {
        return;
    }
    const auto reads = counts.reads.find(defined->second);
    if ((reads != counts.reads.end() && reads->second == 1) || small(module, fun)) {
        result.emplace(defined->second, entry);
    }
}

// Lower one fun clause against `arguments`: its head and guard go to `mismatch` when they fail; its body's value.
llvm::Value *inline_clause(ExpressionLowering &state, const ast::FunctionClause &clause,
                           std::span<llvm::Value *const> arguments, llvm::BasicBlock *mismatch) {
    auto &context = state.entry.getContext();
    const auto plan =
        semantic::make_match_plan(state.module, state.function, clause,
                                  [](const Diagnostic &diagnostic) { throw std::invalid_argument(render(diagnostic)); },
                                  {.word_bits = state.word->getBitWidth()});
    if (!plan) {
        throw std::invalid_argument("lowering: unavailable inlined fun head plan");
    }
    auto *body = llvm::BasicBlock::Create(context, "inline.body", &state.entry);
    auto *guard = clause.guard ? llvm::BasicBlock::Create(context, "inline.guard", &state.entry) : body;
    lower_match_plan(state, *plan, arguments, guard, mismatch);
    if (clause.guard) {
        state.builder.SetInsertPoint(guard);
        lower_guard(state, *clause.guard, {.success = body, .rejection = mismatch});
    }
    state.builder.SetInsertPoint(body);
    llvm::Value *value = nullptr;
    for (const auto &expression : clause.body) {
        value = lower_body(state, expression);
    }
    return value;
}

// Join the values the clauses complete with at `join`; SSAUpdater adds a PHI only when they differ.
llvm::Value *join_clauses(ExpressionLowering &state, llvm::BasicBlock *join, const std::vector<BlockValue> &incoming) {
    state.builder.SetInsertPoint(join);
    // SSA formation inspects successors, so terminate the join until the surrounding expression resumes.
    auto *boundary = state.builder.CreateUnreachable();
    llvm::SmallVector<llvm::PHINode *, 2> phis;
    llvm::SSAUpdater updater(&phis);
    updater.Initialize(state.word, "inline.value");
    for (const auto &[block, value] : incoming) {
        updater.AddAvailableValue(block, value);
    }
    auto *value = updater.GetValueInMiddleOfBlock(join);
    for (auto *phi : phis) {
        phi->setDebugLoc(state.builder.getCurrentDebugLocation());
    }
    boundary->eraseFromParent();
    state.builder.SetInsertPoint(join);
    mark_term(state, value);
    return value;
}
} // namespace

LambdaBindings lambda_bindings(const semantic::Module &module, const semantic::Function &function) {
    const auto &syntax = *module.syntax;
    const auto counts = count_bindings(syntax, function);
    LambdaBindings result;
    auto pending = semantic::function_roots(std::get<ast::Function>(syntax.form(function.form).value));
    while (!pending.empty()) {
        const auto &expression = syntax.expression(pending.back());
        pending.pop_back();
        if (const auto *match = std::get_if<ast::MatchExpression>(&expression.value)) {
            bind_lambda(module, *match, counts, result);
        }
        const auto children = semantic::expression_children(module, expression);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return result;
}

KnownLambda known_lambda(const ExpressionLowering &state, const ast::ExprId &value, const std::size_t arity) {
    if (!inlining(state)) {
        return {};
    }
    const auto &syntax = *state.module.syntax;
    const auto &expression = syntax.expression(semantic::ungroup(syntax, value));
    KnownLambda known;
    if (std::holds_alternative<ast::FunExpression>(expression.value)) {
        known = {inlinable(state.module, expression), &syntax.expression(value)};
    } else if (const auto read = state.reads->find(&expression); read != state.reads->end()) {
        // Inside another fun's code, the fun's captured values may be out of reach: they are not that fun's.
        const auto bound = state.lambdas->find(read->second);
        const auto reachable = [&](const semantic::BindingId &identity) { return state.bindings.contains(identity); };
        known.lambda = bound == state.lambdas->end() || !std::ranges::all_of(bound->second->captures, reachable)
                           ? nullptr
                           : bound->second;
    }
    return known.lambda && known.lambda->arity == arity ? known : KnownLambda{};
}

const ast::Expression *sunk_fun(const ExpressionLowering &state, const ast::Expression &expression) {
    const auto *call = std::get_if<ast::CallExpression>(&expression.value);
    if (!call) {
        return nullptr;
    }
    if (semantic::fun_call(*state.module.syntax, *call)) {
        return known_lambda(state, call->target, call->arguments.size()).literal;
    }
    const auto *list = sunk_list_fun(state, expression, *call);
    return list ? list : sunk_map_fun(state, expression, *call);
}

llvm::Value *inline_lambda(ExpressionLowering &state, const semantic::FunEntry &lambda,
                           std::span<llvm::Value *const> arguments, llvm::function_ref<llvm::Value *()> fun) {
    auto &context = state.entry.getContext();
    const auto saved = state.bindings;
    auto *join = llvm::BasicBlock::Create(context, "inline.join", &state.entry);
    std::vector<BlockValue> incoming;
    ++state.inline_depth;
    for (const auto &clause : *semantic::fun_clauses(lambda.expression->value)) {
        state.bindings = saved;
        auto *next = llvm::BasicBlock::Create(context, "inline.next", &state.entry);
        auto *value = inline_clause(state, clause, arguments, next);
        incoming.emplace_back(state.builder.GetInsertBlock(), value);
        state.builder.CreateBr(join);
        state.builder.SetInsertPoint(next);
    }
    --state.inline_depth;
    state.bindings = saved;
    auto *mismatch = state.builder.GetInsertBlock();
    if (mismatch->use_empty()) {
        state.builder.ClearInsertionPoint();
        mismatch->eraseFromParent();
    } else {
        // No clause matched: calling the fun raises function_clause with the fun's own frame on top.
        auto *value = lower_apply_values(state, fun(), arguments);
        incoming.emplace_back(state.builder.GetInsertBlock(), value);
        state.builder.CreateBr(join);
    }
    return join_clauses(state, join, incoming);
}

llvm::Value *lower_known_fun_call(ExpressionLowering &state, const ast::CallExpression &call) {
    const auto known = known_lambda(state, call.target, call.arguments.size());
    if (!known.lambda) {
        return nullptr;
    }
    const auto &syntax = *state.module.syntax;
    std::vector<llvm::Value *> arguments;
    arguments.reserve(call.arguments.size());
    for (const auto &argument : call.arguments) {
        arguments.push_back(state.values.at(&syntax.expression(argument)));
    }
    return inline_lambda(state, *known.lambda, arguments, [&] {
        return known.literal ? lower_fun(state, *known.lambda->expression)
                             : state.values.at(&syntax.expression(call.target));
    });
}
} // namespace clause::codegen
