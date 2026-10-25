#include "../../terms/immediate_terms.hpp"
#include "bit_types.hpp"
#include "front_end.hpp"
#include "lint.hpp"
#include <algorithm>
#include <stdexcept>

// What erl_lint takes from other modules (plan 21 step LIB34C): erl_eval:partial_eval/1 over constant operators,
// is_pattern_expr/1, erl_bits:set_bit_type/2 as a result instead of an exception, and otp_internal's tables.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using abi::v1::ImmediateOperation;
using forms::element;
using forms::is_atom;
using forms::is_node;

struct OperatorRow {
    std::string_view name;
    std::size_t arity;
    ImmediateOperation operation;
};

constexpr auto OPERATORS = std::to_array<OperatorRow>({
    {"+", 1, ImmediateOperation::positive},
    {"-", 1, ImmediateOperation::negative},
    {"bnot", 1, ImmediateOperation::bit_not},
    {"not", 1, ImmediateOperation::logical_not},
    {"+", 2, ImmediateOperation::add},
    {"-", 2, ImmediateOperation::subtract},
    {"*", 2, ImmediateOperation::multiply},
    {"/", 2, ImmediateOperation::divide},
    {"div", 2, ImmediateOperation::integer_divide},
    {"rem", 2, ImmediateOperation::remainder},
    {"band", 2, ImmediateOperation::bit_and},
    {"bor", 2, ImmediateOperation::bit_or},
    {"bxor", 2, ImmediateOperation::bit_xor},
    {"bsl", 2, ImmediateOperation::shift_left},
    {"bsr", 2, ImmediateOperation::shift_right},
    {"and", 2, ImmediateOperation::logical_and},
    {"or", 2, ImmediateOperation::logical_or},
    {"xor", 2, ImmediateOperation::logical_xor},
    {"==", 2, ImmediateOperation::equal},
    {"/=", 2, ImmediateOperation::not_equal},
    {"=:=", 2, ImmediateOperation::exact_equal},
    {"=/=", 2, ImmediateOperation::exact_not_equal},
    {"<", 2, ImmediateOperation::less},
    {"=<", 2, ImmediateOperation::less_equal},
    {">", 2, ImmediateOperation::greater},
    {">=", 2, ImmediateOperation::greater_equal},
});

// Whether the node is an integer, character, float or atom literal.
bool is_constant_node(const Term &node) {
    return node.is_tuple() && node.tuple_size_unchecked() == 3 &&
           forms::is_one_of(forms::tag_of(node), {"integer", "char", "float", "atom"});
}

// ev_expr/1: the value of a constant expression, none when it raises.
class ConstantEval final {
  public:
    explicit ConstantEval(ProcessContext &context) : build_(context) {}

    std::optional<Term> value(const Term &node) {
        if (is_constant_node(node)) {
            return element(node, 2);
        }
        if (is_node(node, "nil", 2)) {
            return build_.nil();
        }
        if (is_node(node, "op", 4) || is_node(node, "op", 5)) {
            return operation(node, node.tuple_size_unchecked());
        }
        if (is_node(node, "tuple", 3)) {
            return tuple(element(node, 2));
        }
        if (is_node(node, "cons", 4)) {
            return cons(element(node, 2), element(node, 3));
        }
        return std::nullopt;
    }

  private:
    // The list [Head | Tail] when both fold to values.
    std::optional<Term> cons(const Term &head_node, const Term &tail_node) {
        const auto head = value(head_node);
        const auto tail = head ? value(tail_node) : std::nullopt;
        return tail ? std::optional{build_.terms().cons(*head, *tail)} : std::nullopt;
    }

