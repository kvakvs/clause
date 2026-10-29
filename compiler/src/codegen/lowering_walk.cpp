#include "../semantic/capabilities.hpp"
#include "../semantic/records.hpp"
#include "lowering_state.hpp"
#include "source_locations.hpp"
#include <array>
#include <clause/abi/calls.hpp>
#include <llvm/Transforms/Utils/SSAUpdater.h>
#include <span>

namespace clause::codegen {
namespace {
enum class Action : std::uint8_t {
    enter,
    value,
    lazy_left,
    lazy_right,
    record_field,
    case_select,
    case_clause_end,
    catch_end,
    try_body_end,
    after_end,
    after_raise_end,
    maybe_match,
    maybe_end,
    qualifier,
    comprehension_end
};

struct Visit {
    // Explicit actions keep ordinary and conditionally reached operands off the host stack.
    ast::ExprId id;
    Action action = Action::enter;
    // Record-field actions capture one completed evaluation before a reused initializer can run again;
    // case and if clause actions name their clause here, comprehension qualifier actions their qualifier.
    std::size_t field = 0;
    std::optional<ast::ExprId> child = {};
};

struct LazyJoin {
    // Preserve the short-path term and its predecessor until the reached RHS can complete the word PHI.
    llvm::Value *left;
    llvm::BasicBlock *short_path;
    llvm::BasicBlock *merge;
};

struct CaseIncoming {
    // One completed clause: its final block, then its value followed by the case's exported bindings there.
    llvm::BasicBlock *block;
    std::vector<llvm::Value *> values;
};

struct CaseJoin {
    // Every case clause matches the same scrutinee (null for if, the body value for a try's of clauses); clauses
    // start from the bindings before the branch (catch clauses from those before the try).
    llvm::Value *value;
    std::map<semantic::BindingId, llvm::Value *> bindings;
    // The merge block is inserted after the last clause; completed clauses collect their incoming edges.
    llvm::BasicBlock *merge;
    // The mismatch continuation of the current clause starts the next one, or raises case_clause/if_clause.
    llvm::BasicBlock *next = nullptr;
    std::vector<CaseIncoming> incoming = {};
    // A try's catch clauses match the caught class atom and reason.
    Exception exception = {};
    // A receive's loop head: a message no clause matches is skipped and the scan continues there.
    llvm::BasicBlock *loop = nullptr;
    // A receive's expired timeout: its after body starts here.
    llvm::BasicBlock *timeout = nullptr;
    // The expression whose value the clauses match (a case scrutinee or a try body), for its inferred fact.
    const ast::Expression *scrutinee = nullptr;
};

struct ProtectedScope {
    // The enclosing handler and error exits come back once the protected catch expression or try body completes.
    llvm::BasicBlock *outer;
    llvm::BasicBlock *bad_argument;
    llvm::BasicBlock *bad_arithmetic;
    llvm::BasicBlock *system_limit;
    // Names bound inside a catch or try are unsafe afterwards, so the bindings before it are restored.
    std::map<semantic::BindingId, llvm::Value *> bindings;
    // Failures inside the protected part branch here; it is erased when nothing inside can fail.
    llvm::BasicBlock *handler;
};

struct AfterPath {
    // The try value outlives the normal-path after body, whose end `resume` continues once the raising path is done.
    llvm::Value *result;
    llvm::BasicBlock *resume = nullptr;
    // The exception taken by the after handler on the raising path.
    Exception exception = {};
};

// Merge one per-clause value at the join; SSAUpdater adds a PHI only when the clauses disagree.
llvm::Value *merged(const ExpressionLowering &state, const CaseJoin &join, const std::size_t slot) {
    llvm::SmallVector<llvm::PHINode *, 2> phis;
    llvm::SSAUpdater updater(&phis);
    updater.Initialize(state.word, slot == 0 ? "case.value" : "case.export");
    for (const auto &completed : join.incoming) {
        updater.AddAvailableValue(completed.block, completed.values.at(slot));
    }
    auto *value = updater.GetValueInMiddleOfBlock(join.merge);
    for (auto *phi : phis) {
        phi->setDebugLoc(state.builder.getCurrentDebugLocation());
    }
    return value;
}

// Lazy syntax has an executable RHS block; strict boolean operators use ordinary eager value scheduling.
const ast::BinaryExpression *lazy(const ast::ExprValue &value) {
    const auto *binary = std::get_if<ast::BinaryExpression>(&value);
    return binary && (binary->operation == ast::BinaryOperator::and_also ||
                      binary->operation == ast::BinaryOperator::or_else)
               ? binary
               : nullptr;
}

// Literal binary strings expand directly to segment scalars; only their size expression needs evaluation.
std::vector<ast::ExprId> binary_children(const ast::Module &syntax, const ast::Bitstring &binary) {
    std::vector<ast::ExprId> children;
    for (const auto &segment : binary.segments) {
        const auto &value = syntax.expression(semantic::ungroup(syntax, segment.value)).value;
        if (!std::holds_alternative<ast::StringLiteral>(value)) {
            children.push_back(segment.value);
        }
        if (segment.size) {
            children.push_back(*segment.size);
        }
    }
    return children;
}

// Reverse-push eager children to preserve source order; schedule only the left operand for lazy syntax.
bool record_enter(ExpressionLowering &state, const ast::ExprId &id, std::vector<Visit> &pending) {
    const auto &expression = state.module.syntax->expression(id);
    const auto *record = std::get_if<ast::RecordExpression>(&expression.value);
    if (!record || record->base || semantic::external_record(state.module, record->identity)) {
        return false;
    }
    const auto fields = semantic::record_values(state.module, *record, false);
    const auto order = semantic::record_order(state.module, *record);
    state.record_values.insert_or_assign(&expression, std::vector<llvm::Value *>(fields.size()));
    pending.push_back({id, Action::value});
    for (auto position = order.rbegin(); position != order.rend(); ++position) {
        const auto &field = fields[*position];
        pending.push_back({id, Action::record_field, *position, field});
        if (field) {
            pending.push_back({*field});
        }
    }
    return true;
}

// A case evaluates its scrutinee first and clause selection starts once that value exists; an if or a receive
// selects at once.
bool case_enter(ExpressionLowering &state, const ast::ExprId &id, std::vector<Visit> &pending) {
    const auto &value = state.module.syntax->expression(id).value;
    if (!std::holds_alternative<ast::CaseExpression>(value) && !std::holds_alternative<ast::IfExpression>(value) &&
        !std::holds_alternative<ast::ReceiveExpression>(value)) {
        return false;
    }
    pending.push_back({id, Action::case_select});
    if (const auto *selection = std::get_if<ast::CaseExpression>(&value)) {
        pending.push_back({selection->value});
    } else if (const auto *receive = std::get_if<ast::ReceiveExpression>(&value); receive && receive->after) {
        // OTP evaluates the timeout before scanning the mailbox.
        pending.push_back({receive->after->timeout});
    }
    return true;
}

// Record the left operand of `++`, which may be an integer_to_list/1 call whose digits the append writes (digit
// prefixes, OPT3.16).
void note_digit_prefix(ExpressionLowering &state, const ast::Expression &expression) {
    const auto *binary = std::get_if<ast::BinaryExpression>(&expression.value);
    if (binary && binary->operation == ast::BinaryOperator::append) {
        const auto &syntax = *state.module.syntax;
        state.digit_prefixes.insert(&syntax.expression(semantic::ungroup(syntax, binary->left)));
    }
}

// Reverse-push eager children to preserve source order; schedule only the left operand for lazy syntax.
void enter(ExpressionLowering &state, const ast::ExprId &id, std::vector<Visit> &pending) {
    const auto &expression = state.module.syntax->expression(id);
    if (const auto *binary = lazy(expression.value)) {
        pending.push_back({id, Action::lazy_left});
        pending.push_back({binary->left});
        return;
    }
    if (record_enter(state, id, pending) || case_enter(state, id, pending)) {
        return;
    }
    pending.push_back({id, Action::value});
    note_digit_prefix(state, expression);
    // An anonymous fun's clauses are lowered as a function of their own.
    if (std::holds_alternative<ast::FunExpression>(expression.value)) {
        return;
    }
    // A module literal is lowered whole, without its elements (literal heap).
    if (semantic::integer_literal(*state.module.syntax, id, state.word->getBitWidth()) || encoded_literal(state, id)) {
        return;
    }
    const auto *binary = std::get_if<ast::Bitstring>(&expression.value);
    const auto children = binary ? binary_children(*state.module.syntax, *binary)
                                 : semantic::expression_children(state.module, expression);
    // A fun written in a call that inlines it is built only where its value is needed (inline funs, OPT1.8).
    const auto *sunk = sunk_fun(state, expression);
    for (auto child = children.rbegin(); child != children.rend(); ++child) {
        if (&state.module.syntax->expression(*child) != sunk) {
            pending.push_back({*child});
        }
    }
}

// The left operand must be boolean; its errors reject the enclosing guard instead of becoming false.
LazyJoin left(ExpressionLowering &state, const ast::Expression &expression, const ast::BinaryExpression &binary) {
    locate_source(state.builder, *state.module.syntax, expression.source);
    auto *value = state.values.at(&state.module.syntax->expression(binary.left));
    value = lower_immediate(state, abi::v1::ImmediateOperation::boolean_check, value);
    auto *truth = lower_atom(state, ast::Atom{U"true"});
    auto *test = lower_exact(state, value, truth);
    auto *short_path = state.builder.GetInsertBlock();
    auto *merge = llvm::BasicBlock::Create(state.entry.getContext(), "lazy.join", &state.entry);
    auto *right = llvm::BasicBlock::Create(state.entry.getContext(), "lazy.right", &state.entry);
    if (binary.operation == ast::BinaryOperator::and_also) {
        state.builder.CreateCondBr(test, right, merge);
    } else {
        state.builder.CreateCondBr(test, merge, right);
    }
    state.builder.SetInsertPoint(right);
    return {value, short_path, merge};
}

struct Incoming {
    // One predecessor edge of a two-way join and the value it carries.
    llvm::BasicBlock *block;
    llvm::Value *value;
};

struct MaybeScope {
    // Names bound inside a maybe are unsafe afterwards, so its else clauses and its value start from these.
    std::map<semantic::BindingId, llvm::Value *> bindings;
    // Every failed ?= branches here with its unmatched value; it is erased when every match always succeeds.
    llvm::BasicBlock *exit;
    std::vector<Incoming> mismatches = {};
};

// Join two edges at `merge` (the insertion block); SSAUpdater adds a PHI only when the values differ.
llvm::Value *join_two(ExpressionLowering &state, llvm::BasicBlock *merge, const char *name, const Incoming first,
                      const Incoming second) {
    // SSA formation inspects successors, so terminate the unfinished join until its surrounding expression resumes.
    auto *boundary = state.builder.CreateUnreachable();
    llvm::SmallVector<llvm::PHINode *, 2> phis;
    llvm::SSAUpdater updater(&phis);
    updater.Initialize(state.word, name);
    updater.AddAvailableValue(first.block, first.value);
    updater.AddAvailableValue(second.block, second.value);
    auto *result = updater.GetValueInMiddleOfBlock(merge);
    for (auto *phi : phis) {
        phi->setDebugLoc(state.builder.getCurrentDebugLocation());
    }
    boundary->eraseFromParent();
    return result;
}

// The reached RHS may return any admitted term; only its enclosing consumer imposes another boolean check.
llvm::Value *right(ExpressionLowering &state, const ast::Expression &expression, const LazyJoin &join) {
    const auto &binary = std::get<ast::BinaryExpression>(expression.value);
    locate_source(state.builder, *state.module.syntax, expression.source);
    auto *value = state.values.at(&state.module.syntax->expression(binary.right));
    auto *predecessor = state.builder.GetInsertBlock();
    state.builder.CreateBr(join.merge);
    state.builder.SetInsertPoint(join.merge);
    return join_two(state, join.merge, "lazy.value", {join.short_path, join.left}, {predecessor, value});
}

struct Walk {
    // Borrow the function state while retaining bounded AST-sized scheduling and unfinished joins.
    ExpressionLowering &state;
    std::vector<Visit> pending;
    std::map<const ast::Expression *, LazyJoin> joins;
    std::map<const ast::Expression *, CaseJoin> cases;
    std::map<const ast::Expression *, ProtectedScope> catches;
    // A try's after protection encloses its body and clauses; its handler runs the after body on the raising path.
    std::map<const ast::Expression *, ProtectedScope> afters;
    std::map<const ast::Expression *, AfterPath> after_paths;
    std::map<const ast::Expression *, MaybeScope> maybes;
    std::map<const ast::Expression *, Comprehension> comprehensions;

