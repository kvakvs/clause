#include "inference_scopes.hpp"
#include "../capabilities.hpp"
#include "dependent.hpp"
#include "inference_containers.hpp"
#include "inference_narrowing.hpp"
#include "lattice.hpp"
#include <span>

namespace clause::semantic::types {
namespace {
// A clause's guard: optional for case, receive and fun clauses, always present for if clauses.
const ast::GuardSyntax *guard_of(const std::optional<ast::GuardSyntax> &guard) { return guard ? &*guard : nullptr; }

const ast::GuardSyntax *guard_of(const ast::GuardSyntax &guard) { return &guard; }

// Add a guard's tests to frames in evaluation order.
void push_guard(const ast::GuardSyntax *guard, std::vector<Frame> &frames) {
    for (const auto &alternative : guard ? guard->alternatives : std::vector<ast::GuardConjunction>{}) {
        for (const auto &test : alternative.tests) {
            frames.push_back({test});
        }
    }
}

// Frames in evaluation order, pushed onto the walk's stack in reverse at the end.
class Order final {
  public:
    explicit Order(const Module &module) : module_(module) {}

    void visit(const ast::ExprId &id) { frames_.push_back({id}); }

    void visit(const std::vector<ast::ExprId> &ids) {
        for (const auto &id : ids) {
            visit(id);
        }
    }

    void step(const ast::ExprId &id, const Step step, const std::size_t clause = 0) {
        frames_.push_back({id, step, clause});
    }

    // Guard tests of every alternative.
    void guard(const ast::GuardSyntax *guard) {
        for (const auto &alternative : guard ? guard->alternatives : std::vector<ast::GuardConjunction>{}) {
            visit(alternative.tests);
        }
    }

    // A clause of a case, if, receive, try, maybe or fun, `index` among its construct's clauses: entered, guarded,
    // evaluated and left.
    template <typename Clause> void clause(const ast::ExprId &id, const Clause &clause, const std::size_t index) {
        step(id, Step::enter, index);
        guard(guard_of(clause.guard));
        step(id, Step::guarded, index);
        visit(clause.body);
        step(id, Step::leave, index);
    }

    // Each clause of a construct, numbered from `first`.
    template <typename Clauses>
    void clauses(const ast::ExprId &id, const Clauses &clauses, const std::size_t first = 0) {
        for (std::size_t index = 0; index < clauses.size(); ++index) {
            clause(id, clauses[index], first + index);
        }
    }

    void push(std::vector<Frame> &pending) const { pending.insert(pending.end(), frames_.rbegin(), frames_.rend()); }

    const Module &module() const { return module_; }

