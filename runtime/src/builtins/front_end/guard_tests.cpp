#include "guard_tests.hpp"
#include "abstract.hpp"
#include "forms.hpp"
#include "front_end.hpp"
#include <algorithm>

// erl_lint's guard checks (plan 21 step LIB34C, docs/builtins.md#erl-lint): is_guard_test/1,3 and is_guard_expr/1
// written after erl_lint's is_guard_test2/2 and is_gexpr/2, and clause_erl_lint's builtins over them.
namespace clause::runtime::builtins::front_end {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

// The arity of a call's argument list.
std::int64_t arity_of(const Term &args) { return static_cast<std::int64_t>(forms::elements(args).size()); }

// An atom node's name, when `node` is {atom, _, Name}.
std::optional<Term> atom_name(const Term &node) {
    if (is_node(node, "atom", 3) && element(node, 2).is_atom()) {
        return element(node, 2);
    }
    return std::nullopt;
}

// Arithmetic, boolean and comparison operators are allowed in guards; list and send operators are not.
bool guard_operator(const Term &name, std::int64_t arity) {
    if (!name.is_atom()) {
        return false;
    }
    const auto type = operator_type(name.atom_spelling_unchecked(), arity);
    return type == "arith" || type == "bool" || type == "comp";
}

// The walk of one check.
class GuardCheck final {
  public:
    GuardCheck(ProcessContext &context, const GuardScope &scope) : build_(context), scope_(scope) {}

    bool test(const Term &expr) const {
        if (!is_node(expr, "call", 4)) {
            return this->expr(expr);
        }
        const auto callee = element(expr, 2);
        const auto args = element(expr, 3);
        if (const auto name = atom_name(callee)) {
            return local_test(*name, args);
        }
        if (is_erlang_function(callee, "is_record") && arity_of(args) == 2) {
            return local_test(element(element(callee, 3), 2), args);
        }
        return this->expr(expr);
    }

    bool expr(const Term &expr) const {
        const auto tag = forms::tag_of(expr);
        const auto size = expr.is_tuple() ? expr.tuple_size_unchecked() : 0;
        if ((size == 3 && forms::is_one_of(tag, {"var", "char", "integer", "float", "atom", "string"})) ||
            (size == 2 && tag == "nil")) {
            return true;
        }
        return compound(expr);
    }

  private:
    // Every element of a list of expressions.
    bool all(const Term &list) const {
        return std::ranges::all_of(forms::elements(list), [&](const Term &item) { return expr(item); });
    }

    bool both(const Term &first, const Term &second) const { return expr(first) && expr(second); }

    bool overridden(const Term &name, std::int64_t arity) const {
        return scope_.overridden != nullptr && scope_.overridden->contains({name.word(), arity});
    }

    // Name(Args) in a test: record/2 is is_record/2, a type test checks its arguments, is_record/2 a literal name.
    bool local_test(const Term &name, const Term &args) const {
        const auto arity = arity_of(args);
        if (is_atom(name, "record") && arity == 2) {
            return !overridden(build_.atom("is_record"_atom), 2) && all(args);
        }
        if (overridden(name, arity)) {
            return false;
        }
        if (!is_type_test(name.atom_spelling_unchecked(), arity)) {
            return guard_call(name, args);
        }
        if (is_atom(name, "is_record") && arity == 2) {
            return atom_name(forms::elements(args)[1]).has_value() && all(args);
        }
        return all(args);
    }

    // Name(Args) as an expression: a guard BIF not shadowed by a local or imported function.
    bool guard_call(const Term &name, const Term &args) const {
        const auto arity = arity_of(args);
        return !overridden(name, arity) && is_guard_bif(name.atom_spelling_unchecked(), arity) && all(args);
    }