    // Save the enclosing handler and error exits in `scopes`; failures in what follows reach a new handler instead.
    void protect(std::map<const ast::Expression *, ProtectedScope> &scopes, const ast::Expression &expression,
                 const char *name) {
        auto *handler = llvm::BasicBlock::Create(state.entry.getContext(), name, &state.entry);
        scopes.try_emplace(&expression, state.handler, state.bad_argument, state.bad_arithmetic, state.system_limit,
                           state.bindings, handler);
        // Shared error exits created outside the protected part would bypass its handler.
        state.handler = handler;
        state.bad_argument = nullptr;
        state.bad_arithmetic = nullptr;
        state.system_limit = nullptr;
    }

    // Restore the enclosing handler and error exits once a protected part completes.
    void restore(const ProtectedScope &scope) {
        state.handler = scope.outer;
        state.bad_argument = scope.bad_argument;
        state.bad_arithmetic = scope.bad_arithmetic;
        state.system_limit = scope.system_limit;
    }

    // Protect a catch expression: failures inside it reach a fresh handler instead of the enclosing exit.
    bool open_catch(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        const auto *guarded = std::get_if<ast::CatchExpression>(&expression.value);
        if (!guarded) {
            return false;
        }
        protect(catches, expression, "catch.handler");
        pending.push_back({id, Action::catch_end});
        pending.push_back({guarded->expression});
        return true;
    }

