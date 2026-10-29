#include "../semantic/capabilities.hpp"
#include "../semantic/funs.hpp"
#include "lowering_state.hpp"
#include <algorithm>
#include <clause/abi/equality.hpp>
#include <iterator>
#include <llvm/Transforms/Utils/SSAUpdater.h>
#include <set>
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
    // The definition each variable expression makes, the definitions and exact checks of each binding, and its reads.
    std::map<const ast::Expression *, semantic::BindingId> defined;
    std::map<semantic::BindingId, std::size_t> definitions;
    std::map<semantic::BindingId, std::size_t> checks;
    std::map<semantic::BindingId, std::vector<const ast::Expression *>> reads;
};

BindingCounts count_bindings(const ast::Module &syntax, const semantic::Function &function) {
    BindingCounts counts;
    for (const auto &binding : function.bindings) {
        const auto *expression = &syntax.expression(binding.expression);
        if (binding.use == semantic::BindingUse::definition) {
            counts.defined.emplace(expression, binding.identity);
            ++counts.definitions[binding.identity];
        } else if (binding.use == semantic::BindingUse::read) {
            counts.reads[binding.identity].push_back(expression);
        } else {
            ++counts.checks[binding.identity];
        }
    }
    return counts;
}

struct SinkFacts {
    // Sunk bound funs (OPT1.8): expressions whose value nothing reads (every body expression but the last), reads
    // that call a fun or pass it first to a lists or maps call, and the variables some fun captures.
    std::set<const ast::Expression *> statements;
    std::set<const ast::Expression *> call_reads;
    std::set<semantic::BindingId> captured;
};

// Record every expression of `body` but the last as a statement.
void add_statements(const ast::Module &syntax, const std::vector<ast::ExprId> &body, SinkFacts &facts) {
    for (std::size_t index = 0; index + 1 < body.size(); ++index) {
        facts.statements.insert(&syntax.expression(body[index]));
    }
}

// The bodies of a block, a try, a fun and the clauses of a case, if, receive, try or maybe.
std::vector<const std::vector<ast::ExprId> *> bodies(const ast::ExprValue &value) {
    std::vector<const std::vector<ast::ExprId> *> result;
    for (const auto &branch : semantic::branch_clauses(value)) {
        result.push_back(branch.body);
    }
    if (const auto *clauses = semantic::fun_clauses(value)) {
        std::ranges::transform(*clauses, std::back_inserter(result), [](const auto &clause) { return &clause.body; });
    }
    if (const auto *block = std::get_if<ast::BlockExpression>(&value)) {
        result.push_back(&block->body);
    } else if (const auto *attempt = std::get_if<ast::TryExpression>(&value)) {
        result.push_back(&attempt->body);
    }
    return result;
}

// Whether a call is lists:F(...) or maps:F(...), whose fun argument comes first.
bool fun_loop_call(const ast::Module &syntax, const ast::CallExpression &call) {
    const auto *remote =
        std::get_if<ast::RemoteExpression>(&syntax.expression(semantic::ungroup(syntax, call.target)).value);
    const auto *name =
        remote ? std::get_if<ast::Atom>(&syntax.expression(semantic::ungroup(syntax, remote->module)).value) : nullptr;
    return name && (name->name == U"lists" || name->name == U"maps") && !call.arguments.empty();
}

// Record the statements of an expression's bodies and the reads its call may inline.
void add_facts(const ast::Module &syntax, const ast::Expression &expression, SinkFacts &facts) {
    for (const auto *body : bodies(expression.value)) {
        add_statements(syntax, *body, facts);
    }
    const auto *call = std::get_if<ast::CallExpression>(&expression.value);
    if (call && semantic::fun_call(syntax, *call)) {
        facts.call_reads.insert(&syntax.expression(semantic::ungroup(syntax, call->target)));
    } else if (call && fun_loop_call(syntax, *call)) {
        facts.call_reads.insert(&syntax.expression(semantic::ungroup(syntax, call->arguments.front())));
    }
}

// Whether the closure of a match `F = fun ... end` binding `identity` can be built only where a read needs it.
bool sinkable(const ast::Expression &match, const semantic::BindingId &identity, const BindingCounts &counts,
              const SinkFacts &facts) {
    const auto reads = counts.reads.find(identity);
    const auto called = [&](const ast::Expression *read) { return facts.call_reads.contains(read); };
    return facts.statements.contains(&match) && !counts.checks.contains(identity) &&
           !facts.captured.contains(identity) &&
           (reads == counts.reads.end() || std::ranges::all_of(reads->second, called));
}