  private:
    const Module &module_;
    std::vector<Frame> frames_;
};

// The frames of a case, if or fun; false for other expressions.
bool selection(Order &order, const ast::ExprId &id, const ast::ExprValue &value) {
    if (const auto *selection = std::get_if<ast::CaseExpression>(&value)) {
        order.visit(selection->value);
        order.step(id, Step::open);
        order.clauses(id, selection->clauses);
        order.step(id, Step::close);
        return true;
    }
    if (const auto *choice = std::get_if<ast::IfExpression>(&value)) {
        order.step(id, Step::open);
        order.clauses(id, choice->clauses);
        order.step(id, Step::close);
        return true;
    }
    if (const auto *clauses = fun_clauses(value)) {
        order.clauses(id, *clauses);
        return true;
    }
    return false;
}

// The frames of a receive: its timeout first, its clauses, its after body last.
void receive_frames(Order &order, const ast::ExprId &id, const ast::ReceiveExpression &receive) {
    if (receive.after) {
        order.visit(receive.after->timeout);
    }
    order.step(id, Step::open);
    order.clauses(id, receive.clauses);
    if (receive.after) {
        order.step(id, Step::save);
        order.visit(receive.after->body);
        order.step(id, Step::complete);
        order.step(id, Step::restore);
    }
    order.step(id, Step::close);
}

// The frames of a receive, andalso or orelse; false for other expressions.
bool waiting(Order &order, const ast::ExprId &id, const ast::ExprValue &value) {
    if (const auto *receive = std::get_if<ast::ReceiveExpression>(&value)) {
        receive_frames(order, id, *receive);
        return true;
    }
    const auto *binary = std::get_if<ast::BinaryExpression>(&value);
    const bool orelse = binary && binary->operation == ast::BinaryOperator::or_else;
    if (binary && (orelse || binary->operation == ast::BinaryOperator::and_also)) {
        // The right operand runs only when the left one is true (andalso) or false (orelse, which proves nothing).
        order.visit(binary->left);
        order.step(binary->left, orelse ? Step::save : Step::assume);
        order.visit(binary->right);
        order.step(id, Step::restore);
        return true;
    }
    return false;
}

// The frames of a try: its body, its of clauses from the body's value and end, its catch clauses and after body from
// the facts before it. The facts after it join those at the end of the body (without of) and of each completed clause.
void attempt_frames(Order &order, const ast::ExprId &id, const ast::TryExpression &attempt) {
    order.step(id, Step::save);
    order.visit(attempt.body);
    order.step(id, Step::open);
    if (attempt.of) {
        order.clauses(id, *attempt.of);
    } else {
        order.step(id, Step::finish);
    }
    const auto first = attempt.of ? attempt.of->size() : 0;
    for (std::size_t index = 0; attempt.handlers && index < attempt.handlers->size(); ++index) {
        order.step(id, Step::reset);
        order.clause(id, attempt.handlers->at(index), first + index);
    }
    if (attempt.after) {
        order.step(id, Step::reset);
        order.visit(*attempt.after);
    }
    order.step(id, Step::restore);
    order.step(id, Step::close);
}

// Whether a maybe body has a ?= match, which can leave the maybe early.
bool conditional_match(const ast::MaybeExpression &conditional) {
    return std::ranges::any_of(conditional.body,
                               [](const auto &item) { return std::holds_alternative<ast::MaybeMatch>(item); });
}

// The frames of a maybe: its body, each ?= match narrowing what follows it, then its else clauses from the facts
// before it. The facts after it join those at the end of the body, of each completed else clause and, without else,
// those before it (a failed ?= match).
void maybe_frames(Order &order, const ast::ExprId &id, const ast::MaybeExpression &conditional) {
    order.step(id, Step::save);
    order.step(id, Step::open);
    for (std::size_t position = 0; position < conditional.body.size(); ++position) {
        const auto *match = std::get_if<ast::MaybeMatch>(&conditional.body[position]);
        order.visit(match ? match->value : std::get<ast::ExprId>(conditional.body[position]));
        if (match) {
            order.step(id, Step::bind, position);
        }
    }
    order.step(id, Step::finish);
    for (std::size_t index = 0; conditional.otherwise && index < conditional.otherwise->size(); ++index) {
        order.step(id, Step::reset);
        order.clause(id, conditional.otherwise->at(index), index);
    }
    if (!conditional.otherwise && conditional_match(conditional)) {
        order.step(id, Step::reset);
        order.step(id, Step::complete);
    }
    order.step(id, Step::restore);
    order.step(id, Step::close);
}

// The frames of a catch, try or maybe: nothing narrowed inside a catch holds after it; false for others.
bool guarded(Order &order, const ast::ExprId &id, const ast::ExprValue &value) {
    if (const auto *caught = std::get_if<ast::CatchExpression>(&value)) {
        order.step(id, Step::save);
        order.visit(caught->expression);
        order.step(id, Step::restore);
        return true;
    }
    if (const auto *attempt = std::get_if<ast::TryExpression>(&value)) {
        attempt_frames(order, id, *attempt);
        return true;
    }
    if (const auto *conditional = std::get_if<ast::MaybeExpression>(&value)) {
        maybe_frames(order, id, *conditional);
        return true;
    }
    return false;
}

// The frames of a comprehension: generator inputs, filters (each narrowing what follows it) and templates; false
// for other expressions.
bool comprehension(Order &order, const ast::ExprId &id, const ast::ExprValue &value) {
    const auto *qualifiers = comprehension_qualifiers(value);
    if (!qualifiers) {
        return false;
    }
    order.step(id, Step::save);
    for (const auto &qualifier : *qualifiers) {
        for (const auto &simple : zipped(qualifier)) {
            if (const auto input = generator_input(simple)) {
                order.visit(*input);
            } else {
                const auto &filter = std::get<ast::FilterQualifier>(simple.value).expression;
                order.visit(filter);
                order.step(filter, Step::assume);
            }
        }
    }
    order.visit(comprehension_templates(value));
    order.step(id, Step::restore);
    return true;
}

// The fact of an expression the walk recorded, with its relation; term() for one it did not.
Fact recorded_fact(const BindingFacts &bindings, const ast::ExprId &id) {
    const auto found = bindings.inference.expressions.find(&bindings.function.module->syntax->expression(id));
    return found == bindings.inference.expressions.end() ? Fact{bindings.inference.graph.top()} : found->second;
}

Id recorded(const BindingFacts &bindings, const ast::ExprId &id) { return recorded_fact(bindings, id).type; }

// Whether an expression of a body never completes.
bool stops(const BindingFacts &bindings, const std::span<const ast::ExprId> body) {
    return std::ranges::any_of(body, [&](const ast::ExprId &expression) {
        return recorded(bindings, expression) == bindings.inference.graph.bottom();
    });
}

// The value of a body: its last expression's, none() when an expression of it never completes.
Fact body_fact(const BindingFacts &bindings, const std::vector<ast::ExprId> &body) {
    return stops(bindings, body) ? Fact{bindings.inference.graph.bottom()} : recorded_fact(bindings, body.back());
}

// The category the previous clause left out of value `position` (an argument, or 0 for a case's scrutinee): when
// all its patterns were plain variables and its whole guard one type test on the variable at `position`, it can
// only have failed on that test.
template <typename Clause>
std::optional<Id> left_out(BindingFacts &bindings, const Clause &previous, const std::vector<ast::ExprId> &patterns,
                           const std::size_t position) {
    const auto &syntax = *bindings.function.module->syntax;
    const bool plain = std::ranges::all_of(patterns, [&](const ast::ExprId &pattern) {
        return std::holds_alternative<ast::Variable>(syntax.expression(ungroup(syntax, pattern)).value);
    });
    const auto tested = plain && previous.guard ? single_test(bindings, *previous.guard) : std::nullopt;
    if (!tested || position >= patterns.size() || variable(bindings, patterns[position]) != tested->first) {
        return std::nullopt;
    }
    return tested->second;
}

// After a clause's guard: when the previous clause's patterns were all plain variables and its whole guard a single
// comparison of one of them with an integer constant, it can only have failed that comparison, so the value at the
// same position (a plain variable in this clause) narrows by the comparison being false.
template <typename Clause>
void complement(BindingFacts &bindings, const Clause &previous, const std::vector<ast::ExprId> &before,
                const std::vector<ast::ExprId> &now) {
    const auto &syntax = *bindings.function.module->syntax;
    const auto plain = [&](const ast::ExprId &pattern) {
        return std::holds_alternative<ast::Variable>(syntax.expression(ungroup(syntax, pattern)).value);
    };
    const auto compared = std::ranges::all_of(before, plain) && previous.guard
                              ? single_comparison(bindings, *previous.guard)
                              : std::nullopt;
    for (std::size_t position = 0; compared && position < before.size() && position < now.size(); ++position) {
        const auto target = plain(now[position]) ? variable(bindings, now[position]) : std::nullopt;
        if (target && variable(bindings, before[position]) == compared->identity) {
            assume_false(bindings, *compared, *target);
        }
    }
}

// The roots of a clause's argument patterns.
std::vector<ast::ExprId> argument_roots(const ast::Module &syntax, const ast::FunctionClause &clause) {
    std::vector<ast::ExprId> result;
    result.reserve(clause.arguments.size());
    for (const auto &argument : clause.arguments) {
        result.push_back(pattern_root(syntax, argument));
    }
    return result;
}

// Narrow a value by a clause's pattern: publish the pattern's variables, narrow a variable the value was read from,
// and mark the clause impossible when nothing is left.
Id match(BindingFacts &bindings, const ast::ExprId &pattern, const Fact value, const std::vector<ast::ExprId> *body,
         std::size_t &work) {
    Lattice lattice(bindings.inference.graph);
    const auto narrowed = lattice.meet(value.type, pattern_shape(bindings, pattern));
    // A value that never exists, or that the pattern cannot match, never enters the clause.
    if (narrowed == lattice.graph().bottom()) {
        bindings.impossible.insert(body);
    }
    bindings.publish(pattern, {narrowed, value.argument}, work);
    return narrowed;
}

// The value the clauses of a case, a try's of part or a maybe's else part match: the expression it was read from
// (none for a maybe's failed values) and its fact.
struct Scrutinee {
    std::optional<ast::ExprId> expression;
    Id fact;
};

// The scrutinee and pattern matched whole, then, for a tuple scrutinee and a tuple pattern of its size, each element
// of the scrutinee with the pattern's.
std::vector<std::pair<ast::ExprId, ast::ExprId>> element_pairs(const ast::Module &syntax, const ast::ExprId &scrutinee,
                                                               const ast::ExprId &pattern) {
    std::vector<std::pair<ast::ExprId, ast::ExprId>> result{{scrutinee, pattern}};
    const auto *reads = std::get_if<ast::Tuple>(&syntax.expression(ungroup(syntax, scrutinee)).value);
    const auto *parts = std::get_if<ast::Tuple>(&syntax.expression(ungroup(syntax, pattern)).value);
    for (std::size_t index = 0;
         reads && parts && index < reads->elements.size() && reads->elements.size() == parts->elements.size();
         ++index) {
        result.emplace_back(reads->elements[index], ungroup(syntax, parts->elements[index]));
    }
    return result;
}

// Narrow the scrutinee expression to `narrowed`: a variable, or each variable of a tuple of them to its element.
void narrow_scrutinee(BindingFacts &bindings, const ast::ExprId &scrutinee, const Id narrowed) {
    const auto &syntax = *bindings.function.module->syntax;
    bindings.narrow(scrutinee, narrowed);
    const auto *tuple = std::get_if<ast::Tuple>(&syntax.expression(ungroup(syntax, scrutinee)).value);
    Lattice lattice(bindings.inference.graph);
    for (std::size_t index = 0; tuple && index < tuple->elements.size(); ++index) {
        bindings.narrow(tuple->elements[index], tuple_element(lattice, narrowed, index + 1, tuple->elements.size()));
    }
}

// After a clause's pattern narrowed the scrutinee expression: a variable pattern names its value (and a tuple
// pattern's variable a tuple scrutinee's element), so narrowing either narrows both, and a true pattern assumes a
// test scrutinee.
void relate(BindingFacts &bindings, const ast::ExprId &scrutinee, const ast::ExprId &pattern,
            const std::vector<ast::ExprId> &body) {
    const auto &syntax = *bindings.function.module->syntax;
    for (const auto &[read_part, pattern_part] : element_pairs(syntax, scrutinee, pattern)) {
        const auto bound = variable(bindings, pattern_part);
        const auto read = variable(bindings, read_part);
        if (bound && read && std::holds_alternative<ast::Variable>(syntax.expression(pattern_part).value)) {
            bindings.link(*bound, *read);
        }
    }
    const auto *atom = std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, pattern)).value);
    if (atom && atom->name == U"true" && !assume(bindings, scrutinee)) {
        bindings.impossible.insert(&body);
    }
}

