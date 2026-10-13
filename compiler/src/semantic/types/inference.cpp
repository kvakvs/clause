#include "inference.hpp"
#include "../bindings.hpp"
#include "../capabilities.hpp"
#include "../funs.hpp"
#include "../records.hpp"
#include "dependent.hpp"
#include "function_types.hpp"
#include "inference_apply.hpp"
#include "inference_bindings.hpp"
#include "inference_funs.hpp"
#include "inference_inputs.hpp"
#include "inference_operators.hpp"
#include "inference_scopes.hpp"
#include "inference_uses.hpp"
#include "inference_values.hpp"
#include "lattice.hpp"
#include <algorithm>
#include <chrono>
#include <optional>

namespace clause::semantic::types {
namespace {
// Infer only implementation syntax; specifications never narrow an input or result.
Fact leaf(Inference &inference, const FunctionRef function, const ast::ExprId &id, const BindingFacts &bindings) {
    const auto &syntax = *function.module->syntax;
    if (const auto fact = constructed_fact(inference, function, syntax.expression(id).value)) {
        return {*fact};
    }
    return bindings.read(id);
}

// Clause bodies that can never run.
using Impossible = std::set<const std::vector<ast::ExprId> *>;

// Instantiate each projection with this call's actual fact, never a shared mutable type variable.
Fact call_result(const Inference &inference, const ast::Module &syntax, const ast::Expression &expression,
                 const ast::CallExpression &call) {
    const auto callee = inference.callees.at(&expression);
    const auto &summary = summary_of(inference, callee.function);
    if (!summary.result.argument) {
        return summary.result;
    }
    const auto argument = call.arguments.at(*summary.result.argument);
    return inference.expressions.at(&syntax.expression(argument));
}

// The value of a body: its last expression's, or none() when an expression before it never completes.
Fact sequence(const Inference &inference, const ast::Module &syntax, const std::vector<ast::ExprId> &body) {
    for (const auto &expression : body) {
        const auto fact = inference.expressions.at(&syntax.expression(expression));
        if (fact.type == inference.graph.bottom()) {
            return fact;
        }
    }
    return inference.expressions.at(&syntax.expression(body.back()));
}

// Only relations common to every successful function candidate or case/if clause survive the join; a clause that
// can never run adds nothing.
template <typename Clauses>
Fact joined(Inference &inference, const ast::Module &syntax, const Clauses &clauses,
            const Impossible *impossible = nullptr) {
    Fact result{inference.graph.bottom()};
    for (const auto &clause : clauses) {
        if (!impossible || !impossible->contains(&clause.body)) {
            result = merged(inference.graph, result, sequence(inference, syntax, clause.body));
        }
    }
    return result;
}

// The recorded fact of an expression; term() for one that is not evaluated.
Fact recorded_fact(const Inference &inference, const ast::Module &syntax, const ast::ExprId &id) {
    const auto found = inference.expressions.find(&syntax.expression(id));
    return found == inference.expressions.end() ? Fact{inference.graph.top()} : found->second;
}

// Whether a call never happens: its called value or one of its arguments never produces a value.
bool unreachable_call(const Inference &inference, const ast::Module &syntax, const ast::CallExpression &call) {
    // record_info/2 and named functions have no evaluated operands: they read as term().
    const auto never = [&](const ast::ExprId &operand) {
        return recorded_fact(inference, syntax, operand).type == inference.graph.bottom();
    };
    return never(call.target) || std::ranges::any_of(call.arguments, never);
}

// Whether a call's result is unknown: a service, record_info/2 or a dynamic call.
bool opaque_call(const FunctionRef function, const ast::Expression &expression, const ast::CallExpression &call) {
    const auto &syntax = *function.module->syntax;
    return function.function->services.contains(&expression) || record_info_call(syntax, expression.value) ||
           dynamic_call(syntax, call);
}

// The guard tests and bodies of fun clauses, clause by clause.
std::vector<ast::ExprId> clause_roots(const std::vector<ast::FunctionClause> &clauses) {
    std::vector<ast::ExprId> result;
    for (const auto &clause : clauses) {
        for (const auto &alternative :
             clause.guard ? clause.guard->alternatives : std::vector<ast::GuardConjunction>{}) {
            result.insert(result.end(), alternative.tests.begin(), alternative.tests.end());
        }
        result.insert(result.end(), clause.body.begin(), clause.body.end());
    }
    return result;
}

// A receive's value: its clauses' joined values, and its after body's unless the timeout is infinity.
Fact receive_fact(Inference &inference, const ast::Module &syntax, const ast::ReceiveExpression &receive,
                  const Impossible &impossible) {
    const auto clauses = joined(inference, syntax, receive.clauses, &impossible);
    if (!receive.after) {
        return clauses;
    }
    const auto &timeout = inference.graph.get(recorded_fact(inference, syntax, receive.after->timeout).type);
    if (timeout.kind == Kind::atom && timeout.name == "infinity") {
        return clauses;
    }
    return merged(inference.graph, clauses, sequence(inference, syntax, receive.after->body));
}

// A try's value: its of clauses' joined values (its body's without of) joined with its catch clauses' values.
Fact try_fact(Inference &inference, const ast::Module &syntax, const ast::TryExpression &attempt,
              const Impossible &impossible) {
    const auto result =
        attempt.of ? joined(inference, syntax, *attempt.of, &impossible) : sequence(inference, syntax, attempt.body);
    if (!attempt.handlers) {
        return result;
    }
    return merged(inference.graph, result, joined(inference, syntax, *attempt.handlers, &impossible));
}

// A maybe's value: its body's when every ?= match can succeed, joined with its else clauses' values, or without
// else, with the values its ?= matches fail on.
Fact maybe_fact(Inference &inference, const ast::Module &syntax, const ast::Expression &expression,
                const BindingFacts &bindings) {
    const auto &conditional = std::get<ast::MaybeExpression>(expression.value);
    const auto body = bindings.stopped(expression) ? Fact{inference.graph.bottom()}
                                                   : sequence(inference, syntax, maybe_operands(conditional));
    const auto rest = conditional.otherwise ? joined(inference, syntax, *conditional.otherwise, &bindings.impossible)
                                            : Fact{bindings.failures(expression)};
    return merged(inference.graph, body, rest);
}

// The value of a case, if, receive, try or maybe: its clauses' joined values, a case's, if's or try ... of's
// dependent on the variables its clauses narrow; none for other expressions.
std::optional<Fact> selection_fact(Inference &inference, const ast::Module &syntax, const ast::Expression &expression,
                                   BindingFacts &bindings) {
    const auto &value = expression.value;
    const auto &impossible = bindings.impossible;
    if (const auto *selection = std::get_if<ast::CaseExpression>(&value)) {
        return dependent_value(bindings, expression, joined(inference, syntax, selection->clauses, &impossible));
    }
    if (const auto *choice = std::get_if<ast::IfExpression>(&value)) {
        return dependent_value(bindings, expression, joined(inference, syntax, choice->clauses, &impossible));
    }
    if (const auto *receive = std::get_if<ast::ReceiveExpression>(&value)) {
        return receive_fact(inference, syntax, *receive, impossible);
    }
    if (const auto *attempt = std::get_if<ast::TryExpression>(&value)) {
        return dependent_value(bindings, expression, try_fact(inference, syntax, *attempt, impossible));
    }
    if (std::holds_alternative<ast::MaybeExpression>(value)) {
        return maybe_fact(inference, syntax, expression, bindings);
    }
    return std::nullopt;
}

// Anonymous funs called where they are bound are evaluated again for each call, at most this many deep.
constexpr std::size_t INSTANCE_DEPTH = 4;
// Calls evaluate their callee again at most this many deep, for callees of at most this many expressions, within
// this much work per call and per pass over the batch (docs/semantic.md#inference).
constexpr std::size_t REANALYSIS_DEPTH = 4;
constexpr std::size_t CALLEE_SIZE = 256;
constexpr std::size_t CALL_WORK = 4096;
constexpr std::size_t PASS_WORK = 262144;

// A function body's result and the entry domain of its arguments.
struct Body {
    Fact result;
    // The inputs a summary shows (the success domain, or the entry domain of a function that never returns) and the
    // entry domain.
    std::vector<Id> domain;
    std::vector<Id> entry;
    // The function types of its possible clauses, merged within their budget.
    std::vector<FunctionType> types = {};
};

bool walk(Inference &inference, FunctionRef function, BindingFacts &bindings, std::vector<Frame> pending,
          std::size_t &work, std::vector<const ast::Expression *> &recorded);
Fact plain_fact(Inference &inference, FunctionRef function, const ast::ExprId &id, BindingFacts &bindings,
                std::size_t &work);
std::optional<Body> run_body(Inference &inference, FunctionRef function, std::size_t &work,
                             std::vector<const ast::Expression *> &recorded, const std::vector<Id> *inputs);

// Every expression evaluated under `roots`.
std::vector<const ast::Expression *> subtree(const Module &module, const std::vector<ast::ExprId> &roots) {
    std::vector<const ast::Expression *> result;
    std::vector<ast::ExprId> pending(roots.begin(), roots.end());
    while (!pending.empty()) {
        const auto &expression = module.syntax->expression(pending.back());
        pending.pop_back();
        result.push_back(&expression);
        const auto children = expression_children(module, expression);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return result;
}

// Expression facts set aside while a body is evaluated again.
using SetAside = std::vector<std::pair<const ast::Expression *, Fact>>;

// Remove the recorded facts of `expressions`, so evaluating them again records fresh ones; the removed facts.
SetAside set_aside(Inference &inference, const std::vector<const ast::Expression *> &expressions) {
    SetAside saved;
    for (const auto *expression : expressions) {
        if (const auto found = inference.expressions.find(expression); found != inference.expressions.end()) {
            saved.emplace_back(*found);
            inference.expressions.erase(found);
        }
    }
    return saved;
}

// Drop the facts recorded again (`scratch`) and restore the facts set aside.
void put_back(Inference &inference, const std::vector<const ast::Expression *> &scratch, const SetAside &saved) {
    for (const auto *expression : scratch) {
        inference.expressions.erase(expression);
    }
    inference.expressions.insert(saved.begin(), saved.end());
}

// The facts of a function's walk that evaluating an anonymous fun again for a call changes, restored afterwards.
struct Instance {
    std::map<BindingId, Fact> values;
    std::map<const ast::Expression *, std::vector<Waiting>> waiting;
    std::map<std::pair<const ast::Expression *, std::size_t>, BindingFacts::Conditional> conditionals;
    Impossible impossible;
    std::map<std::pair<const ast::Expression *, std::size_t>, FunctionType> fun_inputs;
    std::map<const ast::Expression *, std::vector<Fact>> instances;
    std::map<std::pair<const ast::Expression *, std::size_t>, FunctionType> keys;
    std::map<std::pair<const ast::Expression *, std::size_t>, std::map<BindingId, Fact>> exits;
};

// Keep the facts an evaluation for a call changes.
Instance keep(const BindingFacts &bindings) {
    return {bindings.values,     bindings.waiting,   bindings.conditionals, bindings.impossible,
            bindings.fun_inputs, bindings.instances, bindings.keys,         bindings.exits};
}

// Restore the facts kept before an evaluation for a call.
void restore(BindingFacts &bindings, const Instance &kept) {
    bindings.values = kept.values;
    bindings.waiting = kept.waiting;
    bindings.conditionals = kept.conditionals;
    bindings.impossible = kept.impossible;
    bindings.fun_inputs = kept.fun_inputs;
    bindings.instances = kept.instances;
    bindings.keys = kept.keys;
    bindings.exits = kept.exits;
}

// The result of calling anonymous fun `lambda` with arguments of facts `arguments`: its clauses are entered, guarded
// and left again like a case's, their patterns matching the arguments (a clause they cannot match adds nothing),
// then the facts of its first evaluation are restored.
Fact instantiate(Inference &inference, const FunctionRef function, BindingFacts &bindings, const ast::ExprId &lambda,
                 const std::vector<Fact> &arguments, std::size_t &work) {
    const auto &syntax = *function.module->syntax;
    const auto &expression = syntax.expression(lambda);
    const auto &clauses = *fun_clauses(expression.value);
    if (clauses.front().arguments.size() != arguments.size()) {
        return {inference.graph.bottom()};
    }
    const auto saved = set_aside(inference, subtree(*function.module, clause_roots(clauses)));
    const auto kept = keep(bindings);
    bindings.instances.insert_or_assign(&expression, arguments);
    ++bindings.depth;
    std::vector<const ast::Expression *> scratch;
    std::vector<Frame> frames;
    expand_clauses(*function.module, lambda, frames);
    const auto result = walk(inference, function, bindings, std::move(frames), work, scratch)
                            ? joined(inference, syntax, clauses, &bindings.impossible)
                            : Fact{inference.reanalyses > 0 ? inference.graph.top() : inference.graph.exhausted()};
    put_back(inference, scratch, saved);
    restore(bindings, kept);
    --bindings.depth;
    return result;
}

// The facts of a call's arguments.
std::vector<Fact> argument_facts(const Inference &inference, const ast::Module &syntax,
                                 const ast::CallExpression &call) {
    std::vector<Fact> result;
    result.reserve(call.arguments.size());
    for (const auto &argument : call.arguments) {
        result.push_back(recorded_fact(inference, syntax, argument));
    }
    return result;
}

// A call of the value `target` reads with arguments of facts `arguments`: an anonymous fun bound to the variable is
// evaluated with them; otherwise the value's fact gives the result.
Fact called_value(Inference &inference, const FunctionRef function, const ast::ExprId &target,
                  const std::vector<Fact> &arguments, BindingFacts &bindings, std::size_t &work) {
    const auto &syntax = *function.module->syntax;
    if (const auto lambda = bindings.lambda(target); lambda && bindings.depth < INSTANCE_DEPTH) {
        return instantiate(inference, function, bindings, *lambda, arguments, work);
    }
    Lattice lattice(inference.graph);
    return {call_value(lattice, recorded_fact(inference, syntax, target).type, arguments)};
}

// A call of a value, F(Args).
Fact value_call(Inference &inference, const FunctionRef function, const ast::CallExpression &call,
                BindingFacts &bindings, std::size_t &work) {
    const auto &syntax = *function.module->syntax;
    return called_value(inference, function, ungroup(syntax, call.target), argument_facts(inference, syntax, call),
                        bindings, work);
}

// apply(F, Args) calls the value F, apply(M, F, Args) the exported batch function M:F/A, with the arguments Args
// holds (docs/semantic.md#inference); none for other calls and when the arguments or the target are unknown.
std::optional<Fact> apply_fact(Inference &inference, const FunctionRef function, const ast::Expression &expression,
                               const ast::CallExpression &call, BindingFacts &bindings, std::size_t &work) {
    const auto service = function.function->services.find(&expression);
    if (service == function.function->services.end() || !service->second.apply()) {
        return std::nullopt;
    }
    const auto &syntax = *function.module->syntax;
    Lattice lattice(inference.graph);
    const auto first = recorded_fact(inference, syntax, call.arguments.front()).type;
    if (call.arguments.size() == 2) {
        const auto arguments =
            applied_arguments(inference, lattice, syntax, call.arguments.back(), fun_arity(lattice, first));
        return arguments ? std::optional{called_value(inference, function, ungroup(syntax, call.arguments.front()),
                                                      *arguments, bindings, work)}
                         : std::nullopt;
    }
    const auto arguments = applied_arguments(inference, lattice, syntax, call.arguments.back(), std::nullopt);
    const auto name = recorded_fact(inference, syntax, call.arguments.at(1)).type;
    const auto target = arguments ? named_fun(inference, {first, name}, arguments->size()) : std::nullopt;
    return target ? std::optional{Fact{call_value(lattice, *target, *arguments)}} : std::nullopt;
}

// A fun value: fun F/A and fun M:F/A by the function they name, an anonymous fun by the function types of its
// possible clauses: their arguments' facts after head and guard, and their results.
std::optional<Fact> fun_value_fact(Inference &inference, const FunctionRef function, const ast::Expression &expression,
                                   const BindingFacts &bindings) {
    if (const auto fact = fun_reference_fact(inference, function, expression)) {
        return Fact{*fact};
    }
    const auto *clauses = fun_clauses(expression.value);
    if (!clauses) {
        return std::nullopt;
    }
    const auto arity = clauses->front().arguments.size();
    std::vector<FunctionType> types;
    for (std::size_t index = 0; index < clauses->size(); ++index) {
        const auto &clause = clauses->at(index);
        if (bindings.impossible.contains(&clause.body)) {
            continue;
        }
        const auto found = bindings.fun_inputs.find({&expression, index});
        auto type = found == bindings.fun_inputs.end()
                        ? FunctionType{std::vector<Id>(arity, inference.graph.top()), {inference.graph.bottom()}}
                        : found->second;
        // A relation of the result is to the enclosing function's arguments, not the fun's.
        type.result = {sequence(inference, *function.module->syntax, clause.body).type};
        types.push_back(std::move(type));
    }
    Lattice lattice(inference.graph);
    if (types.empty()) {
        return Fact{lattice.fun(arity, inference.graph.bottom())};
    }
    return Fact{fun_fact(lattice, merge_types(inference.graph, std::move(types)))};
}

// Narrow a returned call's variable arguments to `facts`, position by position.
void narrow_arguments(BindingFacts &bindings, const ast::CallExpression &call, const std::vector<Id> &facts) {
    for (std::size_t index = 0; index < call.arguments.size() && index < facts.size(); ++index) {
        bindings.narrow(call.arguments[index], facts[index]);
    }
}

// Whether a call evaluates its callee again with its argument facts: a callee outside recursive components whose
// inputs hold the arguments and are wider than one, within the depth and work budgets.
bool reanalysable(Inference &inference, const Function &callee, const std::vector<Fact> &arguments) {
    if (inference.recursive.contains(&callee) || inference.reanalyses >= REANALYSIS_DEPTH ||
        inference.reanalysis_work + CALL_WORK > PASS_WORK) {
        return false;
    }
    const auto inputs = inputs_of(inference, callee);
    Lattice lattice(inference.graph);
    bool narrower = false;
    for (std::size_t index = 0; index < inputs.size() && index < arguments.size(); ++index) {
        if (!lattice.within(arguments[index].type, inputs[index])) {
            return false;
        }
        narrower = narrower || arguments[index].type != inputs[index];
    }
    return narrower && inputs.size() == arguments.size();
}

// The callee's body evaluated again with a call's argument facts as its inputs, within the call's work, and the
// callee's own expression facts restored; none past a budget. Its summary never changes.
std::optional<Body> reanalyse(Inference &inference, const FunctionRef callee, const std::vector<Fact> &arguments) {
    const auto &definition = std::get<ast::Function>(callee.module->syntax->form(callee.function->form).value);
    const auto expressions = subtree(*callee.module, function_roots(definition));
    if (expressions.size() > CALLEE_SIZE) {
        return std::nullopt;
    }
    const auto saved = set_aside(inference, expressions);
    std::vector<Id> inputs;
    inputs.reserve(arguments.size());
    for (const auto &argument : arguments) {
        inputs.push_back(argument.type);
    }
    // The call's own work: the walk stops once it has spent CALL_WORK.
    const auto limit = inference.graph.limits().syntax_work;
    const auto start = limit - std::min(limit, CALL_WORK);
    auto work = start;
    std::vector<const ast::Expression *> scratch;
    ++inference.reanalyses;
    auto result = run_body(inference, callee, work, scratch, &inputs);
    --inference.reanalyses;
    inference.reanalysis_work += work - start;
    put_back(inference, scratch, saved);
    return result;
}

// A call of a function of the batch: the function types its arguments select, narrowed by the callee's body
// evaluated again for them; a call that returned narrows its variable arguments to what the callee accepted.
Fact function_call(Inference &inference, const FunctionRef function, const ast::Expression &expression,
                   const ast::CallExpression &call, BindingFacts &bindings) {
    const auto &syntax = *function.module->syntax;
    const auto callee = inference.callees.at(&expression);
    const auto &summary = summary_of(inference, callee.function);
    const auto arguments = argument_facts(inference, syntax, call);
    auto selection = summary.types.empty() ? Selection{call_result(inference, syntax, expression, call)}
                                           : select(inference.graph, summary.types, arguments);
    const auto again =
        selection.result.type != inference.graph.bottom() && reanalysable(inference, *callee.function, arguments)
            ? reanalyse(inference, callee, arguments)
            : std::nullopt;
    if (again) {
        Lattice lattice(inference.graph);
        const auto result = instantiated(lattice, again->result, arguments);
        selection.result = {lattice.meet(selection.result.type, result.type),
                            result.argument ? result.argument : selection.result.argument};
        narrow_arguments(bindings, call, again->domain);
    }
    // A call that returned had arguments within the callee's success domain and the selected types' inputs, once
    // they are final.
    if (selection.result.type != inference.graph.bottom() && !inference.solving.contains(callee.function)) {
        narrow_arguments(bindings, call, summary.inputs);
        narrow_arguments(bindings, call, selection.inputs);
    }
    return selection.result;
}

// A call: none() when it never happens, else by its callee: a value, a function or an unknown target.
Fact call_fact(Inference &inference, const FunctionRef function, const ast::Expression &expression,
               const ast::CallExpression &call, BindingFacts &bindings, std::size_t &work) {
    const auto &syntax = *function.module->syntax;
    if (unreachable_call(inference, syntax, call)) {
        return {inference.graph.bottom()};
    }
    if (fun_call(syntax, call)) {
        return value_call(inference, function, call, bindings, work);
    }
    if (const auto applied = apply_fact(inference, function, expression, call, bindings, work)) {
        return *applied;
    }
    if (opaque_call(function, expression, call)) {
        return {inference.graph.top()};
    }
    return function_call(inference, function, expression, call, bindings);
}

// Groups, matches and blocks have the value of the expression they end with; a match publishes its pattern.
std::optional<Fact> passed_fact(Inference &inference, const FunctionRef function, const ast::Expression &expression,
                                BindingFacts &bindings, std::size_t &work) {
    const auto &syntax = *function.module->syntax;
    if (const auto *group = std::get_if<ast::Group>(&expression.value)) {
        return inference.expressions.at(&syntax.expression(group->expression));
    }
    if (const auto *match = std::get_if<ast::MatchExpression>(&expression.value)) {
        const auto fact = inference.expressions.at(&syntax.expression(match->right));
        bindings.publish(match->left, fact, work);
        bindings.bind_lambda(*match);
        return fact;
    }
    if (const auto *block = std::get_if<ast::BlockExpression>(&expression.value)) {
        return sequence(inference, syntax, block->body);
    }
    return std::nullopt;
}

// The result a call of a batch function selects from the callee's function types (its summary without them), with
// no narrowing of its arguments; none() when it never happens.
Id selected_call(Inference &inference, const ast::Module &syntax, const ast::Expression &expression,
                 const ast::CallExpression &call) {
    if (unreachable_call(inference, syntax, call)) {
        return inference.graph.bottom();
    }
    const auto &summary = summary_of(inference, inference.callees.at(&expression).function);
    return summary.types.empty()
               ? call_result(inference, syntax, expression, call).type
               : select(inference.graph, summary.types, argument_facts(inference, syntax, call)).result.type;
}

// Whether an expression constructs a container from its operands: a tuple, list, map or record.
bool construction(const ast::ExprValue &value) {
    return std::holds_alternative<ast::Tuple>(value) || std::holds_alternative<ast::List>(value) ||
           std::holds_alternative<ast::MapExpression>(value) || std::holds_alternative<ast::RecordExpression>(value);
}

// An operation, a construction or a call of a batch function whose operands are dependent evaluates once per
// combination of their clauses (docs/semantic.md#dependent-facts); other expressions keep `fact`.
Fact lift(Inference &inference, const FunctionRef function, const ast::Expression &expression, BindingFacts &bindings,
          const Fact fact) {
    const auto &syntax = *function.module->syntax;
    const auto *call = std::get_if<ast::CallExpression>(&expression.value);
    const bool operation = std::holds_alternative<ast::BinaryExpression>(expression.value) ||
                           std::holds_alternative<ast::UnaryExpression>(expression.value) ||
                           (call && function.function->services.contains(&expression));
    if (operation) {
        return lifted(bindings, expression_children(*function.module, expression), fact,
                      [&] { return operation_fact(inference, function, expression); });
    }
    if (construction(expression.value)) {
        return lifted(bindings, expression_children(*function.module, expression), fact,
                      [&] { return constructed_fact(inference, function, expression.value); });
    }
    if (call && inference.callees.contains(&expression) && !fun_call(syntax, *call) &&
        !opaque_call(function, expression, *call)) {
        return lifted(bindings, call->arguments, fact,
                      [&]() -> std::optional<Id> { return selected_call(inference, syntax, expression, *call); });
    }
    return fact;
}

// Evaluate a postorder node only after all source-order argument facts are available.
Fact evaluate(Inference &inference, const FunctionRef function, const ast::ExprId &id, BindingFacts &bindings,
              std::size_t &work) {
    const auto &expression = function.module->syntax->expression(id);
    return lift(inference, function, expression, bindings, plain_fact(inference, function, id, bindings, work));
}

// The fact of an expression from its operands' facts as they are.
Fact plain_fact(Inference &inference, const FunctionRef function, const ast::ExprId &id, BindingFacts &bindings,
                std::size_t &work) {
    const auto &syntax = *function.module->syntax;
    const auto &expression = syntax.expression(id);
    if (const auto fact = operation_fact(inference, function, expression)) {
        return {*fact};
    }
    if (const auto *call = std::get_if<ast::CallExpression>(&expression.value)) {
        return call_fact(inference, function, expression, *call, bindings, work);
    }
    if (const auto fact = passed_fact(inference, function, expression, bindings, work)) {
        return *fact;
    }
    if (const auto fact = fun_value_fact(inference, function, expression, bindings)) {
        return *fact;
    }
    if (const auto fact = selection_fact(inference, syntax, expression, bindings)) {
        return *fact;
    }
    return leaf(inference, function, id, bindings);
}

// Run the walk's frames: evaluate expressions in postorder, recording each one's fact and listing it in `recorded`,
// and run the scope steps between them; false when the shared work budget ran out.
bool walk(Inference &inference, const FunctionRef function, BindingFacts &bindings, std::vector<Frame> pending,
          std::size_t &work, std::vector<const ast::Expression *> &recorded) {
    const auto &syntax = *function.module->syntax;
    while (!pending.empty()) {
        if (work >= inference.graph.limits().syntax_work) {
            return false;
        }
        ++work;
        const auto frame = pending.back();
        pending.pop_back();
        const auto &expression = syntax.expression(frame.expression);
        if (frame.step == Step::ready) {
            const auto [fact, added] = inference.expressions.emplace(
                &expression, evaluate(inference, function, frame.expression, bindings, work));
            (void)added;
            recorded.push_back(&expression);
            bindings.matched(expression, fact->second, work);
            if (fact->second.type != inference.graph.bottom()) {
                narrow_uses(bindings, expression);
            }
        } else if (frame.step == Step::visit) {
            bindings.expect(expression.value);
            expand(*function.module, frame.expression, pending);
        } else {
            scope_step(bindings, frame, work);
        }
    }
    return true;
}

// A function's body from `inputs` (by default the ones it starts from); none when the work budget ran out. Every
// recorded expression is listed so a later fixed-point round can discard it.
std::optional<Body> run_body(Inference &inference, const FunctionRef function, std::size_t &work,
                             std::vector<const ast::Expression *> &recorded, const std::vector<Id> *inputs) {
    const auto &syntax = *function.module->syntax;
    const auto &definition = std::get<ast::Function>(syntax.form(function.function->form).value);
    BindingFacts bindings(function, inference, work);
    // Each clause's head matches the inputs; the arguments' facts at clause entry join into the entry domain.
    bindings.inputs = inputs ? *inputs : inputs_of(inference, *function.function);
    bindings.domain.assign(bindings.inputs.size(), inference.graph.bottom());
    bindings.success = bindings.domain;
    std::vector<Frame> frames;
    expand_heads(definition, frames);
    if (!walk(inference, function, bindings, std::move(frames), work, recorded)) {
        return std::nullopt;
    }
    // A function that returns shows its success domain; one that never does, its entry domain. Its dependent facts
    // name its own variables: callers see their join.
    const auto result = erased(joined(inference, syntax, definition.clauses, &bindings.impossible));
    return Body{result, result.type == inference.graph.bottom() ? bindings.domain : bindings.success, bindings.domain,
                merge_types(inference.graph, std::move(bindings.types))};
}

// A shared work budget bounds the entire batch and erases relations as well as concrete types.
Body body(Inference &inference, const FunctionRef function, std::size_t &work,
          std::vector<const ast::Expression *> &recorded) {
    if (auto evaluated = run_body(inference, function, work, recorded, nullptr)) {
        return std::move(*evaluated);
    }
    const auto inputs = inputs_of(inference, *function.function);
    return {{inference.graph.exhausted()}, inputs, inputs};
}

// How a recursive component's solve ended: at a fixed point, or giving every member term() because it reached its
// round cap or its share of the pass work, or because an earlier pass gave up on it. Added for step 58P.
enum class Ending : std::uint8_t { converged, capped, over_share, remembered };

// How the members' summaries changed in a round: grown in place, function types replaced because their inputs
// changed, or flipped back to a summary of an earlier round. Added for step 58Q.
struct RoundChanges {
    std::size_t grown_ = 0;
    std::size_t replaced_ = 0;
    std::size_t flipped_ = 0;

    // The members whose summaries changed.
    std::size_t total() const { return grown_ + replaced_ + flipped_; }

    // Add another round's counts.
    void add(const RoundChanges &other) {
        grown_ += other.grown_;
        replaced_ += other.replaced_;
        flipped_ += other.flipped_;
    }
};

// One solve of a recursive component: its round cap, the members whose summaries changed in each round it ran, the
// kinds of those changes, the member walks it ran (58R), and how it ended. Added for --print-stage-times.
struct SolveTrace {
    std::size_t round_cap_ = 0;
    std::vector<std::size_t> changes_;
    RoundChanges kinds_;
    std::size_t walks_ = 0;
    Ending ending_ = Ending::converged;
};

// Which members of a solve still need a walk, which members each walk read, and the expression facts each recorded,
// so a round walks only members that read a summary changed since their last walk. Added for step 58R.
class Worklist {
  public:
    explicit Worklist(const Component &component)
        : dirty_(component.members.size(), true), readers_(component.members.size()), reads_(component.members.size()),
          recorded_(component.members.size()) {
        for (std::size_t index = 0; index < component.members.size(); ++index) {
            indices_.emplace(component.members[index].function, index);
        }
    }

    // Whether the member needs a walk, and take it off the list for its walk.
    bool dirty(const std::size_t member) const { return dirty_.at(member); }

    void clean(const std::size_t member) { dirty_.at(member) = false; }

    // Every member needs a walk again (giving up records every member's facts).
    void all() { std::ranges::fill(dirty_, true); }

    // Note the members a member's walk read, replacing what its previous walk read.
    void read(const std::size_t member, const std::set<const Function *> &functions) {
        for (const auto read : reads_.at(member)) {
            readers_.at(read).erase(member);
        }
        reads_.at(member).clear();
        for (const auto *function : functions) {
            if (const auto found = indices_.find(function); found != indices_.end()) {
                reads_.at(member).push_back(found->second);
                readers_.at(found->second).insert(member);
            }
        }
    }

    // A member's summary changed: every member that read it needs a walk.
    void changed(const std::size_t member) {
        for (const auto reader : readers_.at(member)) {
            dirty_.at(reader) = true;
        }
    }

    // The expression facts the member's last walk recorded.
    std::vector<const ast::Expression *> &recorded(const std::size_t member) { return recorded_.at(member); }

  private:
    // Member indices by function, and the members still to walk.
    std::map<const Function *, std::size_t> indices_;
    std::vector<bool> dirty_;
    // For each member: the members that read it, and the members it read.
    std::vector<std::set<std::size_t>> readers_;
    std::vector<std::vector<std::size_t>> reads_;
    // For each member: the expressions whose facts its last walk recorded.
    std::vector<std::vector<const ast::Expression *>> recorded_;
};

// A member's summary as rounds compare it: its result and the function types calls select from.
struct Snapshot {
    Fact result;
    std::vector<FunctionType> types;
    bool operator==(const Snapshot &) const = default;
};

// Summaries a member keeps from its last rounds to see it return to one of them. Added for step 58Q.
constexpr std::size_t HISTORY = 4;

// The members' summaries of a solve's last rounds, oldest first, by member index, and the members that gave up after
// returning to one of them. Added for step 58Q.
class RoundHistory {
  public:
    explicit RoundHistory(const std::size_t members) : kept_(members), gave_up_(members, false) {}

    // Whether `next` equals one of the member's kept summaries.
    bool returns(const std::size_t member, const Snapshot &next) const {
        return std::ranges::contains(kept_.at(member), next);
    }

    // Whether the member gave up for the rest of the solve, and make it give up.
    bool gave_up(const std::size_t member) const { return gave_up_.at(member); }

    void give_up(const std::size_t member) { gave_up_.at(member) = true; }

    // Keep the member's summary of this round, forgetting the oldest past HISTORY.
    void keep(const std::size_t member, Snapshot snapshot) {
        auto &kept = kept_.at(member);
        kept.push_back(std::move(snapshot));
        if (kept.size() > HISTORY) {
            kept.erase(kept.begin());
        }
    }

  private:
    // The kept summaries, oldest first, by member index.
    std::vector<std::vector<Snapshot>> kept_;
    // The members whose summaries are term() without function types for the rest of the solve.
    std::vector<bool> gave_up_;
};

// Rounds of plain joins before results widen (docs/semantic.md#inference-domain), as many as the singleton budget:
// cycles of up to that many functions converge exactly.
constexpr std::size_t JOIN_ROUNDS = 8;
// Widening passes over a component after the join rounds: a widened fact moves around a cycle one member per round,
// and an integer fact widens at most three times (to a threshold, the next, unbounded). A component that has not
// converged by then widens every member to top.
constexpr std::size_t WIDENING_PASSES = 4;
// A recursive component may spend this fraction of the pass work on its rounds; past it, it widens every member to
// top like at its round cap, so one component cannot starve the components after it. Added for step 58P.
constexpr std::size_t COMPONENT_WORK_SHARE = 8;

// Replace a summary: its result and the entry domain of its arguments (by default the inputs the body starts from).
void summarize(Inference &inference, const FunctionRef function, const Fact result,
               std::optional<std::vector<Id>> domain = std::nullopt) {
    inference.functions.insert_or_assign(
        function.function, Summary{domain ? std::move(*domain) : inputs_of(inference, *function.function), result});
}

// Count one member's change of summary by its kind, against its previous summary and the ones `history` kept.
void classify(const Summary &previous, const Snapshot &next, const RoundHistory *history, const std::size_t member,
              RoundChanges &changes) {
    if (next.result == previous.result && next.types == previous.types) {
        return;
    }
    if (history && history->returns(member, next)) {
        ++changes.flipped_;
    } else if (!same_inputs(previous.types, next.types)) {
        ++changes.replaced_;
    } else {
        ++changes.grown_;
    }
}

// A member's summary after a round: its result joins (or widens) with the previous one and its function types never
// shrink. A member that returns to a kept summary (joins are not monotone at the container depth limit, so it would
// go round for ever) gives up for the rest of the solve: term() without function types (step 58Q).
Snapshot next_summary(Inference &inference, const Summary &summary, Body evaluated, const bool widening,
                      RoundHistory *history, const std::size_t member, RoundChanges &changes) {
    if (history && history->gave_up(member)) {
        return {{inference.graph.exhausted()}, {}};
    }
    // Calls read the function types too: they converge with the results.
    Snapshot next{merged(inference.graph, summary.result, evaluated.result, widening),
                  next_round(inference.graph, summary.types, std::move(evaluated.types), widening)};
    const auto flips = changes.flipped_;
    classify(summary, next, history, member, changes);
    if (!history) {
        return next;
    }
    if (changes.flipped_ != flips) {
        history->give_up(member);
        return {{inference.graph.exhausted()}, {}};
    }
    history->keep(member, next);
    return next;
}

// Walk one member from the current assumptions, replacing the expression facts of its previous walk and noting the
// summaries it read. Added for step 58R.
Body walk_member(Inference &inference, const FunctionRef member, const std::size_t index, std::size_t &work,
                 Worklist &worklist) {
    auto &recorded = worklist.recorded(index);
    for (const auto *expression : recorded) {
        inference.expressions.erase(expression);
    }
    recorded.clear();
    std::set<const Function *> reads;
    inference.reads = &reads;
    auto evaluated = body(inference, member, work, recorded);
    inference.reads = nullptr;
    worklist.read(index, reads);
    return evaluated;
}

// Re-infer the members the worklist holds, in member order, from the current assumptions; results join with the
// previous round's, or widen once `widening`. A changed summary puts its readers on the worklist: later members in
// this round, earlier ones in the next (step 58R). Returns the members' changes by kind and counts walks in `walks`;
// `history`, when given, keeps each member's summaries to tell flips.
RoundChanges refine(Inference &inference, const Component &component, std::size_t &work, Worklist &worklist,
                    bool widening, RoundHistory *history, std::size_t &walks) {
    RoundChanges changes;
    for (std::size_t index = 0; index < component.members.size(); ++index) {
        if (!worklist.dirty(index)) {
            continue;
        }
        worklist.clean(index);
        ++walks;
        const auto member = component.members[index];
        auto evaluated = walk_member(inference, member, index, work, worklist);
        auto &summary = inference.functions.at(member.function);
        auto domain = std::move(evaluated.domain);
        auto entry = std::move(evaluated.entry);
        const auto before = changes.total();
        auto next = next_summary(inference, summary, std::move(evaluated), widening, history, index, changes);
        summary.result = next.result;
        summary.inputs = std::move(domain);
        summary.entry = std::move(entry);
        summary.types = std::move(next.types);
        if (changes.total() != before) {
            worklist.changed(index);
        }
    }
    return changes;
}

// The members of a solved component have final summaries.
void finish(Inference &inference, const Component &component) {
    for (const auto member : component.members) {
        inference.solving.erase(member.function);
    }
}

// Run rounds from the current summaries until no member changes, the round cap or the component's share of the pass
// work; `trace` gets the members changed per round, their kinds and the walks. Returns how the rounds ended.
Ending iterate(Inference &inference, const Component &component, std::size_t &work, Worklist &worklist,
               SolveTrace &trace) {
    const auto rounds = JOIN_ROUNDS + WIDENING_PASSES * component.members.size();
    const auto share = inference.graph.limits().syntax_work / COMPONENT_WORK_SHARE;
    const auto started = work;
    RoundHistory history(component.members.size());
    for (std::size_t round = 0; round < rounds; ++round) {
        const auto changes = refine(inference, component, work, worklist, round >= JOIN_ROUNDS, &history, trace.walks_);
        trace.changes_.push_back(changes.total());
        trace.kinds_.add(changes);
        if (changes.total() == 0) {
            return Ending::converged;
        }
        if (work - started > share) {
            return Ending::over_share;
        }
    }
    return Ending::capped;
}

// Give every member term() and walk them once to record their expression facts: the summaries of a component that
// does not converge, which keeps only its widened union summaries.
void give_up(Inference &inference, const Component &component, std::size_t &work, Worklist &worklist,
             std::size_t &walks) {
    for (const auto member : component.members) {
        summarize(inference, member, {inference.graph.exhausted()});
    }
    worklist.all();
    (void)refine(inference, component, work, worklist, false, nullptr, walks);
    for (const auto member : component.members) {
        inference.functions.at(member.function).types.clear();
    }
}

// Iterate from bottom to a fixed point; non-convergence visibly widens all members to top and records final facts.
// A component an earlier pass gave up on gives up at once (step 58P). Returns the rounds' trace for
// --print-stage-times.
SolveTrace solve(Inference &inference, const Component &component, std::size_t &work) {
    for (const auto member : component.members) {
        summarize(inference, member, {inference.graph.bottom()});
        inference.solving.insert(member.function);
    }
    Worklist worklist(component);
    SolveTrace trace{.round_cap_ = JOIN_ROUNDS + WIDENING_PASSES * component.members.size(),
                     .changes_ = {},
                     .kinds_ = {},
                     .walks_ = 0,
                     .ending_ = Ending::converged};
    trace.ending_ = inference.failed.contains(&component) ? Ending::remembered
                                                          : iterate(inference, component, work, worklist, trace);
    if (trace.ending_ != Ending::converged) {
        inference.failed.insert(&component);
        give_up(inference, component, work, worklist, trace.walks_);
    }
    finish(inference, component);
    return trace;
}

// Passes over the batch that join local inputs, then passes that widen them, before inputs and fun references are
// given up as unknown.
constexpr std::size_t PASSES = JOIN_ROUNDS + 2 * WIDENING_PASSES;

// Whether a build cache kept the results of every member: the component keeps them. Added for the inference cache
// (58W).
bool reused(const Inference &inference, const Component &component) {
    return !inference.reused.empty() && std::ranges::all_of(component.members, [&](const FunctionRef member) {
        return inference.reused.contains(member.function);
    });
}

// Infer one component from the current summaries of its callees; a recursive one returns its rounds' trace.
SolveTrace infer_component(Inference &inference, const Component &component, std::size_t &work,
                           std::vector<const ast::Expression *> &recorded) {
    if (reused(inference, component)) {
        return {};
    }
    if (component.recursive) {
        return solve(inference, component, work);
    }
    const auto function = component.members.front();
    recorded.clear();
    auto evaluated = body(inference, function, work, recorded);
    summarize(inference, function, evaluated.result, std::move(evaluated.domain));
    auto &summary = inference.functions.at(function.function);
    summary.entry = std::move(evaluated.entry);
    summary.types = std::move(evaluated.types);
    return {};
}

// Add one solve of a recursive component to its cost: rounds, the cap, member walks and changes, and whether the
// pass budget was spent by its end. Added for the recursive-component measurements of --print-stage-times.
void record_solve(InferenceProfile::Cost &cost, const SolveTrace &trace, const std::size_t members,
                  const bool exhausted) {
    if (trace.round_cap_ == 0) {
        return;
    }
    auto &rounds = cost.rounds_;
    ++rounds.solves_;
    rounds.rounds_ += trace.changes_.size();
    rounds.round_cap_ = trace.round_cap_;
    rounds.capped_ += trace.ending_ == Ending::capped ? 1 : 0;
    rounds.over_share_ += trace.ending_ == Ending::over_share ? 1 : 0;
    rounds.remembered_ += trace.ending_ == Ending::remembered ? 1 : 0;
    rounds.exhausted_ += exhausted ? 1 : 0;
    rounds.walks_ += trace.walks_;
    rounds.skipped_ += (trace.changes_.size() + (trace.ending_ != Ending::converged ? 1 : 0)) * members - trace.walks_;
    for (const auto changed : trace.changes_) {
        rounds.changes_ += changed;
    }
    rounds.grown_ += trace.kinds_.grown_;
    rounds.replaced_ += trace.kinds_.replaced_;
    rounds.flipped_ += trace.kinds_.flipped_;
    rounds.last_changes_ = trace.changes_;
}

// Infer one component and add its time, work and widening to the profile, noting the work budget running out in
// it. Added for --print-stage-times.
void profiled_component(Inference &inference, const CallGraph &calls, const std::size_t index, std::size_t &work,
                        std::vector<const ast::Expression *> &recorded) {
    const auto started = std::chrono::steady_clock::now();
    const auto before = work;
    const auto widenings = inference.graph.widenings();
    const auto trace = infer_component(inference, calls.components[index], work, recorded);
    auto &cost = inference.profile->costs_[index];
    const auto limit = inference.graph.limits().syntax_work;
    record_solve(cost, trace, calls.components[index].members.size(), work >= limit);
    cost.elapsed_ += std::chrono::steady_clock::now() - started;
    cost.work_ += work - before;
    ++cost.passes_;
    cost.widened_ = cost.widened_ || inference.graph.widenings() > widenings;
    auto &pass = inference.profile->passes_.back();
    if (!pass.exhausted_at_ && before < limit && work >= limit) {
        pass.exhausted_at_ = &calls.components[index];
    }
}

// Infer every component in callee-before-caller order, from fresh expression facts (besides reused ones) and its own
// work budget.
void infer_pass(Inference &inference, const CallGraph &calls) {
    inference.expressions = inference.reused_expressions;
    inference.fun_reads.clear();
    inference.reanalysis_work = 0;
    std::size_t work = 0;
    std::vector<const ast::Expression *> recorded;
    if (!inference.profile) {
        for (const auto &component : calls.components) {
            (void)infer_component(inference, component, work, recorded);
        }
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    inference.profile->passes_.emplace_back();
    for (std::size_t index = 0; index < calls.components.size(); ++index) {
        profiled_component(inference, calls, index, work, recorded);
    }
    auto &pass = inference.profile->passes_.back();
    pass.elapsed_ = std::chrono::steady_clock::now() - started;
    pass.work_ = work;
}

// Record how the last pass ended. Added for --print-stage-times.
void pass_outcome(const Inference &inference, const std::string_view outcome) {
    if (inference.profile) {
        inference.profile->passes_.back().outcome_ = outcome;
    }
}

// Give the profile the pass budget and one cost per component. Added for --print-stage-times.
void start_profile(InferenceProfile *profile, const CallGraph &calls, const std::size_t budget) {
    if (profile) {
        profile->budget_ = budget;
        profile->costs_.resize(calls.components.size());
        for (std::size_t index = 0; index < calls.components.size(); ++index) {
            profile->costs_[index].component_ = &calls.components[index];
        }
    }
}

// Whether every fun F/A of the pass saw the final facts of its function.
bool settled(Inference &inference) {
    Lattice lattice(inference.graph);
    return std::ranges::all_of(inference.fun_reads, [&](const auto &read) {
        return summary_fun(lattice, inference.functions.at(read.first), read.first->key.arity) == read.second;
    });
}

// Index each call's callee, the exported functions and the members of recursive components.
void index_calls(Inference &inference, const CallGraph &calls) {
    for (const auto &call : calls.calls) {
        inference.callees.emplace(&call.caller.module->syntax->expression(call.expression), call.callee);
    }
    for (const auto function : calls.order) {
        if (function.function->exported) {
            const auto &key = function.function->key;
            inference.exported.emplace(std::tuple{utf8(function.module->name), utf8(key.name), key.arity},
                                       function.function);
        }
    }
    for (const auto &component : calls.components) {
        for (const auto member : component.recursive ? component.members : std::vector<FunctionRef>{}) {
            inference.recursive.insert(member.function);
        }
    }
}
} // namespace

std::unique_ptr<Inference> infer(const CallGraph &calls, const Limits limits, InferenceProfile *profile) {
    auto result = std::make_unique<Inference>(limits);
    result->profile = profile;
    infer(*result, calls);
    return result;
}

void infer(Inference &inference, const CallGraph &calls) {
    start_profile(inference.profile, calls, inference.graph.limits().syntax_work);
    index_calls(inference, calls);
    // Local inputs grow from their calls and a fun F/A reads its function's summary as far as it is known: passes
    // repeat until the inputs stop changing and each fun read its function's final result.
    prepare_inputs(inference, calls);
    for (std::size_t pass = 0; pass < PASSES; ++pass) {
        infer_pass(inference, calls);
        const bool changed = gather_inputs(inference, calls, pass >= JOIN_ROUNDS);
        if (!changed && settled(inference)) {
            pass_outcome(inference, "settled");
            return;
        }
        pass_outcome(inference, changed ? "inputs changed" : "fun results changed");
    }
    // Reused inputs stay: they were gathered from every caller in a module a build cache kept (58W).
    for (auto &[function, inputs] : inference.inputs) {
        if (!inference.reused.contains(function)) {
            std::ranges::fill(inputs, inference.graph.exhausted());
        }
    }
    inference.opaque_funs = true;
    // With every input term() a component may converge where it could not before: it gets one more solve (58P).
    inference.failed.clear();
    infer_pass(inference, calls);
    pass_outcome(inference, "final pass after passes did not settle: inputs widened to term()");
}
} // namespace clause::semantic::types
