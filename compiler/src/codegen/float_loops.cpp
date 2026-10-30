#include "float_loops.hpp"
#include "../semantic/capabilities.hpp"
#include "binary_loops.hpp"
#include "proofs.hpp"
#include <algorithm>
#include <clause/abi/frames.hpp>
#include <cstdint>
#include <iterator>

namespace clause::codegen {
namespace {
using ast::BinaryOperator;
using Op = abi::v1::ImmediateOperation;
using Expressions = std::set<const ast::Expression *>;

// Whether an operator computes a number from two numbers.
bool arithmetic(const BinaryOperator operation) {
    return operation == BinaryOperator::add || operation == BinaryOperator::subtract ||
           operation == BinaryOperator::multiply || operation == BinaryOperator::divide;
}

// Whether an operator compares its operands.
bool comparison(const BinaryOperator operation) {
    return operation >= BinaryOperator::equal && operation <= BinaryOperator::exact_not_equal;
}

// The expression an id names, its grouping parentheses removed.
const ast::Expression &bare(const ast::Module &syntax, const ast::ExprId &id) {
    return syntax.expression(semantic::ungroup(syntax, id));
}

// A call of a math function, which returns a new float and keeps none of its arguments; null for any other value.
const ast::CallExpression *math_call(const ast::Module &syntax, const ast::ExprValue &value) {
    const auto *call = std::get_if<ast::CallExpression>(&value);
    const auto *remote = call ? std::get_if<ast::RemoteExpression>(&bare(syntax, call->target).value) : nullptr;
    const auto *module = remote ? std::get_if<ast::Atom>(&bare(syntax, remote->module).value) : nullptr;
    return module && module->name == U"math" ? call : nullptr;
}

// The immediate operation a call resolves to; none for other expressions.
std::optional<Op> service_operation(const semantic::Function &function, const ast::Expression &expression) {
    const auto found = function.services.find(&expression);
    return found == function.services.end() ? std::nullopt : found->second.operation;
}

// What decides that an expression is float-only (wider float loops, OPT1.11): the lowering (its inferred facts), the
// function's services and reads, the bindings of the float parameters found so far, and the expressions each variable
// is matched to.
struct FloatFacts {
    const ExpressionLowering &state;
    const ast::Module &syntax;
    const semantic::Function &function;
    const BindingReads &reads;
    std::set<semantic::BindingId> floats;
    std::map<semantic::BindingId, std::vector<ast::ExprId>> matches;
};

// Variables matched to variables matched to ... are followed this deep.
constexpr std::size_t MATCH_DEPTH = 8;

bool float_only(const FloatFacts &facts, const ast::ExprId &id, std::size_t depth = 0);

// Whether inference proves every value of an expression a float: a call of a function returning floats, arithmetic
// of parameters callers pass floats (OPT1.11 remainder).
bool inferred_float(const ExpressionLowering &state, const ast::Expression &expression) {
    const auto known = known_expression(state, expression);
    return known && state.proofs->floating(known->fact);
}

// Whether an expression makes a float of any value: a float literal, a math function, float/1, or one inference proves
// a float.
bool float_source(const FloatFacts &facts, const ast::Expression &expression) {
    return std::holds_alternative<ast::FloatLiteral>(expression.value) || math_call(facts.syntax, expression.value) ||
           service_operation(facts.function, expression) == Op::to_float || inferred_float(facts.state, expression);
}

// Whether every value of a variable read is a float: a float parameter's, or one matched only to float-only
// expressions (wider float loops, OPT1.11).
bool float_variable(const FloatFacts &facts, const ast::Expression &read, const std::size_t depth) {
    const auto binding = facts.reads.find(&read);
    if (binding == facts.reads.end()) {
        return false;
    }
    if (facts.floats.contains(binding->second)) {
        return true;
    }
    const auto matched = facts.matches.find(binding->second);
    return matched != facts.matches.end() && depth < MATCH_DEPTH &&
           std::ranges::all_of(
               matched->second, [&](const ast::ExprId &value) { return float_only(facts, value, depth + 1); });
}

// Whether every value of an expression is a float: a float literal, a division, a math function, float/1, a float
// variable, a negated float, or a sum, difference or product with such an operand.
bool float_only(const FloatFacts &facts, const ast::ExprId &id, const std::size_t depth) {
    const auto &expression = bare(facts.syntax, id);
    const auto &value = expression.value;
    if (float_source(facts, expression)) {
        return true;
    }
    if (std::holds_alternative<ast::Variable>(value)) {
        return float_variable(facts, expression, depth);
    }
    if (const auto *unary = std::get_if<ast::UnaryExpression>(&value)) {
        return unary->operation == ast::UnaryOperator::negative && float_only(facts, unary->operand, depth);
    }
    const auto *binary = std::get_if<ast::BinaryExpression>(&value);
    if (!binary || !arithmetic(binary->operation)) {
        return false;
    }
    return binary->operation == BinaryOperator::divide || float_only(facts, binary->left, depth) ||
           float_only(facts, binary->right, depth);
}

// Whether a call's operand stays a double when it is a raw float: float/1, is_float/1, is_number/1 (OPT1.11).
bool float_operand_call(const semantic::Function &function, const ast::Expression &expression) {
    const auto operation = service_operation(function, expression);
    return operation == Op::to_float || operation == Op::is_float || operation == Op::is_number;
}

// Record the operands of an expression that is arithmetic, a comparison, a negation, a math function or a call that
// reads a raw float's double.
void record_operands(const semantic::Function &function, const ast::Module &syntax, const ast::Expression &expression,
                     Expressions &reads) {
    const auto *binary = std::get_if<ast::BinaryExpression>(&expression.value);
    if (binary && (arithmetic(binary->operation) || comparison(binary->operation))) {
        reads.insert(&bare(syntax, binary->left));
        reads.insert(&bare(syntax, binary->right));
    }
    if (const auto *unary = std::get_if<ast::UnaryExpression>(&expression.value);
        unary && unary->operation == ast::UnaryOperator::negative) {
        reads.insert(&bare(syntax, unary->operand));
    }
    const auto *call = std::get_if<ast::CallExpression>(&expression.value);
    if (call && (math_call(syntax, expression.value) || float_operand_call(function, expression))) {
        for (const auto &argument : call->arguments) {
            reads.insert(&bare(syntax, argument));
        }
    }
}

// Every expression of the function but those inside funs (which would capture a cell).
std::vector<const ast::Expression *> body_expressions(const ast::Module &syntax, const ast::Function &function) {
    std::vector<const ast::Expression *> expressions;
    auto pending = semantic::function_roots(function);
    while (!pending.empty()) {
        const auto &expression = syntax.expression(pending.back());
        pending.pop_back();
        if (!semantic::fun_clauses(expression.value)) {
            expressions.push_back(&expression);
            std::ranges::copy(semantic::expression_children(expression), std::back_inserter(pending));
        }
    }
    return expressions;
}

// The operands of arithmetic, comparisons, negations and math functions anywhere in the function but inside funs:
// reading a parameter there takes its double, no cell.
Expressions operand_reads(const semantic::Function &function, const ast::Module &syntax,
                          std::span<const ast::Expression *const> expressions) {
    Expressions reads;
    for (const auto *expression : expressions) {
        record_operands(function, syntax, *expression, reads);
    }
    return reads;
}

// The expressions each variable is matched to (`V = Expression`), for the variables every definition of which is
// such a match (wider float loops, OPT1.11).
std::map<semantic::BindingId, std::vector<ast::ExprId>>
variable_matches(const semantic::Function &function, const ast::Module &syntax,
                 std::span<const ast::Expression *const> expressions) {
    std::map<const ast::Expression *, semantic::BindingId> defined;
    std::map<semantic::BindingId, std::size_t> definitions;
    for (const auto &binding : function.bindings) {
        if (binding.use == semantic::BindingUse::definition) {
            defined.emplace(&syntax.expression(binding.expression), binding.identity);
            ++definitions[binding.identity];
        }
    }
    std::map<semantic::BindingId, std::vector<ast::ExprId>> matches;
    for (const auto *expression : expressions) {
        const auto *match = std::get_if<ast::MatchExpression>(&expression->value);
        const auto found = match ? defined.find(&bare(syntax, match->left)) : defined.end();
        if (found != defined.end()) {
            matches[found->second].push_back(match->right);
        }
    }
    std::erase_if(matches, [&](const auto &entry) { return entry.second.size() != definitions.at(entry.first); });
    return matches;
}

// Whether a tail call calls the function itself.
bool self_call(const ExpressionLowering &state, const ast::Expression &expression) {
    const auto callee = state.inferred.callees.find(&expression);
    return callee != state.inferred.callees.end() && callee->second.function == &state.function &&
           callee->second.module == &state.module;
}

// The self calls among the tail calls.
std::vector<const ast::CallExpression *> self_calls(const ExpressionLowering &state, const TailPositions &tails) {
    std::vector<const ast::CallExpression *> calls;
    for (const auto *expression : tails.calls) {
        if (self_call(state, *expression)) {
            calls.push_back(&std::get<ast::CallExpression>(expression->value));
        }
    }
    return calls;
}

// Every expression inside the tail expressions but the self calls: the function's result, or a tail call of another
// function, ends the loop, so a cell made there is made once.
Expressions final_reads(const ExpressionLowering &state, const TailPositions &tails) {
    const auto &syntax = *state.module.syntax;
    Expressions reads;
    std::vector<ast::ExprId> pending;
    for (const auto *leaf : tails.leaves) {
        if (tails.calls.contains(leaf) && self_call(state, *leaf)) {
            continue;
        }
        reads.insert(leaf);
        std::ranges::copy(semantic::expression_children(*leaf), std::back_inserter(pending));
    }
    while (!pending.empty()) {
        const auto &expression = bare(syntax, pending.back());
        pending.pop_back();
        reads.insert(&expression);
        std::ranges::copy(semantic::expression_children(expression), std::back_inserter(pending));
    }
    return reads;
}

// The arguments the self tail calls pass as `parameter`: a parameter passed back as itself stays a double.
Expressions own_arguments(const ast::Module &syntax, std::span<const ast::CallExpression *const> calls,
                          const std::size_t parameter) {
    Expressions arguments;
    for (const auto *call : calls) {
        if (call->arguments.size() > parameter) {
            arguments.insert(&bare(syntax, call->arguments[parameter]));
        }
    }
    return arguments;
}

// Whether some self tail call passes a float-only expression as `parameter`.
bool passed_floats(const FloatFacts &facts, std::span<const ast::CallExpression *const> calls,
                   const std::size_t parameter) {
    return std::ranges::any_of(calls, [&](const ast::CallExpression *call) {
        return call->arguments.size() > parameter && float_only(facts, call->arguments[parameter]);
    });
}

// What a head node does with `parameter`: nothing (another input, or the end of the match), bind it to a variable,
// or anything else.
enum class HeadUse : std::uint8_t { none, bind, other };

HeadUse head_use(const semantic::MatchNode &node, const std::size_t parameter) {
    using semantic::MatchOperation;
    if (node.input != parameter || node.operation == MatchOperation::success ||
        node.operation == MatchOperation::mismatch) {
        return HeadUse::none;
    }
    return node.operation == MatchOperation::bind && node.binding ? HeadUse::bind : HeadUse::other;
}

// The bindings of `parameter` in every head; none when a head does more than bind it to a variable.
std::optional<std::set<semantic::BindingId>> head_bindings(std::span<const semantic::MatchPlan> plans,
                                                           const std::size_t parameter) {
    std::set<semantic::BindingId> bindings;
    for (const auto &plan : plans) {
        for (const auto &node : plan.nodes) {
            const auto use = head_use(node, parameter);
            if (use == HeadUse::other) {
                return std::nullopt;
            }
            if (use == HeadUse::bind) {
                bindings.insert(*node.binding);
            }
        }
    }
    return bindings;
}

// Whether every other use of the bindings is a read at one of the `allowed` expressions.
bool contained(const ExpressionLowering &state, const std::set<semantic::BindingId> &bindings,
               const Expressions &allowed) {
    return std::ranges::all_of(state.function.bindings, [&](const semantic::Binding &binding) {
        if (!bindings.contains(binding.identity) || binding.use == semantic::BindingUse::definition) {
            return true;
        }
        return binding.use == semantic::BindingUse::read &&
               allowed.contains(&state.module.syntax->expression(binding.expression));
    });
}

struct Evidence {
    // The head plans, the self tail calls, and the reads any float parameter may have.
    std::vector<semantic::MatchPlan> plans;
    std::vector<const ast::CallExpression *> calls;
    Expressions reads;
};

// The head bindings of `parameter` when it may be a float parameter: every head only binds it, and its every read
// takes its double or ends the loop; none otherwise.
std::optional<std::set<semantic::BindingId>>
candidate_parameter(const ExpressionLowering &state, const Evidence &evidence, const std::size_t parameter) {
    const auto &syntax = *state.module.syntax;
    auto bindings = head_bindings(evidence.plans, parameter);
    if (!bindings) {
        return std::nullopt;
    }
    auto allowed = own_arguments(syntax, evidence.calls, parameter);
    allowed.insert(evidence.reads.begin(), evidence.reads.end());
    return contained(state, *bindings, allowed) ? bindings : std::nullopt;
}

// Move the candidates some self tail call passes a float-only expression into the loop, until none is left whose
// argument the float parameters found so far make float-only (wider float loops, OPT1.11).
void grow_parameters(FloatFacts &facts, const Evidence &evidence,
                     std::map<std::size_t, std::set<semantic::BindingId>> &candidates, FloatLoop &loop) {
    for (bool grew = true; grew;) {
        grew = false;
        for (auto at = candidates.begin(); at != candidates.end();) {
            if (!passed_floats(facts, evidence.calls, at->first)) {
                ++at;
                continue;
            }
            loop.parameters.push_back(at->first);
            for (const auto binding : at->second) {
                loop.bindings.emplace(binding, at->first);
                facts.floats.insert(binding);
            }
            at = candidates.erase(at);
            grew = true;
        }
    }
    std::ranges::sort(loop.parameters);
}
} // namespace

std::optional<FloatLoop> float_loop_parameters(const ExpressionLowering &state, const std::size_t clauses,
                                               const TailPositions &tails) {
    // Float arithmetic is inline on 64-bit targets only.
    auto calls = self_calls(state, tails);
    if (state.lambda || state.word->getBitWidth() != 64 || calls.empty() || !state.reads) {
        return std::nullopt;
    }
    const auto &syntax = *state.module.syntax;
    const auto expressions = body_expressions(syntax, std::get<ast::Function>(syntax.form(state.function.form).value));
    Evidence evidence{head_plans(state, clauses), std::move(calls), operand_reads(state.function, syntax, expressions)};
    if (evidence.plans.empty()) {
        return std::nullopt;
    }
    evidence.reads.merge(final_reads(state, tails));
    // A variant takes a double's bits only in a parameter passed in a register: arguments past them go through the
    // register array into term slots, where collections would read them.
    const auto passed = std::min<std::size_t>(state.function.key.arity, abi::v1::register_arguments);
    std::map<std::size_t, std::set<semantic::BindingId>> candidates;
    for (std::size_t parameter = 0; parameter < passed; ++parameter) {
        if (auto bindings = candidate_parameter(state, evidence, parameter)) {
            candidates.emplace(parameter, std::move(*bindings));
        }
    }
    FloatFacts facts{state,        syntax, state.function,
                     *state.reads, {},     variable_matches(state.function, syntax, expressions)};
    FloatLoop loop{};
    grow_parameters(facts, evidence, candidates, loop);
    if (loop.parameters.empty()) {
        return std::nullopt;
    }
    return loop;
}
} // namespace clause::codegen
