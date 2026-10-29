#include "ms_transform.hpp"
#include "../typed.hpp"
#include <algorithm>
#include <array>
#include <iterator>

// ms_transform's module walk and builtins in C++ (C++ ms_transform): transform_forms/3, function_clauses/2, copy/3
// and case_clauses/3 of library/stdlib/ms_transform.erl, and clause_ms_transform:transform_forms/1 and
// clause_list/2, which ms_transform.erl shapes into parse_transform/2's and transform_from_shell/3's results.
namespace clause::runtime::builtins::front_end::ms {
using forms::element;
using forms::is_atom;
using forms::is_node;

namespace {
// The fun2ms/1 dialect of a remote call node, if it is ets:fun2ms or dbg:fun2ms.
std::optional<Dialect> fun2ms_call(const Term &callee) {
    if (!is_node(callee, "remote", 4) || !is_node(element(callee, 2), "atom", 3) ||
        !is_node(element(callee, 3), "atom", 3) || !is_atom(element(element(callee, 3), 2), "fun2ms")) {
        return std::nullopt;
    }
    const auto module = element(element(callee, 2), 2);
    if (is_atom(module, "ets")) {
        return Dialect::ets;
    }
    return is_atom(module, "dbg") ? std::optional{Dialect::dbg} : std::nullopt;
}

// copy/3 over a function: the variables bound so far are tracked, and fun2ms calls of literal funs translated.
class Copier final {
  public:
    explicit Copier(Translator &translator) : t_(translator), build_(translator.build()) {}

    // function_clauses/2: each clause starts with no bound variables; guards are left as they are.
    Term function_clauses(const Term &clauses) {
        std::vector<Term> copied;
        for (const auto &clause : forms::elements(clauses)) {
            if (!is_node(clause, "clause", 5)) {
                throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause};
            }
            Bound bound;
            const auto head = copy(element(clause, 2), bound);
            const auto body = copy(element(clause, 4), bound);
            const std::array items{element(clause, 0), element(clause, 1), head, element(clause, 3), body};
            copied.push_back(build_.tuple(items));
        }
        return build_.list(copied);
    }

    Term copy(const Term &term, Bound &bound) {
        if (term.is_cons()) {
            return copy_list(term, bound);
        }
        if (!term.is_tuple()) {
            return term;
        }
        if (is_node(term, "call", 4)) {
            if (const auto dialect = fun2ms_call(element(term, 2))) {
                return fun2ms(*dialect, term, bound);
            }
        }
        return copy_node(term, bound);
    }

  private:
    Term copy_node(const Term &term, Bound &bound) {
        if (is_node(term, "match", 4)) {
            return copy_match(term, bound);
        }
        if (is_node(term, "var", 3)) {
            if (!is_atom(element(term, 2), "_")) {
                bound.insert(element(term, 2).word());
            }
            return term;
        }
        if (is_node(term, "fun", 3) && is_node(element(term, 2), "clauses", 2)) {
            // A fun exports no bindings.
            auto inner = bound;
            const auto clauses = copy(element(element(term, 2), 1), inner);
            return build_.node("fun"_atom, element(term, 1),
                               build_.tuple(std::array{element(element(term, 2), 0), clauses}));
        }
        if (is_node(term, "named_fun", 4)) {
            auto inner = bound;
            if (!is_atom(element(term, 2), "_")) {
                inner.insert(element(term, 2).word());
            }
            return forms::with_element(build_, term, 3, copy(element(term, 3), inner));
        }
        if (is_node(term, "case", 4)) {
            const auto subject = copy(element(term, 2), bound);
            const auto clauses = case_clauses(element(term, 3), bound);
            return build_.node("case"_atom, element(term, 1), subject, clauses);
        }
        return copy_tuple(term, bound);
    }

    // The value first, then the pattern with the variables bound before the match.
    Term copy_match(const Term &term, Bound &bound) {
        auto pattern_bound = bound;
        const auto value = copy(element(term, 3), bound);
        const auto pattern = copy(element(term, 2), pattern_bound);
        bound.insert(pattern_bound.begin(), pattern_bound.end());
        return build_.node("match"_atom, element(term, 1), pattern, value);
    }

    Term copy_tuple(const Term &term, Bound &bound) {
        std::vector<Term> items;
        items.reserve(term.tuple_size_unchecked());
        for (std::size_t index = 0; index < term.tuple_size_unchecked(); ++index) {
            items.push_back(copy(element(term, index), bound));
        }
        return build_.tuple(items);
    }

    Term copy_list(const Term &term, Bound &bound) {
        std::vector<Term> heads;
        auto rest = term;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            heads.push_back(copy(rest.head_unchecked(), bound));
        }
        return build_.terms().list(heads, copy(rest, bound));
    }