    // Join the protected value with the caught exception value, then restore the enclosing scope.
    void close_catch(const ast::Expression &expression) {
        auto node = catches.extract(&expression);
        auto &scope = node.mapped();
        auto *handler = scope.handler;
        restore(scope);
        state.bindings = std::move(scope.bindings);
        const auto &guarded = std::get<ast::CatchExpression>(expression.value);
        auto *value = state.values.at(&state.module.syntax->expression(guarded.expression));
        if (handler->use_empty()) {
            handler->eraseFromParent();
            state.values.insert_or_assign(&expression, value);
            return;
        }
        locate_source(state.builder, *state.module.syntax, expression.source);
        auto *normal = state.builder.GetInsertBlock();
        auto *merge = llvm::BasicBlock::Create(state.entry.getContext(), "catch.join", &state.entry);
        state.builder.CreateBr(merge);
        state.builder.SetInsertPoint(handler);
        auto *caught = lower_catch(state);
        auto *handled = state.builder.GetInsertBlock();
        state.builder.CreateBr(merge);
        state.builder.SetInsertPoint(merge);
        // Both incoming values are already rooted: the protected result and the catch service's slot.
        auto *result = join_two(state, merge, "catch.value", {normal, value}, {handled, caught});
        state.values.insert_or_assign(&expression, result);
    }

