#include "../../scheduler/executor.hpp"
#include "processes.hpp"
#include <array>

// The option-taking and node-naming process builtins (plan 21 step LIB9A, docs/processes.md#builtins): send/3,
// send_nosuspend/2,3, link/2, exit/3, exit_signal/3, spawn/2,4, spawn_link/2,4 and spawn_monitor/2,4 on a node that
// is not distributed, process_flag/3, bump_reductions/1 and the process iterator.
namespace clause::runtime::builtins {
namespace {
// The roles of the arguments (typed.hpp's Named).
struct DestinationRole;
struct MessageRole;
struct OptionsRole;
struct NodeRole;
using DestinationArg = Named<DestinationRole>;
using MessageArg = Named<MessageRole>;
using OptionsArg = Named<OptionsRole>;
using NodeArg = Named<NodeRole>;

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

// The atoms of a proper option list, each one of `allowed`; badarg for anything else.
std::vector<std::string_view> options_of(const OptionsArg &options, std::span<const std::string_view> allowed) {
    std::vector<std::string_view> names;
    auto rest = options.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto option = rest.head_unchecked();
        const auto name = option.is_atom() ? option.atom_spelling_unchecked() : std::string_view{};
        if (!std::ranges::contains(allowed, name)) {
            bad_argument();
        }
        names.push_back(name);
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return names;
}

// send(Dest, Msg, Options): ok; noconnect for a message to another node sent with noconnect. Priority sends are
// accepted and delivered as ordinary messages.
Term send3(ProcessContext &context, const DestinationArg &destination, const MessageArg &message,
           const OptionsArg &options) {
    static constexpr std::array<std::string_view, 3> ALLOWED{"nosuspend", "noconnect", "priority"};
    const auto names = options_of(options, ALLOWED);
    need(send_message(context, destination.term, message.term));
    const bool noconnect = std::ranges::contains(names, std::string_view{"noconnect"});
    return atom(context, noconnect && remote_destination(destination.term) ? "noconnect" : "ok");
}

// send_nosuspend(Dest, Msg): true; a local send never suspends.
Term send_nosuspend2(ProcessContext &context, const DestinationArg &destination, const MessageArg &message) {
    need(send_message(context, destination.term, message.term));
    return atom(context, "true");
}

// send_nosuspend(Dest, Msg, [noconnect]): false for a message to another node, else true.
Term send_nosuspend3(ProcessContext &context, const DestinationArg &destination, const MessageArg &message,
                     const OptionsArg &options) {
    static constexpr std::array<std::string_view, 1> ALLOWED{"noconnect"};
    const auto names = options_of(options, ALLOWED);
    need(send_message(context, destination.term, message.term));
    return TermFactory(context).boolean(names.empty() || !remote_destination(destination.term)).value();
}

// The options of link/2, exit/3 and exit_signal/3: [] or [priority], which Clause delivers as ordinary signals.
void priority_options(const OptionsArg &options) {
    static constexpr std::array<std::string_view, 1> ALLOWED{"priority"};
    options_of(options, ALLOWED);
}

Term link2(ProcessContext &context, const DestinationArg &pid, const OptionsArg &options) {
    priority_options(options);
    return need(link_process(context, pid.term));
}

template <bool SelfNormal>
Term exit3(ProcessContext &context, const DestinationArg &destination, const MessageArg &reason,
           const OptionsArg &options) {
    priority_options(options);
    return need(send_exit(context, destination.term, reason.term, SelfNormal));
}

bool local_node(const Term &node) { return node.is_atom() && node.atom_spelling_unchecked() == detail::LOCAL_NODE; }

// What a spawn naming a node does besides starting the process.
detail::SpawnOptions spawn_options(bool link, bool monitor) { return {.link = link, .monitor = monitor}; }

// The useless pid of a spawn on another node: a process that ends at once with reason noconnection, so a link or a
// monitor reports noconnection, as OTP's crasher process does on a node that is not alive.
Term remote_spawn(ProcessContext &context, const detail::SpawnOptions &options) {
    const std::array arguments{atom(context, "noconnection")};
    const detail::InitialCall call{atom(context, "erlang"), atom(context, "exit"),
                                   need(TermFactory(context).list(arguments))};
    return need(detail::Executor::of(context).spawn(context, call, options));
}

// spawn(Node, Fun) and its link and monitor variants: Fun runs here when Node is this node.
template <bool Link, bool Monitor> Term spawn_node_fun(ProcessContext &context, const NodeArg &node, const Term &fun) {
    const auto options = spawn_options(Link, Monitor);
    if (local_node(node.term)) {
        if (!fun.is_function()) {
            bad_argument();
        }
        return need(detail::Executor::of(context).spawn(context, fun, options));
    }
    if (!node.term.is_atom() || !fun.is_function()) {
        bad_argument();
    }
    return remote_spawn(context, options);
}

// spawn(Node, Module, Function, Args) and its link and monitor variants.
template <bool Link, bool Monitor>
Term spawn_node_call(ProcessContext &context, const NodeArg &node, const AtomArgument &module,
                     const AtomArgument &function, const Term &arguments) {
    const auto options = spawn_options(Link, Monitor);
    if (!node.term.is_atom()) {
        bad_argument();
    }
    if (!local_node(node.term)) {
        return remote_spawn(context, options);
    }
    if (!arguments.is_list()) {
        bad_argument();
    }
    return need(detail::Executor::of(context).spawn(context, {module.term, function.term, arguments}, options));
}

// process_flag(Pid, save_calls, N): the previous setting; save_calls is the only flag of another process.
Term process_flag3(ProcessContext &context, const DestinationArg &pid, const AtomArgument &flag,
                   const MessageArg &value) {
    const auto calls = small(value.term.word());
    if (!pid.term.is_pid() || flag.spelling != "save_calls" || !calls || *calls < 0 || *calls > 10000) {
        bad_argument();
    }
    const auto previous = detail::Executor::inspect(context, pid.term.word(), [&](ProcessContext &target, auto &) {
        return target.signals().set_save_calls(static_cast<std::size_t>(*calls));
    });
    if (!previous) {
        bad_argument();
    }
    return need(TermFactory(context).integer(static_cast<std::int64_t>(*previous)));
}

// bump_reductions(N): true for a non-negative integer.
Term bump_reductions(ProcessContext &context, const MessageArg &reductions) {
    const auto count = small(reductions.term.word());
    if (!count || *count < 0) {
        bad_argument();
    }
    return atom(context, "true");
}

// processes_iterator(): {0, []}; processes_next/1 then walks the pids above the last word it scanned.
Term processes_iterator(ProcessContext &context) {
    const std::array parts{need(TermFactory(context).integer(0)), need(TermFactory(context).nil())};
    return need(TermFactory(context).tuple(parts));
}

Term next_of(ProcessContext &context, const Term &pid, std::int64_t scanned, const Term &rest) {
    const std::array iterator{need(TermFactory(context).integer(scanned)), rest};
    const std::array result{pid, need(TermFactory(context).tuple(iterator))};
    return need(TermFactory(context).tuple(result));
}

// processes_next({Scanned, Pending}): {Pid, Iterator}, or none once no process above Scanned remains.
Term processes_next(ProcessContext &context, const MessageArg &iterator) {
    const auto &term = iterator.term;
    if (!term.is_tuple() || term.tuple_size_unchecked() != 2) {
        bad_argument();
    }
    const auto scanned = small(term.tuple_element_unchecked(0).word());
    const auto pending = term.tuple_element_unchecked(1);
    if (!scanned || !pending.is_list()) {
        bad_argument();
    }
    if (pending.is_cons()) {
        return next_of(context, pending.head_unchecked(), *scanned, pending.tail_unchecked());
    }
    auto pids = detail::Executor::pids(context);
    std::erase_if(pids, [&](Word pid) { return static_cast<std::int64_t>(pid) <= *scanned; });
    if (pids.empty()) {
        return atom(context, "none");
    }
    const auto last = static_cast<std::int64_t>(pids.back());
    const auto rest =
        need(TermFactory(context).list_words(std::span(pids).subspan(1), need(TermFactory(context).nil())));
    return next_of(context, need(Term::from_word(pids.front(), context)), last, rest);
}

constexpr std::array PROCESS_OPTION_BUILTINS{
    typed_entry<send3>("erlang", "send"),
    typed_entry<send_nosuspend2>("erlang", "send_nosuspend"),
    typed_entry<send_nosuspend3>("erlang", "send_nosuspend"),
    typed_entry<link2>("erlang", "link"),
    typed_entry<exit3<true>>("erlang", "exit"),
    typed_entry<exit3<false>>("erlang", "exit_signal"),
    typed_entry<spawn_node_fun<false, false>>("erlang", "spawn"),
    typed_entry<spawn_node_call<false, false>>("erlang", "spawn"),
    typed_entry<spawn_node_fun<true, false>>("erlang", "spawn_link"),
    typed_entry<spawn_node_call<true, false>>("erlang", "spawn_link"),
    typed_entry<spawn_node_fun<false, true>>("erlang", "spawn_monitor"),
    typed_entry<spawn_node_call<false, true>>("erlang", "spawn_monitor"),
    typed_entry<process_flag3>("erlang", "process_flag"),
    typed_entry<bump_reductions>("erlang", "bump_reductions"),
    typed_entry<processes_iterator>("erlang", "processes_iterator"),
    typed_entry<processes_next>("erlang", "processes_next"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> process_option_builtins() noexcept { return builtins::PROCESS_OPTION_BUILTINS; }
} // namespace clause::runtime