// Enter a clause of a case, a try's of part or a maybe's else part: its pattern narrows the scrutinee, which the
// previous clause's single type test may already have narrowed.
void enter_branch(BindingFacts &bindings, const Scrutinee &scrutinee, const std::vector<ast::BranchClause> &clauses,
                  const std::size_t index, std::size_t &work) {
    const auto &syntax = *bindings.function.module->syntax;
    const auto &clause = clauses[index];
    Lattice lattice(bindings.inference.graph);
    auto value = scrutinee.fact;
    if (index > 0) {
        const auto &previous = clauses[index - 1];
        if (const auto removed = left_out(bindings, previous, {pattern_root(syntax, previous.pattern)}, 0)) {
            value = lattice.subtract(value, *removed);
        }
    }
    const auto pattern = pattern_root(syntax, clause.pattern);
    const auto narrowed = match(bindings, pattern, {value}, &clause.body, work);
    if (scrutinee.expression) {
        narrow_scrutinee(bindings, *scrutinee.expression, narrowed);
        relate(bindings, *scrutinee.expression, pattern, clause.body);
    }
}

// The value of a try or maybe body.
Id body_value(const BindingFacts &bindings, const std::vector<ast::ExprId> &body) {
    return body_fact(bindings, body).type;
}

// Enter a clause of a try: an of clause matches the body's value, a catch clause its class (error, exit or throw)
// and reason.
void enter_try(BindingFacts &bindings, const ast::TryExpression &attempt, const std::size_t index, std::size_t &work) {
    const auto first = attempt.of ? attempt.of->size() : 0;
    if (attempt.of && index < first) {
        enter_branch(bindings, {attempt.body.back(), body_value(bindings, attempt.body)}, *attempt.of, index, work);
        return;
    }
    if (!attempt.handlers) {
        return;
    }
    const auto &handler = attempt.handlers->at(index - first);
    Lattice lattice(bindings.inference.graph);
    if (handler.exception_class) {
        const std::vector<Id> classes{lattice.atom("error"), lattice.atom("exit"), lattice.atom("throw")};
        match(bindings, *handler.exception_class, {lattice.join(classes, 0)}, &handler.body, work);
    }
    const auto &syntax = *bindings.function.module->syntax;
    match(bindings, pattern_root(syntax, handler.reason), {lattice.graph().top()}, &handler.body, work);
}

