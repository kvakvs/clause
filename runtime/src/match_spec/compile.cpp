#include "../memory/heap_object.hpp"
#include "compiler.hpp"
#include <algorithm>
#include <clause/runtime/output.hpp>
#include <terms.hpp>

// Compiling match specifications (plan 21 step LIB11): the list of clauses, trace heads as argument tuples, and the
// checks and messages of OTP's db_match_set_lint and db_match_compile.
namespace clause::runtime::detail::match_spec {
namespace {
// OTP keeps at most 99 bytes of a message (DMC_ERR_STR_LEN).
constexpr std::size_t MESSAGE_BYTES = 99;

// A trace head list as a tuple of its patterns; whether it ended in '_'. None for an improper list.
std::optional<std::pair<Term, bool>> argument_tuple(ProcessContext &context, const Term &list) {
    std::vector<Term> elements;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        elements.push_back(rest.head_unchecked());
    }
    const auto prefix = is_atom(rest, "_");
    if (!rest.is_nil() && !prefix) {
        return std::nullopt;
    }
    return std::pair{need(TermFactory(context).tuple(elements)), prefix};
}

// One {Head, Guards, Body} clause; none, with the message recorded, for any other term (or a trace head that is no
// proper list).
std::optional<ClauseTerms> clause_of(Compiler &compiler, const Term &clause, Dialect dialect) {
    if (!clause.is_tuple() || clause.tuple_size_unchecked() != 3) {
        compiler.error("Match program part is not a tuple of arity 3.");
        return std::nullopt;
    }
    ClauseTerms terms{.head = clause.tuple_element_unchecked(0),
                      .guards = clause.tuple_element_unchecked(1),
                      .body = clause.tuple_element_unchecked(2),
                      .prefix = false};
    if (dialect != Dialect::trace || !terms.head.is_list()) {
        return terms;
    }
    const auto arguments = argument_tuple(compiler.context(), terms.head);
    if (!arguments) {
        compiler.error("Match expression part ", terms.head, " is not a proper list.");
        return std::nullopt;
    }
    terms.head = arguments->first;
    terms.prefix = arguments->second;
    return terms;
}

// The clauses of a specification; none, with the message recorded, when it is no proper list of clauses.
std::optional<std::vector<ClauseTerms>> clauses_of(Compiler &compiler, const Term &spec, Dialect dialect) {
    if (!spec.is_list()) {
        compiler.error("Match programs are not in a list.");
        return std::nullopt;
    }
    std::vector<ClauseTerms> clauses;
    auto rest = spec;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto terms = clause_of(compiler, rest.head_unchecked(), dialect);
        if (!terms) {
            return std::nullopt;
        }
        clauses.push_back(*terms);
    }
    if (!rest.is_nil()) {
        compiler.error("Match programs are not in a proper list.");
        return std::nullopt;
    }
    return clauses;
}
} // namespace

std::optional<std::uint64_t> variable_number(const Term &term) {
    if (!term.is_atom()) {
        return std::nullopt;
    }
    const auto name = term.atom_spelling_unchecked();
    if (name.size() < 2 || name.size() > 19 || name.front() != '$' || (name[1] == '0' && name.size() > 2)) {
        return std::nullopt;
    }
    std::uint64_t number = 0;
    for (const auto digit : name.substr(1)) {
        if (digit < '0' || digit > '9') {
            return std::nullopt;
        }
        number = (number * 10) + static_cast<std::uint64_t>(digit - '0');
    }
    return number;
}

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

void Compiler::error(std::string text) {
    failed_ = true;
    if (!lint_) {
        throw Stop{};
    }
    if (text.size() > MESSAGE_BYTES) {
        text.resize(MESSAGE_BYTES);
    }
    messages_.push_back({.warning = false, .text = std::move(text)});
}

void Compiler::error(std::string_view before, const Term &term, std::string_view after) {
    error(std::string(before) + printed(term) + std::string(after));
}

std::string Compiler::printed(const Term &term) const {
    // Only the first 99 bytes of a message are kept; the term is cut short well past them.
    return format_term(term, TermStyle::display, 4096).value_or(std::string(MESSAGE_BYTES, '.'));
}

Word Compiler::keep(const Term &term) {
    auto &kept = program_.constants.emplace_back(need(DetachedTerm::copy(context_, term)));
    return kept.word();
}

std::size_t Compiler::slot(std::uint64_t number) {
    const auto [found, added] = slots_.try_emplace(number, program_.slots);
    if (added) {
        ++program_.slots;
    }
    return found->second;
}

bool Compiler::clause(const ClauseTerms &terms) {
    const auto &[head, guards, body, prefix] = terms;
    bound_.clear();
    failed_ = false;
    Clause compiled;
    compiled.head = pattern(head, 0);
    compiled.prefix = prefix;
    if (failed_) {
        return false;
    }
    const auto &top = program_.patterns[compiled.head];
    if (program_.dialect == Dialect::trace) {
        compiled.whole_list = top.kind == Pattern::Kind::bind || top.kind == Pattern::Kind::skip;
        if (!compiled.whole_list && top.kind != Pattern::Kind::tuple) {
            error("Match head is invalid in this context.");
            return false;
        }
    }
    in_guard_ = true;
    expressions(guards, compiled.guards);
    in_guard_ = false;
    if (program_.dialect == Dialect::table && !body.is_cons()) {
        error("Body clause does not return anything.");
        return false;
    }
    expressions(body, compiled.body);
    if (failed_) {
        return false;
    }
    for (const auto number : bound_) {
        compiled.bindings.push_back(slots_.at(number));
    }
    program_.clauses.push_back(std::move(compiled));
    return true;
}

Compiled compile(ProcessContext &context, const Term &spec, Dialect dialect, bool lint) {
    Compiled result;
    auto program = std::make_shared<Program>();
    program->dialect = dialect;
    Compiler compiler(context, *program, result.messages, lint);
    try {
        const auto clauses = clauses_of(compiler, spec, dialect);
        if (!clauses) {
            return result;
        }
        for (const auto &terms : *clauses) {
            if (!compiler.clause(terms)) {
                return result;
            }
        }
    } catch (const Stop &) {
        result.system_limit = compiler.system_limit();
        return result;
    }
    program->source = need(DetachedTerm::copy(context, spec));
    result.program = std::move(program);
    return result;
}
} // namespace clause::runtime::detail::match_spec
