#include "select.hpp"
#include <array>
#include <terms.hpp>

// Match specifications as terms (plan 21 step LIB11, docs/builtins.md#ets): ets:match_spec_compile/1,
// match_spec_run_r/3 (match_spec_run/2 and test_ms/2 are Erlang over these) and is_compiled_ms/1, and
// erlang:match_spec_test/3, which compiles a specification as OTP's lint does and runs it on one term or argument
// list.
namespace clause::runtime::builtins::ets {
namespace {
namespace match_spec = detail::match_spec;

// match_spec_compile(MatchSpec): a compiled specification; badarg for an invalid one.
Term match_spec_compile(ProcessContext &context, const Term &spec) {
    return need(match_spec::handle(context, compiled(context, spec)));
}

// is_compiled_ms(Term).
Term is_compiled_ms(ProcessContext &context, const Term &term) {
    return need(TermFactory(context).boolean(match_spec::program_of(term) != nullptr));
}

// The roles of match_spec_run_r/3's list and accumulator.
using ListArgument = Named<struct ListRole>;
using AccumulatorArgument = Named<struct AccumulatorRole>;

// match_spec_run_r(List, CompiledMatchSpec, Acc): the results for the list's terms, last first, in front of Acc.
Term match_spec_run_r(ProcessContext &context, const ListArgument &list, const Term &handle,
                      const AccumulatorArgument &accumulator) {
    const auto program = match_spec::program_of(handle);
    if (!program) {
        bad_argument();
    }
    auto results = accumulator.term;
    auto rest = list.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (const auto result =
                match_spec::run(context, *program, rest.head_unchecked().word(), match_spec::Source::heap)) {
            results = need(TermFactory(context).cons(*result, results));
        }
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return results;
}

// The messages of a compile, [{error | warning, Text}].
Term messages_term(ProcessContext &context, const std::vector<match_spec::Message> &messages) {
    TermFactory factory(context);
    std::vector<Term> items;
    items.reserve(messages.size());
    for (const auto &message : messages) {
        std::vector<Term> text;
        for (const auto byte : message.text) {
            text.push_back(need(factory.integer(static_cast<unsigned char>(byte))));
        }
        const std::array item{atom(context, message.warning ? "warning" : "error"), need(factory.list(text))};
        items.push_back(need(factory.tuple(item)));
    }
    return need(factory.list(items));
}

// The flags a trace run set: [return_trace, exception_trace] as far as set.
Term flags_term(ProcessContext &context, const match_spec::TraceFlags &flags) {
    std::vector<Term> items;
    if (flags.return_trace) {
        items.push_back(atom(context, "return_trace"));
    }
    if (flags.exception_trace) {
        items.push_back(atom(context, "exception_trace"));
    }
    return need(TermFactory(context).list(items));
}

using AgainstArgument = Named<struct AgainstRole>;
using SpecTerm = Named<struct SpecTermRole>;

// erlang:match_spec_test(MatchAgainst, MatchSpec, Type): {ok, Result, Flags, Messages} or {error, Messages}; a
// trace specification needs a list to match against.
Term match_spec_test(ProcessContext &context, const AgainstArgument &against, const SpecTerm &spec, const Term &type) {
    const auto name = spelling(type);
    if ((name != "table" && name != "trace") || (name == "trace" && !against.term.is_list())) {
        bad_argument();
    }
    const auto dialect = name == "table" ? match_spec::Dialect::table : match_spec::Dialect::trace;
    const auto result = match_spec::compile(context, spec.term, dialect, true);
    const auto messages = messages_term(context, result.messages);
    TermFactory factory(context);
    if (!result.program) {
        return need(factory.tuple(std::array{atom(context, "error"), messages}));
    }
    match_spec::TraceFlags flags;
    const auto value = dialect == match_spec::Dialect::table
                           ? match_spec::run(context, *result.program, against.term.word(), match_spec::Source::heap)
                           : match_spec::run_trace(context, *result.program, against.term, flags);
    const std::array reply{atom(context, "ok"), value.value_or(atom(context, "false")), flags_term(context, flags),
                           messages};
    return need(factory.tuple(reply));
}

constexpr std::array ETS_MATCH_SPEC_BUILTINS{
    typed_entry<match_spec_compile>("ets", "match_spec_compile"),
    typed_entry<is_compiled_ms>("ets", "is_compiled_ms"),
    typed_entry<match_spec_run_r>("ets", "match_spec_run_r"),
    typed_entry<match_spec_test>("erlang", "match_spec_test"),
};
} // namespace
} // namespace clause::runtime::builtins::ets

namespace clause::runtime {
std::span<const BuiltinEntry> ets_match_spec_builtins() noexcept { return builtins::ets::ETS_MATCH_SPEC_BUILTINS; }
} // namespace clause::runtime