// Enter a fun clause: its patterns match the arguments' facts of a fun evaluated for a call, otherwise any terms.
void enter_fun(BindingFacts &bindings, const ast::Expression &fun, const std::size_t index, std::size_t &work) {
    const auto &clause = fun_clauses(fun.value)->at(index);
    const auto instance = bindings.instances.find(&fun);
    const auto patterns = argument_roots(*bindings.function.module->syntax, clause);
    for (std::size_t position = 0; position < patterns.size(); ++position) {
        const bool known = instance != bindings.instances.end() && position < instance->second.size();
        const auto fact = known ? instance->second[position] : Fact{bindings.inference.graph.top()};
        match(bindings, patterns[position], fact, &clause.body, work);
    }
}

// Enter a clause of a case, receive, try, maybe or fun: its patterns narrow what they match.
void enter(BindingFacts &bindings, const ast::Expression &expression, const std::size_t index, std::size_t &work) {
    const auto &syntax = *bindings.function.module->syntax;
    const auto &value = expression.value;
    if (const auto *selection = std::get_if<ast::CaseExpression>(&value)) {
        enter_branch(bindings, {selection->value, recorded(bindings, selection->value)}, selection->clauses, index,
                     work);
    } else if (const auto *attempt = std::get_if<ast::TryExpression>(&value)) {
        enter_try(bindings, *attempt, index, work);
    } else if (const auto *conditional = std::get_if<ast::MaybeExpression>(&value);
               conditional && conditional->otherwise) {
        enter_branch(bindings, {std::nullopt, bindings.failures(expression)}, *conditional->otherwise, index, work);
    } else if (const auto *receive = std::get_if<ast::ReceiveExpression>(&value)) {
        const auto &clause = receive->clauses[index];
        match(bindings, pattern_root(syntax, clause.pattern), {bindings.inference.graph.top()}, &clause.body, work);
    } else if (fun_clauses(value)) {
        enter_fun(bindings, expression, index, work);
    }
}

