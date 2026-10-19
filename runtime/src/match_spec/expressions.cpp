#include "compiler.hpp"
#include "functions.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// Guards and bodies of match specifications (plan 21 step LIB11): constants fold into values as OTP's do_emit_constant
// copies them, the rest becomes expression nodes; operands are compiled in OTP's order (calls and tuples last to
// first), so the messages of linting come in OTP's order.
namespace clause::runtime::detail::match_spec {
namespace {
using Kind = Expression::Kind;

Expression node(Kind kind) {
    return {.kind = kind,
            .word = 0,
            .slot = 0,
            .function = Function::immediate,
            .operation = 0,
            .action = Action::message,
            .operands = {}};
}

// A trace action as OTP checks it: its name, whether a guard may use it, the tuple sizes (arguments + 1) it takes,
// and whether its arity message is "called with arguments" (it takes none) or "with wrong number of arguments".
struct ActionForm {
    std::string_view name;
    Action action;
    bool guard;
    std::array<std::size_t, 2> sizes;
    bool takes_none;
};

constexpr std::array ACTIONS{
    ActionForm{"message", Action::message, false, {2, 2}, false},
    ActionForm{"return_trace", Action::return_trace, false, {1, 1}, true},
    ActionForm{"exception_trace", Action::exception_trace, false, {1, 1}, true},
    ActionForm{"display", Action::display, false, {2, 2}, false},
    ActionForm{"process_dump", Action::process_dump, false, {1, 1}, true},
    ActionForm{"caller", Action::caller, false, {1, 1}, true},
    ActionForm{"caller_line", Action::caller_line, false, {1, 1}, true},
    ActionForm{"current_stacktrace", Action::current_stacktrace, false, {1, 2}, false},
    ActionForm{"is_seq_trace", Action::is_seq_trace, true, {1, 1}, true},
    ActionForm{"get_seq_token", Action::get_seq_token, false, {1, 1}, true},
    ActionForm{"set_seq_token", Action::set_seq_token, false, {3, 3}, false},
    ActionForm{"enable_trace", Action::enable_trace, false, {2, 3}, false},
    ActionForm{"disable_trace", Action::disable_trace, false, {2, 3}, false},
    ActionForm{"trace", Action::trace, false, {3, 4}, false},
    ActionForm{"silent", Action::silent, false, {2, 2}, false},
};

const ActionForm *action_named(std::string_view name) {
    const auto found = std::ranges::find(ACTIONS, name, &ActionForm::name);
    return found == ACTIONS.end() ? nullptr : &*found;
}

std::string quoted(std::string_view name) { return "Special form '" + std::string(name) + "'"; }
} // namespace

std::size_t Compiler::add(Expression expression) {
    program_.expressions.push_back(std::move(expression));
    return program_.expressions.size() - 1;
}

std::size_t Compiler::node_of(const Operand &operand) {
    if (!operand.constant) {
        return operand.node;
    }
    auto constant = node(Kind::constant);
    constant.word = keep(*operand.constant);
    return add(std::move(constant));
}

void Compiler::expressions(const Term &list, std::vector<std::size_t> &out) {
    if (list.is_nil()) {
        return;
    }
    if (!list.is_cons()) {
        error("Match expression is not a list.");
        return;
    }
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        out.push_back(node_of(expression(rest.head_unchecked(), 0)));
    }
    if (!rest.is_nil()) {
        error("Match expression is not a proper list.");
    }
}

Compiler::Operand Compiler::expression(const Term &term, std::size_t depth) {
    if (depth > DEPTH_LIMIT) {
        system_limit_ = true;
        error("Excessive nesting; system limit reached near: ", term, "");
        return {.constant = term, .node = 0};
    }
    if (term.is_cons()) {
        return list_expression(term, depth);
    }
    if (term.is_map()) {
        return map_expression(term, depth);
    }
    if (term.is_tuple()) {
        return tuple_expression(term, depth);
    }
    if (term.is_atom()) {
        return atom_expression(term);
    }
    return {.constant = term, .node = 0};
}

Compiler::Operand Compiler::atom_expression(const Term &term) {
    if (const auto number = variable_number(term)) {
        if (!bound_.contains(*number)) {
            error("Variable $" + std::to_string(*number) + " is unbound.");
            return {.constant = term, .node = 0};
        }
        auto variable = node(Kind::variable);
        variable.slot = slot(*number);
        return {.constant = std::nullopt, .node = add(std::move(variable))};
    }
    if (is_atom(term, "$_")) {
        return {.constant = std::nullopt, .node = add(node(Kind::whole))};
    }
    if (is_atom(term, "$$")) {
        return {.constant = std::nullopt, .node = add(node(Kind::bindings))};
    }
    return {.constant = term, .node = 0};
}

