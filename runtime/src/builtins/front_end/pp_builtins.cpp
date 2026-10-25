#include "pp.hpp"
#include <clause/runtime/builtin_registry.hpp>

// erl_pp's builtin (plan 21 step LIB34C, docs/builtins.md#erl-pp): clause_erl_pp:print(Request, Answers) prints a
// form, attribute, function, guard, expression list or expression in C++. A hook item it cannot print alone stops
// the print with {hook, Expr, Column, Precedence}; erl_pp.erl calls the hook and prints again with one more answer.
namespace clause::runtime::builtins::front_end::pp {
namespace {
using forms::element;
using forms::is_atom;

// The roles of the arguments (typed.hpp's Named).
struct RequestRole;
struct AnswersRole;
using RequestArg = Named<RequestRole>;
using AnswersArg = Named<AnswersRole>;

// proplists:get_value(Key, Options, Default): a bare Key is true.
Term option(const Term &options, std::string_view key, const Term &fallback, const forms::Builder &build) {
    for (auto rest = options; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto entry = rest.head_unchecked();
        if (is_atom(entry, key)) {
            return build.atom("true"_atom);
        }
        if (entry.is_tuple() && entry.tuple_size_unchecked() >= 1 && is_atom(element(entry, 0), key)) {
            return entry.tuple_size_unchecked() == 2 ? element(entry, 1) : fallback;
        }
    }
    return fallback;
}

// encoding/1: unicode unless the options say latin1.
bool unicode_of(const Term &options, const forms::Builder &build) {
    const auto encoding = option(options, "encoding", build.atom("utf8"_atom), build);
    if (is_atom(encoding, "latin1")) {
        return false;
    }
    if (is_atom(encoding, "utf8") || is_atom(encoding, "unicode")) {
        return true;
    }
    string::case_clause(encoding);
}

std::int64_t integer_option(const Term &options, std::string_view key, std::int64_t fallback,
                            const forms::Builder &build) {
    const auto value = option(options, key, build.integer(fallback), build);
    const auto small = string::small_of(value);
    if (!small) {
        lists::no_clause();
    }
    return *small;
}

// pp/1: only an options list chooses the encoding of the second pass; a bare hook prints in latin1.
Writers writers_of(const Term &options, const forms::Builder &build) {
    if (!options.is_list()) {
        return {};
    }
    return {.unicode = unicode_of(options, build),
            .quote_singleton =
                is_atom(option(options, "quote_singleton_atom_types", build.atom("false"_atom), build), "true"),
            .width = integer_option(options, "linewidth", Writers{}.width, build),
            .indent = integer_option(options, "indent", Writers{}.indent, build)};
}

// opts/1.
Opts opts_of(const Term &options, const forms::Builder &build) {
    if (!options.is_list()) {
        return {.hook = !is_atom(options, "none"), .unicode = true};
    }
    return {.hook = !is_atom(option(options, "hook", build.atom("none"_atom), build), "none"),
            .unicode = unicode_of(options, build)};
}

// The item a request {What, Form, Indent, Precedence, Options} prints.
Index first_pass(Printer &printer, const Term &request, const Opts &opts) {
    const auto kind = element(request, 0).atom_spelling_unchecked();
    const auto form = element(request, 1);
    if (kind == "form") {
        return printer.form(form, opts);
    }
    if (kind == "attribute") {
        return printer.attribute(form, opts);
    }
    if (kind == "function") {
        return printer.function(form, opts);
    }
    if (kind == "guard") {
        return printer.guard(form, opts);
    }
    if (kind == "exprs") {
        return printer.exprs(form, opts);
    }
    return printer.item(form, string::small_of(element(request, 3)).value_or(0), opts);
}

Term print2(ProcessContext &context, const RequestArg &request, const AnswersArg &answers) {
    const auto &term = request.term;
    const auto indent = string::small_of(element(term, 2)).value_or(0);
    const auto options = element(term, 4);
    Items items(context);
    const auto &build = items.build_;
    Printer printer(items);
    const auto root = first_pass(printer, term, opts_of(options, build));
    const auto given = forms::elements(answers.term);
    try {
        const auto text = layout(items, root, indent, writers_of(options, build), given);
        std::vector<Term> codes;
        codes.reserve(text.size());
        for (const auto code : text) {
            codes.push_back(build.integer(code));
        }
        return build.list(codes);
    } catch (const HookNeeded &hook) {
        const std::array parts{build.atom("hook"_atom), hook.expr, build.integer(hook.column),
                               build.integer(hook.precedence)};
        return build.tuple(parts);
    }
}

constexpr std::array PP_BUILTINS{
    typed_entry<print2>("clause_erl_pp", "print"),
};
} // namespace
} // namespace clause::runtime::builtins::front_end::pp

namespace clause::runtime {
std::span<const BuiltinEntry> erl_pp_builtins() noexcept { return builtins::front_end::pp::PP_BUILTINS; }
} // namespace clause::runtime
