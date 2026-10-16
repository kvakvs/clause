#include "../semantic/bindings.hpp"
#include "../semantic/capabilities.hpp"
#include "../semantic/funs.hpp"
#include "../semantic/records.hpp"
#include "../semantic/services.hpp"
#include "lowering_state.hpp"
#include "source_locations.hpp"
#include <algorithm>
#include <clause/abi/builtins.hpp>
#include <clause/abi/equality.hpp>
#include <clause/abi/term.hpp>
#include <stdexcept>

namespace clause::codegen {
namespace {
// Checked canonical decimal parsing rejects arbitrary-size values before LLVM sees them.
llvm::Value *literal(ExpressionLowering &state, const ast::ExprId &expression) {
    const auto &syntax = *state.module.syntax;
    auto *word = state.word;
    const auto value = semantic::integer_literal(syntax, expression, word->getBitWidth());
    if (!value) {
        return lower_integer(state, std::get<ast::IntegerLiteral>(syntax.expression(expression).value).value.decimal);
    }
    const auto encoded = word->getBitWidth() == 32 ? *abi::v1::IntegerEncoding<32>::encode(*value)
                                                   : *abi::v1::IntegerEncoding<64>::encode(*value);
    return llvm::ConstantInt::get(word, encoded);
}

// Resolve one validated read without relying on original-argument projection facts.
llvm::Value *binding(ExpressionLowering &state, const ast::ExprId &expression) {
    if (!state.reads) {
        throw std::invalid_argument("lowering: binding read index is unavailable");
    }
    const auto identity = state.reads->at(&state.module.syntax->expression(expression));
    return state.bindings.at(identity);
}

// Resolve leaves through existing parameter bindings, preserving every input term unchanged.
llvm::Value *leaf(ExpressionLowering &state, const ast::ExprId &expression) {
    const auto &value = state.module.syntax->expression(expression).value;
    if (std::holds_alternative<ast::Variable>(value)) {
        return binding(state, expression);
    }
    if (const auto *real = std::get_if<ast::FloatLiteral>(&value)) {
        return lower_float(state, real->value);
    }
    if (const auto *atom = std::get_if<ast::Atom>(&value)) {
        return lower_atom(state, *atom);
    }
    if (const auto *binary = std::get_if<ast::Bitstring>(&value)) {
        return lower_bits(state, *binary);
    }
    if (const auto *map = std::get_if<ast::MapExpression>(&value)) {
        return lower_map(state, *map,
                         state.function.owned_map_updates.contains(&state.module.syntax->expression(expression)));
    }
    if (semantic::fun_value(value)) {
        return lower_fun(state, state.module.syntax->expression(expression));
    }
    if (auto *record = lower_record(state, expression)) {
        return record;
    }
    if (auto *container = lower_container(state, value)) {
        return container;
    }
    return literal(state, expression);
}

// Missing capability authorization is a phase-contract failure, never an unchecked optional access.
abi::v1::ImmediateOperation operation(const std::optional<abi::v1::ImmediateOperation> &value) {
    if (!value) {
        throw std::invalid_argument("lowering: unavailable immediate service");
    }
    return *value;
}

// Lower body-only erlang builtins (display/1, halt/0,1, the raise family); null for every other service.
llvm::Value *body_builtin_value(ExpressionLowering &state, const semantic::ServiceResolution &service,
                                const ast::CallExpression &call) {
    const auto argument = [&](const std::size_t index) {
        return state.values.at(&state.module.syntax->expression(call.arguments.at(index)));
    };
    if (service.operation == abi::v1::ImmediateOperation::display) {
        return lower_display(state, argument(0));
    }
    if (service.operation == abi::v1::ImmediateOperation::halt) {
        return lower_halt(state, call.arguments.empty() ? nullptr : argument(0));
    }
    if (service.operation != abi::v1::ImmediateOperation::raise) {
        return nullptr;
    }
    if (service.identity.name == U"raise") {
        return lower_raise_stack(state, std::array{argument(0), argument(1), argument(2)});
    }
    if (service.identity.name == U"error" && call.arguments.size() > 1) {
        // error/3 options only add error_info to the top frame's location, which is not recorded.
        return lower_error(state, argument(0), argument(1));
    }
    return lower_raise(state, service.identity.name, argument(0));
}

// A call that is no service: a call of a value, of a runtime module or function, or of a named function.
llvm::Value *generated_call(ExpressionLowering &state, const ast::Expression &expression,
                            const ast::CallExpression &call) {
    if (semantic::fun_call(*state.module.syntax, call)) {
        return lower_fun_call(state, expression, call);
    }
    return semantic::dynamic_call(*state.module.syntax, call) ? lower_dynamic_call(state, expression, call)
                                                              : lower_call(state, expression, call);
}

// The evaluated values of a call's arguments, in order.
std::vector<llvm::Value *> call_arguments(ExpressionLowering &state, const ast::CallExpression &call) {
    std::vector<llvm::Value *> arguments;
    arguments.reserve(call.arguments.size());
    for (const auto &id : call.arguments) {
        arguments.push_back(state.values.at(&state.module.syntax->expression(id)));
    }
    return arguments;
}

// The bridge indexes of setelement/3 (OPT0.2C) and make_tuple/2, which have inline forms.
constexpr auto SETELEMENT = abi::v1::find_bridge_builtin("erlang", "setelement", 3).value_or(0);
static_assert(abi::v1::find_bridge_builtin("erlang", "setelement", 3));
constexpr auto MAKE_TUPLE = abi::v1::find_bridge_builtin("erlang", "make_tuple", 2).value_or(0);
static_assert(abi::v1::find_bridge_builtin("erlang", "make_tuple", 2));

constexpr auto ATOM_TO_LIST = abi::v1::find_bridge_builtin("erlang", "atom_to_list", 1).value_or(0);
static_assert(abi::v1::find_bridge_builtin("erlang", "atom_to_list", 1));

// A pure builtin of literal arguments evaluated at compile time, as OTP's compiler does (sys_core_fold): the name of
// atom_to_list(Atom) as a literal string; null when the call is not one (folded builtins).
llvm::Value *folded_builtin(ExpressionLowering &state, std::size_t builtin, const ast::CallExpression &call) {
    if (builtin != ATOM_TO_LIST || call.arguments.size() != 1) {
        return nullptr;
    }
    const auto &syntax = *state.module.syntax;
    const auto *atom = std::get_if<ast::Atom>(&syntax.expression(semantic::ungroup(syntax, call.arguments[0])).value);
    return atom ? lower_container(state, ast::StringLiteral{atom->name}) : nullptr;
}

// A bridge builtin call: folded at compile time, inline (math, make_tuple/2, setelement/3) or entered.
llvm::Value *bridge_value(ExpressionLowering &state, std::size_t index, const ast::CallExpression &call) {
    if (auto *folded = folded_builtin(state, index, call)) {
        return folded;
    }
    const auto values = call_arguments(state, call);
    const auto builtin = [&] { return lower_builtin(state, index, values); };
    if (auto *math = inline_math(state, index, values, builtin)) {
        return math;
    }
    if (index == MAKE_TUPLE) {
        return lower_make_tuple(state, index, values);
    }
    return index == SETELEMENT ? lower_setelement(state, index, values) : builtin();
}

// Lower services with lowerings of their own: apply/2,3, bridge builtins and compound guard tests; null for others.
llvm::Value *special_service(ExpressionLowering &state, const ast::Expression &expression,
                             const semantic::ServiceResolution &service, const ast::CallExpression &call) {
    if (service.apply()) {
        return lower_apply(state, expression, call);
    }
    if (service.builtin) {
        return bridge_value(state, *service.builtin, call);
    }
    if (service.operation == abi::v1::ImmediateOperation::is_integer_range) {
        return lower_integer_range(state, call);
    }
    if (service.operation == abi::v1::ImmediateOperation::is_record) {
        return lower_record_test(state, expression, call);
    }
    return body_builtin_value(state, service, call);
}

// Keep resolved runtime services and generated calls on their existing checked boundaries.
llvm::Value *call_value(ExpressionLowering &state, const ast::Expression &expression, const ast::CallExpression &call) {
    if (semantic::record_info_call(*state.module.syntax, expression.value)) {
        return lower_record_info(state, expression);
    }
    const auto service = state.function.services.find(&expression);
    if (service == state.function.services.end()) {
        return generated_call(state, expression, call);
    }
    if (auto *value = special_service(state, expression, service->second, call)) {
        return value;
    }
    if (service->second.operation == abi::v1::ImmediateOperation::binary_part) {
        return lower_binary_part(state, call_arguments(state, call));
    }
    // self/0 and node/0 pass the empty list as their unused operand.
    auto *left = call.arguments.empty() ? llvm::ConstantInt::get(state.word, abi::v1::empty_list)
                                        : state.values.at(&state.module.syntax->expression(call.arguments[0]));
    auto *right =
        call.arguments.size() == 2 ? state.values.at(&state.module.syntax->expression(call.arguments[1])) : nullptr;
    return lower_operation(state, operation(service->second.operation), left, right);
}

// Bridge indexes of the builtins of the list and send operators.
constexpr auto APPEND = abi::v1::find_bridge_builtin("erlang", "++", 2).value_or(0);
constexpr auto SUBTRACT = abi::v1::find_bridge_builtin("erlang", "--", 2).value_or(0);
constexpr auto SEND = abi::v1::find_bridge_builtin("erlang", "!", 2).value_or(0);
static_assert(abi::v1::find_bridge_builtin("erlang", "++", 2) && abi::v1::find_bridge_builtin("erlang", "--", 2) &&
              abi::v1::find_bridge_builtin("erlang", "!", 2));

// The bridge builtin of an operator that has one: ++, -- and !.
std::optional<std::size_t> operator_builtin(ast::BinaryOperator operation) {
    switch (operation) {
    case ast::BinaryOperator::append:
        return APPEND;
    case ast::BinaryOperator::subtract_list:
        return SUBTRACT;
    case ast::BinaryOperator::send:
        return SEND;
    default:
        return std::nullopt;
    }
}

// `Left ++ Right` where Left is written as a proper list (a string literal, or a list without a tail) built as
// [E1, ..., En | Right], as OTP's compiler does, without the builtin's copy (literal prefixes); null otherwise.
llvm::Value *literal_prefix(ExpressionLowering &state, const ast::BinaryExpression &binary, llvm::Value *right) {
    const auto &syntax = *state.module.syntax;
    const auto &left = syntax.expression(semantic::ungroup(syntax, binary.left)).value;
    std::vector<llvm::Value *> values;
    if (const auto *string = std::get_if<ast::StringLiteral>(&left)) {
        for (const auto character : string->value) {
            values.push_back(llvm::ConstantInt::get(
                state.word, *abi::v1::IntegerEncoding<32>::encode(static_cast<std::int64_t>(character))));
        }
    } else if (const auto *list = std::get_if<ast::List>(&left); list && !list->tail) {
        for (const auto &element : list->elements) {
            values.push_back(state.values.at(&syntax.expression(element)));
        }
    } else {
        return nullptr;
    }
    if (values.empty()) {
        return right;
    }
    values.push_back(right);
    return lower_list(state, values);
}

// List and send operators call their bridge builtins; every other binary operator uses the shared checked services.
llvm::Value *binary_value(ExpressionLowering &state, const ast::BinaryExpression &binary) {
    const std::array values{state.values.at(&state.module.syntax->expression(binary.left)),
                            state.values.at(&state.module.syntax->expression(binary.right))};
    if (binary.operation == ast::BinaryOperator::append) {
        if (auto *prefixed = literal_prefix(state, binary, values[1])) {
            return prefixed;
        }
    }
    if (const auto builtin = operator_builtin(binary.operation)) {
        return lower_builtin(state, *builtin, values);
    }
    const OperandProofs proofs{small_expression(state, binary.left), small_expression(state, binary.right)};
    return lower_operation(state, operation(semantic::immediate_operator(binary.operation)), values[0], values[1],
                           proofs);
}

// Parentheses and begin/end blocks yield the value of their (last) inner expression.
std::optional<ast::ExprId> forwarded(const ast::ExprValue &value) {
    if (const auto *group = std::get_if<ast::Group>(&value)) {
        return group->expression;
    }
    if (const auto *block = std::get_if<ast::BlockExpression>(&value)) {
        return block->body.back();
    }
    return {};
}
} // namespace

llvm::Value *lower_value(ExpressionLowering &state, const ast::ExprId &id) {
    const auto &expression = state.module.syntax->expression(id);
    locate_source(state.builder, *state.module.syntax, expression.source);
    if (semantic::integer_literal(*state.module.syntax, id, state.word->getBitWidth())) {
        return literal(state, id);
    }
    if (const auto *call = std::get_if<ast::CallExpression>(&expression.value)) {
        return call_value(state, expression, *call);
    }
    if (const auto *match = std::get_if<ast::MatchExpression>(&expression.value)) {
        return lower_body_match(state, *match);
    }
    if (const auto *binary = std::get_if<ast::BinaryExpression>(&expression.value)) {
        return binary_value(state, *binary);
    }
    if (const auto inner = forwarded(expression.value)) {
        return state.values.at(&state.module.syntax->expression(*inner));
    }
    if (const auto *unary = std::get_if<ast::UnaryExpression>(&expression.value)) {
        return lower_operation(state, operation(semantic::immediate_unary(unary->operation)),
                               state.values.at(&state.module.syntax->expression(unary->operand)));
    }
    return leaf(state, id);
}

} // namespace clause::codegen
