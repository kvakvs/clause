#pragma once
#include "inference_bindings.hpp"
#include <functional>

// Dependent facts (docs/semantic.md#dependent-facts): the value of a case or if as a function of the variables its
// clauses narrow, one function type per possible clause, like a fun applied to those variables.
namespace clause::semantic::types {
// Parameters a dependent fact keeps at most; later ones are dropped and their narrowing forgotten.
inline constexpr std::size_t DEPENDENT_PARAMETERS = 4;
// Combinations of dependent operands' clauses a use is evaluated for at most; past it the use is their plain join.
inline constexpr std::size_t LIFT_ROWS = 16;

// The index a dependent fact is kept at, interning it once.
std::size_t intern(Inference &inference, Dependent dependent);
// The fact without its dependence: what is kept beyond the walked function.
Fact erased(Fact fact);
// After a possible clause's guard: keep the facts of its case's or if's parameters entering it.
void record_key(BindingFacts &bindings, const ast::Expression &construct, std::size_t index);
// The value of a case or if from its clauses' joined value: dependent on its parameters when its clauses give
// different values for different parameter facts.
Fact dependent_value(BindingFacts &bindings, const ast::Expression &construct, Fact joined);
// After a clause of a case or if completed: keep the facts of the variables every clause binds for after it.
void record_exit(BindingFacts &bindings, const ast::Expression &construct, std::size_t index);
// After a case or if closed: each variable every clause binds depends on its parameters like the construct's value.
void export_dependents(BindingFacts &bindings, const ast::Expression &construct);
// A use of dependent operands (an operation, a construction or a call): evaluated by `again` once per combination
// of their clauses with their values in place of their facts, as a dependent fact beside `plain`, its value.
Fact lifted(BindingFacts &bindings, const std::vector<ast::ExprId> &operands, Fact plain,
            const std::function<std::optional<Id>()> &again);
// What narrowing a dependent fact to `fact` proves of its parameters (the join of the inputs of the clauses whose
// values meet it), against their facts in `values`.
std::vector<std::pair<BindingId, Id>> implied(Inference &inference, const Fact &fact, const BindingValues &values);
// A fact as read where `values` hold: a dependent fact keeps the clauses its parameters' facts enter, and its type
// narrows to their results.
Fact resolved(Inference &inference, Fact fact, const BindingValues &values);
// A function clause's function types from its result: one per clause of a dependent result whose parameters name
// arguments, their facts met with the arguments'; else one.
std::vector<FunctionType> clause_types(const BindingFacts &bindings, const std::vector<ast::ExprId> &patterns,
                                       Fact result);
} // namespace clause::semantic::types