    std::optional<Term> tuple(const Term &list) {
        if (!list.is_list()) {
            return std::nullopt;
        }
        std::vector<Term> items;
        auto rest = list;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            const auto item = value(rest.head_unchecked());
            if (!item) {
                return std::nullopt;
            }
            items.push_back(*item);
        }
        return rest.is_nil() ? std::optional{build_.tuple(items)} : std::nullopt;
    }

    std::optional<Term> operation(const Term &node, std::size_t size) {
        const auto op = element(node, 2);
        const auto left = value(element(node, 3));
        if (!left || !op.is_atom()) {
            return std::nullopt;
        }
        const auto right = size == 5 ? value(element(node, 4)) : left;
        if (!right) {
            return std::nullopt;
        }
        return apply_operator(op.atom_spelling_unchecked(), size, {*left, *right});
    }

    // The operator of the given node size (4 unary, 5 binary) over the operand values.
    std::optional<Term> apply_operator(std::string_view name, std::size_t size, const std::array<Term, 2> &operands) {
        if (size == 5 && (name == "++" || name == "--")) {
            return list_operation(name, operands[0], operands[1]);
        }
        const auto arity = size - 3;
        const auto found = std::ranges::find_if(
            OPERATORS, [&](const OperatorRow &row) { return row.name == name && row.arity == arity; });
        if (found == OPERATORS.end()) {
            return std::nullopt;
        }
        const auto result = detail::immediate_terms(build_.context(), found->operation, operands[0], operands[1]);
        if (!result) {
            throw std::bad_alloc();
        }
        return *result ? std::optional{need(Term::from_word(**result, build_.context()))} : std::nullopt;
    }

    // erlang:'++'/2 and erlang:'--'/2 over proper lists (badarg otherwise).
    std::optional<Term> list_operation(std::string_view name, const Term &left, const Term &right) {
        const auto proper = [](const Term &list) {
            auto rest = list;
            for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            }
            return rest.is_nil();
        };
        if (!proper(left) || (name == "--" && !proper(right))) {
            return std::nullopt;
        }
        auto items = forms::elements(left);
        if (name == "++") {
            return build_.terms().list(items, right);
        }
        for (const auto &removed : forms::elements(right)) {
            const auto found =
                std::ranges::find_if(items, [&](const Term &item) { return collections::same(item, removed); });
            if (found != items.end()) {
                items.erase(found);
            }
        }
        return build_.list(items);
    }

    forms::Builder build_;
};

struct ObsoleteRow {
    std::string_view table;
    std::string_view module;
    std::string_view function;
    std::int64_t arity;
    std::string_view tag;
    std::string_view info;
    std::string_view info_atom;
    std::string_view release;
};

constexpr auto OBSOLETE = std::to_array<ObsoleteRow>({
#include "otp_obsolete.inc"
});

// An otp_internal lookup: the table, the module and function names and the arity.
struct ObsoleteQuery {
    std::string_view table;
    std::string_view module;
    std::string_view function;
    std::int64_t arity;
};

bool row_matches(const ObsoleteRow &row, const ObsoleteQuery &query) {
    return row.table == query.table && row.module == query.module &&
           (row.function.empty() || row.function == query.function) && (row.arity < 0 || row.arity == query.arity);
}

// The {Tag, Info} or {Tag, Info, Release} term of an otp_internal row.
Term obsolete_term(Lint &lint, const ObsoleteRow &row) {
    const auto info = row.info_atom.empty() ? lint.chars(row.info) : lint.atom(row.info_atom);
    if (row.release.empty()) {
        return lint.pair(lint.atom(row.tag), info);
    }
    return lint.triple(lint.atom(row.tag), info, lint.chars(row.release));
}

std::optional<Term> obsolete_in(Lint &lint, std::string_view table, const Term &module, const Term &name,
                                std::int64_t arity) {
    if (!module.is_atom() || !name.is_atom()) {
        return std::nullopt;
    }
    const ObsoleteQuery query{.table = table,
                              .module = module.atom_spelling_unchecked(),
                              .function = name.atom_spelling_unchecked(),
                              .arity = arity};
    const auto found = std::ranges::find_if(OBSOLETE, [&](const ObsoleteRow &row) { return row_matches(row, query); });
    if (found == OBSOLETE.end()) {
        return std::nullopt;
    }
    return obsolete_term(lint, *found);
}
} // namespace