// Record `Variable = fun ... end` when the variable is bound only there and the fun is read once or small.
void bind_lambda(const semantic::Module &module, const ast::Expression &expression, const BindingCounts &counts,
                 const SinkFacts &facts, LambdaBindings &result) {
    const auto &syntax = *module.syntax;
    const auto &match = std::get<ast::MatchExpression>(expression.value);
    const auto &fun = syntax.expression(semantic::ungroup(syntax, match.right));
    const auto defined = counts.defined.find(&syntax.expression(semantic::ungroup(syntax, match.left)));
    const auto *entry = inlinable(module, fun);
    if (!entry || defined == counts.defined.end() || counts.definitions.at(defined->second) != 1) {
        return;
    }
    const auto reads = counts.reads.find(defined->second);
    if ((reads != counts.reads.end() && reads->second.size() == 1) || small(module, fun)) {
        result.funs.emplace(defined->second, entry);
        if (sinkable(expression, defined->second, counts, facts)) {
            result.sunk.emplace(&expression, defined->second);
        }
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

// The variable of a sunk binding's match when funs may be inlined here (sunk bound funs, OPT1.8); null otherwise.
const semantic::BindingId *sunk_match(const ExpressionLowering &state, const ast::Expression &expression) {
    if (!inlining(state)) {
        return nullptr;
    }
    const auto found = state.lambdas->sunk.find(&expression);
    return found == state.lambdas->sunk.end() ? nullptr : &found->second;
}

// The fun a variable read `read` holds when bound once to one; `read` itself is not lowered when its binding is sunk.
KnownLambda bound_lambda(const ExpressionLowering &state, const semantic::BindingId &variable,
                         const ast::Expression &read) {
    // Inside another fun's code, the fun's captured values may be out of reach: they are not that fun's.
    const auto bound = state.lambdas->funs.find(variable);
    const auto reachable = [&](const semantic::BindingId &identity) { return state.bindings.contains(identity); };
    if (bound == state.lambdas->funs.end() || !std::ranges::all_of(bound->second->captures, reachable)) {
        return {};
    }
    return {bound->second, state.sunk_lambdas.contains(variable) ? &read : nullptr};
}
} // namespace

llvm::Value *join_inlined(ExpressionLowering &state, llvm::BasicBlock *join, const std::vector<BlockValue> &incoming) {
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
    // Matches of the value answer from the clauses' constructions (OPT1.13).
    record_join(state, value, join, incoming);
    return value;
}

LambdaBindings lambda_bindings(const semantic::Module &module, const semantic::Function &function) {
    const auto &syntax = *module.syntax;
    const auto counts = count_bindings(syntax, function);
    SinkFacts facts;
    for (const auto &fun : module.funs) {
        if (fun.owner == &function) {
            facts.captured.insert(fun.captures.begin(), fun.captures.end());
        }
    }
    const auto &form = std::get<ast::Function>(syntax.form(function.form).value);
    for (const auto &clause : form.clauses) {
        add_statements(syntax, clause.body, facts);
    }
    std::vector<const ast::Expression *> matches;
    auto pending = semantic::function_roots(form);
    while (!pending.empty()) {
        const auto &expression = syntax.expression(pending.back());
        pending.pop_back();
        if (std::holds_alternative<ast::MatchExpression>(expression.value)) {
            matches.push_back(&expression);
        }
        add_facts(syntax, expression, facts);
        const auto children = semantic::expression_children(module, expression);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    LambdaBindings result;
    for (const auto *match : matches) {
        bind_lambda(module, *match, counts, facts, result);
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
        known = bound_lambda(state, read->second, syntax.expression(value));
    }
    return known.lambda && known.lambda->arity == arity ? known : KnownLambda{};
}

const ast::Expression *sunk_fun(const ExpressionLowering &state, const ast::Expression &expression) {
    if (const auto *match = std::get_if<ast::MatchExpression>(&expression.value)) {
        return sunk_match(state, expression) ? &state.module.syntax->expression(match->right) : nullptr;
    }
    const auto *call = std::get_if<ast::CallExpression>(&expression.value);
    if (!call) {
        return nullptr;
    }
    if (semantic::fun_call(*state.module.syntax, *call)) {
        return known_lambda(state, call->target, call->arguments.size()).unlowered;
    }
    const auto *list = sunk_list_fun(state, expression, *call);
    return list ? list : sunk_map_fun(state, expression, *call);
}

llvm::Value *lower_sunk_binding(ExpressionLowering &state, const ast::Expression &expression) {
    const auto *identity = sunk_match(state, expression);
    if (!identity) {
        return nullptr;
    }
    // Nothing reads this word: the binding's reads inline the fun or build it (sunk_read).
    auto *placeholder = llvm::ConstantInt::get(state.word, abi::v1::empty_list);
    state.bindings.insert_or_assign(*identity, placeholder);
    state.sunk_lambdas.insert_or_assign(*identity, state.lambdas->funs.at(*identity));
    return placeholder;
}

llvm::Value *sunk_read(ExpressionLowering &state, const semantic::BindingId identity) {
    const auto sunk = state.sunk_lambdas.find(identity);
    return sunk == state.sunk_lambdas.end() ? nullptr : lower_fun(state, *sunk->second->expression);
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
        // No clause matched: calling the fun raises function_clause with the fun's own frame on top. Its clauses reject
        // the same arguments, so the call never returns and the join has only the clauses' values (OPT1.13).
        lower_apply_values(state, fun(), arguments);
        state.builder.CreateUnreachable();
    }
    return join_inlined(state, join, incoming);
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
        return known.unlowered ? lower_fun(state, *known.lambda->expression)
                               : state.values.at(&syntax.expression(call.target));
    });
}
} // namespace clause::codegen
