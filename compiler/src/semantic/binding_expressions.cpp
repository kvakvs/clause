#include "binding_state.hpp"
#include "capabilities.hpp"
#include "pattern_state.hpp"
#include "records.hpp"
#include <algorithm>
#include <cstdint>
#include <set>
#include <span>

namespace clause::semantic {
namespace {
enum class Action : std::uint8_t {
    expression,
    pattern,
    conditional_enter,
    conditional_exit,
    siblings_enter,
    sibling_end,
    siblings_exit,
    branch,
    branch_end,
    comprehension_enter,
    generator,
    comprehension_exit,
    fun_clause,
    fun_clause_end,
    fun_exit
};

struct Visit {
    // Explicit tasks preserve RHS-first matches and isolate conditionally evaluated definitions.
    ast::ExprId id;
    Action action = Action::expression;
    // Branch tasks name the case or if clause they start or finish; generator tasks their qualifier.
    std::size_t clause = 0;
};

struct SiblingScope {
    // All siblings read the same incoming scope; successful definitions accumulate for later expressions.
    BindingEnvironment incoming;
    BindingEnvironment accumulated;
};

struct CaseScope {
    // Every clause starts from the scope after a case scrutinee, before an if or after a try body; finished scopes
    // wait for the join.
    BindingEnvironment incoming;
    std::vector<BindingEnvironment> clauses;
    // A try's catch clauses (from `first_handler` on) start before the try with the body's names unsafe (OTP Uvt).
    std::optional<BindingEnvironment> handlers = {};
    std::size_t first_handler = 0;
};

struct FunScope {
    // The scope at a fun, which comes back after it.
    BindingEnvironment incoming;
    // Clause names of enclosing cases do not reach into the fun; they come back after it.
    std::vector<std::map<std::u32string, BindingId>> branch_names;
    // The clause's definition count and the binding events before the fun: an event inside it that uses an earlier
    // definition captures it.
    std::size_t first_local;
    std::size_t first_binding;
    // The scope every clause starts from: the scope at the fun plus a named fun's own name.
    BindingEnvironment inside = {};
};

struct Scopes {
    // Indirection avoids allocating map moves during Windows scope-stack relocation.
    std::vector<std::unique_ptr<BindingEnvironment>> conditional;
    std::vector<std::unique_ptr<SiblingScope>> siblings;
    std::vector<std::unique_ptr<CaseScope>> cases;
    // The scope before each enclosing comprehension, which binds nothing outside itself.
    std::vector<std::unique_ptr<BindingEnvironment>> comprehensions;
    // The enclosing anonymous funs.
    std::vector<std::unique_ptr<FunScope>> funs;
};

// Merge constraints without exposing an earlier sibling's new names to the next sibling's reads.
void finish_sibling(BindingEnvironment &environment, SiblingScope &scope) {
    scope.accumulated.names.insert(environment.names.begin(), environment.names.end());
    scope.accumulated.unsafe.insert(environment.unsafe.begin(), environment.unsafe.end());
    environment = scope.incoming;
    environment.checks.insert(scope.accumulated.names.begin(), scope.accumulated.names.end());
}

// A record wildcard that initializes nothing is checked last, like a skipped RHS: names it binds stay unsafe.
void schedule_unused_wildcard(const Module &module, const ast::ExprValue &value, std::vector<Visit> &pending) {
    const auto *record = std::get_if<ast::RecordExpression>(&value);
    const auto wildcard = record ? unused_wildcard(module, *record) : std::nullopt;
    if (wildcard) {
        pending.push_back({*wildcard, Action::conditional_exit});
        pending.push_back({*wildcard});
        pending.push_back({*wildcard, Action::conditional_enter});
    }
}

// Sequence nodes export immediately; ordinary sibling operands export together after all are analyzed.
void schedule_children(const Module &module, const ast::ExprId &id, const ast::ExprValue &value,
                       std::vector<Visit> &pending) {
    schedule_unused_wildcard(module, value, pending);
    const auto children = std::holds_alternative<ast::RecordExpression>(value)
                              ? expression_children(module, module.syntax->expression(id))
                              : binding_children(value);
    const bool siblings = children.size() > 1 && !std::holds_alternative<ast::BlockExpression>(value);
    if (siblings) {
        pending.push_back({id, Action::siblings_exit});
    }
    for (auto child = children.rbegin(); child != children.rend(); ++child) {
        if (siblings) {
            pending.push_back({*child, Action::sibling_end});
        }
        pending.push_back({*child});
    }
    if (siblings) {
        pending.push_back({id, Action::siblings_enter});
    }
}

// A skipped RHS must not publish its new names, but later reads must diagnose them as unsafe.
void finish_conditional(BindingEnvironment &environment, BindingEnvironment incoming) {
    for (const auto &[name, identity] : environment.names) {
        if (!incoming.names.contains(name)) {
            incoming.unsafe.insert(name);
        }
    }
    incoming.unsafe.insert(environment.unsafe.begin(), environment.unsafe.end());
    environment = std::move(incoming);
}

// Schedule short-circuit operands with a private environment around the optional RHS.
bool conditional(const ast::ExprValue &value, std::vector<Visit> &pending) {
    const auto *binary = std::get_if<ast::BinaryExpression>(&value);
    if (!binary ||
        (binary->operation != ast::BinaryOperator::and_also && binary->operation != ast::BinaryOperator::or_else)) {
        return false;
    }
    pending.push_back({binary->right, Action::conditional_exit});
    pending.push_back({binary->right});
    pending.push_back({binary->right, Action::conditional_enter});
    pending.push_back({binary->left});
    return true;
}

// A catch may stop its expression anywhere, so names bound inside it become unsafe afterwards (OTP vtunsafe).
bool protect(const ast::ExprValue &value, std::vector<Visit> &pending) {
    const auto *guarded = std::get_if<ast::CatchExpression>(&value);
    if (!guarded) {
        return false;
    }
    pending.push_back({guarded->expression, Action::conditional_exit});
    pending.push_back({guarded->expression});
    pending.push_back({guarded->expression, Action::conditional_enter});
    return true;
}

// Queues expression visits so the first expression is analyzed first.
void push_reversed(std::vector<Visit> &pending, const std::vector<ast::ExprId> &expressions) {
    for (auto item = expressions.rbegin(); item != expressions.rend(); ++item) {
        pending.push_back({*item});
    }
}

// Everything a try binds is unsafe afterwards: its body and clauses form one conditional scope, then the after body
// another. Of and catch clauses start once the body is analyzed.
bool attempt(const ast::ExprId &id, const ast::ExprValue &value, std::vector<Visit> &pending) {
    const auto *guarded = std::get_if<ast::TryExpression>(&value);
    if (!guarded) {
        return false;
    }
    if (guarded->after) {
        pending.push_back({id, Action::conditional_exit});
        push_reversed(pending, *guarded->after);
        pending.push_back({id, Action::conditional_enter});
    }
    pending.push_back({id, Action::conditional_exit});
    if (!branch_clauses(value).empty()) {
        pending.push_back({id, Action::branch});
    }
    push_reversed(pending, guarded->body);
    pending.push_back({id, Action::conditional_enter});
    return true;
}

// Nothing a maybe binds is exported (OTP): its body and else clauses form one conditional scope. Each ?= binds its
// pattern after its value, for the following body expressions; else clauses see the body's names as unsafe.
bool conditional_block(const ast::Module &syntax, const ast::ExprId &id, const ast::ExprValue &value,
                       std::vector<Visit> &pending) {
    const auto *block = std::get_if<ast::MaybeExpression>(&value);
    if (!block) {
        return false;
    }
    pending.push_back({id, Action::conditional_exit});
    if (block->otherwise) {
        pending.push_back({id, Action::branch});
    }
    for (auto item = block->body.rbegin(); item != block->body.rend(); ++item) {
        if (const auto *match = std::get_if<ast::MaybeMatch>(&*item)) {
            pending.push_back({pattern_root(syntax, match->pattern), Action::pattern});
            pending.push_back({match->value});
        } else {
            pending.push_back({std::get<ast::ExprId>(*item)});
        }
    }
    pending.push_back({id, Action::conditional_enter});
    return true;
}

// Templates read the same scope as siblings: a name one of them binds is not visible to another (OTP).
void schedule_templates(const ast::ExprId &id, const ast::ExprValue &value, std::vector<Visit> &pending) {
    const auto templates = comprehension_templates(value);
    if (templates.size() == 1) {
        pending.push_back({templates.front()});
        return;
    }
    pending.push_back({id, Action::siblings_exit});
    for (auto item = templates.rbegin(); item != templates.rend(); ++item) {
        pending.push_back({*item, Action::sibling_end});
        pending.push_back({*item});
    }
    pending.push_back({id, Action::siblings_enter});
}

// Nothing a comprehension binds is visible after it. Each qualifier in order evaluates its generator inputs or
// filter, then binds its generator patterns for the following qualifiers and the templates.
bool comprehension(const ast::ExprId &id, const ast::ExprValue &value, std::vector<Visit> &pending) {
    const auto *qualifiers = comprehension_qualifiers(value);
    if (!qualifiers) {
        return false;
    }
    pending.push_back({id, Action::comprehension_exit});
    schedule_templates(id, value, pending);
    for (std::size_t i = qualifiers->size(); i != 0; --i) {
        const auto parts = zipped((*qualifiers)[i - 1]);
        pending.push_back({id, Action::generator, i - 1});
        for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
            // A filter inside a zip group is rejected by capability analysis and analyzed no further.
            if (const auto input = generator_input(*part)) {
                pending.push_back({*input});
            } else if (parts.size() == 1) {
                pending.push_back({std::get<ast::FilterQualifier>(part->value).expression});
            }
        }
    }
    pending.push_back({id, Action::comprehension_enter});
    return true;
}

// Generator patterns shadow incoming names; the patterns of a zip group bind together, so a repeated name must match.
void bind_generators(BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment) {
    const auto &qualifier = comprehension_qualifiers(state.module.syntax->expression(visit.id).value)->at(visit.clause);
    BindingCandidate candidate{environment, {}};
    candidate.fresh = true;
    for (const auto &part : zipped(qualifier)) {
        for (const auto &pattern : generator_patterns(part)) {
            bind_pattern(state, pattern, candidate, BindingContext::body);
        }
    }
    candidate.shadow(environment);
}

// Save the scope before a comprehension, bind a qualifier's generator patterns, or restore the saved scope.
bool comprehension_scope(BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment, Scopes &scopes) {
    switch (visit.action) {
    case Action::comprehension_enter:
        scopes.comprehensions.push_back(std::make_unique<BindingEnvironment>(environment));
        return true;
    case Action::generator:
        bind_generators(state, visit, environment);
        return true;
    case Action::comprehension_exit:
        environment = std::move(*scopes.comprehensions.back());
        scopes.comprehensions.pop_back();
        return true;
    default:
        return false;
    }
}

// A case evaluates its scrutinee and a receive its timeout in the enclosing scope before any clause is bound; an if
// starts with its clauses. A receive's after body is its last clause.
bool branches(const ast::ExprId &id, const ast::ExprValue &value, std::vector<Visit> &pending) {
    const auto *receive = std::get_if<ast::ReceiveExpression>(&value);
    if (!std::holds_alternative<ast::CaseExpression>(value) && !std::holds_alternative<ast::IfExpression>(value) &&
        !receive) {
        return false;
    }
    pending.push_back({id, Action::branch});
    if (const auto *selection = std::get_if<ast::CaseExpression>(&value)) {
        pending.push_back({selection->value});
    } else if (receive && receive->after) {
        pending.push_back({receive->after->timeout});
    }
    return true;
}

// A catch clause's stack variable must be new: neither bound before nor in its class or reason pattern.
void bind_stack(BindingAnalysis &state, const ast::ExprId &id, BindingCandidate &head) {
    const auto &expression = state.module.syntax->expression(id);
    const auto &name = std::get<ast::Variable>(expression.value).name;
    if (name != U"_" && (head.find(name) || head.incoming.unsafe.contains(name))) {
        report(state.module, &expression.source, "stacktrace variable " + utf8(name) + " must not be previously bound",
               state.out);
        return;
    }
    bind_pattern(state, id, head, BindingContext::body);
}

// Bind a clause pattern; a catch clause matches Class:Reason:Stack left to right.
void bind_head(BindingAnalysis &state, const Branch &clause, BindingCandidate &head) {
    if (clause.handler && clause.handler->exception_class) {
        bind_pattern(state, *clause.handler->exception_class, head, BindingContext::body);
    }
    if (clause.pattern) {
        bind_pattern(state, *clause.pattern, head, BindingContext::body);
    }
    if (clause.handler && clause.handler->stacktrace) {
        bind_stack(state, *clause.handler->stacktrace, head);
    }
}

// The guard of a catch clause must not read its stack variable.
std::optional<std::u32string> stack_name(const ast::Module &syntax, const Branch &clause) {
    if (!clause.handler || !clause.handler->stacktrace) {
        return {};
    }
    const auto &name = std::get<ast::Variable>(syntax.expression(*clause.handler->stacktrace).value).name;
    return name == U"_" ? std::nullopt : std::optional{name};
}

// Bind one clause's pattern and guard over the incoming scope, then schedule its body before the clause end.
void begin_branch(BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment,
                  std::vector<Visit> &pending) {
    const auto clause = branch_clauses(state.module.syntax->expression(visit.id).value).at(visit.clause);
    BindingCandidate head{environment, {}};
    bind_head(state, clause, head);
    if (clause.guard) {
        state.guard_stack = stack_name(*state.module.syntax, clause);
        bind_guard(state, *clause.guard, head);
        state.guard_stack.reset();
    }
    head.commit(environment);
    pending.push_back({visit.id, Action::branch_end, visit.clause});
    for (auto body = clause.body->rbegin(); body != clause.body->rend(); ++body) {
        pending.push_back({*body});
    }
}

// Names bound by every clause are exported; names bound by only some clauses, or unsafe in any, become unsafe.
BindingEnvironment join_branches(BindingAnalysis &state, const ast::ExprId &id, const CaseScope &scope) {
    auto result = scope.incoming;
    std::vector<BindingId> exports;
    for (const auto &[name, identity] : state.branch_names.back()) {
        const bool everywhere = std::ranges::all_of(scope.clauses, [&name](const auto &clause) {
            return clause.names.contains(name) && !clause.unsafe.contains(name);
        });
        if (everywhere) {
            result.names.emplace(name, identity);
            exports.push_back(identity);
        } else {
            result.unsafe.insert(name);
        }
    }
    for (const auto &clause : scope.clauses) {
        result.unsafe.insert(clause.unsafe.begin(), clause.unsafe.end());
    }
    state.function.exports.insert_or_assign(&state.module.syntax->expression(id), std::move(exports));
    return result;
}

// Open the clause scope of a case, if or try; a try's handler scope is the try's conditional scope (innermost).
std::unique_ptr<CaseScope> open_branches(const ast::ExprValue &value, const BindingEnvironment &environment,
                                         const Scopes &scopes) {
    auto scope = std::make_unique<CaseScope>(environment, std::vector<BindingEnvironment>{});
    if (std::holds_alternative<ast::TryExpression>(value) || std::holds_alternative<ast::MaybeExpression>(value)) {
        scope->first_handler = first_handler(value);
        scope->handlers = environment;
        finish_conditional(*scope->handlers, *scopes.conditional.back());
    }
    return scope;
}

// Start a clause from the case's incoming scope (a try's catch clauses from its handler scope); the first clause
// opens the case scope.
void branch(BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment, std::vector<Visit> &pending,
            Scopes &scopes) {
    if (visit.clause == 0) {
        const auto &value = state.module.syntax->expression(visit.id).value;
        scopes.cases.push_back(open_branches(value, environment, scopes));
        state.branch_names.emplace_back();
    }
    const auto &scope = *scopes.cases.back();
    environment = scope.handlers && visit.clause >= scope.first_handler ? *scope.handlers : scope.incoming;
    begin_branch(state, visit, environment, pending);
}

// Record the clause's new names for later clauses, then start the next clause or join them all.
void branch_end(BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment,
                std::vector<Visit> &pending, Scopes &scopes) {
    auto &current = *scopes.cases.back();
    for (const auto &[name, identity] : environment.names) {
        if (!current.incoming.names.contains(name)) {
            state.branch_names.back().emplace(name, identity);
        }
    }
    current.clauses.push_back(std::move(environment));
    if (visit.clause + 1 < branch_clauses(state.module.syntax->expression(visit.id).value).size()) {
        pending.push_back({visit.id, Action::branch, visit.clause + 1});
        return;
    }
    environment = join_branches(state, visit.id, current);
    state.branch_names.pop_back();
    scopes.cases.pop_back();
}

// Guards may read both operands for diagnostics, but a match can never introduce guard bindings.
void match(const BindingAnalysis &state, const ast::ExprId &id, const ast::MatchExpression &value,
           const BindingContext context, std::vector<Visit> &pending) {
    if (context == BindingContext::guard) {
        report(state.module, &state.module.syntax->expression(id).source, "guards cannot bind variables", state.out);
        pending.push_back({value.left});
    } else {
        pending.push_back({value.left, Action::pattern});
    }
    pending.push_back({value.right});
}

// A fun binds nothing outside itself: its clauses are analyzed one after the other, then the scope at
// the fun comes back.
bool fun_scope(const ast::ExprId &id, const ast::ExprValue &value, std::vector<Visit> &pending) {
    if (!fun_clauses(value)) {
        return false;
    }
    pending.push_back({id, Action::fun_exit});
    pending.push_back({id, Action::fun_clause, 0});
    return true;
}

// Schedule syntax that opens binding scopes of its own; ordinary value syntax returns false.
bool scoped(const ast::Module &syntax, const ast::ExprId &id, const ast::ExprValue &value,
            std::vector<Visit> &pending) {
    return conditional(value, pending) || branches(id, value, pending) || protect(value, pending) ||
           attempt(id, value, pending) || conditional_block(syntax, id, value, pending) ||
           comprehension(id, value, pending) || fun_scope(id, value, pending);
}

// Ordinary value traversal never descends into deferred branch, exception or closure scopes.
void expression(BindingAnalysis &state, const ast::ExprId &id, const BindingEnvironment &environment,
                const BindingContext context, std::vector<Visit> &pending) {
    const auto &value = state.module.syntax->expression(id).value;
    if (const auto *binary = std::get_if<ast::Bitstring>(&value)) {
        pattern_binary(state, id, *binary, false);
    }
    if (const auto *assignment = std::get_if<ast::MatchExpression>(&value)) {
        match(state, id, *assignment, context, pending);
        return;
    }
    if (scoped(*state.module.syntax, id, value, pending)) {
        return;
    }
    BindingCandidate scope{environment, {}};
    state.read(id, scope, context);
    if (state.work <= state.limit) {
        schedule_children(state.module, id, value, pending);
    }
}

// Charge copied scope entries as well as nodes so wide nested scopes cannot evade the shared budget.
bool scope_budget(BindingAnalysis &state, const Visit &visit, const BindingEnvironment &environment) {
    const auto amount = environment.names.size() + environment.unsafe.size() + environment.checks.size() + 1;
    return state.spend(visit.id, amount);
}

// Open or close the conditional, sibling and case scopes that surround scheduled expressions.
void scope(BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment, std::vector<Visit> &pending,
           Scopes &scopes) {
    switch (visit.action) {
    case Action::conditional_enter:
        scopes.conditional.push_back(std::make_unique<BindingEnvironment>(environment));
        break;
    case Action::conditional_exit:
        finish_conditional(environment, std::move(*scopes.conditional.back()));
        scopes.conditional.pop_back();
        break;
    case Action::siblings_enter:
        scopes.siblings.push_back(std::make_unique<SiblingScope>(environment, environment));
        break;
    case Action::sibling_end:
        finish_sibling(environment, *scopes.siblings.back());
        break;
    case Action::siblings_exit:
        environment = std::move(scopes.siblings.back()->accumulated);
        scopes.siblings.pop_back();
        break;
    case Action::branch:
        branch(state, visit, environment, pending, scopes);
        break;
    default:
        branch_end(state, visit, environment, pending, scopes);
        break;
    }
}

// A named fun's name is a new definition every clause sees, shadowing an outer name; it is never captured.
void name_fun(BindingAnalysis &state, const ast::ExprId &id, BindingEnvironment &incoming) {
    const auto &fun = std::get<ast::FunExpression>(state.module.syntax->expression(id).value);
    if (!fun.name || fun.name->name == U"_") {
        return;
    }
    auto &definitions = state.function.clause_bindings.at(state.clause).definitions;
    const BindingId identity{state.clause, definitions.size()};
    definitions.push_back({fun.name->name, id, std::nullopt});
    incoming.names.insert_or_assign(fun.name->name, identity);
    incoming.unsafe.erase(fun.name->name);
    state.function.fun_names.insert_or_assign(&state.module.syntax->expression(id), identity);
}

// Open the fun's scope before its first clause: sibling checks and case-clause names stay outside.
FunScope &open_fun(BindingAnalysis &state, const ast::ExprId &id, const BindingEnvironment &environment,
                   Scopes &scopes) {
    auto scope = std::make_unique<FunScope>(environment, std::move(state.branch_names),
                                            state.function.clause_bindings.at(state.clause).definitions.size(),
                                            state.function.bindings.size());
    scope->incoming.checks.clear();
    state.branch_names.clear();
    scope->inside = scope->incoming;
    name_fun(state, id, scope->inside);
    scopes.funs.push_back(std::move(scope));
    return *scopes.funs.back();
}

// Bind one fun clause's head (new names shadow the scope at the fun) and guard, then schedule its body.
void fun_clause(BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment,
                std::vector<Visit> &pending, Scopes &scopes) {
    const auto &scope = visit.clause == 0 ? open_fun(state, visit.id, environment, scopes) : *scopes.funs.back();
    const auto &clause = fun_clauses(state.module.syntax->expression(visit.id).value)->at(visit.clause);
    environment = scope.inside;
    BindingCandidate head{scope.inside, {}};
    head.fresh = true;
    for (const auto &argument : clause.arguments) {
        bind_pattern(state, argument, head, BindingContext::head);
    }
    head.shadow(environment);
    if (clause.guard) {
        for (const auto &alternative : clause.guard->alternatives) {
            auto visible = environment;
            bind_expressions(state, alternative.tests, visible, BindingContext::guard);
        }
    }
    pending.push_back({visit.id, Action::fun_clause_end, visit.clause});
    for (auto body = clause.body.rbegin(); body != clause.body.rend(); ++body) {
        pending.push_back({*body});
    }
}

// Start the next clause from the scope at the fun.
void fun_clause_end(const BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment,
                    std::vector<Visit> &pending, const Scopes &scopes) {
    environment = scopes.funs.back()->inside;
    if (visit.clause + 1 < fun_clauses(state.module.syntax->expression(visit.id).value)->size()) {
        pending.push_back({visit.id, Action::fun_clause, visit.clause + 1});
    }
}

// Record the definitions the fun uses from outside it, in definition order, and restore the scope at the fun.
void fun_exit(BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment, Scopes &scopes) {
    auto &scope = *scopes.funs.back();
    std::set<BindingId> captures;
    const auto events = std::span(state.function.bindings).subspan(scope.first_binding);
    for (const auto &event : events) {
        if (event.use != BindingUse::definition && event.identity.local < scope.first_local) {
            captures.insert(event.identity);
        }
    }
    state.function.captures.insert_or_assign(&state.module.syntax->expression(visit.id),
                                             std::vector(captures.begin(), captures.end()));
    environment = std::move(scope.incoming);
    state.branch_names = std::move(scope.branch_names);
    scopes.funs.pop_back();
}

// Analyze one anonymous fun scope task; other actions return false.
bool fun_task(BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment, std::vector<Visit> &pending,
              Scopes &scopes) {
    switch (visit.action) {
    case Action::fun_clause:
        fun_clause(state, visit, environment, pending, scopes);
        return true;
    case Action::fun_clause_end:
        fun_clause_end(state, visit, environment, pending, scopes);
        return true;
    case Action::fun_exit:
        fun_exit(state, visit, environment, scopes);
        return true;
    default:
        return false;
    }
}

// Task boundaries are the only publication points; no pattern walk mutates its incoming environment.
bool execute(BindingAnalysis &state, const Visit &visit, BindingEnvironment &environment, const BindingContext context,
             std::vector<Visit> &pending, Scopes &scopes) {
    if (visit.action != Action::expression && !scope_budget(state, visit, environment)) {
        return false;
    }
    if (visit.action == Action::pattern) {
        BindingCandidate candidate{environment, {}};
        bind_pattern(state, visit.id, candidate, context);
        candidate.commit(environment);
    } else if (visit.action == Action::expression) {
        expression(state, visit.id, environment, context, pending);
    } else if (!comprehension_scope(state, visit, environment, scopes) &&
               !fun_task(state, visit, environment, pending, scopes)) {
        scope(state, visit, environment, pending, scopes);
    }
    return true;
}
} // namespace

void bind_expressions(BindingAnalysis &state, const std::vector<ast::ExprId> &roots, BindingEnvironment &environment,
                      const BindingContext context) {
    std::vector<Visit> pending;
    for (auto root = roots.rbegin(); root != roots.rend(); ++root) {
        pending.push_back({*root});
    }
    Scopes scopes;
    while (!pending.empty()) {
        const auto visit = pending.back();
        pending.pop_back();
        if (!state.spend(visit.id) || !execute(state, visit, environment, context, pending, scopes)) {
            return;
        }
    }
}
} // namespace clause::semantic
