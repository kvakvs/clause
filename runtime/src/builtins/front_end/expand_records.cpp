#include "../collections/collections.hpp"
#include "abstract.hpp"
#include "forms.hpp"
#include "front_end.hpp"
#include "guard_tests.hpp"
#include <algorithm>
#include <map>
#include <set>

// erl_expand_records:module/2 (plan 21 step LIB34C, docs/builtins.md#erl-expand-records): Clause's expansion of
// records written after OTP's documentation and observed output, moved from Erlang to C++ clause for clause. Tuple
// records become tuple operations, native records stay with their module, calls of imported functions and BIFs get
// their module, new variables are named rec0, rec1, ... in OTP's order.
namespace clause::runtime::builtins::front_end {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;
using forms::tag_of;

// The roles of the arguments (typed.hpp's Named).
struct FormsRole;
struct OptionsRole;
using FormsArg = Named<FormsRole>;
using OptionsArg = Named<OptionsRole>;

// error(Reason) with `reason` built by the caller.
[[noreturn]] void raise_error(const Term &reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = reason.word()};
}

// How a local name F/A is called: a function of the module (no module), or one imported from `module`.
struct CallKind {
    std::optional<Term> module;
};

// A native record: its definition's field list when local, else the module it is imported from.
struct Native {
    bool local = true;
    Term value;
};

// A record test a guard's field access needs: its key ({Name, Term} with annotations zeroed), annotation and test.
struct RecordCheck {
    Term key;
    Term anno;
    Term test;
};

// The options module/2 reads from its argument and the forms' compile attributes.
struct Options {
    bool dialyzer = false;
    bool expand_inits = false;
    bool strict = true;
};

// A tuple record used at an annotation: its name and the annotation its fields get.
struct RecordAt {
    Term name;
    Term anno;
};

// A tuple record's name and fields (annotated where it is used).
struct RecordUse {
    Term name;
    std::vector<Term> fields;
};

// A native record construction to complete from its definition.
struct NativeConstruction {
    Term anno;
    Term name;
    Term definitions;
    Term inits;
};

// Rec#Name.Field at an annotation.
struct FieldAccess {
    Term anno;
    Term record;
    Term field;
    Term name;
};

// A clause's head and guard, which record tests may turn into patterns.
struct HeadGuard {
    Term head;
    Term guard;
};

// The state of a guard's is_record/3 tests on one variable: needed by every conjunction, or made a pattern already.
struct VariableTest {
    Term tag;
    Term size;
    bool removed = false;
};

