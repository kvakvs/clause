#include "capabilities.hpp"
#include "funs.hpp"
#include "records.hpp"
#include "services.hpp"
#include <algorithm>
#include <limits>

namespace clause::semantic {
namespace {
struct GuardSyntax {
    // Closed syntax allowlist checks legality independently of executable support.
    template <typename T> bool operator()(const T &) const { return false; }

    bool operator()(const ast::Variable &) const { return true; }

    bool operator()(const ast::IntegerLiteral &) const { return true; }

    bool operator()(const ast::CharacterLiteral &) const { return true; }

    bool operator()(const ast::Atom &) const { return true; }

    bool operator()(const ast::FloatLiteral &) const { return true; }

    bool operator()(const ast::StringLiteral &) const { return true; }

    bool operator()(const ast::Group &) const { return true; }

    bool operator()(const ast::Tuple &) const { return true; }

    bool operator()(const ast::List &) const { return true; }

    bool operator()(const ast::Bitstring &) const { return true; }

    bool operator()(const ast::RecordExpression &value) const { return !value.base; }

    bool operator()(const ast::RecordAccess &) const { return true; }

    bool operator()(const ast::RecordIndex &) const { return true; }

    bool operator()(const ast::MapExpression &value) const {
        return value.base || std::ranges::none_of(value.fields, [](const auto &field) {
                   return field.kind == ast::MapFieldKind::exact;
               });
    }

    bool operator()(const ast::UnaryExpression &) const { return true; }

