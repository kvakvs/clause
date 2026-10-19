#include "../builtins/support.hpp"
#include "../memory/heap_object.hpp"
#include "../scheduler/executor.hpp"
#include "running.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/callable.hpp>
#include <clause/runtime/output.hpp>
#include <terms.hpp>

// Trace actions of match specifications (plan 21 step LIB11), run as erlang:match_spec_test/3 runs them: message sets
// the result, return_trace and exception_trace set flags, display prints, and the actions that would change tracing
// or sequential tracing check their arguments and report what OTP reports for a process without tracing, changing
// nothing (Clause has no tracing yet, plan 21 step LIB71; docs/differences.md).
namespace clause::runtime::detail::match_spec {
namespace {
Term term_of(Word word) { return TermAccess::make(word); }

Word atom_word(Run &run, AtomName name) { return run.built(TermFactory(run.context()).atom(name)); }

// The process trace flags OTP knows (erts_trace_flag2bit).
constexpr std::array<std::string_view, 22> TRACE_FLAGS{"timestamp",
                                                       "strict_monotonic_timestamp",
                                                       "monotonic_timestamp",
                                                       "all",
                                                       "send",
                                                       "receive",
                                                       "set_on_spawn",
                                                       "procs",
                                                       "set_on_first_spawn",
                                                       "set_on_link",
                                                       "set_on_first_link",
                                                       "running",
                                                       "exiting",
                                                       "garbage_collection",
                                                       "call",
                                                       "arity",
                                                       "return_to",
                                                       "silent",
                                                       "scheduler_id",
                                                       "running_ports",
                                                       "running_procs",
                                                       "ports"};

bool trace_flag(const Term &flag) {
    return flag.is_atom() && std::ranges::find(TRACE_FLAGS, flag.atom_spelling_unchecked()) != TRACE_FLAGS.end();
}

// A list of trace flags, as trace/2,3 takes them; cpu_timestamp too.
bool trace_flags(const Term &list) {
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto flag = rest.head_unchecked();
        if (!trace_flag(flag) && !(flag.is_atom() && flag.atom_spelling_unchecked() == "cpu_timestamp")) {
            return false;
        }
    }
    return rest.is_nil();
}

// Whether a pid names a live process, as the actions taking a pid need.
bool alive(Run &run, const Term &pid) { return pid.is_pid() && Executor::alive(run.context(), pid.word()); }

// The value seq_trace:get_token(Flag) gives a process without a token: the flag's value, or none for a bad flag.
std::optional<Word> sequential_flag(Run &run, const Term &flag) {
    static constexpr std::array<std::string_view, 6> FLAGS{
        "send", "receive", "print", "timestamp", "monotonic_timestamp", "strict_monotonic_timestamp"};
    if (!flag.is_atom()) {
        return std::nullopt;
    }
    const auto name = flag.atom_spelling_unchecked();
    if (std::ranges::find(FLAGS, name) != FLAGS.end()) {
        return abi::v1::false_atom;
    }
    if (name == "label" || name == "serial") {
        return run.built(TermFactory(run.context()).nil());
    }
    return std::nullopt;
}

// display(Term): the term as erlang:display/1 prints it, on standard output.
Word display(Run &run, Word value) {
    const auto text = builtins::need(format_term(term_of(value), TermStyle::display)) + "\n";
    static_cast<void>(write_output(run.context().standard_output(), text));
    return abi::v1::true_atom;
}

// enable_trace/disable_trace(Flag) and (Pid, Flag); trace(Disable, Enable) and (Pid, Disable, Enable).
std::optional<Word> tracing(Run &run, Action action, const std::vector<Word> &values) {
    const auto lists = action == Action::trace;
    const auto with_pid = values.size() == (lists ? 3 : 2);
    if (with_pid && !alive(run, term_of(values[0]))) {
        return std::nullopt;
    }
    const auto first = with_pid ? 1U : 0U;
    const auto valid = lists ? trace_flags(term_of(values[first])) && trace_flags(term_of(values[first + 1]))
                             : trace_flag(term_of(values[first]));
    return valid ? std::optional{abi::v1::true_atom} : std::nullopt;
}

std::optional<Word> act(Run &run, const Expression &node, const std::vector<Word> &values) {
    switch (node.action) {
    case Action::message:
        run.set_result(values[0]);
        return abi::v1::true_atom;
    case Action::return_trace:
        run.flags().return_trace = true;
        return abi::v1::true_atom;
    case Action::exception_trace:
        run.flags().exception_trace = true;
        return abi::v1::true_atom;
    case Action::display:
        return display(run, values[0]);
    case Action::set_seq_token:
        return sequential_flag(run, term_of(values[0]));
    case Action::enable_trace:
    case Action::disable_trace:
    case Action::trace:
        return tracing(run, node.action, values);
    default:
        return std::nullopt;
    }
}

// The actions whose value needs no operand: what OTP gives inside erlang:match_spec_test/3, where the caller is not a
// traced function (caller and caller_line are undefined); Clause keeps no stack trace or process dump for them.
std::optional<Word> query(Run &run, Action action) {
    switch (action) {
    case Action::caller:
    case Action::caller_line:
        return atom_word(run, "undefined"_atom);
    case Action::is_seq_trace:
        return abi::v1::false_atom;
    case Action::process_dump:
        return run.built(TermFactory(run.context()).binary({}));
    case Action::silent:
        return abi::v1::true_atom;
    default:
        return run.built(TermFactory(run.context()).nil());
    }
}
} // namespace

std::optional<Word> run_action(Run &run, const Expression &node) {
    const auto values = run.operands(node);
    if (!values) {
        return std::nullopt;
    }
    const auto answered = node.action == Action::caller || node.action == Action::caller_line ||
                          node.action == Action::is_seq_trace || node.action == Action::process_dump ||
                          node.action == Action::get_seq_token || node.action == Action::current_stacktrace ||
                          node.action == Action::silent;
    return answered ? query(run, node.action) : act(run, node, *values);
}
} // namespace clause::runtime::detail::match_spec
