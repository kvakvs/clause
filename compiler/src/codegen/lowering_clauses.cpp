#include "../semantic/capabilities.hpp"
#include "../semantic/funs.hpp"
#include "binary_loops.hpp"
#include "float_loops.hpp"
#include "lowering_state.hpp"
#include "source_locations.hpp"
#include <algorithm>
#include <cstdint>
#include <string>

namespace clause::codegen {
namespace {
// Index validated reads once without transferring bindings between candidate environments.
BindingReads read_bindings(const semantic::Module &module, const semantic::Function &function) {
    BindingReads result;
    for (const auto &binding : function.bindings) {
        if (binding.use == semantic::BindingUse::read) {
            result.emplace(&module.syntax->expression(binding.expression), binding.identity);
        }
    }
    return result;
}

// Whether a call transfers to Erlang code: a named function, a value, a runtime module or function, or apply/2,3.
bool transfers(const ExpressionLowering &state, const ast::Expression &expression, const ast::CallExpression &call) {
    const auto &syntax = *state.module.syntax;
    if (state.inferred.callees.contains(&expression)) {
        return true;
    }
    const auto service = state.function.services.find(&expression);
    if (service != state.function.services.end()) {
        return service->second.apply();
    }
    return semantic::fun_call(syntax, call) || semantic::dynamic_call(syntax, call);
}

// Follow one tail position: blocks and groups end in their last expression, case, if and receive in each clause's;
// any other expression is a tail leaf.
void tail_position(const ast::Expression &expression, std::vector<ast::ExprId> &pending,
                   std::set<const ast::Expression *> &leaves) {
    if (const auto *group = std::get_if<ast::Group>(&expression.value)) {
        pending.push_back(group->expression);
    } else if (const auto *block = std::get_if<ast::BlockExpression>(&expression.value)) {
        pending.push_back(block->body.back());
    } else if (std::holds_alternative<ast::CaseExpression>(expression.value) ||
               std::holds_alternative<ast::IfExpression>(expression.value) ||
               std::holds_alternative<ast::ReceiveExpression>(expression.value)) {
        for (const auto &clause : semantic::branch_clauses(expression.value)) {
            pending.push_back(clause.body->back());
        }
    } else {
        leaves.insert(&expression);
    }
}

// The expressions in tail position of any clause, outside every catch and try: their value is the function's result.
std::set<const ast::Expression *> tail_leaves(const ExpressionLowering &state,
                                              const std::vector<ast::FunctionClause> &clauses) {
    std::set<const ast::Expression *> leaves;
    std::vector<ast::ExprId> pending;
    pending.reserve(clauses.size());
    for (const auto &clause : clauses) {
        pending.push_back(clause.body.back());
    }
    while (!pending.empty()) {
        const auto &expression = state.module.syntax->expression(pending.back());
        pending.pop_back();
        tail_position(expression, pending, leaves);
    }
    return leaves;
}

// The tail leaves that are calls transferring to Erlang code: they become tail transfers.
std::set<const ast::Expression *> tail_calls(const ExpressionLowering &state,
                                             const std::set<const ast::Expression *> &leaves) {
    std::set<const ast::Expression *> calls;
    for (const auto *expression : leaves) {
        const auto *call = std::get_if<ast::CallExpression>(&expression->value);
        if (call && transfers(state, *expression, *call)) {
            calls.insert(expression);
        }
    }
    return calls;
}

// Each candidate owns fresh SSA maps; its failed head/guard can only enter the next candidate.
void candidate(ExpressionLowering &state, const ast::FunctionClause &clause, llvm::BasicBlock *mismatch) {
    const bool unconditional = lower_unconditional_head(state);
    if (!unconditional || clause.guard) {
        auto *success = llvm::BasicBlock::Create(state.entry.getContext(), "match.success", &state.entry);
        auto *guard =
            clause.guard ? llvm::BasicBlock::Create(state.entry.getContext(), "guard.entry", &state.entry) : success;
        if (unconditional) {
            state.builder.CreateBr(guard);
        } else {
            lower_head(state, guard, mismatch);
        }
        if (clause.guard) {
            state.builder.SetInsertPoint(guard);
            lower_guard(state, *clause.guard, {.success = success, .rejection = mismatch});
        }
        state.builder.SetInsertPoint(success);
    }
    llvm::Value *result = nullptr;
    for (const auto &expression : clause.body) {
        result = lower_body(state, expression);
    }
    locate_source(state.builder, *state.module.syntax, state.module.syntax->expression(clause.body.back()).source);
    state.builder.CreateRet(result);
}

// An anonymous fun's captured values, read from the arguments after its own; none for a function.
std::map<semantic::BindingId, llvm::Value *> captured(ExpressionLowering &state) {
    std::map<semantic::BindingId, llvm::Value *> result;
    if (!state.lambda) {
        return result;
    }
    const auto &captures = state.lambda->captures;
    for (std::size_t i = 0; i < captures.size(); ++i) {
        auto *slot = state.builder.CreateGEP(state.word, state.entry.getArg(1),
                                             llvm::ConstantInt::get(state.word, state.lambda->arity + i));
        result.emplace(captures[i], state.builder.CreateAlignedLoad(
                                        state.word, slot, llvm::Align(state.word->getBitWidth() / 8), "captured"));
    }
    return result;
}

// A named fun reads its own name as the fun itself: built once from its captured values and kept rooted like an
// argument; nothing is built when no clause reads the name.
void name_self(ExpressionLowering &initial) {
    const auto self = initial.lambda ? initial.lambda->self : std::nullopt;
    const auto read = [&](const auto &entry) { return entry.second == self; };
    if (!self || !std::ranges::any_of(*initial.reads, read)) {
        return;
    }
    initial.bindings.emplace(*self, lower_fun(initial, *initial.lambda->expression));
    initial.roots->arguments = initial.roots->next;
}

// In a binary loop's variant, read the parameter's view once before the clauses: it is always the loop's own
// sub-binary cell, and no collection runs before the heads read it. Reads of the whole parameter copy it (OPT1.4E).
void read_loop_view(ExpressionLowering &initial) {
    auto *loop = initial.binary_loop;
    if (!loop || !loop->in_variant) {
        return;
    }
    auto *slot = initial.builder.CreateGEP(initial.word, initial.entry.getArg(1),
                                           llvm::ConstantInt::get(initial.word, loop->reads.parameter));
    auto *cell = initial.builder.CreateAlignedLoad(initial.word, slot, llvm::Align(initial.word->getBitWidth() / 8),
                                                   "binary.loop.cell");
    mark_term(initial, cell);
    loop->view = sub_view(initial, cell, true);
    loop->whole = whole_rest(initial, cell, *loop->view);
}

// In a float loop's variant, read each float parameter's double from the bits of its argument word once before the
// clauses: every head binds the parameter to a cell of it, which reads make anew (float loops, OPT1.2).
void read_float_parameters(ExpressionLowering &initial) {
    auto *loop = initial.float_loop;
    if (!loop || !loop->in_variant) {
        return;
    }
    std::map<std::size_t, llvm::Value *> cells;
    for (const auto parameter : loop->parameters) {
        auto *slot = initial.builder.CreateGEP(initial.word, initial.entry.getArg(1),
                                               llvm::ConstantInt::get(initial.word, parameter));
        auto *bits = initial.builder.CreateAlignedLoad(initial.word, slot, llvm::Align(initial.word->getBitWidth() / 8),
                                                       "float.loop.bits");
        cells.emplace(parameter,
                      float_box(initial, initial.builder.CreateBitCast(bits, initial.builder.getDoubleTy())));
    }
    for (const auto &[binding, parameter] : loop->bindings) {
        initial.bindings.emplace(binding, cells.at(parameter));
    }
}

// Lower every candidate of `clauses` into the entry `initial` names, each seeing the captured values; exhaustion
// raises function_clause.
void lower_candidates(ExpressionLowering &initial, const std::vector<ast::FunctionClause> &clauses,
                      const std::set<const ast::Expression *> &tails) {
    auto &builder = initial.builder;
    auto &entry = initial.entry;
    auto roots = begin_roots(initial);
    initial.roots = &roots;
    root_arguments(initial);
    initial.bindings = captured(initial);
    name_self(initial);
    read_loop_view(initial);
    read_float_parameters(initial);
    const auto captures = initial.bindings;
    auto *exhausted = llvm::BasicBlock::Create(entry.getContext(), "match.mismatch", &entry);
    llvm::BasicBlock *failure = initial.failure;
    for (std::size_t index = 0; index < clauses.size(); ++index) {
        auto *next = index + 1 == clauses.size() ? exhausted
                                                 : llvm::BasicBlock::Create(entry.getContext(), "clause.next", &entry);
        ExpressionLowering state{
            builder, entry, initial.module, initial.function, initial.inferred, initial.word, {}, initial.reads, index};
        state.bindings = captures;
        state.roots = &roots;
        state.tail_calls = &tails;
        state.failure = failure;
        state.lambda = initial.lambda;
        state.proofs = initial.proofs;
        state.binary_loop = initial.binary_loop;
        state.float_loop = initial.float_loop;
        state.raw_floats = initial.raw_floats;
        state.lambdas = initial.lambdas;
        reset_candidate_roots(state);
        candidate(state, clauses[index], next);
        failure = state.failure;
        builder.SetInsertPoint(next);
    }
    if (exhausted->use_empty()) {
        builder.ClearInsertionPoint();
        exhausted->eraseFromParent();
    } else {
        raise_function_clause(initial);
    }
    finish_roots(initial);
    drop_unused_floats(entry);
}

// A binary loop (OPT1.4C): the function enters its variant with each rest cell it makes, and the variant, lowered
// from the same clauses, keeps updating that one cell.
void lower_loop(ExpressionLowering &initial, const std::vector<ast::FunctionClause> &clauses,
                const std::set<const ast::Expression *> &tails, const LoopReads &reads) {
    auto *variant = declare_loop_variant(initial, ".loop");
    variant->addFnAttr(BINARY_LOOP_ATTRIBUTE);
    BinaryLoop outer{reads, variant, false};
    initial.binary_loop = &outer;
    lower_candidates(initial, clauses, tails);
    llvm::IRBuilder<> builder(llvm::BasicBlock::Create(variant->getContext(), "entry", variant));
    ExpressionLowering inner{builder, *variant, initial.module, initial.function, initial.inferred, initial.word, {}};
    inner.reads = initial.reads;
    inner.proofs = initial.proofs;
    BinaryLoop looping{reads, variant, true};
    inner.binary_loop = &looping;
    lower_candidates(inner, clauses, tails);
    keep_invariants_alone(*variant);
}

// A float loop (OPT1.2): the function and the variant, lowered from the same clauses, enter the variant with the
// bits of the new doubles; the variant's service transfers enter the function with cells made of them instead.
void lower_float_loop(ExpressionLowering &initial, const std::vector<ast::FunctionClause> &clauses,
                      const std::set<const ast::Expression *> &tails, FloatLoop loop) {
    loop.variant = declare_loop_variant(initial, ".float");
    std::uint64_t mask = 0;
    for (const auto parameter : loop.parameters) {
        mask |= std::uint64_t{1} << parameter;
    }
    loop.variant->addFnAttr(FLOAT_PARAMETERS_ATTRIBUTE, std::to_string(mask));
    loop.variant->addFnAttr(FLOAT_GENERIC_ATTRIBUTE, initial.entry.getName());
    define_float_box(initial);
    FloatLoop outer = loop;
    initial.float_loop = &outer;
    lower_candidates(initial, clauses, tails);
    llvm::IRBuilder<> builder(llvm::BasicBlock::Create(loop.variant->getContext(), "entry", loop.variant));
    ExpressionLowering inner{builder, *loop.variant, initial.module, initial.function, initial.inferred, initial.word,
                             {}};
    inner.reads = initial.reads;
    inner.proofs = initial.proofs;
    loop.in_variant = true;
    inner.float_loop = &loop;
    lower_candidates(inner, clauses, tails);
}

void lower_clauses(ExpressionLowering &initial, const std::vector<ast::FunctionClause> &clauses) {
    const auto leaves = tail_leaves(initial, clauses);
    const auto tails = tail_calls(initial, leaves);
    if (const auto reads = binary_loop_reads(initial, clauses.size(), tails)) {
        lower_loop(initial, clauses, tails, *reads);
    } else if (auto floats = float_loop_parameters(initial, clauses.size(), {.calls = tails, .leaves = leaves})) {
        lower_float_loop(initial, clauses, tails, std::move(*floats));
    } else {
        lower_candidates(initial, clauses, tails);
    }
}
} // namespace

void lower_function(llvm::IRBuilder<> &builder, llvm::Function &entry, const semantic::Module &module,
                    const semantic::Function &function, llvm::IntegerType *word,
                    const semantic::types::Inference &inferred, const Proofs *proofs, const LambdaBindings *lambdas) {
    const auto reads = read_bindings(module, function);
    ExpressionLowering initial{builder, entry, module, function, inferred, word, {}};
    initial.reads = &reads;
    initial.proofs = proofs;
    initial.lambdas = lambdas;
    lower_clauses(initial, std::get<ast::Function>(module.syntax->form(function.form).value).clauses);
}

void lower_lambda(llvm::IRBuilder<> &builder, llvm::Function &entry, const semantic::Module &module,
                  const semantic::FunEntry &lambda, llvm::IntegerType *word, const semantic::types::Inference &inferred,
                  const LambdaBindings *lambdas) {
    const auto reads = read_bindings(module, *lambda.owner);
    ExpressionLowering initial{builder, entry, module, *lambda.owner, inferred, word, {}};
    initial.reads = &reads;
    initial.lambda = &lambda;
    initial.lambdas = lambdas;
    lower_clauses(initial, *semantic::fun_clauses(lambda.expression->value));
}
} // namespace clause::codegen