    // Protect a try body: its exceptions reach the catch clauses, which like the of clauses run outside that
    // protection; an after protection encloses the body and all clauses.
    bool open_try(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        const auto *attempt = std::get_if<ast::TryExpression>(&expression.value);
        if (!attempt) {
            return false;
        }
        if (attempt->after) {
            protect(afters, expression, "try.after");
        }
        if (attempt->handlers) {
            protect(catches, expression, "try.handler");
        }
        pending.push_back({id, Action::try_body_end});
        for (auto body = attempt->body.rbegin(); body != attempt->body.rend(); ++body) {
            pending.push_back({*body});
        }
        return true;
    }

    // Leave the protected try body, then select an of clause on its value or join that value directly.
    void try_body_end(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        const auto &attempt = std::get<ast::TryExpression>(expression.value);
        auto *value = state.values.at(&state.module.syntax->expression(attempt.body.back()));
        if (const auto scope = catches.find(&expression); scope != catches.end()) {
            restore(scope->second);
        }
        auto *merge = llvm::BasicBlock::Create(state.entry.getContext(), "try.join");
        auto &join = cases.try_emplace(&expression, value, state.bindings, merge).first->second;
        join.scrutinee = &state.module.syntax->expression(attempt.body.back());
        if (attempt.of) {
            start_clause(id, 0);
            return;
        }
        join.incoming.push_back({state.builder.GetInsertBlock(), {value}});
        state.builder.CreateBr(merge);
        handlers(id);
    }

    // Start the catch clauses from the try's handler with the bindings before the try, or finish the try when it
    // has none or nothing in its body can fail.
    void handlers(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        auto &join = cases.at(&expression);
        auto node = catches.extract(&expression);
        if (node.empty()) {
            finish(id, join);
            return;
        }
        auto &scope = node.mapped();
        join.bindings = std::move(scope.bindings);
        if (scope.handler->use_empty()) {
            scope.handler->eraseFromParent();
            state.builder.ClearInsertionPoint();
            finish(id, join);
            return;
        }
        state.builder.SetInsertPoint(scope.handler);
        locate_source(state.builder, *state.module.syntax, expression.source);
        join.exception = lower_exception(state);
        start_clause(id, semantic::first_handler(expression.value));
    }

    // Enter an expression: a catch or try opens its protected scope, a maybe its exit, every other node schedules
    // its operands.
    void enter_node(const ast::ExprId &id) {
        if (!open_catch(id) && !open_try(id) && !open_maybe(id) && !open_comprehension(id)) {
            enter(state, id, pending);
        }
    }

    // Schedule each qualifier after its generator inputs or body filter, then the templates; guard filters
    // need no value before their qualifier.
    bool open_comprehension(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        const auto *qualifiers = semantic::comprehension_qualifiers(expression.value);
        if (!qualifiers) {
            return false;
        }
        comprehensions.try_emplace(&expression, begin_comprehension(state, expression.value));
        pending.push_back({id, Action::comprehension_end});
        const auto templates = semantic::comprehension_templates(expression.value);
        for (auto item = templates.rbegin(); item != templates.rend(); ++item) {
            pending.push_back({*item});
        }
        for (std::size_t i = qualifiers->size(); i != 0; --i) {
            pending.push_back({id, Action::qualifier, i - 1});
            const auto parts = semantic::zipped((*qualifiers)[i - 1]);
            for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
                schedule_operand(*part);
            }
        }
        return true;
    }

    // A generator's input, or a filter evaluated as an ordinary expression.
    void schedule_operand(const ast::Qualifier &part) {
        const auto *filter = std::get_if<ast::FilterQualifier>(&part.value);
        if (!filter) {
            pending.push_back({semantic::qualifier_expression(part)});
        } else if (!state.function.guard_filters.contains(&state.module.syntax->expression(filter->expression))) {
            pending.push_back({filter->expression});
        }
    }

    // Loop over a qualifier's generators or test its filter.
    void lower_qualifier(const Visit &visit) {
        const auto &expression = state.module.syntax->expression(visit.id);
        const auto &qualifier = semantic::comprehension_qualifiers(expression.value)->at(visit.field);
        const auto *simple = std::get_if<ast::Qualifier>(&qualifier);
        const auto *filter = simple ? std::get_if<ast::FilterQualifier>(&simple->value) : nullptr;
        if (filter) {
            lower_filter(state, comprehensions.at(&expression), filter->expression);
        } else {
            lower_generators(state, comprehensions.at(&expression), qualifier);
        }
    }

    // Accumulate the template values, then produce the result once the generators are exhausted.
    void comprehension_end(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        auto node = comprehensions.extract(&expression);
        locate_source(state.builder, *state.module.syntax, expression.source);
        lower_templates(state, node.mapped());
        state.values.insert_or_assign(&expression, finish_comprehension(state, node.mapped()));
    }

