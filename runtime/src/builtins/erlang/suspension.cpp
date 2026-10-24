#include "../../scheduler/executor.hpp"
#include "../typed.hpp"
#include <array>
#include <clause/runtime/code_server.hpp>
#include <cstdio>
#include <string>

// suspend_process/1,2, resume_process/1 and process_display/2 (plan 21 step LIB9B, docs/processes.md#builtins):
// suspends are executor holds; process_display writes the call stack of a process on standard error.
namespace clause::runtime::builtins {
namespace {
// The roles of the arguments (typed.hpp's Named).
struct PidRole;
struct OptionsRole;
using PidArg = Named<PidRole>;
using OptionsArg = Named<OptionsRole>;

Word self_word(ProcessContext &context) { return need(TermFactory(context).pid(context.identity())).word(); }

// A pid of another live process, else badarg.
Word other_pid(ProcessContext &context, const PidArg &pid) {
    if (!pid.term.is_pid() || pid.term.word() == self_word(context)) {
        bad_argument();
    }
    return pid.term.word();
}

// The parsed options of suspend_process/2, with the reply tag of {asynchronous, Tag}.
struct SuspendOptions {
    detail::Executor::SuspendRequest request;
    std::optional<Term> reply_tag;
};

// One option: asynchronous, {asynchronous, Tag} or unless_suspending; badarg otherwise.
void apply_option(SuspendOptions &options, const Term &option) {
    const bool tagged = option.is_tuple() && option.tuple_size_unchecked() == 2 &&
                        option.tuple_element_unchecked(0).is_atom() &&
                        option.tuple_element_unchecked(0).atom_spelling_unchecked() == "asynchronous";
    const auto name = option.is_atom() ? option.atom_spelling_unchecked() : std::string_view{};
    if (tagged) {
        options.request.asynchronous = true;
        options.reply_tag = option.tuple_element_unchecked(1);
    } else if (name == "asynchronous") {
        options.request.asynchronous = true;
    } else if (name == "unless_suspending") {
        options.request.unless_suspending = true;
    } else {
        bad_argument();
    }
}

SuspendOptions options_of(const OptionsArg &options) {
    SuspendOptions parsed;
    auto rest = options.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        apply_option(parsed, rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return parsed;
}

// suspend_process(Pid, Options): true when the suspend count grew; {Tag, suspended} follows for {asynchronous, Tag}.
Term suspend_process2(ProcessContext &context, const PidArg &pid, const OptionsArg &options) {
    const auto parsed = options_of(options);
    const auto suspended = detail::Executor::suspend(context, other_pid(context, pid), parsed.request);
    if (!suspended) {
        bad_argument();
    }
    if (*suspended && parsed.reply_tag) {
        const std::array reply{*parsed.reply_tag, atom(context, "suspended"_atom)};
        const auto message = need(TermFactory(context).tuple(reply));
        if (const auto sent = detail::Executor::send(context, self_word(context), message); !sent) {
            throw BuiltinFailure{.term = sent.error()};
        }
    }
    return need(TermFactory(context).boolean(*suspended));
}

Term suspend_process1(ProcessContext &context, const PidArg &pid) {
    return suspend_process2(context, pid, {need(TermFactory(context).nil())});
}

// resume_process(Pid): true; badarg unless the caller suspends the live process Pid.
Term resume_process(ProcessContext &context, const PidArg &pid) {
    const auto resumed = detail::Executor::resume(context, other_pid(context, pid));
    if (!resumed || !*resumed) {
        bad_argument();
    }
    return atom(context, "true"_atom);
}

// "Module:Function/Arity" of one frame.
std::string frame_name(ProcessContext &context, const abi::v1::FrameDescriptor &frame) {
    const auto function = context.code_server().atom_word(frame.module, frame.function_atom);
    const auto atom = function ? Term::from_word(*function, context) : TermResult<Term>{};
    const std::string_view name = atom && atom->is_atom() ? atom->atom_spelling_unchecked() : "?";
    return std::string(frame.module->name, frame.module->name_size) + ":" + std::string(name) + "/" +
           std::to_string(frame.arity);
}

// The call stack of a process, current function first, as process_display(Pid, backtrace) writes it.
void write_backtrace(ProcessContext &context, const StackTrace &trace) {
    std::string text;
    text.reserve(trace.depth * 64);
    for (std::size_t index = 0; index < trace.depth; ++index) {
        text += index == 0 ? "Program counter: " : "Return addr ";
        text += frame_name(context, *trace.frames.at(index));
        text += "\n";
    }
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::fflush(stderr);
}

// process_display(Pid, backtrace): true after writing the call stack of the live process Pid on standard error.
Term process_display(ProcessContext &context, const PidArg &pid, const AtomArgument &type) {
    if (!pid.term.is_pid() || type.spelling != "backtrace") {
        bad_argument();
    }
    std::optional<StackTrace> trace;
    if (pid.term.word() == self_word(context)) {
        trace = context.stack().trace();
    } else {
        trace = detail::Executor::inspect(context, pid.term.word(),
                                          [](ProcessContext &target, auto &) { return target.stack().trace(); });
    }
    if (!trace) {
        bad_argument();
    }
    write_backtrace(context, *trace);
    return atom(context, "true"_atom);
}

constexpr std::array SUSPENSION_BUILTINS{
    typed_entry<suspend_process1>("erlang", "suspend_process"),
    typed_entry<suspend_process2>("erlang", "suspend_process"),
    typed_entry<resume_process>("erlang", "resume_process"),
    typed_entry<process_display>("erlang", "process_display"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> suspension_builtins() noexcept { return builtins::SUSPENSION_BUILTINS; }
} // namespace clause::runtime