    // erlang:Name(Args), written as a remote call or an {erlang, Name} tuple: a guard BIF or operator.
    bool erlang_call(const Term &name, const Term &args) const {
        const auto arity = arity_of(args);
        return (is_guard_bif(name.atom_spelling_unchecked(), arity) || guard_operator(name, arity)) && all(args);
    }

    // {remote, _, {atom, _, erlang}, {atom, _, Name}}, Name being `name` when not empty.
    static bool is_erlang_function(const Term &callee, std::string_view name = {}) {
        if (!is_node(callee, "remote", 4)) {
            return false;
        }
        const auto module = atom_name(element(callee, 2));
        const auto function = atom_name(element(callee, 3));
        return module && is_atom(*module, "erlang") && function && (name.empty() || is_atom(*function, name));
    }

    // The function of {tuple, _, [{atom, _, erlang}, {atom, _, Name}]}.
    static std::optional<Term> erlang_tuple(const Term &callee) {
        if (!is_node(callee, "tuple", 3)) {
            return std::nullopt;
        }
        const auto items = forms::elements(element(callee, 2));
        if (items.size() != 2 || !atom_name(items[0]) || !is_atom(*atom_name(items[0]), "erlang")) {
            return std::nullopt;
        }
        return atom_name(items[1]);
    }

    bool call(const Term &call) const {
        const auto callee = element(call, 2);
        const auto args = element(call, 3);
        if (const auto name = atom_name(callee)) {
            return guard_call(*name, args);
        }
        if (is_erlang_function(callee)) {
            return erlang_call(element(element(callee, 3), 2), args);
        }
        const auto name = erlang_tuple(callee);
        return name && erlang_call(*name, args);
    }

    using Handler = bool (GuardCheck::*)(const Term &) const;

    // A node kind (tag and tuple size) and its check.
    struct Rule {
        std::string_view tag;
        std::size_t size;
        Handler handler;
    };

    bool compound(const Term &expr) const {
        static constexpr auto RULES = std::to_array<Rule>({
            {"cons", 4, &GuardCheck::cons},
            {"tuple", 3, &GuardCheck::tuple},
            {"map", 3, &GuardCheck::map},
            {"map", 4, &GuardCheck::map},
            {"record_index", 4, &GuardCheck::record_index},
            {"record_field", 5, &GuardCheck::record_field},
            {"record", 4, &GuardCheck::record},
            {"bin", 3, &GuardCheck::bin},
            {"call", 4, &GuardCheck::call},
            {"op", 4, &GuardCheck::unary},
            {"op", 5, &GuardCheck::binary},
        });
        const auto tag = forms::tag_of(expr);
        const auto size = expr.is_tuple() ? expr.tuple_size_unchecked() : 0;
        for (const auto &rule : RULES) {
            if (rule.tag == tag && rule.size == size) {
                return (this->*rule.handler)(expr);
            }
        }
        return false;
    }

    bool cons(const Term &expr) const { return both(element(expr, 2), element(expr, 3)); }

    bool tuple(const Term &expr) const { return all(element(expr, 2)); }

    // {map, A, Fields} and {map, A, Map, Fields}.
    bool map(const Term &expr) const {
        const auto size = expr.tuple_size_unchecked();
        return (size == 3 || this->expr(element(expr, 2))) && map_fields(element(expr, size - 1));
    }

    bool record_index(const Term &expr) const { return this->expr(element(expr, 3)); }

    bool record_field(const Term &expr) const { return both(element(expr, 2), element(expr, 4)); }

    bool bin(const Term &expr) const { return segments(element(expr, 2)); }

    bool unary(const Term &expr) const { return guard_operator(element(expr, 2), 1) && this->expr(element(expr, 3)); }

    bool binary(const Term &expr) const {
        const auto op = element(expr, 2);
        const auto lazy = is_atom(op, "andalso") || is_atom(op, "orelse");
        return (lazy || guard_operator(op, 2)) && both(element(expr, 3), element(expr, 4));
    }