// A clause's guard and body, by construct.
std::pair<const ast::GuardSyntax *, const std::vector<ast::ExprId> *> clause_parts(const ast::ExprValue &value,
                                                                                   const std::size_t index) {
    const auto clauses = branch_clauses(value);
    if (const auto *fun = fun_clauses(value)) {
        const auto &clause = fun->at(index);
        return {clause.guard ? &*clause.guard : nullptr, &clause.body};
    }
    return {clauses.at(index).guard, clauses.at(index).body};
}

// The function clauses of the walked function.
const std::vector<ast::FunctionClause> &heads(const BindingFacts &bindings) {
    const auto &syntax = *bindings.function.module->syntax;
    return std::get<ast::Function>(syntax.form(bindings.function.function->form).value).clauses;
}

// Each argument's fact in the current function clause: a plain variable's current fact, else its pattern's at entry.
std::vector<Id> argument_facts(const BindingFacts &bindings, const std::vector<ast::ExprId> &patterns) {
    std::vector<Id> result;
    result.reserve(patterns.size());
    for (std::size_t position = 0; position < patterns.size(); ++position) {
        const auto identity = variable(bindings, patterns[position]);
        const auto *found = identity ? bindings.values.find(*identity) : nullptr;
        result.push_back(found ? found->type : bindings.entry.at(position));
    }
    return result;
}

// Add a possible function clause's function types: one per clause of a dependent result whose parameters name its
// arguments, else one for the whole clause.
void add_types(BindingFacts &bindings, const ast::FunctionClause &clause) {
    const auto &syntax = *bindings.function.module->syntax;
    for (auto &type : clause_types(bindings, argument_roots(syntax, clause), body_fact(bindings, clause.body))) {
        bindings.types.push_back(std::move(type));
    }
}

// The names of a clause's plain variable arguments and the names bound to the same values: what guards testing them
// narrow is what the clause is entered with.
std::set<BindingId> argument_names(const BindingFacts &bindings, const std::vector<ast::ExprId> &patterns) {
    std::set<BindingId> result;
    for (const auto &pattern : patterns) {
        const auto identity = variable(bindings, pattern);
        if (!identity) {
            continue;
        }
        result.insert(*identity);
        if (const auto linked = bindings.aliases.find(*identity); linked != bindings.aliases.end()) {
            result.insert(linked->second.begin(), linked->second.end());
        }
    }
    return result;
}

// Whether every value of a clause's arguments' facts enters it: exact patterns and an exact guard.
bool exact_clause(BindingFacts &bindings, const std::vector<ast::ExprId> &patterns, const ast::GuardSyntax *guard) {
    return std::ranges::all_of(patterns, [&](const ast::ExprId &pattern) { return exact_shape(bindings, pattern); }) &&
           exact_guard(bindings, guard, argument_names(bindings, patterns));
}