    // The clauses of a case export the variables every clause binds.
    Term case_clauses(const Term &clauses, Bound &bound) {
        std::vector<Term> copied;
        std::optional<Bound> common;
        for (const auto &clause : forms::elements(clauses)) {
            if (!is_node(clause, "clause", 5)) {
                throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause};
            }
            auto inner = bound;
            const auto pattern = copy(element(clause, 2), inner);
            const auto guard = copy(element(clause, 3), inner);
            const auto body = copy(element(clause, 4), inner);
            copied.push_back(build_.node("clause"_atom, element(clause, 1), pattern, guard, body));
            Bound added;
            std::ranges::set_difference(inner, bound, std::inserter(added, added.end()));
            if (common) {
                std::erase_if(*common, [&](Word name) { return !added.contains(name); });
            } else {
                common = std::move(added);
            }
        }
        if (!common) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause};
        }
        bound.insert(common->begin(), common->end());
        return build_.list(copied);
    }

    // fun2ms(Fun) of a literal fun: its match specification as an abstract list.
    Term fun2ms(Dialect dialect, const Term &call, const Bound &bound) {
        const auto args = element(call, 3);
        const auto fun = args.is_cons() && args.tail_unchecked().is_nil() ? args.head_unchecked() : build_.nil();
        if (!is_node(fun, "fun", 3) || !is_node(element(fun, 2), "clauses", 2)) {
            throw MsError{.anno = element(call, 1), .reason = build_.integer(ERR_NOFUN)};
        }
        return t_.clause_list({.anno = element(fun, 1), .clauses = element(element(fun, 2), 1)}, dialect, bound);
    }

    Translator &t_;
    const Builder &build_;
};

// The value of a 2-tuple attribute `name`: {attribute, _, Name, {A, B}}.
std::optional<Term> pair_attribute(const Term &form, std::string_view name) {
    if (!is_node(form, "attribute", 4) || !is_atom(element(form, 2), name)) {
        return std::nullopt;
    }
    const auto value = element(form, 3);
    return value.is_tuple() && value.tuple_size_unchecked() == 2 ? std::optional{value} : std::nullopt;
}

// The text of a string as a character list.
Term chars(const Builder &build, std::string_view text) {
    std::vector<Term> codes;
    for (const auto c : text) {
        codes.push_back(build.integer(static_cast<unsigned char>(c)));
    }
    return build.list(codes);
}
} // namespace

ModuleRun transform_forms(ProcessContext &context, const Term &forms) {
    Translator translator(context);
    const auto &build = translator.build();
    ModuleRun run{.forms = {}, .file = chars(build, "TOP_LEVEL"), .error = std::nullopt, .warnings = {}, .used = {}};
    auto rest = forms;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto form = rest.head_unchecked();
        if (const auto file = pair_attribute(form, "file")) {
            run.file = element(*file, 0);
        } else if (const auto record = pair_attribute(form, "record")) {
            translator.define(*record);
        } else if (is_node(form, "function", 5)) {
            try {
                const auto clauses = Copier(translator).function_clauses(element(form, 4));
                run.forms.push_back(forms::with_element(build, form, 4, clauses));
            } catch (const MsError &error) {
                run.error = error;
                run.forms.clear();
                return run;
            }
            continue;
        }
        run.forms.push_back(form);
    }
    if (!rest.is_nil()) {
        // OTP walks the forms with a comprehension.
        const auto reason = build.tuple(std::array{build.atom("bad_generator"_atom), rest});
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = reason.word()};
    }
    run.warnings = translator.warnings();
    run.used = translator.used();
    return run;
}

namespace {
// clause_ms_transform:transform_forms(Forms): {ok, Forms, File, Warnings, Used} (warnings and used records newest
// first) or {error, File, Anno, Reason}.
Term transform_forms1(ProcessContext &context, const Term &forms) {
    const auto run = transform_forms(context, forms);
    const Builder build(context);
    if (run.error) {
        const std::array items{build.atom("error"_atom), run.file, run.error->anno, run.error->reason};
        return build.tuple(items);
    }
    const std::vector warnings(run.warnings.rbegin(), run.warnings.rend());
    const std::vector used(run.used.rbegin(), run.used.rend());
    const std::array items{build.atom("ok"_atom), build.list(run.forms), run.file, build.list(warnings),
                           build.list(used)};
    return build.tuple(items);
}

using DialectArg = Named<struct DialectRole>;
using ClausesArg = Named<struct ClausesRole>;

// clause_ms_transform:clause_list(Dialect, Clauses): {ok, Spec} for a shell fun's clauses, or {error, Anno, Reason}.
Term clause_list2(ProcessContext &context, const DialectArg &dialect, const ClausesArg &clauses) {
    Translator translator(context);
    const auto &build = translator.build();
    const auto kind = is_atom(dialect.term, "dbg") ? Dialect::dbg : Dialect::ets;
    try {
        const auto spec = translator.clause_list({.anno = build.integer(1), .clauses = clauses.term}, kind, {});
        return build.tuple(std::array{build.atom("ok"_atom), spec});
    } catch (const MsError &error) {
        return build.tuple(std::array{build.atom("error"_atom), error.anno, error.reason});
    }
}

constexpr std::array MS_TRANSFORM_BUILTINS{
    typed_entry<transform_forms1>("clause_ms_transform", "transform_forms"),
    typed_entry<clause_list2>("clause_ms_transform", "clause_list"),
};
} // namespace
} // namespace clause::runtime::builtins::front_end::ms

namespace clause::runtime {
std::span<const BuiltinEntry> ms_transform_builtins() noexcept {
    return builtins::front_end::ms::MS_TRANSFORM_BUILTINS;
}
} // namespace clause::runtime
