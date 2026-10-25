#include "abstract.hpp"
#include "front_end.hpp"
#include "lint.hpp"
#include <algorithm>

// erl_lint's builtins (plan 21 step LIB34C, docs/builtins.md#erl-lint): module/1,2,3 through
// clause_erl_lint:module/4, the command checks erl_eval uses (exprs/2, exprs_opt/3, used_vars/2), the guard and
// pattern predicates, check_format_string/1,2 and the messages of format_error/1.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

// The roles of the arguments (typed.hpp's Named).
struct FormsRole;
struct FileRole;
struct OptionsRole;
struct BehavioursRole;
struct ExprsRole;
struct BindingsRole;
struct ExprRole;
struct StrictRole;
using FormsArg = Named<FormsRole>;
using FileArg = Named<FileRole>;
using OptionsArg = Named<OptionsRole>;
using BehavioursArg = Named<BehavioursRole>;
using ExprsArg = Named<ExprsRole>;
using BindingsArg = Named<BindingsRole>;
using ExprArg = Named<ExprRole>;
using StrictArg = Named<StrictRole>;

// set_file/2: every annotation of a list of forms or expressions given the file.
Term with_file(ProcessContext &context, const Term &nodes, const FileArg &file) {
    return abstract::map_annos(context, nodes,
                               [&](const Term &anno) { return anno_set(context, "file", file.term, anno); });
}

// The {Variable, Value} bindings of a bindings list: each bound and unused, as orddict:from_list/1 sorts them.
VarTable binding_table(const Term &bindings) {
    VarTable table;
    for (const auto &binding : forms::elements(bindings)) {
        if (!binding.is_tuple() || binding.tuple_size_unchecked() != 2) {
            lists::no_clause();
        }
        const auto name = element(binding, 0);
        if (is_node(name, "record", 3) || !name.is_atom()) {
            continue;
        }
        const auto place =
            std::ranges::lower_bound(table, name.atom_spelling_unchecked(), {},
                                     [](const VarEntry &entry) { return entry.name.atom_spelling_unchecked(); });
        if (place != table.end() && place->name.word() == name.word()) {
            continue;
        }
        table.insert(place, {.name = name, .info = {}});
    }
    return table;
}

Term module4(ProcessContext &context, const FormsArg &forms, const FileArg &file, const OptionsArg &options,
             const BehavioursArg &behaviours) {
    TermMap<BehaviourInfo> infos;
    for (const auto &entry : forms::elements(behaviours.term)) {
        infos.insert_or_assign(element(entry, 0),
                               BehaviourInfo{.callbacks = element(entry, 1), .optional_callbacks = element(entry, 2)});
    }
    Lint lint(context, file.term, {options.term});
    lint.module(forms.term, infos);
    return lint.status();
}

Term exprs_opt(ProcessContext &context, const ExprsArg &exprs, const BindingsArg &bindings, const OptionsArg &options) {
    forms::Builder build(context);
    Lint lint(context, build.terms().list(std::vector<Term>{}), {options.term});
    lint.file_ = lint.chars("nofile");
    lint.command(exprs.term, {bindings.term});
    return lint.status();
}

Term exprs2(ProcessContext &context, const ExprsArg &exprs, const BindingsArg &bindings) {
    return exprs_opt(context, exprs, bindings, {forms::Builder(context).nil()});
}

Term used_vars2(ProcessContext &context, const ExprsArg &exprs, const BindingsArg &bindings) {
    forms::Builder build(context);
    Lint lint(context, build.nil(), {build.nil()});
    lint.file_ = lint.chars("nofile");
    return lint.used_vars(exprs.term, bindings.term);
}

Term is_pattern_expr1(ProcessContext &context, const ExprArg &expr) {
    return string::Terms(context).boolean(is_pattern_expr(context, expr.term));
}

Term is_guard_test1(ProcessContext &context, const ExprArg &expr) {
    return string::Terms(context).boolean(front_end::is_guard_test(context, expr.term, {}));
}

Term is_guard_expr1(ProcessContext &context, const ExprArg &expr) {
    return string::Terms(context).boolean(front_end::is_guard_expr(context, expr.term, {}));
}

// clause_erl_lint:is_guard_test(Expr, Forms, Overridden): is_guard_test/3 with the record definitions of Forms and
// the local calls erl_lint.erl found overridden.
Term is_guard_test3(ProcessContext &context, const ExprArg &expr, const FormsArg &forms,
                    const BindingsArg &overridden) {
    std::map<Word, Term> records;
    for (const auto &form : forms::elements(forms.term)) {
        if (is_node(form, "attribute", 4) && is_atom(element(form, 2), "record")) {
            const auto value = element(form, 3);
            if (value.is_tuple() && value.tuple_size_unchecked() == 2) {
                records.emplace(element(value, 0).word(), element(value, 1));
            }
        }
    }
    std::set<FunctionKey> keys;
    for (const auto &function : forms::elements(overridden.term)) {
        keys.insert({element(function, 0).word(), string::small_of(element(function, 1)).value_or(-1)});
    }
    return string::Terms(context).boolean(
        front_end::is_guard_test(context, expr.term, {.records = &records, .overridden = &keys}));
}