// The arguments' facts after a fun clause's head and guard: a plain variable's narrowed fact, else its pattern's
// shape.
std::vector<Id> fun_arguments(BindingFacts &bindings, const ast::FunctionClause &clause) {
    const auto &syntax = *bindings.function.module->syntax;
    std::vector<Id> result;
    for (const auto &pattern : argument_roots(syntax, clause)) {
        const auto identity = variable(bindings, pattern);
        const auto *found = identity ? bindings.values.find(*identity) : nullptr;
        result.push_back(found ? found->type : pattern_shape(bindings, pattern));
    }
    return result;
}

// After a possible clause's guard: keep an anonymous fun clause's argument facts, or the parameters' facts of a case
// or if clause (docs/semantic.md#dependent-facts).
void record_clause(BindingFacts &bindings, const ast::Expression &expression, const std::size_t index) {
    if (bindings.impossible.contains(clause_parts(expression.value, index).second)) {
        return;
    }
    const auto &syntax = *bindings.function.module->syntax;
    const Fact unknown{bindings.inference.graph.bottom()};
    if (const auto *clauses = fun_clauses(expression.value)) {
        const auto &clause = clauses->at(index);
        const auto exact = exact_clause(bindings, argument_roots(syntax, clause), guard_of(clause.guard));
        bindings.fun_inputs.insert_or_assign({&expression, index},
                                             FunctionType{fun_arguments(bindings, clause), unknown, exact});
        return;
    }
    record_key(bindings, expression, index);
}

// Enter a function clause: its head patterns narrow the inputs, which the previous clause's single type test may
// already have narrowed.
void enter_head(BindingFacts &bindings, const std::size_t index, std::size_t &work) {
    const auto &syntax = *bindings.function.module->syntax;
    const auto &clauses = heads(bindings);
    const auto patterns = argument_roots(syntax, clauses[index]);
    Lattice lattice(bindings.inference.graph);
    bindings.head = index;
    bindings.entry.assign(patterns.size(), bindings.inference.graph.top());
    for (std::size_t position = 0; position < patterns.size(); ++position) {
        auto input = bindings.inputs.at(position);
        if (index > 0) {
            const auto previous = argument_roots(syntax, clauses[index - 1]);
            if (const auto removed = left_out(bindings, clauses[index - 1], previous, position)) {
                input = lattice.subtract(input, *removed);
            }
        }
        bindings.entry[position] = match(bindings, patterns[position], {input, position}, &clauses[index].body, work);
    }
}

// After a function clause's guard: add each argument's fact (a plain variable's narrowed by the guard) to the
// entry domain, unless the clause can never run.
void guarded_head(BindingFacts &bindings, const std::size_t index) {
    const auto &syntax = *bindings.function.module->syntax;
    const auto &clause = heads(bindings)[index];
    if (clause.guard && !assume_guard(bindings, *clause.guard)) {
        bindings.impossible.insert(&clause.body);
    }
    if (index > 0) {
        const auto &previous = heads(bindings)[index - 1];
        complement(bindings, previous, argument_roots(syntax, previous), argument_roots(syntax, clause));
    }
    if (bindings.impossible.contains(&clause.body)) {
        return;
    }
    Lattice lattice(bindings.inference.graph);
    const auto patterns = argument_roots(syntax, clause);
    bindings.arguments = argument_facts(bindings, patterns);
    bindings.exact = exact_clause(bindings, patterns, guard_of(clause.guard));
    for (std::size_t position = 0; position < bindings.arguments.size(); ++position) {
        bindings.domain.at(position) = lattice.join(bindings.domain.at(position), bindings.arguments[position]);
    }
}

// Whether a clause body can run and complete normally: it is not impossible and none of its expressions is none().
bool completes(const BindingFacts &bindings, const std::vector<ast::ExprId> *body) {
    return !bindings.impossible.contains(body) && std::ranges::none_of(*body, [&](const ast::ExprId &expression) {
        return recorded(bindings, expression) == bindings.inference.graph.bottom();
    });
}

// Add the current facts to the join of the innermost construct's completed clauses.
void complete(BindingFacts &bindings) {
    auto &merged = bindings.merged.back();
    if (!merged) {
        merged = bindings.values;
        return;
    }
    Lattice lattice(bindings.inference.graph);
    const auto &current = bindings.values.slots();
    auto &facts = merged->slots();
    for (std::size_t slot = 0; slot < facts.size(); ++slot) {
        auto &fact = facts[slot];
        const auto &other = current[slot];
        fact = {lattice.join(fact.type, other.type), fact.argument == other.argument ? fact.argument : std::nullopt,
                fact.dependent == other.dependent ? fact.dependent : std::nullopt};
    }
}