// lists:flatten/1 of one compile attribute's value, appended to `out`.
void flatten_into(std::vector<Term> &out, const Term &value) {
    if (!value.is_list()) {
        out.push_back(value);
        return;
    }
    auto rest = value;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        flatten_into(out, rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
}

bool is_pair(const Term &term) { return term.is_tuple() && term.tuple_size_unchecked() == 2; }

// The value of attribute `name` when `form` is one.
std::optional<Term> attribute_value(const Term &form, std::string_view name) {
    if (is_node(form, "attribute", 4) && is_atom(element(form, 2), name)) {
        return element(form, 3);
    }
    return std::nullopt;
}

// The value of attribute `name` when it is {Atom, Value}, as import and import_record are.
std::optional<Term> atom_pair_value(const Term &form, std::string_view name) {
    const auto value = attribute_value(form, name);
    if (value && is_pair(*value) && element(*value, 0).is_atom()) {
        return value;
    }
    return std::nullopt;
}

// Options ++ lists:flatten(the values of the compile attributes): badarg for an improper Options.
std::vector<Term> all_options(const OptionsArg &given, const FormsArg &forms) {
    std::vector<Term> all;
    auto rest = given.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        all.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    for (const auto &form : forms::elements(forms.term)) {
        if (const auto value = attribute_value(form, "compile")) {
            flatten_into(all, *value);
        }
    }
    return all;
}

Options options_of(const OptionsArg &given, const FormsArg &forms) {
    Options options;
    for (const auto &option : all_options(given, forms)) {
        options.dialyzer = options.dialyzer || is_atom(option, "dialyzer");
        options.expand_inits = options.expand_inits || is_atom(option, "expand_inits");
        if (is_atom(option, "strict_record_tests") || is_atom(option, "no_strict_record_tests")) {
            options.strict = is_atom(option, "strict_record_tests");
        }
    }
    return options;
}

// The is_ form of atom(X) and the other old type tests; empty for any other call.
std::string new_test_name(const Term &name, std::size_t arity) {
    static constexpr std::array<std::string_view, 11> TESTS{"atom",   "binary", "float", "function",  "integer", "list",
                                                            "number", "pid",    "port",  "reference", "tuple"};
    if (!name.is_atom()) {
        return {};
    }
    const auto spelling = name.atom_spelling_unchecked();
    if ((arity == 2 && spelling == "record") || (arity == 1 && std::ranges::find(TESTS, spelling) != TESTS.end())) {
        return "is_" + std::string(spelling);
    }
    return {};
}

// The expansion of one module.
class Expander final {
  public:
    Expander(ProcessContext &context, const Options &options) : build_(context), options_(options) {}

    Term module(const Term &forms) {
        collect_calls(forms);
        collect_natives(forms);
        return each(forms, [&](const Term &form) { return this->form(form); });
    }

  private:
    using Handler = Term (Expander::*)(const Term &);

    // A node kind (tag and tuple size) and its expansion.
    struct Rule {
        std::string_view tag;
        std::size_t size;
        Handler handler;
    };

    // Local functions, then the imports overriding them.
    void collect_calls(const Term &forms) {
        const auto all = forms::elements(forms);
        for (const auto &form : all) {
            if (is_node(form, "function", 5)) {
                add_call(element(form, 2), element(form, 3), {});
            }
        }
        for (const auto &form : all) {
            if (const auto import = atom_pair_value(form, "import")) {
                add_imports(*import);
            }
        }
    }

    // {Module, [{F, A}]} of an import attribute.
    void add_imports(const Term &import) {
        for (const auto &function : forms::elements(element(import, 1))) {
            if (is_pair(function)) {
                add_call(element(function, 0), element(function, 1), {element(import, 0)});
            }
        }
    }

    void add_call(const Term &name, const Term &arity, const CallKind &kind) {
        const FunctionKey key{name.word(), string::small_of(arity).value_or(-1)};
        calls_.insert_or_assign(key, kind);
        overridden_.insert(key);
    }

    // Native record definitions, then the imported ones overriding them.
    void collect_natives(const Term &forms) {
        const auto all = forms::elements(forms);
        for (const auto &form : all) {
            if (const auto value = attribute_value(form, "native_record"); value && is_pair(*value)) {
                natives_.insert_or_assign(element(*value, 0).word(),
                                          Native{.local = true, .value = element(*value, 1)});
            }
        }
        for (const auto &form : all) {
            const auto import = atom_pair_value(form, "import_record");
            for (const auto &name : import ? forms::elements(element(*import, 1)) : std::vector<Term>{}) {
                natives_.insert_or_assign(name.word(), Native{.local = false, .value = element(*import, 0)});
            }
        }
    }

    Term form(const Term &form) {
        if (const auto definition = attribute_value(form, "record"); definition && is_pair(*definition)) {
            const auto name = element(*definition, 0).word();
            records_.insert_or_assign(name, normalise_fields(element(*definition, 1)));
            raw_records_.emplace(name, element(*definition, 1));
            return form;
        }
        if (is_node(form, "function", 5)) {
            return forms::with_element(build_, form, 4, clauses(element(form, 4)));
        }
        if (is_node(form, "attribute", 4) && is_atom(element(form, 2), "module")) {
            module_ = element(form, 3);
        }
        return form;
    }

    // Each element of a list in order.
    template <typename Walk> Term each(const Term &list, Walk &&walk) {
        const auto items = forms::elements(list);
        std::vector<Term> out;
        out.reserve(items.size());
        for (const auto &item : items) {
            out.push_back(walk(item));
        }
        return build_.list(out);
    }

    Term clauses(const Term &list) {
        return each(list, [&](const Term &clause) { return this->clause(clause); });
    }

    Term clause(const Term &clause) {
        if (!is_node(clause, "clause", 5)) {
            lists::no_clause();
        }
        const auto head = patterns(element(clause, 2));
        const auto guard = this->guard(element(clause, 3));
        const auto parts = record_tests_as_patterns({.head = head, .guard = guard});
        const auto body = exprs(element(clause, 4));
        const std::array items{element(clause, 0), element(clause, 1), parts.head, parts.guard, body};
        return build_.tuple(items);
    }

    // Patterns

    Term patterns(const Term &list) {
        return each(list, [&](const Term &pattern) { return this->pattern(pattern); });
    }

    Term pattern(const Term &node) {
        static constexpr auto RULES = std::to_array<Rule>({
            {"var", 3, &Expander::same},
            {"char", 3, &Expander::same},
            {"integer", 3, &Expander::same},
            {"float", 3, &Expander::same},
            {"atom", 3, &Expander::same},
            {"string", 3, &Expander::same},
            {"nil", 2, &Expander::same},
            {"cons", 4, &Expander::cons_pattern},
            {"tuple", 3, &Expander::tuple_pattern},
            {"map", 3, &Expander::tuple_pattern},
            {"map_field_exact", 4, &Expander::map_field_pattern},
            {"record_field", 4, &Expander::field_pattern},
            {"record_index", 4, &Expander::record_index},
            {"record", 4, &Expander::record_pattern},
            {"bin", 3, &Expander::bin_pattern},
            {"match", 4, &Expander::match_pattern},
            {"op", 4, &Expander::operator_pattern},
            {"op", 5, &Expander::operator_pattern},
        });
        return dispatch(RULES, node);
    }

    template <std::size_t N> Term dispatch(const std::array<Rule, N> &rules, const Term &node) {
        const auto tag = tag_of(node);
        const auto size = node.is_tuple() ? node.tuple_size_unchecked() : 0;
        for (const auto &rule : rules) {
            if (rule.tag == tag && rule.size == size) {
                return (this->*rule.handler)(node);
            }
        }
        lists::no_clause();
    }

    Term same(const Term &node) { return node; }

    // [H | T] patterns along the spine, heads first.
    Term cons_pattern(const Term &node) {
        return spine(node, [&](const Term &item) { return pattern(item); });
    }

    template <typename Walk> Term spine(const Term &node, Walk &&walk) {
        std::vector<Term> cells;
        auto rest = node;
        for (; is_node(rest, "cons", 4); rest = element(rest, 3)) {
            cells.push_back(rest);
        }
        std::vector<Term> heads;
        heads.reserve(cells.size());
        for (const auto &cell : cells) {
            heads.push_back(walk(element(cell, 2)));
        }
        auto tail = walk(rest);
        for (auto index = cells.size(); index > 0; --index) {
            const auto &cell = cells[index - 1];
            tail = build_.node("cons"_atom, element(cell, 1), heads[index - 1], tail);
        }
        return tail;
    }

    // {tuple, A, Ps} and {map, A, Ps}.
    Term tuple_pattern(const Term &node) { return forms::with_element(build_, node, 2, patterns(element(node, 2))); }

    Term map_field_pattern(const Term &node) {
        const auto key = expr(element(node, 2));
        const auto value = pattern(element(node, 3));
        return build_.node("map_field_exact"_atom, element(node, 1), key, value);
    }

    Term field_pattern(const Term &node) { return forms::with_element(build_, node, 3, pattern(element(node, 3))); }

    Term record_index(const Term &node) {
        const auto anno = element(node, 1);
        return index_expr(anno, element(node, 3), record_fields({.name = element(node, 2), .anno = anno}));
    }

    Term record_pattern(const Term &node) {
        const auto anno = element(node, 1);
        const auto name = element(node, 2);
        const auto native = native_of(name);
        if (!name.is_atom() || (native && native->local)) {
            return forms::with_element(build_, node, 3, patterns(element(node, 3)));
        }
        if (native) {
            return pattern(forms::with_element(build_, node, 2, module_name(native->value, name)));
        }
        const auto fields = field_patterns(record_fields({.name = name, .anno = anno}), element(node, 3));
        auto items = forms::elements(patterns(build_.list(fields)));
        items.insert(items.begin(), build_.atom_node(anno, name));
        return build_.node("tuple"_atom, mark_record(anno), build_.list(items));
    }

    Term bin_pattern(const Term &node) {
        return segments(node, [&](const Term &value) { return pattern(value); });
    }

    // The segments of a binary from the last to the first, as lists:foldr/3 visits them.
    template <typename Walk> Term segments(const Term &node, Walk &&walk) {
        const auto items = forms::elements(element(node, 2));
        std::vector<Term> out(items.size());
        for (auto index = items.size(); index > 0; --index) {
            const auto &segment = items[index - 1];
            if (!is_node(segment, "bin_element", 5)) {
                lists::no_clause();
            }
            const auto value = walk(element(segment, 2));
            const auto size = is_atom(element(segment, 3), "default") ? element(segment, 3) : expr(element(segment, 3));
            const std::array parts{element(segment, 0), element(segment, 1), value, size, element(segment, 4)};
            out[index - 1] = build_.tuple(parts);
        }
        return forms::with_element(build_, node, 2, build_.list(out));
    }

    Term match_pattern(const Term &node) {
        const auto right = pattern(element(node, 3));
        const auto left = pattern(element(node, 2));
        return build_.node("match"_atom, element(node, 1), left, right);
    }

    Term operator_pattern(const Term &node) {
        std::vector<Term> items{element(node, 0), element(node, 1), element(node, 2)};
        for (std::size_t index = 3; index < node.tuple_size_unchecked(); ++index) {
            items.push_back(pattern(element(node, index)));
        }
        return build_.tuple(items);
    }

    // Guards

    Term guard(const Term &disjunction) {
        return each(disjunction, [&](const Term &conjunction) {
            const auto tests = each(conjunction, [&](const Term &test) { return guard_test(test); });
            checked_.clear();
            return tests;
        });
    }

    // One guard test expanded in guard context with the record tests its field accesses need joined to it.
    Term guard_test(const Term &test) {
        const auto outer = in_guard_;
        in_guard_ = true;
        auto renamed = test;
        if (is_node(test, "call", 4) && is_node(element(test, 2), "atom", 3)) {
            const auto callee = element(test, 2);
            const auto name = new_test_name(element(callee, 2), forms::elements(element(test, 3)).size());
            if (!name.empty()) {
                renamed = forms::with_element(build_, test, 2, build_.atom_node(element(callee, 1), build_.atom(name)));
            }
        }
        const auto expanded = strict_record_access(expr(renamed));
        in_guard_ = outer;
        return expanded;
    }

    // is_record(Term, Name): of a native record the module test, of a tuple record is_record/3 (on a new variable
    // bound to Term outside guards, as Term may have effects).
    Term record_test(const Term &anno, const Term &term, const Term &name) {
        const auto generated = this->generated(anno);
        if (const auto native = native_of(name)) {
            const auto module = native->local ? module_ : native->value;
            const std::array args{term, build_.atom_node(anno, module), build_.atom_node(anno, name)};
            return expr(build_.erlang_call(generated, "is_record"_atom, args));
        }
        if (in_guard_ && not_a_tuple(term)) {
            return build_.atom_node(anno, "false"_atom);
        }
        const auto size = build_.integer_node(
            anno, static_cast<std::int64_t>(record_fields({.name = name, .anno = anno}).size()) + 1);
        if (in_guard_) {
            const std::array args{term, build_.atom_node(anno, name), size};
            return expr(build_.erlang_call(generated, "is_record"_atom, args));
        }
        const auto var = new_var(anno);
        const std::array args{var, build_.atom_node(anno, name), size};
        const std::array body{build_.node("match"_atom, anno, var, term),
                              build_.erlang_call(generated, "is_record"_atom, args)};
        return expr(build_.node("block"_atom, anno, build_.list(body)));
    }

    static bool not_a_tuple(const Term &term) {
        const auto tag = tag_of(term);
        const auto size = term.is_tuple() ? term.tuple_size_unchecked() : 0;
        if (size == 3 && forms::is_one_of(tag, {"atom", "integer", "float", "char", "string", "bin"})) {
            return true;
        }
        return (size == 2 && tag == "nil") || (size == 4 && forms::is_one_of(tag, {"cons", "record_index"})) ||
               (tag == "op" && (size == 4 || size == 5));
    }

    // Expressions

    Term exprs(const Term &list) {
        return each(list, [&](const Term &expr) { return this->expr(expr); });
    }

    Term expr(const Term &node) {
        static constexpr auto RULES = std::to_array<Rule>({
            {"var", 3, &Expander::same},
            {"char", 3, &Expander::same},
            {"integer", 3, &Expander::same},
            {"float", 3, &Expander::same},
            {"atom", 3, &Expander::same},
            {"string", 3, &Expander::same},
            {"nil", 2, &Expander::same},
            {"executable_line", 3, &Expander::same},
            {"debug_line", 3, &Expander::same},
            {"ssa_check_when", 7, &Expander::same},
            {"cons", 4, &Expander::cons_expr},
            {"lc", 4, &Expander::comprehension},
            {"bc", 4, &Expander::comprehension},
            {"mc", 4, &Expander::comprehension},
            {"tuple", 3, &Expander::tuple_expr},
            {"map", 3, &Expander::tuple_expr},
            {"block", 3, &Expander::tuple_expr},
            {"maybe", 3, &Expander::tuple_expr},
            {"map", 4, &Expander::map_update},
            {"map_field_assoc", 4, &Expander::map_field},
            {"map_field_exact", 4, &Expander::map_field},
            {"record_index", 4, &Expander::record_index},
            {"record", 4, &Expander::record_construction},
            {"record", 5, &Expander::record_update_expr},
            {"record_field", 5, &Expander::record_access},
            {"record_field", 4, &Expander::field_expr},
            {"bin", 3, &Expander::bin_expr},
            {"if", 3, &Expander::if_expr},
            {"receive", 3, &Expander::if_expr},
            {"case", 4, &Expander::case_expr},
            {"receive", 5, &Expander::receive_after},
            {"fun", 3, &Expander::fun_expr},
            {"named_fun", 4, &Expander::named_fun},
            {"call", 4, &Expander::call},
            {"try", 6, &Expander::try_expr},
            {"catch", 3, &Expander::catch_expr},
            {"maybe", 4, &Expander::maybe_else},
            {"match", 4, &Expander::match_expr},
            {"maybe_match", 4, &Expander::match_expr},
            {"op", 4, &Expander::unary},
            {"op", 5, &Expander::binary},
        });
        return dispatch(RULES, node);
    }

    Term cons_expr(const Term &node) {
        return spine(node, [&](const Term &item) { return expr(item); });
    }

    // {lc, A, E, Qs}, {bc, ...}, {mc, ...}: the qualifiers first, then the template.
    Term comprehension(const Term &node) {
        const auto qualifiers = this->qualifiers(element(node, 3));
        const auto template_ = element(node, 2);
        const auto expanded =
            !is_atom(element(node, 0), "bc") && template_.is_list() ? exprs(template_) : expr(template_);
        return build_.node(element(node, 0).atom_spelling_unchecked(), element(node, 1), expanded, qualifiers);
    }

    // {tuple, A, Es}, {map, A, Es}, {block, A, Es}, {maybe, A, Es}.
    Term tuple_expr(const Term &node) { return forms::with_element(build_, node, 2, exprs(element(node, 2))); }

    Term map_update(const Term &node) {
        const auto map = expr(element(node, 2));
        const auto fields = exprs(element(node, 3));
        return build_.node("map"_atom, element(node, 1), map, fields);
    }

    // {map_field_assoc, A, K, V} and {map_field_exact, A, K, V}.
    Term map_field(const Term &node) {
        const auto key = expr(element(node, 2));
        const auto value = expr(element(node, 3));
        return build_.node(element(node, 0).atom_spelling_unchecked(), element(node, 1), key, value);
    }

    Term field_expr(const Term &node) { return forms::with_element(build_, node, 3, expr(element(node, 3))); }

    Term bin_expr(const Term &node) {
        return segments(node, [&](const Term &value) { return expr(value); });
    }

    // {'if', A, Cs} and {'receive', A, Cs}.
    Term if_expr(const Term &node) { return forms::with_element(build_, node, 2, clauses(element(node, 2))); }

    Term case_expr(const Term &node) {
        const auto subject = expr(element(node, 2));
        const auto clauses = this->clauses(element(node, 3));
        return build_.node("case"_atom, element(node, 1), subject, clauses);
    }

    Term receive_after(const Term &node) {
        const auto timeout = expr(element(node, 3));
        const auto after = exprs(element(node, 4));
        const auto clauses = this->clauses(element(node, 2));
        return build_.node("receive"_atom, element(node, 1), clauses, timeout, after);
    }

    Term fun_expr(const Term &node) {
        const auto anno = element(node, 1);
        const auto body = element(node, 2);
        if (is_node(body, "function", 3)) {
            return local_fun(node);
        }
        if (is_node(body, "function", 4)) {
            return node;
        }
        if (!is_node(body, "clauses", 2)) {
            lists::no_clause();
        }
        const auto clauses = outside_guard([&] { return this->clauses(element(body, 1)); });
        return build_.node("fun"_atom, anno, forms::with_element(build_, body, 1, clauses));
    }

    Term named_fun(const Term &node) {
        const auto clauses = outside_guard([&] { return this->clauses(element(node, 3)); });
        return forms::with_element(build_, node, 3, clauses);
    }

    // A body expanded outside guard context (a fun inside a guard).
    template <typename Walk> Term outside_guard(Walk &&walk) {
        const auto outer = in_guard_;
        in_guard_ = false;
        const auto result = walk();
        in_guard_ = outer;
        return result;
    }

    Term try_expr(const Term &node) {
        const auto body = exprs(element(node, 2));
        const auto cases = clauses(element(node, 3));
        const auto catches = clauses(element(node, 4));
        const auto after = exprs(element(node, 5));
        return build_.node("try"_atom, element(node, 1), body, cases, catches, after);
    }

    Term catch_expr(const Term &node) { return forms::with_element(build_, node, 2, expr(element(node, 2))); }

    Term maybe_else(const Term &node) {
        const auto body = exprs(element(node, 2));
        const auto otherwise = element(node, 3);
        if (!is_node(otherwise, "else", 3)) {
            lists::no_clause();
        }
        const auto clauses = this->clauses(element(otherwise, 2));
        return build_.node("maybe"_atom, element(node, 1), body, forms::with_element(build_, otherwise, 2, clauses));
    }

    // {match, A, P, E} and {maybe_match, A, P, E}: the expression first.
    Term match_expr(const Term &node) {
        const auto value = expr(element(node, 3));
        const auto pattern = this->pattern(element(node, 2));
        return build_.node(element(node, 0).atom_spelling_unchecked(), element(node, 1), pattern, value);
    }

    Term unary(const Term &node) {
        const auto operand = is_atom(element(node, 2), "not") ? bool_operand(element(node, 3)) : expr(element(node, 3));
        return forms::with_element(build_, node, 3, operand);
    }

    Term binary(const Term &node) {
        const auto op = element(node, 2);
        if (is_atom(op, "and") || is_atom(op, "or")) {
            const auto left = bool_operand(element(node, 3));
            const auto right = bool_operand(element(node, 4));
            return build_.node("op"_atom, element(node, 1), op, left, right);
        }
        if (is_atom(op, "andalso") || is_atom(op, "orelse")) {
            // Tests added for the right operand do not count as made after it.
            const auto left = bool_operand(element(node, 3));
            auto made = checked_;
            const auto right = bool_operand(element(node, 4));
            checked_ = std::move(made);
            return build_.node("op"_atom, element(node, 1), op, left, right);
        }
        const auto left = expr(element(node, 3));
        const auto right = expr(element(node, 4));
        return build_.node("op"_atom, element(node, 1), op, left, right);
    }

    // fun F/A: kept for a local function, M:F/A for an imported one or a BIF, else a fun calling F.
    Term local_fun(const Term &fun) {
        const auto anno = element(fun, 1);
        const auto name = element(element(fun, 2), 1);
        const auto arity = element(element(fun, 2), 2);
        const auto count = string::small_of(arity).value_or(-1);
        const auto found = calls_.find({name.word(), count});
        if (found != calls_.end() && !found->second.module) {
            return fun;
        }
        std::optional<Term> module;
        if (found != calls_.end()) {
            module = found->second.module;
        } else if (name.is_atom() && is_bif(name.atom_spelling_unchecked(), count)) {
            module = build_.atom("erlang"_atom);
        }
        if (module) {
            const std::array function{build_.atom("function"_atom), build_.atom_node(anno, *module),
                                      build_.atom_node(anno, name), build_.node("integer"_atom, anno, arity)};
            return build_.node("fun"_atom, anno, build_.tuple(function));
        }
        std::vector<Term> vars(static_cast<std::size_t>(std::max<std::int64_t>(count, 0)));
        for (auto index = vars.size(); index > 0; --index) {
            vars[index - 1] = new_var(anno);
        }
        const auto params = build_.list(vars);
        const std::array body{build_.node("call"_atom, anno, build_.atom_node(anno, name), params)};
        const std::array clause{build_.node("clause"_atom, anno, params, build_.nil(), build_.list(body))};
        const std::array clauses{build_.atom("clauses"_atom), build_.list(clause)};
        return expr(build_.node("fun"_atom, anno, build_.tuple(clauses)));
    }

    Term call(const Term &node) {
        const auto anno = element(node, 1);
        const auto callee = element(node, 2);
        const auto args = forms::elements(element(node, 3));
        if (args.size() == 2 && is_record_function(callee, true) && is_node(args[1], "atom", 3)) {
            return record_test(anno, args[0], element(args[1], 2));
        }
        if (args.size() == 3 && is_record_function(callee, false) && is_empty_size(args[2])) {
            return build_.atom_node(anno, "false"_atom);
        }
        if (is_node(callee, "atom", 3)) {
            return local_call(node);
        }
        if (is_node(callee, "remote", 4)) {
            return remote_call(node);
        }
        const auto function = expr(callee);
        return build_.node("call"_atom, anno, function, exprs(element(node, 3)));
    }

    // {integer, _, Size} with Size =< 0: is_record/3 of such a size is false.
    bool is_empty_size(const Term &size) const {
        return is_node(size, "integer", 3) && element(size, 2).is_integer() &&
               collections::compare(element(size, 2), build_.integer(0)) <= 0;
    }

    Term remote_call(const Term &node) {
        const auto callee = element(node, 2);
        const auto module = expr(element(callee, 2));
        const auto function = expr(element(callee, 3));
        const auto expanded = exprs(element(node, 3));
        return build_.node("call"_atom, element(node, 1),
                           build_.node("remote"_atom, element(callee, 1), module, function), expanded);
    }

    // is_record as a local call or erlang:is_record (also as an {erlang, is_record} tuple when `tuple` is set).
    static bool is_record_function(const Term &callee, bool tuple) {
        const auto is_name = [](const Term &node, std::string_view name) {
            return is_node(node, "atom", 3) && is_atom(element(node, 2), name);
        };
        if (is_name(callee, "is_record")) {
            return true;
        }
        if (is_node(callee, "remote", 4)) {
            return is_name(element(callee, 2), "erlang") && is_name(element(callee, 3), "is_record");
        }
        if (!tuple || !is_node(callee, "tuple", 3)) {
            return false;
        }
        const auto items = forms::elements(element(callee, 2));
        return items.size() == 2 && is_name(items[0], "erlang") && is_name(items[1], "is_record");
    }

    // F(Args): record_info/2, a local function, an imported function or a BIF (which gets erlang:).
    Term local_call(const Term &node) {
        const auto anno = element(node, 1);
        const auto callee = element(node, 2);
        const auto name = element(callee, 2);
        const auto args = exprs(element(node, 3));
        const auto arity = static_cast<std::int64_t>(forms::elements(args).size());
        if (is_atom(name, "record_info") && arity == 2) {
            return record_info(anno, forms::elements(args));
        }
        const auto found = calls_.find({name.word(), arity});
        std::optional<Term> module;
        if (found != calls_.end()) {
            module = found->second.module;
        } else if (name.is_atom() && is_bif(name.atom_spelling_unchecked(), arity)) {
            module = build_.atom("erlang"_atom);
        }
        const auto function =
            module ? build_.node("remote"_atom, anno, build_.atom_node(anno, *module), callee) : callee;
        return build_.node("call"_atom, anno, function, args);
    }

    // An operand of a boolean operator with the record tests its field accesses need.
    Term bool_operand(const Term &node) { return strict_record_access(expr(node)); }

    // The record tests needed and not made yet, joined to `node` as (Test orelse fail) and ... and node.
    Term strict_record_access(const Term &node) {
        if (check_needed_.empty()) {
            return node;
        }
        std::vector<RecordCheck> fresh;
        for (auto index = check_needed_.size(); index > 0; --index) {
            const auto &test = check_needed_[index - 1];
            const auto made = std::ranges::any_of(
                checked_, [&](const RecordCheck &done) { return collections::same(done.key, test.key); });
            if (!made) {
                fresh.insert(fresh.begin(), test);
                checked_.push_back(test);
            }
        }
        check_needed_.clear();
        const auto joined = fresh.empty() ? node : *conjunction(fresh, node);
        return expr(joined);
    }

    std::optional<Term> conjunction(std::span<const RecordCheck> tests, const std::optional<Term> &node) {
        if (tests.empty()) {
            return std::nullopt;
        }
        const auto &first = tests.front();
        const auto failing = build_.node("op"_atom, first.anno, build_.atom("orelse"_atom), first.test,
                                         build_.atom_node(first.anno, "fail"_atom));
        const auto rest = conjunction(tests.subspan(1), std::nullopt);
        const auto joined = rest ? op_and(first.anno, *rest, failing) : failing;
        if (node) {
            return op_and(first.anno, joined, *node);
        }
        return rest ? joined : op_and(first.anno, joined, build_.atom_node(first.anno, "true"_atom));
    }

    Term op_and(const Term &anno, const Term &left, const Term &right) const {
        return build_.node("op"_atom, anno, build_.atom("and"_atom), left, right);
    }

    Term qualifiers(const Term &list) {
        const auto out = each(list, [&](const Term &qualifier) { return this->qualifier(qualifier); });
        checked_.clear();
        return out;
    }

    Term qualifier(const Term &node) {
        const auto tag = tag_of(node);
        if (node.is_tuple() && node.tuple_size_unchecked() == 4 &&
            forms::is_one_of(tag, {"generate", "generate_strict", "b_generate", "b_generate_strict"})) {
            const auto generator = expr(element(node, 3));
            const auto pattern = this->pattern(element(node, 2));
            return build_.node(tag, element(node, 1), pattern, generator);
        }
        if (node.is_tuple() && node.tuple_size_unchecked() == 4 &&
            forms::is_one_of(tag, {"m_generate", "m_generate_strict"}) &&
            is_node(element(node, 2), "map_field_exact", 4)) {
            const auto field = element(node, 2);
            const auto generator = expr(element(node, 3));
            const auto key = pattern(element(field, 2));
            const auto value = pattern(element(field, 3));
            return build_.node(tag, element(node, 1),
                               build_.node("map_field_exact"_atom, element(field, 1), key, value), generator);
        }
        if (is_node(node, "zip", 3)) {
            return forms::with_element(build_, node, 2, qualifiers(element(node, 2)));
        }
        // A filter that is a guard test (record/2 included) is expanded as one.
        const GuardScope scope{.records = &raw_records_, .overridden = &overridden_};
        return is_guard_test(build_.context(), node, scope) ? guard_test(node) : expr(node);
    }

    // Records

    std::optional<Native> native_of(const Term &name) const {
        const auto found = name.is_atom() ? natives_.find(name.word()) : natives_.end();
        return found == natives_.end() ? std::nullopt : std::optional{found->second};
    }

    // {Module, Name} of a native record.
    Term module_name(const Term &module, const Term &name) const {
        const std::array items{module, name};
        return build_.tuple(items);
    }

    // Every field with a default: undefined when the definition gives none.
    std::vector<Term> normalise_fields(const Term &definitions) const {
        std::vector<Term> fields;
        for (const auto &field : forms::elements(definitions)) {
            auto plain = is_node(field, "typed_record_field", 3) ? element(field, 1) : field;
            if (is_node(plain, "record_field", 3)) {
                const auto anno = element(plain, 1);
                plain =
                    build_.node("record_field"_atom, anno, element(plain, 2), build_.atom_node(anno, "undefined"_atom));
            }
            fields.push_back(plain);
        }
        return fields;
    }

    // The fields of a tuple record, their defaults copied with annotation `anno`.
    std::vector<Term> record_fields(const RecordAt &at) const {
        const auto &anno = at.anno;
        const auto found = records_.find(at.name.word());
        if (found == records_.end()) {
            const std::array reason{build_.atom("badkey"_atom), at.name};
            raise_error(build_.tuple(reason));
        }
        std::vector<Term> fields;
        for (const auto &field : found->second) {
            if (is_node(field, "record_field", 4) && is_node(element(field, 2), "atom", 3)) {
                fields.push_back(build_.node("record_field"_atom, anno,
                                             build_.atom_node(anno, element(element(field, 2), 2)),
                                             copy(element(field, 3), anno)));
            }
        }
        return fields;
    }

    Term copy(const Term &node, const Term &anno) const {
        return abstract::replace_annos(build_.context(), node, {anno});
    }

    // The value given for field `name`.
    static std::optional<Term> find_field(const Term &name, const std::vector<Term> &fields) {
        for (const auto &field : fields) {
            if (is_node(field, "record_field", 4) && is_node(element(field, 2), "atom", 3) &&
                element(element(field, 2), 2).word() == name.word()) {
                return element(field, 3);
            }
        }
        return std::nullopt;
    }

    static Term field_name(const Term &field) { return element(element(field, 2), 2); }

    Term index_expr(const Term &anno, const Term &field, const std::vector<Term> &fields) const {
        return build_.integer_node(anno, field_number(field, fields));
    }

    // The tuple index of the field an {atom, _, F} node names.
    static std::int64_t field_number(const Term &field, const std::vector<Term> &fields) {
        if (!is_node(field, "atom", 3)) {
            lists::no_clause();
        }
        return field_index(element(field, 2), fields);
    }

    static std::int64_t field_index(const Term &name, const std::vector<Term> &fields) {
        for (std::size_t index = 0; index < fields.size(); ++index) {
            if (field_name(fields[index]).word() == name.word()) {
                return static_cast<std::int64_t>(index) + 2;
            }
        }
        lists::no_clause();
    }

    // The value of the first _ = Value field.
    static std::optional<Term> wildcard(const std::vector<Term> &given) {
        for (const auto &field : given) {
            if (is_node(field, "record_field", 4) && is_node(element(field, 2), "var", 3) &&
                is_atom(element(element(field, 2), 2), "_")) {
                return element(field, 3);
            }
        }
        return std::nullopt;
    }

    // The patterns of a tuple record's elements: the given ones, else _ = Pattern's, else _.
    std::vector<Term> field_patterns(const std::vector<Term> &fields, const Term &given_list) const {
        const auto given = forms::elements(given_list);
        const auto any = wildcard(given);
        std::vector<Term> out;
        for (const auto &field : fields) {
            const auto found = find_field(field_name(field), given);
            out.push_back(found ? *found : any ? *any : build_.var_node(element(field, 1), "_"_atom));
        }
        return out;
    }

    // The initial values of a tuple record's elements: the given ones, else _ = Value's, else the defaults.
    static std::vector<Term> field_inits(const std::vector<Term> &fields, const Term &given_list) {
        const auto given = forms::elements(given_list);
        const auto any = wildcard(given);
        std::vector<Term> out;
        for (const auto &field : fields) {
            const auto found = find_field(field_name(field), given);
            out.push_back(found ? *found : any ? *any : element(field, 3));
        }
        return out;
    }

    Term record_construction(const Term &node) {
        const auto anno = element(node, 1);
        const auto id = element(node, 2);
        if (id.is_tuple() && id.tuple_size_unchecked() == 2) {
            return is_atom(element(id, 0), "shell_default") ? shell_construction(node) : native_construction(node);
        }
        if (!id.is_atom()) {
            lists::no_clause();
        }
        const auto native = native_of(id);
        if (native && native->local) {
            const auto expanded =
                expanded_inits({.anno = anno, .name = id, .definitions = native->value, .inits = element(node, 3)});
            return expanded ? expr(*expanded) : native_construction(node);
        }
        if (native) {
            return expr(forms::with_element(build_, node, 2, module_name(native->value, id)));
        }
        auto items = field_inits(record_fields({.name = id, .anno = anno}), element(node, 3));
        items.insert(items.begin(), build_.atom_node(anno, id));
        return expr(build_.node("tuple"_atom, mark_record(anno), build_.list(items)));
    }

    Term native_construction(const Term &node) { return forms::with_element(build_, node, 3, exprs(element(node, 3))); }

    Term shell_construction(const Term &node) {
        const auto name = element(element(node, 2), 1);
        const auto native = native_of(name);
        if (!native || !native->local) {
            raise_error(build_.atom("badmatch"_atom));
        }
        const auto expanded = expanded_inits(
            {.anno = element(node, 1), .name = name, .definitions = native->value, .inits = element(node, 3)});
        if (!expanded) {
            raise_error(build_.atom("badmatch"_atom));
        }
        return native_construction(*expanded);
    }

    Term record_update_expr(const Term &node) {
        const auto anno = element(node, 1);
        const auto source = element(node, 2);
        const auto id = element(node, 3);
        const auto native = native_of(id);
        if (!id.is_atom() || (native && native->local)) {
            if (!id.is_atom() && !id.is_nil() && !(id.is_tuple() && id.tuple_size_unchecked() == 2)) {
                lists::no_clause();
            }
            const auto first = abstract::first_anno(build_.context(), source);
            const auto expanded = expr(source);
            const auto updates = exprs(element(node, 4));
            return build_.node("record"_atom, first, expanded, id, updates);
        }
        if (native) {
            return expr(forms::with_element(build_, node, 3, module_name(native->value, id)));
        }
        return expr(
            record_update(source, {.name = id, .fields = record_fields({.name = id, .anno = anno})}, element(node, 4)));
    }

    Term record_access(const Term &node) {
        const auto source = element(node, 2);
        const auto id = element(node, 3);
        const auto native = native_of(id);
        if (!id.is_atom() || (native && native->local)) {
            const auto expanded = expr(source);
            return forms::with_element(build_, forms::with_element(build_, node, 2, expanded), 1,
                                       abstract::first_anno(build_.context(), expanded));
        }
        if (native) {
            return expr(forms::with_element(build_, node, 3, module_name(native->value, id)));
        }
        return get_field({.anno = abstract::first_anno(build_.context(), source),
                          .record = source,
                          .field = element(node, 4),
                          .name = id});
    }

    // The fields of a native record construction completed from its definition: badfield for an unknown field,
    // novalue for one without a default (dropped with dialyzer). With neither option the construction stays.
    std::optional<Term> expanded_inits(const NativeConstruction &construction) {
        const auto dialyzer = options_.dialyzer && construction.definitions.is_list();
        if (!dialyzer && !options_.expand_inits) {
            return std::nullopt;
        }
        auto fields = native_inits(construction);
        if (dialyzer) {
            std::erase_if(fields, [](const Term &field) { return is_missing(element(field, 3)); });
        }
        return build_.node("record"_atom, construction.anno, module_name(module_, construction.name),
                           build_.list(fields));
    }

    static bool is_missing(const Term &value) {
        return is_node(value, "nil", 2) &&
               (is_atom(element(value, 1), "novalue") || is_atom(element(value, 1), "badfield"));
    }

    static Term untyped(const Term &definition) {
        return is_node(definition, "typed_record_field", 3) ? element(definition, 1) : definition;
    }

    // The definition's fields without their types: record_field nodes named by atoms.
    static std::vector<Term> untyped_fields(const Term &definitions) {
        std::vector<Term> fields;
        for (const auto &raw : forms::elements(definitions)) {
            const auto definition = untyped(raw);
            const auto plain = is_node(definition, "record_field", 4) || is_node(definition, "record_field", 3);
            if (!plain || !is_node(element(definition, 2), "atom", 3)) {
                lists::no_clause();
            }
            fields.push_back(definition);
        }
        return fields;
    }

    // The sorted names of the given fields the definition does not have.
    static std::vector<Term> unknown_names(const std::vector<Term> &definitions,
                                           const NativeConstruction &construction) {
        std::vector<Term> unknown;
        for (const auto &field : forms::elements(construction.inits)) {
            const auto known = [&](const Term &definition) {
                return field_name(definition).word() == field_name(field).word();
            };
            if (is_node(field, "record_field", 4) && is_node(element(field, 2), "atom", 3) &&
                std::ranges::none_of(definitions, known)) {
                unknown.push_back(field_name(field));
            }
        }
        std::ranges::sort(unknown,
                          [](const Term &left, const Term &right) { return collections::compare(left, right) < 0; });
        const auto [first, last] = std::ranges::unique(
            unknown, [](const Term &left, const Term &right) { return left.word() == right.word(); });
        unknown.erase(first, last);
        return unknown;
    }

    std::vector<Term> native_inits(const NativeConstruction &construction) const {
        const auto &anno = construction.anno;
        const auto defaults = untyped_fields(construction.definitions);
        const auto unknown = unknown_names(defaults, construction);
        const auto given = forms::elements(construction.inits);
        std::vector<Term> out;
        out.reserve(unknown.size() + defaults.size());
        for (const auto &name : unknown) {
            out.push_back(
                build_.node("record_field"_atom, anno, build_.atom_node(anno, name), missing("badfield"_atom)));
        }
        for (const auto &definition : defaults) {
            out.push_back(native_init(anno, definition, given));
        }
        return out;
    }

    // One field of a native record: the given value, else the default (copied with `anno`) or novalue.
    Term native_init(const Term &anno, const Term &definition, const std::vector<Term> &given) const {
        const auto name = field_name(definition);
        const auto value = find_field(name, given);
        if (is_node(definition, "record_field", 4)) {
            const auto field = build_.node("record_field"_atom, anno, build_.atom_node(anno, name),
                                           copy(element(definition, 3), anno));
            return build_.node("record_field"_atom, anno, element(field, 2), value ? *value : element(field, 3));
        }
        if (!is_node(definition, "record_field", 3)) {
            lists::no_clause();
        }
        return build_.node("record_field"_atom, element(definition, 1), element(definition, 2),
                           value ? *value : missing("novalue"_atom));
    }

    // {nil, Reason}: a field without a value.
    Term missing(AtomName reason) const {
        const std::array items{build_.atom("nil"_atom), build_.atom(reason)};
        return build_.tuple(items);
    }

    // Rec#Name.F: in a body a case binding the field (badrecord otherwise), in a guard element/2 with an is_record/3
    // test to join to the guard test; without strict record tests element/2 alone.
    Term get_field(const FieldAccess &access) {
        const auto &anno = access.anno;
        const auto &name = access.name;
        if (!options_.strict) {
            const auto index = field_number(access.field, record_fields({.name = name, .anno = anno}));
            return expr(element_call(anno, index, access.record));
        }
        if (!in_guard_) {
            return body_field(access);
        }
        const auto fields = record_fields({.name = name, .anno = anno});
        const auto index = field_number(access.field, fields);
        const auto expanded = expr(access.record);
        const std::array key{name, abstract::replace_annos(build_.context(), expanded, {build_.integer(0)})};
        const auto generated = this->generated(anno);
        const std::array args{expanded, build_.atom_node(generated, name),
                              build_.integer_node(generated, static_cast<std::int64_t>(fields.size()) + 1)};
        check_needed_.push_back(RecordCheck{.key = build_.tuple(key),
                                            .anno = generated,
                                            .test = build_.erlang_call(generated, "is_record"_atom, args)});
        return element_call(anno, index, expanded);
    }

    Term body_field(const FieldAccess &access) {
        const auto &anno = access.anno;
        const auto &name = access.name;
        if (!is_node(access.field, "atom", 3)) {
            lists::no_clause();
        }
        const auto var = new_var(anno);
        const auto fields = record_fields({.name = name, .anno = anno});
        const auto index = field_index(element(access.field, 2), fields);
        const auto elements = element_pattern(index, var, static_cast<std::int64_t>(fields.size()) + 1, anno, name);
        const auto generated = this->generated(anno);
        const std::array first{build_.node("tuple"_atom, mark_record(generated), build_.list(elements))};
        const std::array any{var};
        const std::array failure{badrecord(generated, var)};
        const std::array clauses{
            build_.node("clause"_atom, generated, build_.list(first), build_.nil(), build_.list(any)),
            build_.node("clause"_atom, generated, build_.list(any), build_.nil(), build_.list(failure))};
        return expr(build_.node("case"_atom, anno, access.record, build_.list(clauses)));
    }

    // erlang:element(Index, Record).
    Term element_call(const Term &anno, std::int64_t index, const Term &record) const {
        const std::array args{build_.integer_node(anno, index), record};
        return build_.erlang_call(anno, "element"_atom, args);
    }

    // The elements of a record pattern: {atom, Anno, Name}, then `var` at `index` and _ elsewhere up to `size`.
    std::vector<Term> element_pattern(std::int64_t index, const Term &var, std::int64_t size, const Term &anno,
                                      const Term &name) const {
        std::vector<Term> elements{build_.atom_node(anno, name)};
        for (std::int64_t at = 2; at <= size; ++at) {
            elements.push_back(at == index ? var : build_.var_node(anno, "_"_atom));
        }
        return elements;
    }

    Term badrecord(const Term &anno, const Term &term) const {
        const std::array reason{build_.atom_node(anno, "badrecord"_atom), term};
        const std::array args{build_.node("tuple"_atom, anno, build_.list(reason))};
        return build_.erlang_call(anno, "error"_atom, args);
    }

    // Rec#Name{Updates}: the update values that are not simple bound first, the record bound to a new variable, then
    // a setelement/3 chain (or, with dialyzer or no updates, a case rebuilding the tuple).
    Term record_update(const Term &record, const RecordUse &use, const Term &update_list) {
        const auto anno = element(record, 1);
        std::vector<Term> body;
        std::vector<Term> updates;
        for (const auto &update : forms::elements(update_list)) {
            if (!is_node(update, "record_field", 4) || !is_node(element(update, 2), "atom", 3)) {
                lists::no_clause();
            }
            if (is_simple(element(update, 3))) {
                updates.insert(updates.begin(), update);
                continue;
            }
            const auto var = new_var(element(update, 1));
            body.push_back(build_.node("match"_atom, element(update, 1), var, element(update, 3)));
            updates.insert(updates.begin(), forms::with_element(build_, update, 3, var));
        }
        const auto var = new_var(anno);
        body.push_back(build_.node("match"_atom, anno, var, record));
        body.push_back(!options_.dialyzer && !updates.empty() ? setelement_chain(var, use, updates)
                                                              : rebuild_case(var, use, anno, updates));
        return build_.node("block"_atom, anno, build_.list(body));
    }

    Term rebuild_case(const Term &record, const RecordUse &use, const Term &anno, const std::vector<Term> &updates) {
        std::vector<Term> patterns{build_.atom_node(anno, use.name)};
        std::vector<Term> values{build_.atom_node(anno, use.name)};
        for (const auto &field : use.fields) {
            const auto var = new_var(element(field, 1));
            patterns.push_back(var);
            values.push_back(find_field(field_name(field), updates).value_or(var));
        }
        const auto marked = mark_record(anno);
        const std::array head{build_.node("tuple"_atom, marked, build_.list(patterns))};
        const std::array result{build_.node("tuple"_atom, marked, build_.list(values))};
        const std::array clauses{build_.node("clause"_atom, anno, build_.list(head), build_.nil(), build_.list(result)),
                                 failing_clause(record, generated(anno))};
        return build_.node("case"_atom, anno, record, build_.list(clauses));
    }

    // _ -> error({badrecord, Record}), the last clause of an update's case.
    Term failing_clause(const Term &record, const Term &generated) const {
        const std::array any{build_.var_node(generated, "_"_atom)};
        const std::array failure{badrecord(generated, record)};
        return build_.node("clause"_atom, generated, build_.list(any), build_.nil(), build_.list(failure));
    }

    Term setelement_chain(const Term &record, const RecordUse &use, const std::vector<Term> &updates) const {
        std::vector<Term> indexed;
        indexed.reserve(updates.size());
        for (const auto &update : updates) {
            const auto anno = element(update, 1);
            const auto index = index_expr(anno, element(update, 2), use.fields);
            const std::array value{index, anno, element(update, 3)};
            const std::array pair{element(index, 2), build_.tuple(value)};
            indexed.push_back(build_.tuple(pair));
        }
        std::ranges::sort(indexed,
                          [](const Term &left, const Term &right) { return collections::compare(left, right) < 0; });
        const auto anno = element(element(indexed.front(), 1), 1);
        auto chain = record;
        for (auto index = indexed.size(); index > 0; --index) {
            const auto update = element(indexed[index - 1], 1);
            const std::array args{element(update, 0), chain, element(update, 2)};
            chain = build_.erlang_call(element(update, 1), "setelement"_atom, args);
        }
        std::vector<Term> elements(use.fields.size() + 1, build_.var_node(anno, "_"_atom));
        elements.front() = build_.atom_node(anno, use.name);
        const std::array head{build_.node("tuple"_atom, anno, build_.list(elements))};
        const std::array body{chain};
        const std::array clauses{build_.node("clause"_atom, anno, build_.list(head), build_.nil(), build_.list(body)),
                                 failing_clause(record, generated(anno))};
        return build_.node("case"_atom, anno, record, build_.list(clauses));
    }

    // A variable or a literal erl_parse:normalise/1 accepts.
    bool is_simple(const Term &value) const {
        if (is_node(value, "var", 3)) {
            return true;
        }
        try {
            static_cast<void>(abstract::normalise(build_.context(), value));
            return true;
        } catch (const BuiltinFailure &) {
            return false;
        }
    }

    Term record_info(const Term &anno, const std::vector<Term> &args) const {
        if (!is_node(args[0], "atom", 3) || !is_node(args[1], "atom", 3)) {
            lists::no_clause();
        }
        const auto what = element(args[0], 2);
        if (!is_atom(what, "size") && !is_atom(what, "fields")) {
            lists::no_clause();
        }
        const auto fields = record_fields({.name = element(args[1], 2), .anno = anno});
        if (is_atom(what, "size")) {
            return build_.integer_node(anno, static_cast<std::int64_t>(fields.size()) + 1);
        }
        auto list = build_.node("nil"_atom, anno);
        for (auto index = fields.size(); index > 0; --index) {
            list = build_.node("cons"_atom, anno, element(fields[index - 1], 2), list);
        }
        return list;
    }

    Term new_var(const Term &anno) {
        const auto name = "rec" + std::to_string(vcount_++);
        return build_.node("var"_atom, anno, build_.atom(name));
    }

    Term generated(const Term &anno) const {
        return anno_set(build_.context(), "generated", build_.atom("true"_atom), anno);
    }

    Term mark_record(const Term &anno) const {
        return options_.dialyzer ? anno_set(build_.context(), "record", build_.atom("true"_atom), anno) : anno;
    }

    // is_record/3 tests that the whole guard needs become patterns: the variable matched against a tuple of the
    // record's name and size, the test replaced by true (not with dialyzer).
    HeadGuard record_tests_as_patterns(const HeadGuard &parts) {
        if (options_.dialyzer) {
            return parts;
        }
        auto tests = needed_tests(parts.guard);
        if (tests.empty()) {
            return parts;
        }
        const auto head = each(parts.head, [&](const Term &pattern) { return test_pattern(pattern, tests); });
        const auto guard = each(parts.guard, [&](const Term &conjunction) {
            return each(conjunction, [&](const Term &test) { return remove_tests(test, tests); });
        });
        return {.head = head, .guard = guard};
    }

    // The variables whose is_record/3 test every conjunction of the guard needs.
    static std::map<Word, VariableTest> needed_tests(const Term &guard) {
        const auto conjunctions = forms::elements(guard);
        std::map<Word, VariableTest> needed;
        for (std::size_t index = 0; index < conjunctions.size(); ++index) {
            std::map<Word, VariableTest> tests;
            for (const auto &test : forms::elements(conjunctions[index])) {
                needed_test(test, tests);
            }
            if (index == 0) {
                needed = std::move(tests);
            } else {
                std::erase_if(needed, [&](const auto &entry) { return !agrees(tests, {entry.first, entry.second}); });
            }
        }
        return needed;
    }

    // Whether `tests` has the same test as `entry` on its variable.
    static bool agrees(const std::map<Word, VariableTest> &tests, const std::pair<Word, VariableTest> &entry) {
        const auto found = tests.find(entry.first);
        return found != tests.end() && found->second.tag.word() == entry.second.tag.word() &&
               collections::same(found->second.size, entry.second.size);
    }

    // {atom, _, Name}.
    static bool is_atom_node(const Term &node, std::string_view name) {
        return is_node(node, "atom", 3) && is_atom(element(node, 2), name);
    }

    // {op, _, Op, L, R} for Op one of `names`.
    static bool is_operation(const Term &test, std::initializer_list<std::string_view> names) {
        return is_node(test, "op", 5) && element(test, 2).is_atom() &&
               forms::is_one_of(element(test, 2).atom_spelling_unchecked(), names);
    }

    static void needed_test(const Term &test, std::map<Word, VariableTest> &tests) {
        if (is_operation(test, {"and", "andalso"})) {
            needed_test(element(test, 3), tests);
            needed_test(element(test, 4), tests);
            return;
        }
        if (is_operation(test, {"orelse"}) && is_atom_node(element(test, 4), "fail")) {
            needed_test(element(test, 3), tests);
            return;
        }
        if (const auto found = record_test_of(test)) {
            const auto size = string::small_of(found->second.size);
            if (size && *size > 0 && *size < 100) {
                tests.insert_or_assign(found->first, found->second);
            }
        }
    }

    // is_record(Var, Tag, Size) as a local or erlang: call: Var's name and the test.
    static std::optional<std::pair<Word, VariableTest>> record_test_of(const Term &test) {
        if (!is_node(test, "call", 4) || !is_record_function(element(test, 2), false)) {
            return std::nullopt;
        }
        const auto args = forms::elements(element(test, 3));
        if (args.size() != 3 || !is_node(args[0], "var", 3) || !is_node(args[1], "atom", 3) ||
            !is_node(args[2], "integer", 3)) {
            return std::nullopt;
        }
        return std::pair{element(args[0], 2).word(),
                         VariableTest{.tag = element(args[1], 2), .size = element(args[2], 2)}};
    }

    Term test_pattern(const Term &pattern, std::map<Word, VariableTest> &tests) {
        if (is_node(pattern, "var", 3)) {
            const auto found = tests.find(element(pattern, 2).word());
            if (found == tests.end() || found->second.removed) {
                return pattern;
            }
            found->second.removed = true;
            const auto anno = element(pattern, 1);
            const auto size = string::small_of(found->second.size).value_or(1);
            const auto elements = element_pattern(-1, pattern, size, anno, found->second.tag);
            return build_.node("match"_atom, anno, build_.node("tuple"_atom, anno, build_.list(elements)), pattern);
        }
        if (is_node(pattern, "cons", 4) || is_node(pattern, "match", 4)) {
            const auto first = test_pattern(element(pattern, 2), tests);
            const auto second = test_pattern(element(pattern, 3), tests);
            return build_.node(element(pattern, 0).atom_spelling_unchecked(), element(pattern, 1), first, second);
        }
        if (is_node(pattern, "tuple", 3)) {
            return forms::with_element(build_, pattern, 2, each(element(pattern, 2), [&](const Term &item) {
                                           return test_pattern(item, tests);
                                       }));
        }
        return pattern;
    }

    Term remove_tests(const Term &test, const std::map<Word, VariableTest> &tests) {
        if (is_operation(test, {"and", "andalso"})) {
            const auto left = remove_tests(element(test, 3), tests);
            const auto right = remove_tests(element(test, 4), tests);
            return build_.node("op"_atom, element(test, 1), element(test, 2), left, right);
        }
        if (is_operation(test, {"orelse"})) {
            return forms::with_element(build_, test, 3, remove_tests(element(test, 3), tests));
        }
        const auto found = record_test_of(test);
        const auto known = found ? tests.find(found->first) : tests.end();
        const auto removed = known != tests.end() && known->second.removed && agrees(tests, *found);
        return removed ? build_.atom_node(element(test, 1), "true"_atom) : test;
    }

    forms::Builder build_;
    Options options_;
    // The number of the next recN variable.
    std::int64_t vcount_ = 0;
    // How each local name F/A is called, and the same names as a set for the guard checks.
    std::map<FunctionKey, CallKind> calls_;
    std::set<FunctionKey> overridden_;
    // Native records by name.
    std::map<Word, Native> natives_;
    // Tuple record definitions with every field given a default, and as written (the first definition of a name).
    std::map<Word, std::vector<Term>> records_;
    std::map<Word, Term> raw_records_;
    // The record tests a guard test still needs (oldest first) and those it made already.
    std::vector<RecordCheck> check_needed_;
    std::vector<RecordCheck> checked_;
    // The module name, '' before the module attribute.
    Term module_ = build_.atom(""_atom);
    // Whether a guard is being expanded.
    bool in_guard_ = false;
};

// erl_expand_records:module(Forms, Options).
Term module2(ProcessContext &context, const FormsArg &forms, const OptionsArg &options) {
    return Expander(context, options_of(options, forms)).module(forms.term);
}

constexpr std::array EXPAND_RECORDS_BUILTINS{
    typed_entry<module2>("erl_expand_records", "module"),
};
} // namespace
} // namespace clause::runtime::builtins::front_end

namespace clause::runtime {
std::span<const BuiltinEntry> expand_records_builtins() noexcept {
    return builtins::front_end::EXPAND_RECORDS_BUILTINS;
}
} // namespace clause::runtime