// clause_erl_lint:local_calls(Expr): the {F, A} of the local calls in an expression, for is_guard_test/3.
Term local_calls1(ProcessContext &context, const ExprArg &expr) {
    forms::Builder build(context);
    TermSet calls;
    std::vector<Term> pending{expr.term};
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        if (node.is_cons()) {
            pending.push_back(node.head_unchecked());
            pending.push_back(node.tail_unchecked());
            continue;
        }
        if (!node.is_tuple()) {
            continue;
        }
        if (is_node(node, "call", 4) && is_node(element(node, 2), "atom", 3)) {
            const auto arity = static_cast<std::int64_t>(forms::elements(element(node, 3)).size());
            const std::array items{element(element(node, 2), 2), build.integer(arity)};
            calls.insert(build.tuple(items));
        }
        for (std::size_t index = 0; index < node.tuple_size_unchecked(); ++index) {
            pending.push_back(element(node, index));
        }
    }
    return build.list(std::vector<Term>(calls.begin(), calls.end()));
}

std::u32string format_text(const Term &format) {
    std::u32string text;
    if (format.is_atom()) {
        for (const auto byte : format.atom_spelling_unchecked()) {
            text.push_back(static_cast<unsigned char>(byte));
        }
        return text;
    }
    if (format.is_binary()) {
        for (const auto byte : string::binary_bytes(format)) {
            text.push_back(static_cast<unsigned char>(byte));
        }
        return text;
    }
    for (const auto &code : forms::elements(format)) {
        text.push_back(static_cast<char32_t>(string::small_of(code).value_or(0)));
    }
    return text;
}

Term check_format_string1(ProcessContext &context, const ExprArg &format) {
    return check_format_string(context, format_text(format.term), true);
}

Term check_format_string2(ProcessContext &context, const ExprArg &format, const StrictArg &strict) {
    return check_format_string(context, format_text(format.term), is_atom(strict.term, "true"));
}

Term format_spec1(ProcessContext &context, const ExprArg &error) {
    forms::Builder build(context);
    Lint lint(context, build.nil(), {build.nil()});
    return lint.format_spec(error.term);
}

constexpr std::array LINT_BUILTINS{
    typed_entry<module4>("clause_erl_lint", "module"),
    typed_entry<format_spec1>("clause_erl_lint", "format_spec"),
    typed_entry<local_calls1>("clause_erl_lint", "local_calls"),
    typed_entry<is_guard_test3>("clause_erl_lint", "is_guard_test"),
    typed_entry<exprs2>("erl_lint", "exprs"),
    typed_entry<exprs_opt>("erl_lint", "exprs_opt"),
    typed_entry<used_vars2>("erl_lint", "used_vars"),
    typed_entry<is_pattern_expr1>("erl_lint", "is_pattern_expr"),
    typed_entry<is_guard_test1>("erl_lint", "is_guard_test"),
    typed_entry<is_guard_expr1>("erl_lint", "is_guard_expr"),
    typed_entry<check_format_string1>("erl_lint", "check_format_string"),
    typed_entry<check_format_string2>("erl_lint", "check_format_string"),
};
} // namespace

void Lint::command(const Term &exprs, const BindingList &bindings) {
    for (const auto &binding : forms::elements(bindings.list)) {
        if (binding.is_tuple() && binding.tuple_size_unchecked() == 2 && is_node(element(binding, 0), "record", 3)) {
            attribute_state(with_file(build_.context(), element(binding, 1), {chars("none")}));
        }
    }
    const auto vt = binding_table(bindings.list);
    static_cast<void>(this->exprs(with_file(build_.context(), exprs, {chars("nofile")}), vt));
}

Term Lint::used_vars(const Term &exprs, const Term &bindings) {
    fun_used_vars_.emplace();
    static_cast<void>(this->exprs(exprs, binding_table(bindings)));
    std::vector<std::pair<Term, Term>> entries;
    if (fun_used_vars_) {
        entries.assign(fun_used_vars_->begin(), fun_used_vars_->end());
    }
    return need(TermFactory(build_.context()).map(entries));
}

GuardScope Lint::guard_scope() {
    guard_records_.clear();
    for (const auto &[name, record] : records_) {
        guard_records_.emplace(name.word(), build_.list(record.fields));
    }
    guard_overridden_.clear();
    for (const auto &function : locals_) {
        guard_overridden_.insert({element(function, 0).word(), string::small_of(element(function, 1)).value_or(-1)});
    }
    for (const auto &[function, module] : imports_) {
        guard_overridden_.insert({element(function, 0).word(), string::small_of(element(function, 1)).value_or(-1)});
    }
    return {.records = &guard_records_, .overridden = &guard_overridden_};
}
} // namespace clause::runtime::builtins::front_end::lint

namespace clause::runtime {
std::span<const BuiltinEntry> lint_builtins() noexcept { return builtins::front_end::lint::LINT_BUILTINS; }
} // namespace clause::runtime