// Leave a clause of a case, if, receive, try or maybe: the facts at its end join the construct's when it completed,
// and a case or if keeps the facts of the variables its clauses bind.
void leave(BindingFacts &bindings, const ast::Expression &expression, const std::size_t index) {
    if (!fun_clauses(expression.value) && completes(bindings, clause_parts(expression.value, index).second)) {
        complete(bindings);
        record_exit(bindings, expression, index);
    }
}

// After a try body without of clauses, or a maybe body: its end facts join the construct's when it completed.
void finish(BindingFacts &bindings, const ast::Expression &expression) {
    if (const auto *attempt = std::get_if<ast::TryExpression>(&expression.value)) {
        if (body_value(bindings, attempt->body) != bindings.inference.graph.bottom()) {
            complete(bindings);
        }
        return;
    }
    const auto &conditional = std::get<ast::MaybeExpression>(expression.value);
    if (!bindings.stopped(expression) &&
        body_value(bindings, maybe_operands(conditional)) != bindings.inference.graph.bottom()) {
        complete(bindings);
    }
}

// Match a maybe's ?= pattern with its value: its variables and a variable value narrow to what matches; the values
// it fails on are kept for the else clauses and the maybe's value.
void bind(BindingFacts &bindings, const ast::Expression &expression, const std::size_t position, std::size_t &work) {
    const auto &syntax = *bindings.function.module->syntax;
    const auto &item = std::get<ast::MaybeMatch>(std::get<ast::MaybeExpression>(expression.value).body.at(position));
    const auto pattern = pattern_root(syntax, item.pattern);
    const auto found = bindings.inference.expressions.find(&syntax.expression(item.value));
    const auto value =
        found == bindings.inference.expressions.end() ? Fact{bindings.inference.graph.top()} : found->second;
    Lattice lattice(bindings.inference.graph);
    const auto shape = pattern_shape(bindings, pattern);
    const auto failed = exact_shape(bindings, pattern) ? lattice.subtract(value.type, shape) : value.type;
    const auto matched = lattice.meet(value.type, shape);
    bindings.publish(pattern, {matched, value.argument}, work);
    bindings.narrow(item.value, matched);
    bindings.conditionals.insert_or_assign({&expression, position}, BindingFacts::Conditional{matched, failed});
}

// Close a construct: the facts after it are the join of its completed clauses' facts, if any completed.
void close(BindingFacts &bindings) {
    if (auto &merged = bindings.merged.back()) {
        bindings.values = std::move(*merged);
    }
    bindings.merged.pop_back();
}

// Leave a function clause: the arguments' facts at its normal return join the success domain.
void leave_head(BindingFacts &bindings, const std::size_t index) {
    const auto &syntax = *bindings.function.module->syntax;
    const auto &clause = heads(bindings)[index];
    if (bindings.impossible.contains(&clause.body)) {
        return;
    }
    add_types(bindings, clause);
    if (!completes(bindings, &clause.body)) {
        return;
    }
    Lattice lattice(bindings.inference.graph);
    const auto facts = argument_facts(bindings, argument_roots(syntax, clause));
    for (std::size_t position = 0; position < facts.size(); ++position) {
        bindings.success.at(position) = lattice.join(bindings.success.at(position), facts[position]);
    }
}

// Restore the facts saved when the scope began, keeping them saved when `keep`.
void restore(BindingFacts &bindings, const bool keep) {
    if (keep) {
        bindings.values = bindings.saved.back();
        return;
    }
    bindings.values = std::move(bindings.saved.back());
    bindings.saved.pop_back();
}

// The clauses of a case, a try's of part or a maybe's else part, which match one value, when clause `index` is
// one of them.
const std::vector<ast::BranchClause> *matching(const ast::ExprValue &value, const std::size_t index) {
    const std::vector<ast::BranchClause> *clauses = nullptr;
    if (const auto *selection = std::get_if<ast::CaseExpression>(&value)) {
        clauses = &selection->clauses;
    } else if (const auto *attempt = std::get_if<ast::TryExpression>(&value); attempt && attempt->of) {
        clauses = &*attempt->of;
    } else if (const auto *conditional = std::get_if<ast::MaybeExpression>(&value);
               conditional && conditional->otherwise) {
        clauses = &*conditional->otherwise;
    }
    return clauses && index < clauses->size() ? clauses : nullptr;
}