    // Schedule a maybe body in order; each ?= matches once its value exists.
    bool open_maybe(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        const auto *block = std::get_if<ast::MaybeExpression>(&expression.value);
        if (!block) {
            return false;
        }
        auto *exit = llvm::BasicBlock::Create(state.entry.getContext(), "maybe.else", &state.entry);
        maybes.try_emplace(&expression, state.bindings, exit);
        pending.push_back({id, Action::maybe_end});
        for (std::size_t i = block->body.size(); i != 0; --i) {
            const auto &item = block->body[i - 1];
            if (const auto *match = std::get_if<ast::MaybeMatch>(&item)) {
                pending.push_back({id, Action::maybe_match, i - 1});
                pending.push_back({match->value});
            } else {
                pending.push_back({std::get<ast::ExprId>(item)});
            }
        }
        return true;
    }

    // Match one ?= pattern; a mismatch leaves the body for the maybe's exit with the unmatched value.
    void maybe_match(const Visit &visit) {
        const auto &expression = state.module.syntax->expression(visit.id);
        const auto &match =
            std::get<ast::MaybeMatch>(std::get<ast::MaybeExpression>(expression.value).body.at(visit.field));
        auto &scope = maybes.at(&expression);
        auto *value = state.values.at(&state.module.syntax->expression(match.value));
        auto &context = state.entry.getContext();
        auto *matched = llvm::BasicBlock::Create(context, "maybe.matched", &state.entry);
        auto *failed = llvm::BasicBlock::Create(context, "maybe.mismatch", &state.entry);
        const auto plan = body_pattern_plan(state, semantic::pattern_root(*state.module.syntax, match.pattern));
        lower_match_plan(state, plan, std::array{value}, matched, failed);
        if (failed->use_empty()) {
            failed->eraseFromParent();
        } else {
            state.builder.SetInsertPoint(failed);
            state.builder.CreateBr(scope.exit);
            scope.mismatches.push_back({failed, value});
        }
        state.builder.SetInsertPoint(matched);
    }

    // The value of a maybe body: its last expression, or the value its last ?= matched.
    llvm::Value *maybe_value(const ast::MaybeExpression &block) const {
        const auto &last = block.body.back();
        const auto *match = std::get_if<ast::MaybeMatch>(&last);
        return state.values.at(&state.module.syntax->expression(match ? match->value : std::get<ast::ExprId>(last)));
    }

    // Merge the values of every failed ?= at the maybe's exit (the insertion block).
    llvm::Value *unmatched_value(const MaybeScope &scope) {
        // SSA formation inspects successors, so terminate the exit until the else part resumes there.
        auto *boundary = state.builder.CreateUnreachable();
        llvm::SmallVector<llvm::PHINode *, 2> phis;
        llvm::SSAUpdater updater(&phis);
        updater.Initialize(state.word, "maybe.unmatched");
        for (const auto &mismatch : scope.mismatches) {
            updater.AddAvailableValue(mismatch.block, mismatch.value);
        }
        auto *value = updater.GetValueInMiddleOfBlock(scope.exit);
        boundary->eraseFromParent();
        state.builder.SetInsertPoint(scope.exit);
        return value;
    }

    // Join the body value with the unmatched value, or select an else clause on the unmatched value.
    void maybe_end(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        const auto &block = std::get<ast::MaybeExpression>(expression.value);
        auto node = maybes.extract(&expression);
        auto &scope = node.mapped();
        auto *value = maybe_value(block);
        state.bindings = std::move(scope.bindings);
        if (scope.mismatches.empty()) {
            scope.exit->eraseFromParent();
            state.values.insert_or_assign(&expression, value);
            return;
        }
        locate_source(state.builder, *state.module.syntax, expression.source);
        auto *normal = state.builder.GetInsertBlock();
        auto *merge = llvm::BasicBlock::Create(state.entry.getContext(), "maybe.join");
        state.builder.CreateBr(merge);
        state.builder.SetInsertPoint(scope.exit);
        auto *unmatched = unmatched_value(scope);
        auto &join = cases.try_emplace(&expression, unmatched, state.bindings, merge).first->second;
        join.incoming.push_back({normal, {value}});
        if (block.otherwise) {
            start_clause(id, 0);
            return;
        }
        join.incoming.push_back({scope.exit, {unmatched}});
        state.builder.CreateBr(merge);
        finish(id, join);
    }

    // Open the case or if join and try its first clause.
    void select(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        if (std::holds_alternative<ast::ReceiveExpression>(expression.value)) {
            receive(id);
            return;
        }
        const auto *selection = std::get_if<ast::CaseExpression>(&expression.value);
        auto *merge = llvm::BasicBlock::Create(state.entry.getContext(), "case.join");
        const auto *scrutinee = selection ? &state.module.syntax->expression(selection->value) : nullptr;
        auto *value = scrutinee ? state.values.at(scrutinee) : nullptr;
        cases.try_emplace(&expression, value, state.bindings, merge).first->second.scrutinee = scrutinee;
        start_clause(id, 0);
    }