Term partial_eval(ProcessContext &context, const Term &expr) {
    if (!expr.is_tuple() || expr.tuple_size_unchecked() < 2) {
        return expr;
    }
    ConstantEval eval(context);
    const auto value = eval.value(expr);
    if (!value) {
        return expr;
    }
    forms::Builder build(context);
    const auto anno = element(expr, 1);
    if (value->is_integer()) {
        return build.node("integer"_atom, anno, *value);
    }
    if (value->is_float()) {
        return build.node("float"_atom, anno, *value);
    }
    if (value->is_atom()) {
        return build.node("atom"_atom, anno, *value);
    }
    return expr;
}

// is_pattern_expr_1/1: literals, tuples and lists of them, and arithmetic over them.
bool pattern_expr_shape(const Term &expr);

// An arithmetic operator over pattern expression shapes.
bool arithmetic_shape(const Term &expr) {
    const auto size = expr.tuple_size_unchecked();
    const auto op = element(expr, 2);
    const auto arith =
        op.is_atom() && operator_type(op.atom_spelling_unchecked(), static_cast<std::int64_t>(size) - 3) == "arith";
    return arith && pattern_expr_shape(element(expr, 3)) && (size == 4 || pattern_expr_shape(element(expr, 4)));
}

bool pattern_expr_shape(const Term &expr) {
    if (is_constant_node(expr) || is_node(expr, "nil", 2)) {
        return true;
    }
    if (is_node(expr, "tuple", 3)) {
        return std::ranges::all_of(forms::elements(element(expr, 2)), pattern_expr_shape);
    }
    if (is_node(expr, "cons", 4)) {
        return pattern_expr_shape(element(expr, 2)) && pattern_expr_shape(element(expr, 3));
    }
    if (is_node(expr, "op", 4) || is_node(expr, "op", 5)) {
        return arithmetic_shape(expr);
    }
    return false;
}

bool is_pattern_expr(ProcessContext &context, const Term &expr) {
    if (!pattern_expr_shape(expr)) {
        return false;
    }
    return is_constant_node(partial_eval(context, expr));
}

Term Lint::partial_eval(const Term &expr) const { return lint::partial_eval(build_.context(), expr); }

bool Lint::is_pattern_expr(const Term &expr) const { return lint::is_pattern_expr(build_.context(), expr); }

BitTypeResult set_bit_type(Lint &lint, const Term &size, const Term &types) {
    auto &context = lint.build_.context();
    BitTypeSpec spec;
    try {
        spec = settle_bit_type(context, {.size = size, .types = types});
    } catch (const BuiltinFailure &failure) {
        if (failure.reason != abi::v1::ErrorReason::raised_error) {
            throw;
        }
        BitTypeResult failed;
        failed.size = size;
        failed.type = lint.atom("integer"_atom);
        failed.error = need(Term::from_word(failure.payload, context));
        return failed;
    }
    BitTypeResult result{.size = size,
                         .type = lint.atom("integer"_atom),
                         .float_type = spec.type == abi::v1::BitType::floating,
                         .binary_type = spec.type == abi::v1::BitType::binary,
                         .utf_type = is_utf_type(spec.type),
                         .unit = spec.unit ? std::optional<std::int64_t>{*spec.unit} : std::nullopt,
                         .error = std::nullopt};
    if (is_atom(size, "default")) {
        result.size = result.utf_type      ? lint.atom("undefined"_atom)
                      : result.binary_type ? lint.atom("all"_atom)
                                           : lint.build_.integer(spec.default_size.value_or(8));
    }
    return result;
}

std::optional<Term> obsolete(Lint &lint, const Term &module, const Term &name, std::int64_t arity) {
    return obsolete_in(lint, "obsolete", module, name, arity);
}

std::optional<Term> obsolete_type(Lint &lint, const Term &module, const Term &name, std::int64_t arity) {
    return obsolete_in(lint, "obsolete_type", module, name, arity);
}

std::optional<Term> obsolete_callback(Lint &lint, const Term &module, const Term &name, std::int64_t arity) {
    return obsolete_in(lint, "obsolete_callback", module, name, arity);
}
} // namespace clause::runtime::builtins::front_end::lint
