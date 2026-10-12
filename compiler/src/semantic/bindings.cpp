#include "binding_state.hpp"
#include "funs.hpp"
#include "map_ownership.hpp"
#include "records.hpp"
#include <algorithm>
#include <limits>

namespace clause::semantic {
std::optional<BindingId> BindingCandidate::find(const std::u32string &name) const {
    const auto added = tentative.find(name);
    if (fresh && added != tentative.end()) {
        return added->second;
    }
    const auto existing = incoming.names.find(name);
    if (existing != incoming.names.end()) {
        return existing->second;
    }
    return added == tentative.end() ? std::nullopt : std::optional{added->second};
}

void BindingCandidate::commit(BindingEnvironment &destination) const {
    if (valid) {
        destination.names.insert(tentative.begin(), tentative.end());
    }
}

void BindingCandidate::shadow(BindingEnvironment &destination) const {
    if (!valid) {
        return;
    }
    for (const auto &[name, identity] : tentative) {
        destination.names.insert_or_assign(name, identity);
        destination.unsafe.erase(name);
    }
}

namespace {
// Reuse the identity an earlier clause of an enclosing case gave this name, innermost case first.
std::optional<BindingId> branch_identity(const std::vector<std::map<std::u32string, BindingId>> &branches,
                                         const std::u32string &name) {
    for (auto names = branches.rbegin(); names != branches.rend(); ++names) {
        if (const auto found = names->find(name); found != names->end()) {
            return found->second;
        }
    }
    return {};
}
} // namespace

bool BindingAnalysis::spend(const ast::ExprId &id, const std::size_t amount) {
    if (amount > limit - std::min(work, limit)) {
        if (work <= limit) {
            report(module, &module.syntax->expression(id).source, "binding analysis work limit exceeded", out);
        }
        work = limit + 1;
        return false;
    }
    work += amount;
    return true;
}

void BindingAnalysis::read(const ast::ExprId &id, BindingCandidate &scope, const BindingContext context) {
    const auto &expression = module.syntax->expression(id);
    if (!record_budget(*this, id)) {
        return;
    }
    validate_record(module, expression, out);
    if (const auto *reference = std::get_if<ast::RemoteFunReference>(&expression.value)) {
        read_operands(id, *reference, scope, context);
    } else if (const auto *variable = std::get_if<ast::Variable>(&expression.value)) {
        read_name(id, variable->name, scope, context);
    }
}

std::optional<BindingId> BindingAnalysis::read_name(const ast::ExprId &id, const std::u32string &name,
                                                    BindingCandidate &scope, const BindingContext context) {
    const auto &expression = module.syntax->expression(id);
    if (guard_stack == name) {
        report(module, &expression.source, "stacktrace variable " + utf8(name) + " must not be used in a guard", out);
        scope.valid = false;
        return {};
    }
    const auto identity = scope.find(name);
    const bool shadowed = scope.fresh && scope.tentative.contains(name);
    if (identity && (shadowed || !scope.incoming.unsafe.contains(name))) {
        function.bindings.push_back({id, *identity, BindingUse::read, context});
        return identity;
    }
    const auto message = name == U"_"                           ? "wildcard '_' cannot be read"
                         : scope.incoming.unsafe.contains(name) ? "unsafe variable " + utf8(name)
                                                                : "unbound variable " + utf8(name);
    report(module, &expression.source, message, out);
    scope.valid = false;
    return {};
}

void BindingAnalysis::read_operands(const ast::ExprId &id, const ast::RemoteFunReference &reference,
                                    BindingCandidate &scope, const BindingContext context) {
    if (!dynamic_fun(reference)) {
        return;
    }
    const auto operand = [&](const auto &part) -> std::optional<BindingId> {
        const auto *variable = std::get_if<ast::Variable>(&part);
        return variable ? read_name(id, variable->name, scope, context) : std::nullopt;
    };
    function.fun_operands.insert_or_assign(
        &module.syntax->expression(id),
        std::array{operand(reference.module), operand(reference.name), operand(reference.arity)});
}

void BindingAnalysis::define(const ast::ExprId &id, BindingCandidate &scope, const BindingContext context,
                             const std::optional<std::size_t> argument) {
    const auto &expression = module.syntax->expression(id);
    const auto &name = std::get<ast::Variable>(expression.value).name;
    if (name == U"_") {
        return;
    }
    if (scope.fresh) {
        define_fresh(id, scope, context, name);
        return;
    }
    if (scope.incoming.unsafe.contains(name)) {
        read(id, scope, context);
        return;
    }
    if (const auto existing = scope.find(name)) {
        function.bindings.push_back({id, *existing, BindingUse::exact_check, context});
        return;
    }
    if (const auto sibling = scope.incoming.checks.find(name); sibling != scope.incoming.checks.end()) {
        scope.tentative.emplace(name, sibling->second);
        function.bindings.push_back({id, sibling->second, BindingUse::exact_check, context});
        return;
    }
    auto identity = branch_identity(branch_names, name);
    if (!identity) {
        auto &definitions = function.clause_bindings.at(clause).definitions;
        identity = BindingId{clause, definitions.size()};
        definitions.push_back({name, id, argument});
    }
    scope.tentative.emplace(name, *identity);
    function.bindings.push_back({id, *identity, BindingUse::definition, context});
}

void BindingAnalysis::define_fresh(const ast::ExprId &id, BindingCandidate &scope, const BindingContext context,
                                   const std::u32string &name) {
    if (const auto added = scope.tentative.find(name); added != scope.tentative.end()) {
        function.bindings.push_back({id, added->second, BindingUse::exact_check, context});
        return;
    }
    auto &definitions = function.clause_bindings.at(clause).definitions;
    const BindingId identity{clause, definitions.size()};
    definitions.push_back({name, id, std::nullopt});
    scope.tentative.emplace(name, identity);
    function.bindings.push_back({id, identity, BindingUse::definition, context});
}

void bind_guard(BindingAnalysis &state, const ast::GuardSyntax &guard, const BindingCandidate &head) {
    BindingEnvironment visible = head.incoming;
    head.commit(visible);
    for (const auto &alternative : guard.alternatives) {
        bind_expressions(state, alternative.tests, visible, BindingContext::guard);
    }
}

namespace {

// Only the successful head candidate is made available to the body; the incoming scope stays empty.
void bind_clause(BindingAnalysis &state, const ast::FunctionClause &clause) {
    const BindingEnvironment incoming;
    BindingCandidate head{incoming, {}};
    for (std::size_t i = 0; i < clause.arguments.size(); ++i) {
        bind_pattern(state, clause.arguments[i], head, BindingContext::head, i);
    }
    if (clause.guard) {
        bind_guard(state, *clause.guard, head);
    }
    BindingEnvironment body;
    head.commit(body);
    bind_expressions(state, clause.body, body, BindingContext::body);
}

// A new analysis or budget failure must not expose stale or partially constructed side tables.
void clear_bindings(Module &module) {
    for (auto &function : module.functions) {
        function.bindings.clear();
        function.clause_bindings.clear();
        function.exports.clear();
        function.patterns.clear();
        function.captures.clear();
        function.fun_names.clear();
        function.fun_operands.clear();
        function.owned_map_updates.clear();
    }
    module.funs.clear();
    module.fun_entries.clear();
}
} // namespace

void bind_parameters(Module &module, const Reporter &out, const std::size_t work_limit) {
    std::size_t work = 0;
    const auto limit = std::min(work_limit, std::numeric_limits<std::size_t>::max() - 1);
    clear_bindings(module);
    bool failed = false;
    const Reporter transactional = [&](const Diagnostic &diagnostic) {
        failed = failed || diagnostic.severity == Severity::error;
        out(diagnostic);
    };
    for (auto &function : module.functions) {
        const auto &clauses = std::get<ast::Function>(module.syntax->form(function.form).value).clauses;
        function.clause_bindings.resize(clauses.size());
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            BindingAnalysis state{module, function, transactional, i, work, limit};
            bind_clause(state, clauses[i]);
            if (work > limit) {
                clear_bindings(module);
                return;
            }
        }
    }
    if (failed) {
        clear_bindings(module);
    } else {
        index_funs(module);
        mark_owned_map_updates(module);
    }
}

std::optional<std::size_t> binding_argument(const Function &function, const ast::ExprId &expression) {
    const auto found = std::ranges::find(function.bindings, expression, &Binding::expression);
    if (found == function.bindings.end() || found->use != BindingUse::read) {
        return {};
    }
    const auto identity = found->identity;
    return function.clause_bindings.at(identity.clause).definitions.at(identity.local).argument;
}
} // namespace clause::semantic