// Narrow by a clause's guard, and in clauses matching one value by the previous clause's failed comparison.
void guard_clause(BindingFacts &bindings, const ast::ExprValue &value, const std::size_t index) {
    const auto [guard, body] = clause_parts(value, index);
    if (guard && !assume_guard(bindings, *guard)) {
        bindings.impossible.insert(body);
    }
    const auto *clauses = matching(value, index);
    if (clauses && index > 0) {
        const auto &syntax = *bindings.function.module->syntax;
        const auto &previous = clauses->at(index - 1);
        complement(bindings, previous, {pattern_root(syntax, previous.pattern)},
                   {pattern_root(syntax, clauses->at(index).pattern)});
    }
}
} // namespace

void expand(const Module &module, const ast::ExprId &id, std::vector<Frame> &pending) {
    const auto &value = module.syntax->expression(id).value;
    pending.push_back({id, Step::ready});
    Order order(module);
    if (!selection(order, id, value) && !waiting(order, id, value) && !guarded(order, id, value) &&
        !comprehension(order, id, value)) {
        order.visit(expression_children(module, module.syntax->expression(id)));
    }
    order.push(pending);
}

void expand_clauses(const Module &module, const ast::ExprId &fun, std::vector<Frame> &pending) {
    Order order(module);
    order.clauses(fun, *fun_clauses(module.syntax->expression(fun).value));
    order.push(pending);
}

void expand_heads(const ast::Function &definition, std::vector<Frame> &pending) {
    std::vector<Frame> frames;
    for (std::size_t index = 0; index < definition.clauses.size(); ++index) {
        const auto &clause = definition.clauses[index];
        frames.push_back({clause.body.front(), Step::enter_head, index});
        push_guard(guard_of(clause.guard), frames);
        frames.push_back({clause.body.front(), Step::guarded_head, index});
        for (const auto &expression : clause.body) {
            frames.push_back({expression});
        }
        frames.push_back({clause.body.front(), Step::leave_head, index});
    }
    pending.insert(pending.end(), frames.rbegin(), frames.rend());
}

void scope_step(BindingFacts &bindings, const Frame &frame, std::size_t &work) {
    using Handler = void (*)(BindingFacts &, const Frame &, std::size_t &);
    static const std::map<Step, Handler> HANDLERS{
        {Step::save, [](BindingFacts &b, const Frame &, std::size_t &) { b.saved.push_back(b.values); }},
        {Step::restore, [](BindingFacts &b, const Frame &, std::size_t &) { restore(b, false); }},
        {Step::reset, [](BindingFacts &b, const Frame &, std::size_t &) { restore(b, true); }},
        {Step::leave,
         [](BindingFacts &b, const Frame &f, std::size_t &) {
             leave(b, b.function.module->syntax->expression(f.expression), f.clause);
             restore(b, false);
         }},
        {Step::leave_head,
         [](BindingFacts &b, const Frame &f, std::size_t &) {
             leave_head(b, f.clause);
             restore(b, false);
         }},
        {Step::open, [](BindingFacts &b, const Frame &, std::size_t &) { b.merged.emplace_back(); }},
        {Step::close,
         [](BindingFacts &b, const Frame &f, std::size_t &) {
             close(b);
             export_dependents(b, b.function.module->syntax->expression(f.expression));
         }},
        {Step::complete, [](BindingFacts &b, const Frame &, std::size_t &) { complete(b); }},
        {Step::finish, [](BindingFacts &b, const Frame &f,
                          std::size_t &) { finish(b, b.function.module->syntax->expression(f.expression)); }},
        {Step::bind,
         [](BindingFacts &b, const Frame &f, std::size_t &work) {
             bind(b, b.function.module->syntax->expression(f.expression), f.clause, work);
         }},
        {Step::assume,
         [](BindingFacts &b, const Frame &f, std::size_t &) {
             b.saved.push_back(b.values);
             (void)assume(b, f.expression);
         }},
        {Step::enter,
         [](BindingFacts &b, const Frame &f, std::size_t &work) {
             b.saved.push_back(b.values);
             const auto &expression = b.function.module->syntax->expression(f.expression);
             b.exits.erase({&expression, f.clause});
             enter(b, expression, f.clause, work);
         }},
        {Step::guarded,
         [](BindingFacts &b, const Frame &f, std::size_t &) {
             const auto &expression = b.function.module->syntax->expression(f.expression);
             guard_clause(b, expression.value, f.clause);
             record_clause(b, expression, f.clause);
         }},
        {Step::enter_head,
         [](BindingFacts &b, const Frame &f, std::size_t &work) {
             b.saved.push_back(b.values);
             enter_head(b, f.clause, work);
         }},
        {Step::guarded_head, [](BindingFacts &b, const Frame &f, std::size_t &) { guarded_head(b, f.clause); }}};
    HANDLERS.at(frame.step)(bindings, frame, work);
}
} // namespace clause::semantic::types