    // Loop over the mailbox (docs/processes.md#receive): the next message not yet examined is matched against the
    // clauses; when every message has been examined the process waits for another, then the scan continues. An
    // expired timeout runs the after body instead.
    void receive(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        const auto &after = std::get<ast::ReceiveExpression>(expression.value).after;
        auto &context = state.entry.getContext();
        auto *loop = llvm::BasicBlock::Create(context, "receive.loop", &state.entry);
        auto *wait = llvm::BasicBlock::Create(context, "receive.wait", &state.entry);
        auto *match = llvm::BasicBlock::Create(context, "receive.match", &state.entry);
        auto *timeout = after ? state.values.at(&state.module.syntax->expression(after->timeout))
                              : lower_atom(state, {U"infinity"});
        state.builder.CreateBr(loop);
        state.builder.SetInsertPoint(loop);
        auto *slot = root_slot(state);
        auto *found = lower_receive(state, abi::v1::ReceiveOperation::peek, slot);
        state.builder.CreateCondBr(state.builder.CreateIsNotNull(found), match, wait);
        state.builder.SetInsertPoint(match);
        auto *message =
            state.builder.CreateAlignedLoad(state.word, slot, llvm::Align(state.word->getBitWidth() / 8), "message");
        auto &join =
            cases.try_emplace(&expression, message, state.bindings, llvm::BasicBlock::Create(context, "receive.join"))
                .first->second;
        join.loop = loop;
        state.builder.SetInsertPoint(wait);
        join.timeout = wait_for(timeout, loop, after.has_value());
        if (semantic::first_handler(expression.value) != 0) {
            state.builder.SetInsertPoint(match);
            start_clause(id, 0);
            return;
        }
        // An after-only receive leaves every message in place.
        state.builder.SetInsertPoint(match);
        lower_receive(state, abi::v1::ReceiveOperation::skip, nullptr);
        state.builder.CreateBr(loop);
        start_after(id, join);
    }

    // Wait for a message, then continue the scan at `loop`; with an after part, an expired timeout continues at the
    // returned block instead (null without one).
    llvm::BasicBlock *wait_for(llvm::Value *timeout, llvm::BasicBlock *loop, bool after) {
        auto *arrived = lower_wait(state, timeout);
        if (!after) {
            state.builder.CreateBr(loop);
            return nullptr;
        }
        auto *expired = llvm::BasicBlock::Create(state.entry.getContext(), "receive.timeout", &state.entry);
        state.builder.CreateCondBr(lower_exact(state, arrived, lower_atom(state, ast::Atom{U"true"})), loop, expired);
        return expired;
    }

    // Start a receive's after body once its timeout expired, from the bindings before the receive.
    void start_after(const ast::ExprId &id, const CaseJoin &join) {
        state.builder.SetInsertPoint(join.timeout);
        lower_receive(state, abi::v1::ReceiveOperation::restart, nullptr);
        start_clause(id, semantic::first_handler(state.module.syntax->expression(id).value));
    }

    // Match one clause pattern (case only) and guard from the entry bindings, then schedule its body.
    void start_clause(const ast::ExprId &id, const std::size_t index) {
        const auto &expression = state.module.syntax->expression(id);
        const auto clause = semantic::branch_clauses(expression.value).at(index);
        auto &join = cases.at(&expression);
        auto &context = state.entry.getContext();
        state.bindings = join.bindings;
        auto *body = llvm::BasicBlock::Create(context, "case.body", &state.entry);
        join.next = llvm::BasicBlock::Create(context, "case.next", &state.entry);
        if (clause.pattern) {
            auto *guard = clause.guard ? llvm::BasicBlock::Create(context, "case.guard", &state.entry) : body;
            match_head(clause, join, guard);
            state.builder.SetInsertPoint(guard);
        }
        if (clause.guard) {
            lower_guard(state, *clause.guard, {.success = body, .rejection = join.next});
        } else if (!clause.pattern) {
            // A receive's after body is a clause without pattern or guard: it always runs once reached.
            state.builder.CreateBr(body);
        }
        state.builder.SetInsertPoint(body);
        if (join.loop && index < semantic::first_handler(expression.value)) {
            // A matched message leaves the mailbox before the clause body runs.
            lower_receive(state, abi::v1::ReceiveOperation::take, nullptr);
        }
        pending.push_back({id, Action::case_clause_end, index});
        for (auto child = clause.body->rbegin(); child != clause.body->rend(); ++child) {
            pending.push_back({*child});
        }
    }

    // Match a clause pattern against the case value, or a catch clause's Class:Reason:Stack against the exception.
    void match_head(const semantic::Branch &clause, const CaseJoin &join, llvm::BasicBlock *success) {
        auto *input = join.value;
        auto *matched = success;
        if (clause.handler) {
            auto *reason = llvm::BasicBlock::Create(state.entry.getContext(), "catch.reason", &state.entry);
            match_class(*clause.handler, join.exception[0], reason, join.next);
            state.builder.SetInsertPoint(reason);
            input = join.exception[1];
            if (clause.handler->stacktrace) {
                matched = llvm::BasicBlock::Create(state.entry.getContext(), "catch.stack", &state.entry);
            }
        }
        const auto plan = body_pattern_plan(state, semantic::pattern_root(*state.module.syntax, *clause.pattern));
        const auto known = clause.handler || !join.scrutinee ? std::nullopt : known_expression(state, *join.scrutinee);
        lower_match_plan(state, plan, std::array{input}, matched, join.next, std::array{known});
        if (matched != success) {
            // The stack variable is always new, so binding it cannot fail.
            state.builder.SetInsertPoint(matched);
            const auto stack = body_pattern_plan(state, *clause.handler->stacktrace);
            lower_match_plan(state, stack, std::array{join.exception[2]}, success, join.next);
        }
    }