    // Each field a map_field_assoc or map_field_exact whose key and value are guard expressions.
    bool map_fields(const Term &fields) const {
        auto rest = fields;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            const auto field = rest.head_unchecked();
            const auto tag = forms::tag_of(field);
            const auto known = tag == "map_field_assoc" || tag == "map_field_exact";
            if (!known || field.tuple_size_unchecked() != 4 || !both(element(field, 2), element(field, 3))) {
                return false;
            }
        }
        return rest.is_nil();
    }

    bool segments(const Term &segments) const {
        return std::ranges::all_of(forms::elements(segments), [&](const Term &segment) {
            if (!is_node(segment, "bin_element", 5)) {
                lists::no_clause();
            }
            const auto size = element(segment, 3);
            return expr(element(segment, 2)) && (is_atom(size, "default") || expr(size));
        });
    }

    // #Name{Inits}: the given values and the defaults of the fields not given are guard expressions.
    bool record(const Term &record) const {
        auto fields = forms::elements(element(record, 3));
        const auto defaults = default_inits(record, fields);
        fields.insert(fields.end(), defaults.begin(), defaults.end());
        return std::ranges::all_of(
            fields, [&](const Term &field) { return is_node(field, "record_field", 4) && expr(element(field, 3)); });
    }

    // The names of the fields a construction gives.
    static std::vector<Term> given_names(const std::vector<Term> &inits) {
        std::vector<Term> given;
        for (const auto &init : inits) {
            if (is_node(init, "record_field", 4) && atom_name(element(init, 2))) {
                given.push_back(element(element(init, 2), 2));
            }
        }
        return given;
    }

    // The definition's fields with a default that the construction does not give, copied with its annotation.
    std::vector<Term> default_inits(const Term &record, const std::vector<Term> &inits) const {
        const auto name = element(record, 2);
        std::vector<Term> defaults;
        if (scope_.records == nullptr || !name.is_atom() || !scope_.records->contains(name.word())) {
            return defaults;
        }
        const auto given = given_names(inits);
        for (const auto &field : forms::elements(scope_.records->at(name.word()))) {
            if (!is_node(field, "record_field", 4) || !atom_name(element(field, 2))) {
                continue;
            }
            const auto field_name = element(element(field, 2), 2);
            if (std::ranges::none_of(given, [&](const Term &known) { return known.word() == field_name.word(); })) {
                const auto value = abstract::replace_annos(build_.context(), element(field, 3), element(record, 1));
                defaults.push_back(forms::with_element(build_, field, 3, value));
            }
        }
        return defaults;
    }

    forms::Builder build_;
    const GuardScope &scope_;
};

// clause_erl_lint:is_guard_test(Expr): erl_lint:is_guard_test/1, with no record definitions.
Term is_guard_test1(ProcessContext &context, const Term &expr) {
    return string::Terms(context).boolean(is_guard_test(context, expr, {}));
}

// clause_erl_lint:is_guard_expr(Expr).
Term is_guard_expr1(ProcessContext &context, const Term &expr) {
    return string::Terms(context).boolean(is_guard_expr(context, expr, {}));
}

constexpr std::array GUARD_TEST_BUILTINS{
    typed_entry<is_guard_expr1>("clause_erl_lint", "is_guard_expr"),
    typed_entry<is_guard_test1>("clause_erl_lint", "is_guard_test"),
};
} // namespace

bool is_guard_test(ProcessContext &context, const Term &expr, const GuardScope &scope) {
    return GuardCheck(context, scope).test(expr);
}

bool is_guard_expr(ProcessContext &context, const Term &expr, const GuardScope &scope) {
    return GuardCheck(context, scope).expr(expr);
}
} // namespace clause::runtime::builtins::front_end

namespace clause::runtime {
std::span<const BuiltinEntry> guard_test_builtins() noexcept { return builtins::front_end::GUARD_TEST_BUILTINS; }
} // namespace clause::runtime