    bool operator()(const ast::BinaryExpression &value) const {
        return value.operation != ast::BinaryOperator::send && value.operation != ast::BinaryOperator::append &&
               value.operation != ast::BinaryOperator::subtract_list;
    }
};

// Record a body builtin: its inline operation, or else the bridge builtin it calls.
void body_service(BindingAnalysis &state, const ast::ExprId &id, const ast::CallExpression &call) {
    const auto *expression = &state.module.syntax->expression(id);
    if (const auto builtin = body_builtin(state, id, call)) {
        const auto operation = immediate_service(*builtin);
        state.function.services.emplace(
            expression,
            ServiceResolution{*builtin, false, false, operation, operation ? std::nullopt : bridge_builtin(*builtin)});
    } else if (const auto other = module_builtin(*state.module.syntax, call)) {
        state.function.services.emplace(expression,
                                        ServiceResolution{other->first, false, false, std::nullopt, other->second});
    } else if (const auto imported = imported_builtin(state.module, call)) {
        state.function.services.emplace(
            expression, ServiceResolution{imported->first, false, false, std::nullopt, imported->second});
    }
}

// Whether a body call of this guard signature runs as its bridge builtin in portions (docs/builtins.md#portions).
bool portioned(const FunctionKey &key) { return key.name == U"length" && key.arity == 1; }

// The resolution of an authorized guard signature: its inline operation, or in a body the bridge builtin of a
// signature without one or that runs in portions (length/1).
ServiceResolution guard_service(const FunctionKey &key, const bool guard, const bool legacy) {
    const auto operation = guard || !portioned(key) ? immediate_service(key) : std::nullopt;
    const auto builtin = guard || operation ? std::nullopt : bridge_builtin(key);
    return ServiceResolution{key, true, legacy, operation, builtin};
}

// A call that is no guard signature: illegal in a guard, else possibly a body builtin.
void unresolved(BindingAnalysis &state, const ast::ExprId &id, const ast::CallExpression &call, const bool guard) {
    if (guard) {
        report(state.module, &state.module.syntax->expression(id).source,
               "illegal guard call (not an authorized erlang guard signature)", state.out);
    } else {
        body_service(state, id, call);
    }
}

// Authorize erlang identities before any lowering or runtime lookup; local body calls retain normal resolution.
void call(BindingAnalysis &state, const ast::ExprId &id, const ast::CallExpression &call, const bool guard,
          const bool top) {
    const auto resolved = guard_identity(state, id, call, guard && top);
    if (!resolved) {
        unresolved(state, id, call, guard);
        return;
    }
    const auto &expression = state.module.syntax->expression(id);
    const auto &target = state.module.syntax->expression(ungroup(*state.module.syntax, call.target)).value;
    const auto *name = std::get_if<ast::Atom>(&target);
    const bool legacy = guard && top && name && name->name != resolved->name;
    if (resolved->name == U"is_record" && resolved->arity >= 2) {
        validate_record_test(state.module, expression, call, guard, state.out);
    }
    state.function.services.emplace(&expression, guard_service(*resolved, guard, legacy));
}

struct Visit {
    // Parentheses preserve the top-level legacy-test context; other descendants are ordinary guard expressions.
    ast::ExprId id;
    bool top;
    // Case clause guards nested in a body are guard contexts of their own.
    bool guard;
};

// Each guard test is a top-level legacy-test position.
void push_guard(const ast::GuardSyntax &guard, std::vector<Visit> &pending) {
    for (const auto &alternative : guard.alternatives) {
        for (const auto &test : alternative.tests) {
            pending.push_back({test, true, true});
        }
    }
}

// A case scrutinee, a receive timeout, a try's body and after body, or a maybe body stay in the current context.
void branch_operands(const ast::ExprValue &value, const Visit &visit, std::vector<Visit> &pending) {
    std::vector<ast::ExprId> operands;
    if (const auto *selection = std::get_if<ast::CaseExpression>(&value)) {
        operands.push_back(selection->value);
    } else if (const auto *receive = std::get_if<ast::ReceiveExpression>(&value); receive && receive->after) {
        operands.push_back(receive->after->timeout);
    } else if (const auto *attempt = std::get_if<ast::TryExpression>(&value)) {
        operands = attempt->body;
        if (attempt->after) {
            operands.insert(operands.end(), attempt->after->begin(), attempt->after->end());
        }
    } else if (const auto *block = std::get_if<ast::MaybeExpression>(&value)) {
        operands = maybe_operands(*block);
    }
    for (const auto &operand : operands) {
        pending.push_back({operand, false, visit.guard});
    }
}

// Schedule branch operands and clause bodies in the current context and clause guards as top-level guard tests.
bool branch_guards(const ast::Expression &expression, const Visit &visit, std::vector<Visit> &pending) {
    const auto clauses = branch_clauses(expression.value);
    if (clauses.empty()) {
        return false;
    }
    branch_operands(expression.value, visit, pending);
    for (const auto &clause : clauses) {
        if (clause.guard) {
            push_guard(*clause.guard, pending);
        }
        for (const auto &body : *clause.body) {
            pending.push_back({body, false, visit.guard});
        }
    }
    return true;
}

// An anonymous fun's clause guards are top-level guard tests and its bodies ordinary expressions.
bool fun_guards(const ast::Expression &expression, std::vector<Visit> &pending) {
    const auto *clauses = fun_clauses(expression.value);
    if (!clauses) {
        return false;
    }
    for (const auto &clause : *clauses) {
        if (clause.guard) {
            push_guard(*clause.guard, pending);
        }
        for (const auto &body : clause.body) {
            pending.push_back({body, false, false});
        }
    }
    return true;
}

// Whether a filter is a guard test (erl_lint:is_guard_test/3): guard syntax throughout, calling only guard BIFs
// that no local function or import overrides; legacy type tests count at its top level.
bool guard_test(BindingAnalysis &state, const ast::ExprId &root) {
    std::vector<std::pair<ast::ExprId, bool>> pending{{root, true}};
    while (!pending.empty()) {
        const auto [id, top] = pending.back();
        pending.pop_back();
        const auto &expression = state.module.syntax->expression(id);
        const auto *call = std::get_if<ast::CallExpression>(&expression.value);
        if (call ? !guard_identity(state, id, *call, top) : !std::visit(GuardSyntax{}, expression.value)) {
            return false;
        }
        const bool group = std::holds_alternative<ast::Group>(expression.value);
        for (const auto &child : expression_children(state.module, expression)) {
            pending.emplace_back(child, top && group);
        }
    }
    return true;
}

// A generator input stays in the current context; a filter that is a guard test is a top-level guard test (OTP
// lc_guard_tests), any other filter an ordinary expression.
void schedule_qualifier(BindingAnalysis &state, const ast::Qualifier &part, const Visit &visit,
                        std::vector<Visit> &pending) {
    const auto *filter = std::get_if<ast::FilterQualifier>(&part.value);
    if (!filter) {
        pending.push_back({qualifier_expression(part), false, visit.guard});
    } else if (!visit.guard && guard_test(state, filter->expression)) {
        state.function.guard_filters.insert(&state.module.syntax->expression(filter->expression));
        pending.push_back({filter->expression, true, true});
    } else {
        pending.push_back({filter->expression, false, visit.guard});
    }
}

// Schedule every qualifier, then the templates in the current context.
bool comprehension_guards(BindingAnalysis &state, const ast::Expression &expression, const Visit &visit,
                          std::vector<Visit> &pending) {
    const auto *qualifiers = comprehension_qualifiers(expression.value);
    if (!qualifiers) {
        return false;
    }
    for (const auto &qualifier : *qualifiers) {
        for (const auto &part : zipped(qualifier)) {
            schedule_qualifier(state, part, visit, pending);
        }
    }
    for (const auto &item : comprehension_templates(expression.value)) {
        pending.push_back({item, false, visit.guard});
    }
    return true;
}

// Guards may build tuple records only; OTP names native construction separately.
bool native_construction(const Module &module, const ast::ExprValue &value) {
    const auto *record = std::get_if<ast::RecordExpression>(&value);
    if (!record || record->base) {
        return false;
    }
    const auto *layout = record_layout(module, record->identity);
    return (layout && layout->native) || external_record(module, record->identity);
}

// Schedule the operands, clause guards and bodies of expressions with scopes of their own; false for others.
bool scoped_guards(BindingAnalysis &state, const ast::Expression &expression, const Visit &visit,
                   std::vector<Visit> &pending) {
    return branch_guards(expression, visit, pending) || comprehension_guards(state, expression, visit, pending) ||
           fun_guards(expression, pending);
}

// A local fun F/A naming an auto-imported builtin is the external fun erlang:F/A.
void reference(BindingAnalysis &state, const ast::ExprId &id, const ast::LocalFunReference &value) {
    if (const auto key = builtin_fun(state, id, value)) {
        state.function.builtin_funs.emplace(&state.module.syntax->expression(id), *key);
    }
}

// Authorize one node: calls and builtin funs resolve, guard-illegal syntax reports.
void authorize(BindingAnalysis &state, const Visit &visit, const ast::Expression &expression) {
    if (const auto *value = std::get_if<ast::CallExpression>(&expression.value)) {
        call(state, visit.id, *value, visit.guard, visit.top);
    } else if (const auto *fun = std::get_if<ast::LocalFunReference>(&expression.value); fun && !visit.guard) {
        reference(state, visit.id, *fun);
    } else if (visit.guard && native_construction(state.module, expression.value)) {
        report(state.module, &expression.source, "creating a record in a guard is only supported for tuple records",
               state.out);
    } else if (visit.guard && !std::visit(GuardSyntax{}, expression.value)) {
        report(state.module, &expression.source, "illegal guard expression", state.out);
    }
}

// Node authorization and child scheduling remain independent so an invalid parent cannot hide operands.
void visit(BindingAnalysis &state, const Visit &visit, std::vector<Visit> &pending) {
    const auto &expression = state.module.syntax->expression(visit.id);
    authorize(state, visit, expression);
    if (scoped_guards(state, expression, visit, pending)) {
        return;
    }
    const auto children = expression_children(state.module, expression);
    const bool top = visit.top && std::holds_alternative<ast::Group>(expression.value);
    for (const auto &child : children) {
        pending.push_back({child, top, visit.guard});
    }
}

// Every operand is traversed even behind constant lazy branches; no folding can hide semantic errors.
void expressions(BindingAnalysis &state, const std::vector<ast::ExprId> &roots, const bool guard,
                 const bool legacy = true) {
    std::vector<Visit> pending;
    pending.reserve(roots.size());
    for (const auto &root : roots) {
        pending.push_back({root, guard && legacy, guard});
    }
    while (!pending.empty()) {
        const auto visit = pending.back();
        pending.pop_back();
        if (!state.spend(visit.id)) {
            return;
        }
        semantic::visit(state, visit, pending);
    }
}

// Each clause uses a common semantic budget while keeping legacy-test roots separate.
void clause(BindingAnalysis &state, const ast::FunctionClause &clause) {
    if (clause.guard) {
        for (const auto &alternative : clause.guard->alternatives) {
            expressions(state, alternative.tests, true);
        }
    }
    expressions(state, clause.body, false);
}
} // namespace

void resolve_services(Module &module, const Reporter &out, const std::optional<std::size_t> work_limit) {
    std::size_t work = 0;
    const auto limit =
        std::min(work_limit.value_or(default_work_limit(module)), std::numeric_limits<std::size_t>::max() - 1);
    bool failed = false;
    const Reporter transactional = [&](const Diagnostic &d) {
        failed = failed || d.severity == Severity::error;
        out(d);
    };
    for (auto &function : module.functions) {
        function.services.clear();
        function.guard_filters.clear();
        function.builtin_funs.clear();
        BindingAnalysis pattern_state{module, function, transactional, 0, work, limit};
        expressions(pattern_state, pattern_reads(module, function), true, false);
        const auto &clauses = std::get<ast::Function>(module.syntax->form(function.form).value).clauses;
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            BindingAnalysis state{module, function, transactional, i, work, limit};
            clause(state, clauses[i]);
        }
    }
    if (failed) {
        for (auto &function : module.functions) {
            function.services.clear();
            function.guard_filters.clear();
            function.builtin_funs.clear();
        }
    }
    for (const auto &function : module.functions) {
        for (const auto &[expression, key] : function.builtin_funs) {
            add_builtin_fun(module, *expression, key);
        }
    }
}
} // namespace clause::semantic
