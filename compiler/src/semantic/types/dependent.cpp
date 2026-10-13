#include "dependent.hpp"
#include "../capabilities.hpp"
#include "function_types.hpp"
#include "inference_narrowing.hpp"
#include "lattice.hpp"
#include <algorithm>
#include <functional>

namespace clause::semantic::types {
namespace {
// Every expression under `roots`, each before its operands, in source order.
std::vector<const ast::Expression *> preorder(const Module &module, const std::vector<ast::ExprId> &roots) {
    std::vector<const ast::Expression *> result;
    std::vector<ast::ExprId> pending(roots.rbegin(), roots.rend());
    while (!pending.empty()) {
        const auto &expression = module.syntax->expression(pending.back());
        pending.pop_back();
        result.push_back(&expression);
        const auto children = expression_children(module, expression);
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
    return result;
}

// The clauses whose patterns match a construct's scrutinee: a case's, a try's of clauses; null for other constructs.
const std::vector<ast::BranchClause> *matching_clauses(const ast::ExprValue &value) {
    if (const auto *selection = std::get_if<ast::CaseExpression>(&value)) {
        return &selection->clauses;
    }
    const auto *attempt = std::get_if<ast::TryExpression>(&value);
    return attempt && attempt->of ? &*attempt->of : nullptr;
}

// The value a construct's clauses match: a case's scrutinee, a try's body's last expression; none for an if.
std::optional<ast::ExprId> scrutinee_of(const ast::ExprValue &value) {
    if (const auto *selection = std::get_if<ast::CaseExpression>(&value)) {
        return selection->value;
    }
    const auto *attempt = std::get_if<ast::TryExpression>(&value);
    return attempt && attempt->of ? std::optional{attempt->body.back()} : std::nullopt;
}

// How many of a construct's clauses (branch_clauses order) its value depends on: every clause of a case or if, the
// of clauses of a try; none for other constructs.
std::size_t keyed_count(const ast::ExprValue &value) {
    if (std::holds_alternative<ast::IfExpression>(value)) {
        return std::get<ast::IfExpression>(value).clauses.size();
    }
    const auto *clauses = matching_clauses(value);
    return clauses ? clauses->size() : 0;
}

// The expressions a construct's parameters are read in: its scrutinee and its keyed clauses' guard tests.
std::vector<ast::ExprId> parameter_roots(const ast::ExprValue &value) {
    std::vector<ast::ExprId> result;
    if (const auto scrutinee = scrutinee_of(value)) {
        result.push_back(*scrutinee);
    }
    const auto clauses = branch_clauses(value);
    for (std::size_t index = 0; index < keyed_count(value); ++index) {
        const auto *guard = clauses[index].guard;
        for (const auto &alternative : guard ? guard->alternatives : std::vector<ast::GuardConjunction>{}) {
            result.insert(result.end(), alternative.tests.begin(), alternative.tests.end());
        }
    }
    return result;
}

// The identities a construct's clause patterns bind: new in each clause, they are no parameters.
std::set<BindingId> pattern_definitions(const BindingFacts &bindings, const ast::ExprValue &value) {
    const auto *clauses = matching_clauses(value);
    std::vector<ast::ExprId> patterns;
    for (const auto &clause : clauses ? *clauses : std::vector<ast::BranchClause>{}) {
        patterns.push_back(pattern_root(*bindings.function.module->syntax, clause.pattern));
    }
    std::set<BindingId> result;
    for (const auto *expression : preorder(*bindings.function.module, patterns)) {
        const auto event = bindings.events.find(expression);
        if (event != bindings.events.end() && event->second->use == BindingUse::definition) {
            result.insert(event->second->identity);
        }
    }
    return result;
}

// Add the variable a read names to `parameters` unless it is bound by the construct's clauses or already there.
void add_read(const BindingFacts &bindings, const ast::Expression &read, const std::set<BindingId> &local,
              std::vector<BindingId> &parameters) {
    const auto event = bindings.events.find(&read);
    if (event == bindings.events.end() || event->second->use != BindingUse::read) {
        return;
    }
    const auto identity = event->second->identity;
    if (!local.contains(identity) && !std::ranges::contains(parameters, identity)) {
        parameters.push_back(identity);
    }
}

// The variables a case, if or try ... of depends on: those bound before it that its scrutinee and guards read, in
// source order; none for other constructs.
std::vector<BindingId> find_parameters(const BindingFacts &bindings, const ast::Expression &construct) {
    const auto &value = construct.value;
    if (keyed_count(value) == 0) {
        return {};
    }
    const auto local = pattern_definitions(bindings, value);
    std::vector<BindingId> result;
    for (const auto *expression : preorder(*bindings.function.module, parameter_roots(value))) {
        add_read(bindings, *expression, local, result);
        if (result.size() == DEPENDENT_PARAMETERS) {
            break;
        }
    }
    return result;
}

// The parameters of a case or if, found once per construct.
const std::vector<BindingId> &parameters_of(BindingFacts &bindings, const ast::Expression &construct) {
    const auto found = bindings.parameters.find(&construct);
    if (found != bindings.parameters.end()) {
        return found->second;
    }
    return bindings.parameters.emplace(&construct, find_parameters(bindings, construct)).first->second;
}

// The current fact of a variable; term() when it has none.
Id current(const Inference &inference, const std::map<BindingId, Fact> &values, const BindingId identity) {
    const auto found = values.find(identity);
    return found == values.end() ? inference.graph.top() : found->second.type;
}

// The names bound to the same values as `identities`, themselves included.
std::set<BindingId> same_values(const BindingFacts &bindings, const std::vector<BindingId> &identities) {
    std::set<BindingId> result(identities.begin(), identities.end());
    std::vector<BindingId> pending(identities.begin(), identities.end());
    while (!pending.empty()) {
        const auto linked = bindings.aliases.find(pending.back());
        pending.pop_back();
        for (const auto other : linked == bindings.aliases.end() ? std::set<BindingId>{} : linked->second) {
            if (result.insert(other).second) {
                pending.push_back(other);
            }
        }
    }
    return result;
}

// Whether a scrutinee is a parameter or a tuple of parameters, among `names`.
bool parameter_scrutinee(const BindingFacts &bindings, const ast::ExprId &scrutinee, const std::set<BindingId> &names) {
    const auto &syntax = *bindings.function.module->syntax;
    const auto named = [&](const ast::ExprId &read) {
        const auto identity = variable(bindings, read);
        return identity && names.contains(*identity);
    };
    const auto *tuple = std::get_if<ast::Tuple>(&syntax.expression(ungroup(syntax, scrutinee)).value);
    return named(scrutinee) || (tuple && std::ranges::all_of(tuple->elements, named));
}

// Whether every parameter value within a clause's key that reaches the clause enters it: a case or try reads
// parameters with an exact pattern and guard, an if has an exact guard (docs/semantic.md#inference); a try's catch
// clause never is.
bool exact_key(BindingFacts &bindings, const ast::Expression &construct, const std::size_t index,
               const std::vector<BindingId> &parameters) {
    if (index >= keyed_count(construct.value)) {
        return false;
    }
    const auto names = same_values(bindings, parameters);
    const auto clause = branch_clauses(construct.value).at(index);
    const auto scrutinee = scrutinee_of(construct.value);
    if (!scrutinee) {
        return exact_guard(bindings, clause.guard, names);
    }
    const auto &syntax = *bindings.function.module->syntax;
    return parameter_scrutinee(bindings, *scrutinee, names) &&
           exact_shape(bindings, pattern_root(syntax, *clause.pattern)) && exact_guard(bindings, clause.guard, names);
}

// The value of a clause body: its last expression's, none() when an expression of it never completes.
Fact clause_value(const BindingFacts &bindings, const std::vector<ast::ExprId> &body) {
    const auto &syntax = *bindings.function.module->syntax;
    Fact result{bindings.inference.graph.top()};
    for (const auto &expression : body) {
        const auto found = bindings.inference.expressions.find(&syntax.expression(expression));
        result = found == bindings.inference.expressions.end() ? Fact{bindings.inference.graph.top()} : found->second;
        if (result.type == bindings.inference.graph.bottom()) {
            return result;
        }
    }
    return result;
}

// The clauses of a dependent fact being built: their parameters (by identity) and function types.
struct Table {
    std::vector<BindingId> parameters;
    std::vector<FunctionType> types;
};

// Inputs over `from` placed over `to`, met with `into` (term() for parameters `from` lacks); false when a parameter
// of `from` that `to` drops was narrowed, so the type can no longer be exact.
bool place(Lattice &lattice, const Table &from, const std::vector<Id> &inputs, const std::vector<BindingId> &to,
           std::vector<Id> &into) {
    bool kept = true;
    for (std::size_t index = 0; index < from.parameters.size() && index < inputs.size(); ++index) {
        const auto position = std::ranges::find(to, from.parameters[index]) - to.begin();
        if (static_cast<std::size_t>(position) < to.size()) {
            into[position] = lattice.meet(into[position], inputs[index]);
        } else {
            kept = kept && inputs[index] == lattice.graph().top();
        }
    }
    return kept;
}

// Add the parameters of a clause's dependent result to `table`, within the parameter budget.
void add_parameters(Table &table, const Dependent &result) {
    for (const auto identity : result.parameters) {
        if (table.parameters.size() < DEPENDENT_PARAMETERS && !std::ranges::contains(table.parameters, identity)) {
            table.parameters.push_back(identity);
        }
    }
}

// One clause of a case or if: its key over the construct's parameters and its value.
struct Clause {
    FunctionType key;
    Fact value;
};

// Add a clause's function types to `table`: one per clause of a dependent value (the key met with each of its
// clauses' inputs), else one.
void add_clause(Inference &inference, Table &table, const Table &outer, const Clause &clause) {
    Lattice lattice(inference.graph);
    std::vector<Id> inputs(table.parameters.size(), inference.graph.top());
    const bool exact = place(lattice, outer, clause.key.inputs, table.parameters, inputs) && clause.key.exact;
    if (!clause.value.dependent) {
        table.types.push_back({std::move(inputs), clause.value, exact});
        return;
    }
    const auto inner = inference.dependents.at(*clause.value.dependent);
    const Table nested{inner.parameters, inner.types};
    for (const auto &type : inner.types) {
        auto combined = inputs;
        const bool kept = place(lattice, nested, type.inputs, table.parameters, combined);
        table.types.push_back({std::move(combined), type.result, exact && kept && type.exact});
    }
}

// Whether a function type can be entered: no input is none() and no earlier exact type holds all its inputs.
bool reachable(Lattice &lattice, const std::vector<FunctionType> &earlier, const FunctionType &type) {
    if (std::ranges::contains(type.inputs, lattice.graph().bottom())) {
        return false;
    }
    return std::ranges::none_of(earlier, [&](const FunctionType &other) {
        return other.exact && std::ranges::equal(type.inputs, other.inputs,
                                                 [&](Id inner, Id outer) { return lattice.within(inner, outer); });
    });
}

// Drop the types that can never be entered and the parameters whose inputs are the same in every type.
void prune(Lattice &lattice, Table &table) {
    std::vector<FunctionType> types;
    for (auto &type : table.types) {
        if (reachable(lattice, types, type)) {
            types.push_back(std::move(type));
        }
    }
    table.types = std::move(types);
    for (std::size_t index = table.parameters.size(); index-- > 0;) {
        const auto same = std::ranges::all_of(table.types, [&](const FunctionType &type) {
            return type.inputs[index] == table.types.front().inputs[index];
        });
        if (same) {
            table.parameters.erase(table.parameters.begin() + static_cast<std::ptrdiff_t>(index));
            for (auto &type : table.types) {
                type.inputs.erase(type.inputs.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
    }
}

// Whether a table tells more than its join: two or more types over parameters, not all of the same value.
bool telling(const Table &table) {
    return !table.parameters.empty() && table.types.size() > 1 &&
           std::ranges::any_of(table.types,
                               [&](const FunctionType &type) { return type.result != table.types.front().result; });
}

// The key of a clause of a case or if: the parameters' facts entering it, any terms when it was not recorded.
FunctionType key_of(const BindingFacts &bindings, const ast::Expression &construct, const std::size_t index) {
    const auto key = bindings.keys.find({&construct, index});
    if (key != bindings.keys.end()) {
        return key->second;
    }
    const auto &parameters = bindings.parameters.at(&construct);
    return {std::vector<Id>(parameters.size(), bindings.inference.graph.top()), {}, false};
}

// The joined values of a try's catch clauses that can run, which can follow any value of its body; none() for other
// constructs.
Fact handled(const BindingFacts &bindings, const ast::Expression &construct) {
    const auto clauses = branch_clauses(construct.value);
    Fact result{bindings.inference.graph.bottom()};
    for (std::size_t index = keyed_count(construct.value); index < clauses.size(); ++index) {
        if (!bindings.impossible.contains(clauses[index].body)) {
            result = merged(bindings.inference.graph, result, clause_value(bindings, *clauses[index].body));
        }
    }
    return result;
}

// The keyed clauses of a construct that can run, each with its key and value (a try's joined with its catch
// clauses').
std::vector<Clause> possible_clauses(const BindingFacts &bindings, const ast::Expression &construct) {
    std::vector<Clause> result;
    const auto clauses = branch_clauses(construct.value);
    const auto handlers = handled(bindings, construct);
    for (std::size_t index = 0; index < keyed_count(construct.value); ++index) {
        if (!bindings.impossible.contains(clauses[index].body)) {
            const auto value = clause_value(bindings, *clauses[index].body);
            result.push_back({key_of(bindings, construct, index), merged(bindings.inference.graph, value, handlers)});
        }
    }
    return result;
}

// The clauses of a case or if that completed, each with its key and the fact `identity` had at its end.
std::vector<Clause> exit_clauses(const BindingFacts &bindings, const ast::Expression &construct,
                                 const BindingId identity) {
    std::vector<Clause> result;
    const auto clauses = branch_clauses(construct.value);
    for (std::size_t index = 0; index < clauses.size(); ++index) {
        const auto exit = bindings.exits.find({&construct, index});
        if (exit == bindings.exits.end()) {
            continue;
        }
        const auto found = exit->second.find(identity);
        result.push_back({key_of(bindings, construct, index),
                          found == exit->second.end() ? Fact{bindings.inference.graph.top()} : found->second});
    }
    return result;
}

// The construct a dependent fact of a case, if or try ... of prints with.
Operator construct_of(const ast::Expression &construct) {
    if (std::holds_alternative<ast::IfExpression>(construct.value)) {
        return Operator::if_operator;
    }
    return std::holds_alternative<ast::TryExpression>(construct.value) ? Operator::try_of_operator
                                                                       : Operator::case_operator;
}

// The names of a function's variables, for printing.
std::vector<std::string> names_of(const BindingFacts &bindings, const std::vector<BindingId> &identities) {
    std::vector<std::string> result;
    result.reserve(identities.size());
    for (const auto identity : identities) {
        const auto &clause = bindings.function.function->clause_bindings.at(identity.clause);
        result.push_back(utf8(clause.definitions.at(identity.local).name));
    }
    return result;
}

// The argument each parameter of a dependent fact names, through the names bound to the same value; none for a
// parameter that names no argument.
std::vector<std::optional<std::size_t>>
argument_positions(const BindingFacts &bindings, const std::vector<ast::ExprId> &patterns, const Dependent &dependent) {
    std::vector<std::optional<std::size_t>> result(dependent.parameters.size());
    for (std::size_t position = 0; position < patterns.size(); ++position) {
        const auto identity = variable(bindings, patterns[position]);
        const auto names = identity ? same_values(bindings, {*identity}) : std::set<BindingId>{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            if (!result[index] && names.contains(dependent.parameters[index])) {
                result[index] = position;
            }
        }
    }
    return result;
}

// The function type of a dependent clause: the clause's argument facts met with the parameters' inputs at their
// `positions`; exact only when every parameter it narrows is an argument. None when it can never be entered.
std::optional<FunctionType> argument_type(const BindingFacts &bindings,
                                          const std::vector<std::optional<std::size_t>> &positions,
                                          const FunctionType &type) {
    Lattice lattice(bindings.inference.graph);
    FunctionType result{bindings.arguments, type.result, bindings.exact && type.exact};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        if (!positions[index]) {
            result.exact = result.exact && type.inputs[index] == bindings.inference.graph.top();
            continue;
        }
        auto &input = result.inputs.at(*positions[index]);
        input = lattice.meet(input, type.inputs[index]);
        if (input == bindings.inference.graph.bottom()) {
            return std::nullopt;
        }
    }
    return result;
}

// The fact of a table of clauses beside `plain`, its erased join: dependent once its unreachable types and constant
// parameters are dropped and its types merged, unless every remaining type gives the same value.
Fact finished(BindingFacts &bindings, const Operator construct, Table table, const Fact plain) {
    auto &inference = bindings.inference;
    Lattice lattice(inference.graph);
    prune(lattice, table);
    table.types = merge_types(inference.graph, std::move(table.types));
    if (!telling(table)) {
        return {plain.type, plain.argument};
    }
    auto names = names_of(bindings, table.parameters);
    const auto index =
        intern(inference, {construct, std::move(table.parameters), std::move(names), std::move(table.types)});
    return {plain.type, plain.argument, index};
}

// The fact of a case's or if's clauses (keys over `parameters`, values) beside `plain`, their join; a value that is
// itself dependent adds its own parameters.
Fact clause_fact(BindingFacts &bindings, const Operator construct, const std::vector<BindingId> &parameters,
                 const std::vector<Clause> &clauses, const Fact plain) {
    auto &inference = bindings.inference;
    const Table outer{parameters, {}};
    Table table{parameters, {}};
    for (const auto &clause : clauses) {
        if (clause.value.dependent) {
            add_parameters(table, inference.dependents.at(*clause.value.dependent));
        }
    }
    for (const auto &clause : clauses) {
        add_clause(inference, table, outer, clause);
    }
    return finished(bindings, construct, std::move(table), plain);
}

// The current facts of a dependent fact's parameters.
std::vector<Fact> parameter_facts(const Inference &inference, const Dependent &dependent,
                                  const std::map<BindingId, Fact> &values) {
    std::vector<Fact> result;
    result.reserve(dependent.parameters.size());
    for (const auto identity : dependent.parameters) {
        result.push_back({current(inference, values, identity)});
    }
    return result;
}

// The operands of a use that read one dependent value: reads of one variable share its clause, other operands
// combine with each other.
struct Factor {
    std::size_t dependent;
    std::vector<const ast::Expression *> operands;
    std::optional<BindingId> name;
};

// The dependent operands of a use, grouped into factors.
std::vector<Factor> factors(const BindingFacts &bindings, const std::vector<ast::ExprId> &operands) {
    const auto &syntax = *bindings.function.module->syntax;
    std::vector<Factor> result;
    for (const auto &operand : operands) {
        const auto *expression = &syntax.expression(operand);
        const auto found = bindings.inference.expressions.find(expression);
        if (found == bindings.inference.expressions.end() || !found->second.dependent) {
            continue;
        }
        const auto name = variable(bindings, operand);
        const auto same = std::ranges::find_if(result, [&](const Factor &factor) {
            return name && factor.name == name && factor.dependent == *found->second.dependent;
        });
        if (same != result.end()) {
            same->operands.push_back(expression);
        } else {
            result.push_back({*found->second.dependent, {expression}, name});
        }
    }
    return result;
}

// How many combinations of clauses the factors have, counted up to just past LIFT_ROWS.
std::size_t rows(const Inference &inference, const std::vector<Factor> &parts) {
    std::size_t result = 1;
    for (const auto &part : parts) {
        result *= inference.dependents.at(part.dependent).types.size();
        if (result > LIFT_ROWS) {
            break;
        }
    }
    return result;
}

// The next combination of clauses, the last factor's first; false after the last one.
bool advance(const Inference &inference, const std::vector<Factor> &parts, std::vector<std::size_t> &choice) {
    for (std::size_t index = parts.size(); index-- > 0;) {
        if (++choice[index] < inference.dependents.at(parts[index].dependent).types.size()) {
            return true;
        }
        choice[index] = 0;
    }
    return false;
}

// The value of a use with its dependent operands' facts set to the clauses `choice` picks, restored afterwards.
std::optional<Id> substituted(Inference &inference, const std::vector<Factor> &parts,
                              const std::vector<std::size_t> &choice, const std::function<std::optional<Id>()> &again) {
    std::vector<std::pair<const ast::Expression *, Fact>> saved;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const auto result = inference.dependents.at(parts[index].dependent).types.at(choice[index]).result;
        for (const auto *operand : parts[index].operands) {
            auto &fact = inference.expressions.at(operand);
            saved.emplace_back(operand, fact);
            fact = {result.type, result.argument};
        }
    }
    const auto value = again();
    for (const auto &[operand, fact] : saved) {
        inference.expressions.at(operand) = fact;
    }
    return value;
}

// Add the function type of one combination of the factors' clauses to `table`: their inputs met, the use's value
// with their values; false when the use cannot be evaluated again.
bool add_row(BindingFacts &bindings, const std::vector<Factor> &parts, const std::vector<std::size_t> &choice,
             const Fact plain, const std::function<std::optional<Id>()> &again, Table &table) {
    auto &inference = bindings.inference;
    Lattice lattice(inference.graph);
    FunctionType row{std::vector<Id>(table.parameters.size(), inference.graph.top()), {}, true};
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const auto dependent = inference.dependents.at(parts[index].dependent);
        const auto &type = dependent.types.at(choice[index]);
        const Table from{dependent.parameters, {}};
        row.exact = place(lattice, from, type.inputs, table.parameters, row.inputs) && row.exact && type.exact;
    }
    if (std::ranges::contains(row.inputs, inference.graph.bottom())) {
        return true;
    }
    const auto value = substituted(inference, parts, choice, again);
    if (!value) {
        return false;
    }
    row.result = {lattice.meet(*value, plain.type)};
    table.types.push_back(std::move(row));
    return true;
}
} // namespace

std::size_t intern(Inference &inference, Dependent dependent) {
    const auto found = inference.dependent_indices.find(dependent);
    if (found != inference.dependent_indices.end()) {
        return found->second;
    }
    inference.dependents.push_back(dependent);
    inference.dependent_indices.emplace(std::move(dependent), inference.dependents.size() - 1);
    return inference.dependents.size() - 1;
}

Fact erased(const Fact fact) { return {fact.type, fact.argument}; }

void record_key(BindingFacts &bindings, const ast::Expression &construct, const std::size_t index) {
    const auto parameters = parameters_of(bindings, construct);
    if (parameters.empty()) {
        return;
    }
    std::vector<Id> inputs;
    inputs.reserve(parameters.size());
    for (const auto identity : parameters) {
        inputs.push_back(current(bindings.inference, bindings.values, identity));
    }
    const auto exact = exact_key(bindings, construct, index, parameters);
    bindings.keys.insert_or_assign({&construct, index},
                                   FunctionType{std::move(inputs), {bindings.inference.graph.bottom()}, exact});
}

Fact dependent_value(BindingFacts &bindings, const ast::Expression &construct, const Fact joined) {
    const auto &parameters = parameters_of(bindings, construct);
    if (parameters.empty() || joined.type == bindings.inference.graph.bottom()) {
        return joined;
    }
    return clause_fact(bindings, construct_of(construct), parameters, possible_clauses(bindings, construct), joined);
}

void record_exit(BindingFacts &bindings, const ast::Expression &construct, const std::size_t index) {
    const auto exported = bindings.function.function->exports.find(&construct);
    if (exported == bindings.function.function->exports.end() || parameters_of(bindings, construct).empty()) {
        return;
    }
    std::map<BindingId, Fact> facts;
    for (const auto identity : exported->second) {
        if (const auto found = bindings.values.find(identity); found != bindings.values.end()) {
            facts.emplace(identity, found->second);
        }
    }
    bindings.exits.insert_or_assign({&construct, index}, std::move(facts));
}

void export_dependents(BindingFacts &bindings, const ast::Expression &construct) {
    const auto exported = bindings.function.function->exports.find(&construct);
    if (exported == bindings.function.function->exports.end() || parameters_of(bindings, construct).empty()) {
        return;
    }
    const auto parameters = parameters_of(bindings, construct);
    for (const auto identity : exported->second) {
        const auto found = bindings.values.find(identity);
        if (found != bindings.values.end() && found->second.type != bindings.inference.graph.bottom()) {
            found->second = clause_fact(bindings, construct_of(construct), parameters,
                                        exit_clauses(bindings, construct, identity), found->second);
        }
    }
}

Fact lifted(BindingFacts &bindings, const std::vector<ast::ExprId> &operands, const Fact plain,
            const std::function<std::optional<Id>()> &again) {
    auto &inference = bindings.inference;
    const auto parts = factors(bindings, operands);
    if (parts.empty() || plain.type == inference.graph.bottom() || rows(inference, parts) > LIFT_ROWS) {
        return plain;
    }
    Table table;
    for (const auto &part : parts) {
        add_parameters(table, inference.dependents.at(part.dependent));
    }
    std::vector<std::size_t> choice(parts.size(), 0);
    do {
        if (!add_row(bindings, parts, choice, plain, again, table)) {
            return plain;
        }
    } while (advance(inference, parts, choice));
    const auto construct = inference.dependents.at(parts.front().dependent).construct;
    return finished(bindings, construct, std::move(table), plain);
}

std::vector<std::pair<BindingId, Id>> implied(Inference &inference, const Fact &fact,
                                              const std::map<BindingId, Fact> &values) {
    if (!fact.dependent) {
        return {};
    }
    const auto dependent = inference.dependents.at(*fact.dependent);
    Lattice lattice(inference.graph);
    std::vector<Id> joined(dependent.parameters.size(), inference.graph.bottom());
    bool any = false;
    for (const auto index : entered(inference.graph, dependent.types, parameter_facts(inference, dependent, values))) {
        const auto &type = dependent.types[index];
        if (lattice.meet(type.result.type, fact.type) == inference.graph.bottom()) {
            continue;
        }
        any = true;
        for (std::size_t position = 0; position < joined.size(); ++position) {
            joined[position] = lattice.join(joined[position], type.inputs[position]);
        }
    }
    std::vector<std::pair<BindingId, Id>> result;
    for (std::size_t position = 0; any && position < joined.size(); ++position) {
        result.emplace_back(dependent.parameters[position], joined[position]);
    }
    return result;
}

Fact resolved(Inference &inference, const Fact fact, const std::map<BindingId, Fact> &values) {
    if (!fact.dependent) {
        return fact;
    }
    auto dependent = inference.dependents.at(*fact.dependent);
    Lattice lattice(inference.graph);
    std::vector<FunctionType> kept;
    auto result = inference.graph.bottom();
    for (const auto index : entered(inference.graph, dependent.types, parameter_facts(inference, dependent, values))) {
        const auto &type = dependent.types[index];
        if (lattice.meet(type.result.type, fact.type) != inference.graph.bottom()) {
            kept.push_back(type);
            result = lattice.join(result, type.result.type);
        }
    }
    if (kept.empty() || kept.size() == dependent.types.size()) {
        return fact;
    }
    const auto type = lattice.meet(fact.type, result);
    if (kept.size() == 1) {
        return {type, fact.argument ? fact.argument : kept.front().result.argument};
    }
    dependent.types = std::move(kept);
    return {type, fact.argument, intern(inference, std::move(dependent))};
}

std::vector<FunctionType> clause_types(const BindingFacts &bindings, const std::vector<ast::ExprId> &patterns,
                                       const Fact result) {
    const FunctionType whole{bindings.arguments, erased(result), bindings.exact};
    if (!result.dependent) {
        return {whole};
    }
    const auto &dependent = bindings.inference.dependents.at(*result.dependent);
    const auto positions = argument_positions(bindings, patterns, dependent);
    if (std::ranges::none_of(positions, [](const auto &position) { return position.has_value(); })) {
        return {whole};
    }
    std::vector<FunctionType> types;
    for (const auto &type : dependent.types) {
        if (auto split = argument_type(bindings, positions, type)) {
            types.push_back(std::move(*split));
        }
    }
    return types.empty() ? std::vector<FunctionType>{whole} : types;
}
} // namespace clause::semantic::types