std::vector<Compiler::Operand> Compiler::operands(const Term &tuple, std::size_t first, std::size_t depth,
                                                  bool reversed) {
    std::vector<Operand> result;
    const auto size = tuple.tuple_size_unchecked();
    for (std::size_t index = 0; index + first < size; ++index) {
        const auto at = reversed ? size - 1 - index : first + index;
        result.push_back(expression(tuple.tuple_element_unchecked(at), depth + 1));
    }
    if (reversed) {
        std::ranges::reverse(result);
    }
    return result;
}

// {{E1, ..., En}} builds a tuple; {Name, Arguments...} calls a function or special form; anything else is an error.
Compiler::Operand Compiler::tuple_expression(const Term &term, std::size_t depth) {
    const auto size = term.tuple_size_unchecked();
    if (size == 1 && term.tuple_element_unchecked(0).is_tuple()) {
        const auto inner = term.tuple_element_unchecked(0);
        const auto elements = operands(inner, 0, depth, true);
        if (std::ranges::all_of(elements, [](const Operand &operand) { return operand.constant.has_value(); })) {
            std::vector<Term> values;
            std::ranges::transform(elements, std::back_inserter(values),
                                   [](const Operand &operand) { return *operand.constant; });
            return {.constant = need(TermFactory(context_).tuple(values)), .node = 0};
        }
        auto tuple = node(Kind::tuple);
        std::ranges::transform(elements, std::back_inserter(tuple.operands),
                               [this](const Operand &operand) { return node_of(operand); });
        return {.constant = std::nullopt, .node = add(std::move(tuple))};
    }
    const auto name = size >= 1 ? term.tuple_element_unchecked(0) : Term{};
    if (size >= 1 && name.is_atom() && !variable_number(name)) {
        return call(term, depth);
    }
    error("", term, " is neither a function call, nor a tuple (tuples are written {{ ... }}).");
    return {.constant = term, .node = 0};
}

// A list's heads in order, then its tail; constant when every part is.
Compiler::Operand Compiler::list_expression(const Term &term, std::size_t depth) {
    std::vector<Operand> heads;
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        heads.push_back(expression(rest.head_unchecked(), depth + 1));
    }
    auto tail = expression(rest, depth + 1);
    const auto constant = [](const Operand &operand) { return operand.constant.has_value(); };
    if (tail.constant && std::ranges::all_of(heads, constant)) {
        std::vector<Term> values;
        std::ranges::transform(heads, std::back_inserter(values), [](const Operand &head) { return *head.constant; });
        return {.constant = need(TermFactory(context_).list(values, *tail.constant)), .node = 0};
    }
    auto node_index = node_of(tail);
    for (auto at = heads.rbegin(); at != heads.rend(); ++at) {
        auto cons = node(Kind::cons);
        cons.operands = {node_of(*at), node_index};
        node_index = add(std::move(cons));
    }
    return {.constant = std::nullopt, .node = node_index};
}

// A map's values, then its keys; a constant map keeps its keys as written, as OTP's constant copy does, and a map
// built at run time computes them too.
Compiler::Operand Compiler::map_expression(const Term &term, std::size_t depth) {
    const auto entries = need(term.map_entries());
    std::vector<Operand> values;
    std::vector<Operand> keys;
    for (auto at = entries.rbegin(); at != entries.rend(); ++at) {
        values.push_back(expression(at->second, depth + 1));
    }
    for (auto at = entries.rbegin(); at != entries.rend(); ++at) {
        keys.push_back(expression(at->first, depth + 1));
    }
    std::ranges::reverse(values);
    std::ranges::reverse(keys);
    const auto constant = [](const Operand &operand) { return operand.constant.has_value(); };
    if (std::ranges::all_of(values, constant) && std::ranges::all_of(keys, constant)) {
        std::vector<std::pair<Term, Term>> pairs;
        pairs.reserve(entries.size());
        for (std::size_t at = 0; at < entries.size(); ++at) {
            pairs.emplace_back(entries[at].first, *values[at].constant);
        }
        return {.constant = need(TermFactory(context_).map(pairs)), .node = 0};
    }
    auto map = node(Kind::map);
    for (const auto &key : keys) {
        map.operands.push_back(node_of(key));
    }
    for (const auto &value : values) {
        map.operands.push_back(node_of(value));
    }
    return {.constant = std::nullopt, .node = add(std::move(map))};
}

Compiler::Operand Compiler::call(const Term &term, std::size_t depth) {
    const auto name = term.tuple_element_unchecked(0).atom_spelling_unchecked();
    if (name == "and" || name == "or" || name == "andalso" || name == "andthen" || name == "orelse") {
        return logical(term, name, depth);
    }
    if (name == "const" || name == "self") {
        return special_form(term, name);
    }
    if (action_named(name) != nullptr) {
        return action(term, name, depth);
    }
    return function_call(term, name, depth);
}

