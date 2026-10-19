#include "float_loops.hpp"
#include "../semantic/capabilities.hpp"
#include "binary_loops.hpp"
#include <algorithm>
#include <clause/abi/frames.hpp>
#include <cstdint>
#include <iterator>

namespace clause::codegen {
namespace {
using ast::BinaryOperator;
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

// Whether every value of an expression is a float: a float literal, a division, a math function, or a sum,
// difference or product with such an operand.
bool float_only(const ast::Module &syntax, const ast::ExprId &id) {
    const auto &value = bare(syntax, id).value;
    if (std::holds_alternative<ast::FloatLiteral>(value) || math_call(syntax, value)) {
        return true;
    }
    const auto *binary = std::get_if<ast::BinaryExpression>(&value);
    if (!binary || !arithmetic(binary->operation)) {
        return false;
    }
    return binary->operation == BinaryOperator::divide || float_only(syntax, binary->left) ||
           float_only(syntax, binary->right);
}

// Record the operands of an expression that is arithmetic, a comparison or a math function.
void record_operands(const ast::Module &syntax, const ast::Expression &expression, Expressions &reads) {
    const auto *binary = std::get_if<ast::BinaryExpression>(&expression.value);
    if (binary && (arithmetic(binary->operation) || comparison(binary->operation))) {
        reads.insert(&bare(syntax, binary->left));
        reads.insert(&bare(syntax, binary->right));
    }
    if (const auto *call = math_call(syntax, expression.value)) {
        for (const auto &argument : call->arguments) {
            reads.insert(&bare(syntax, argument));
        }
    }
}

// The operands of arithmetic, comparisons and math functions anywhere in the function but inside funs (which would
// capture a cell): reading a parameter there takes its double, no cell.
Expressions operand_reads(const ast::Module &syntax, const ast::Function &function) {
    Expressions reads;
    auto pending = semantic::function_roots(function);
    while (!pending.empty()) {
        const auto &expression = syntax.expression(pending.back());
        pending.pop_back();
        if (!semantic::fun_clauses(expression.value)) {
            record_operands(syntax, expression, reads);
            std::ranges::copy(semantic::expression_children(expression), std::back_inserter(pending));
        }
    }
    return reads;
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
bool passed_floats(const ast::Module &syntax, std::span<const ast::CallExpression *const> calls,
                   const std::size_t parameter) {
    return std::ranges::any_of(calls, [&](const ast::CallExpression *call) {
        return call->arguments.size() > parameter && float_only(syntax, call->arguments[parameter]);
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

// The head bindings of `parameter` when it is a float parameter; none otherwise.
std::optional<std::set<semantic::BindingId>> float_parameter(const ExpressionLowering &state, const Evidence &evidence,
                                                             const std::size_t parameter) {
    const auto &syntax = *state.module.syntax;
    auto bindings = head_bindings(evidence.plans, parameter);
    if (!bindings || !passed_floats(syntax, evidence.calls, parameter)) {
        return std::nullopt;
    }
    auto allowed = own_arguments(syntax, evidence.calls, parameter);
    allowed.insert(evidence.reads.begin(), evidence.reads.end());
    return contained(state, *bindings, allowed) ? bindings : std::nullopt;
}
} // namespace

std::optional<FloatLoop> float_loop_parameters(const ExpressionLowering &state, const std::size_t clauses,
                                               const TailPositions &tails) {
    // Float arithmetic is inline on 64-bit targets only.
    auto calls = self_calls(state, tails);
    if (state.lambda || state.word->getBitWidth() != 64 || calls.empty()) {
        return std::nullopt;
    }
    const auto &syntax = *state.module.syntax;
    Evidence evidence{head_plans(state, clauses), std::move(calls),
                      operand_reads(syntax, std::get<ast::Function>(syntax.form(state.function.form).value))};
    if (evidence.plans.empty()) {
        return std::nullopt;
    }
    evidence.reads.merge(final_reads(state, tails));
    FloatLoop loop{};
    // A variant takes a double's bits only in a parameter passed in a register: arguments past them go through the
    // register array into term slots, where collections would read them.
    const auto passed = std::min<std::size_t>(state.function.key.arity, abi::v1::register_arguments);
    for (std::size_t parameter = 0; parameter < passed; ++parameter) {
        if (const auto bindings = float_parameter(state, evidence, parameter)) {
            loop.parameters.push_back(parameter);
            for (const auto binding : *bindings) {
                loop.bindings.emplace(binding, parameter);
            }
        }
    }
    if (loop.parameters.empty()) {
        return std::nullopt;
    }
    return loop;
}
} // namespace clause::codegen