    // An explicit class is an atom or variable pattern; an omitted class matches throw.
    void match_class(const ast::CatchClause &handler, llvm::Value *name, llvm::BasicBlock *success,
                     llvm::BasicBlock *mismatch) {
        if (handler.exception_class) {
            const auto plan = body_pattern_plan(state, *handler.exception_class);
            lower_match_plan(state, plan, std::array{name}, success, mismatch);
            return;
        }
        auto *test = lower_exact(state, name, lower_atom(state, ast::Atom{U"throw"}));
        state.builder.CreateCondBr(test, success, mismatch);
    }

    // Bindings a case or if exports; everything bound inside a try or maybe is unsafe afterwards.
    std::span<const semantic::BindingId> exported(const ast::Expression &expression) const {
        if (std::holds_alternative<ast::TryExpression>(expression.value) ||
            std::holds_alternative<ast::MaybeExpression>(expression.value)) {
            return {};
        }
        return state.function.exports.at(&expression);
    }

    // Record a completed clause's edge to the join, then try the next clause of the same group (case and if
    // clauses, a try's of clauses or its catch clauses) or finish the group.
    void clause_end(const Visit &visit) {
        const auto &expression = state.module.syntax->expression(visit.id);
        const auto clauses = semantic::branch_clauses(expression.value);
        auto &join = cases.at(&expression);
        const auto &last = state.module.syntax->expression(clauses.at(visit.field).body->back());
        CaseIncoming completed{state.builder.GetInsertBlock(), {state.values.at(&last)}};
        const auto identities = exported(expression);
        completed.values.reserve(1 + identities.size());
        for (const auto &identity : identities) {
            completed.values.push_back(state.bindings.at(identity));
        }
        join.incoming.push_back(std::move(completed));
        state.builder.CreateBr(join.merge);
        state.builder.SetInsertPoint(join.next);
        const auto first = semantic::first_handler(expression.value);
        const auto end = visit.field < first ? first : clauses.size();
        if (visit.field + 1 < end) {
            start_clause(visit.id, visit.field + 1);
            return;
        }
        no_match(expression, join, clauses.at(visit.field));
        if (join.timeout && visit.field < first) {
            start_after(visit.id, join);
            return;
        }
        if (std::holds_alternative<ast::TryExpression>(expression.value) && visit.field < first) {
            handlers(visit.id);
            return;
        }
        finish(visit.id, join);
    }

    // Join the selection, then leave a try's after protection and lower its after body on the normal path.
    void finish(const ast::ExprId &id, CaseJoin &join) {
        const auto &expression = state.module.syntax->expression(id);
        finish_case(expression, join);
        cases.erase(&expression);
        if (const auto found = afters.find(&expression); found != afters.end()) {
            restore(found->second);
            state.bindings = found->second.bindings;
            after_paths.try_emplace(&expression, state.values.at(&expression));
            schedule_after(id, Action::after_end);
        }
    }

    // Lower the after body; its value is discarded and its names are unsafe afterwards.
    void schedule_after(const ast::ExprId &id, const Action action) {
        const auto &after = std::get<ast::TryExpression>(state.module.syntax->expression(id).value).after;
        if (!after) {
            return;
        }
        pending.push_back({id, action});
        for (auto body = after->rbegin(); body != after->rend(); ++body) {
            pending.push_back({*body});
        }
    }

    // Finish the normal path; when anything protected can raise, take that exception at the after handler and
    // lower the after body again before raising it.
    void after_end(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        const auto &scope = afters.at(&expression);
        state.bindings = scope.bindings;
        if (scope.handler->use_empty()) {
            scope.handler->eraseFromParent();
            close_after(expression);
            return;
        }
        auto &path = after_paths.at(&expression);
        path.resume = state.builder.GetInsertBlock();
        state.builder.SetInsertPoint(scope.handler);
        locate_source(state.builder, *state.module.syntax, expression.source);
        path.exception = lower_exception(state);
        schedule_after(id, Action::after_raise_end);
    }

    // Raise the taken exception again after the raising-path after body, then resume the normal path.
    void after_raise_end(const ast::ExprId &id) {
        const auto &expression = state.module.syntax->expression(id);
        const auto &path = after_paths.at(&expression);
        reraise(state, path.exception);
        state.builder.SetInsertPoint(path.resume);
        state.bindings = afters.at(&expression).bindings;
        close_after(expression);
    }

    // The try's value is the selected clause value, never the after body's.
    void close_after(const ast::Expression &expression) {
        state.values.insert_or_assign(&expression, after_paths.at(&expression).result);
        after_paths.erase(&expression);
        afters.erase(&expression);
    }