// {const, Term} is Term as it is; {self} is the caller's pid.
Compiler::Operand Compiler::special_form(const Term &term, std::string_view name) {
    const auto size = term.tuple_size_unchecked();
    if (name == "const") {
        if (size != 2) {
            error(quoted(name) + " called with more than one argument in ", term, ".");
            return {.constant = term, .node = 0};
        }
        return {.constant = term.tuple_element_unchecked(1), .node = 0};
    }
    if (size != 1) {
        error(quoted(name) + " called with arguments in ", term, ".");
        return {.constant = term, .node = 0};
    }
    return {.constant = std::nullopt, .node = add(node(Kind::self))};
}

// and/or evaluate every operand (compiled last to first); andalso (or andthen) and orelse stop early, first to last.
Compiler::Operand Compiler::logical(const Term &term, std::string_view name, std::size_t depth) {
    if (term.tuple_size_unchecked() < 2) {
        error(quoted(name) + " called without arguments in ", term, ".");
        return {.constant = term, .node = 0};
    }
    const auto all = name == "and" || name == "or";
    const auto kind = name == "and"      ? Kind::and_all
                      : name == "or"     ? Kind::or_all
                      : name == "orelse" ? Kind::or_else
                                         : Kind::and_also;
    auto logic = node(kind);
    for (const auto &operand : operands(term, 1, depth, all)) {
        logic.operands.push_back(node_of(operand));
    }
    return {.constant = std::nullopt, .node = add(std::move(logic))};
}

bool Compiler::trace_form(std::string_view name, bool in_guard) {
    if (program_.dialect != Dialect::trace) {
        error(quoted(name) + " used in wrong dialect.");
        return false;
    }
    if (in_guard_ && !in_guard) {
        error(quoted(name) + " called in guard context.");
        return false;
    }
    return true;
}

// Whether current_stacktrace's depth argument is a small non-negative integer; records OTP's error otherwise.
bool Compiler::stacktrace_depth(const Term &term) {
    const auto depth_term = term.tuple_element_unchecked(1);
    const auto value = builtins::small(depth_term.word());
    if (!value) {
        error("Special form 'current_stacktrace' called with non small argument.");
        return false;
    }
    if (*value < 0) {
        error("Special form 'current_stacktrace' called with negative integer argument.");
        return false;
    }
    return true;
}

// A trace action: checked as OTP checks it (dialect, guard context, then arguments), operands compiled last to first.
Compiler::Operand Compiler::action(const Term &term, std::string_view name, std::size_t depth) {
    const auto &form = *action_named(name);
    // OTP's check_trace names set_seq_token set_seq_trace.
    if (!trace_form(form.action == Action::set_seq_token ? "set_seq_trace" : name, form.guard)) {
        return {.constant = term, .node = 0};
    }
    const auto size = term.tuple_size_unchecked();
    if (size != form.sizes[0] && size != form.sizes[1]) {
        const auto *message =
            form.takes_none ? " called with arguments in " : " called with wrong number of arguments in ";
        error(quoted(name) + message, term, ".");
        return {.constant = term, .node = 0};
    }
    if (form.action == Action::current_stacktrace && size == 2 && !stacktrace_depth(term)) {
        return {.constant = term, .node = 0};
    }
    auto action_node = node(Kind::action);
    action_node.action = form.action;
    for (const auto &operand : operands(term, 1, depth, true)) {
        action_node.operands.push_back(node_of(operand));
    }
    return {.constant = std::nullopt, .node = add(std::move(action_node))};
}

// A guard function of OTP's table that may be called in this dialect and context; operands last to first.
Compiler::Operand Compiler::function_call(const Term &term, std::string_view name, std::size_t depth) {
    const auto arity = term.tuple_size_unchecked() - 1;
    const auto *function = guard_function(name, arity);
    const auto suffix = "/" + std::to_string(arity);
    if (function == nullptr) {
        error("Function ", term.tuple_element_unchecked(0), suffix + " does_not_exist.");
        return {.constant = term, .node = 0};
    }
    const auto trace = program_.dialect == Dialect::trace;
    const auto context = in_guard_ ? (trace ? TRACE_GUARD : TABLE_GUARD) : (trace ? TRACE_BODY : TABLE_BODY);
    if ((function->contexts & context) == 0) {
        error("Function ", term.tuple_element_unchecked(0), suffix + " cannot be called in this context.");
        return {.constant = term, .node = 0};
    }
    auto call_node = node(Kind::call);
    // The trace control word is only read and reported here, never set (OTP's fake destructive mode).
    call_node.function = function->function == Function::set_tcw ? Function::set_tcw_fake : function->function;
    call_node.operation = static_cast<std::uint8_t>(function->operation);
    for (const auto &operand : operands(term, 1, depth, true)) {
        call_node.operands.push_back(node_of(operand));
    }
    return {.constant = std::nullopt, .node = add(std::move(call_node))};
}
} // namespace clause::runtime::detail::match_spec