    // Raise {case_clause, Value}, if_clause, {try_clause, Value} or {else_clause, Value}, re-raise an unmatched
    // exception, or skip an unmatched message, from the last clause's mismatch continuation; drop it when that clause
    // always matches.
    void no_match(const ast::Expression &expression, const CaseJoin &join, const semantic::Branch &last) {
        if (join.next->use_empty()) {
            state.builder.ClearInsertionPoint();
            join.next->eraseFromParent();
        } else if (join.loop) {
            // A message no clause matches stays in the mailbox; the receive goes on with the next one.
            lower_receive(state, abi::v1::ReceiveOperation::skip, nullptr);
            state.builder.CreateBr(join.loop);
        } else if (last.handler) {
            reraise(state, join.exception);
        } else if (std::holds_alternative<ast::TryExpression>(expression.value)) {
            raise_reason(state, abi::v1::ErrorReason::try_clause, join.value);
        } else if (std::holds_alternative<ast::MaybeExpression>(expression.value)) {
            raise_reason(state, abi::v1::ErrorReason::else_clause, join.value);
        } else if (join.value) {
            raise_reason(state, abi::v1::ErrorReason::case_clause, join.value);
        } else {
            raise_reason(state, abi::v1::ErrorReason::if_clause);
        }
    }

    // Join clause values and exported bindings after every clause has been tried.
    void finish_case(const ast::Expression &expression, CaseJoin &join) {
        join.merge->insertInto(&state.entry);
        state.builder.SetInsertPoint(join.merge);
        locate_source(state.builder, *state.module.syntax, expression.source);
        // SSA formation inspects successors, so terminate the join until the surrounding expression resumes.
        auto *boundary = state.builder.CreateUnreachable();
        auto *result = merged(state, join, 0);
        // Matches of the case value answer from the clauses' constructions (OPT1.13).
        std::vector<BlockValue> incoming;
        incoming.reserve(join.incoming.size());
        for (const auto &completed : join.incoming) {
            incoming.emplace_back(completed.block, completed.values.front());
        }
        record_join(state, result, join.merge, std::move(incoming));
        state.bindings = std::move(join.bindings);
        const auto exports = exported(expression);
        for (std::size_t i = 0; i < exports.size(); ++i) {
            state.bindings.insert_or_assign(exports[i], merged(state, join, i + 1));
        }
        boundary->eraseFromParent();
        state.builder.SetInsertPoint(join.merge);
        state.values.insert_or_assign(&expression, result);
        mark_term(state, result);
    }

    // Every action either emits a visited value or schedules the next source-ordered operand.
    void visit(const Visit &visit) {
        const auto &expression = state.module.syntax->expression(visit.id);
        switch (visit.action) {
        case Action::enter:
            enter_node(visit.id);
            break;
        case Action::value: {
            auto *value = lower_value(state, visit.id);
            state.values.insert_or_assign(&expression, value);
            mark_term(state, value);
            break;
        }
        case Action::lazy_left: {
            const auto &binary = std::get<ast::BinaryExpression>(expression.value);
            joins.emplace(&expression, left(state, expression, binary));
            pending.push_back({visit.id, Action::lazy_right});
            pending.push_back({binary.right});
            break;
        }
        case Action::lazy_right: {
            auto *value = right(state, expression, joins.at(&expression));
            state.values.insert_or_assign(&expression, value);
            mark_term(state, value);
            joins.erase(&expression);
            break;
        }
        case Action::record_field:
            state.record_values.at(&expression).at(visit.field) =
                visit.child ? state.values.at(&state.module.syntax->expression(*visit.child))
                            : lower_atom(state, ast::Atom{U"undefined"});
            break;
        default:
            branch(visit);
            break;
        }
    }

    // Clause selection and exception handler actions continue a case, if, catch or try.
    void branch(const Visit &visit) {
        switch (visit.action) {
        case Action::case_select:
            select(visit.id);
            break;
        case Action::case_clause_end:
            clause_end(visit);
            break;
        case Action::catch_end:
            close_catch(state.module.syntax->expression(visit.id));
            break;
        case Action::try_body_end:
            try_body_end(visit.id);
            break;
        case Action::after_end:
            after_end(visit.id);
            break;
        case Action::maybe_match:
            maybe_match(visit);
            break;
        case Action::maybe_end:
            maybe_end(visit.id);
            break;
        case Action::after_raise_end:
            after_raise_end(visit.id);
            break;
        default:
            comprehend(visit);
            break;
        }
    }

    // Comprehension actions: a qualifier, or the templates' end.
    void comprehend(const Visit &visit) {
        if (visit.action == Action::qualifier) {
            lower_qualifier(visit);
        } else {
            comprehension_end(visit.id);
        }
    }
};
} // namespace

llvm::Value *lower_body(ExpressionLowering &state, const ast::ExprId &root) {
    Walk walk{state, {{root}}, {}, {}, {}, {}, {}, {}, {}};
    while (!walk.pending.empty()) {
        const auto visit = walk.pending.back();
        walk.pending.pop_back();
        walk.visit(visit);
    }
    return state.values.at(&state.module.syntax->expression(root));
}
} // namespace clause::codegen
