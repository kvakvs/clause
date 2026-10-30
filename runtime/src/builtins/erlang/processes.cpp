#include "processes.hpp"
#include "../../terms/identities.hpp"
#include "../typed.hpp"
#include "ports.hpp"
#include "spawning.hpp"
#include <array>

// The process builtins of the bridge (docs/builtins.md, docs/processes.md): self/0, make_ref/0, spawn/1,3,
// spawn_link/1,3, spawn_monitor/1,3, is_process_alive/1, sends ('!'/2, send/2), links (link/1, unlink/1), monitors
// (monitor/2, demonitor/1,2), exit signals (exit/2, exit_signal/2), process_flag(trap_exit, Bool) and registered
// names (register/2, unregister/1, whereis/1, registered/0).
namespace clause::runtime::builtins {
namespace {
// self(): the pid of the calling process.
TermResult<Term> self(ProcessContext &context) { return TermFactory(context).pid(context.identity()); }

// make_ref(): a reference unique within the program run.
TermResult<Term> make_ref(ProcessContext &context) { return TermFactory(context).make_reference(); }

// spawn(Fun): a new process calling Fun(); badarg unless Fun is a fun.
TermResult<Term> spawn_fun(ProcessContext &context, const Term &fun) {
    if (!fun.is_function()) {
        bad_argument();
    }
    return detail::Executor::of(context).spawn(context, fun);
}

// spawn(Module, Function, Args): a new process calling Module:Function(Args...).
TermResult<Term> spawn_call(ProcessContext &context, const AtomArgument &module, const AtomArgument &function,
                            const ListArgument &arguments) {
    return detail::Executor::of(context).spawn(context, {module.term, function.term, arguments.term});
}

// spawn_link(Fun): spawn(Fun), linked to the caller before the new process runs.
TermResult<Term> spawn_link_fun(ProcessContext &context, const Term &fun) {
    if (!fun.is_function()) {
        bad_argument();
    }
    return detail::Executor::of(context).spawn(context, fun, {.link = true});
}

// spawn_link(Module, Function, Args): spawn(Module, Function, Args), linked to the caller.
TermResult<Term> spawn_link_call(ProcessContext &context, const AtomArgument &module, const AtomArgument &function,
                                 const ListArgument &arguments) {
    return detail::Executor::of(context).spawn(context, {module.term, function.term, arguments.term}, {.link = true});
}

// The atom `name`.
// spawn_monitor(Fun): {Pid, Ref} of spawn(Fun), monitored by the caller before it runs.
TermResult<Term> spawn_monitor_fun(ProcessContext &context, const Term &fun) {
    if (!fun.is_function()) {
        bad_argument();
    }
    return detail::Executor::of(context).spawn(context, fun, {.monitor = true});
}

// spawn_monitor(Module, Function, Args): {Pid, Ref} of spawn(Module, Function, Args), monitored by the caller.
TermResult<Term> spawn_monitor_call(ProcessContext &context, const AtomArgument &module, const AtomArgument &function,
                                    const ListArgument &arguments) {
    return detail::Executor::of(context).spawn(context, {module.term, function.term, arguments.term},
                                               {.monitor = true});
}

// A {Name, Node} pair of two atoms, as sends and monitors accept.
bool remote_name(const Term &destination) {
    if (!destination.is_tuple() || destination.tuple_size_unchecked() != 2) {
        return false;
    }
    return destination.tuple_element_unchecked(0).is_atom() && destination.tuple_element_unchecked(1).is_atom();
}

// Whether {Name, Node} names a process of this node.
bool local_name(const Term &destination) { return detail::is_this_node(destination.tuple_element_unchecked(1)); }

// The name (an atom word) a monitor item gives: an atom or {Name, Node} of this node; badarg for anything else.
Word monitored_name(const Term &item) {
    if (item.is_atom()) {
        return item.word();
    }
    if (!remote_name(item) || !local_name(item)) {
        bad_argument();
    }
    return item.tuple_element_unchecked(0).word();
}

// monitor(process | port, Item[, Options]): a reference; the caller gets {'DOWN', Ref, Type, Item, Reason} (or
// {Tag, ...} with {tag, Tag}) when the process ends or the port closes. Item is a pid or port of the type, or a
// registered name as Name or {Name, Node} of this node. Options apply to process monitors (step 41G2).
TermResult<Term> monitor_with(ProcessContext &context, const AtomArgument &type, const Term &item,
                              const detail::MonitorOptions &options) {
    const bool port = type.spelling == "port";
    if ((!port && type.spelling != "process") || (port ? item.is_pid() : item.is_port()) ||
        (port && (options.alias || options.tag))) {
        bad_argument();
    }
    auto &executor = detail::Executor::of(context);
    if (item.is_pid() || item.is_port()) {
        return executor.monitor(context, item.word(), 0, port, options);
    }
    return executor.monitor(context, 0, monitored_name(item), port, options);
}

TermResult<Term> monitor(ProcessContext &context, const AtomArgument &type, const Term &item) {
    return monitor_with(context, type, item, {});
}

TermResult<Term> monitor3(ProcessContext &context, const AtomArgument &type, const Term &item, const Term &options) {
    return monitor_with(context, type, item, monitor_options(context, options));
}

// The options of demonitor/2: a proper list of flush and info.
struct DemonitorOptions {
    bool flush = false;
    bool info = false;
};

// Parse demonitor/2's options; badarg for any other option.
DemonitorOptions demonitor_options(const ListArgument &options) {
    DemonitorOptions parsed;
    for (const auto &option : options.elements) {
        const auto spelling = option.is_atom() ? option.atom_spelling_unchecked() : "";
        if (spelling != "flush" && spelling != "info") {
            bad_argument();
        }
        (spelling == "flush" ? parsed.flush : parsed.info) = true;
    }
    return parsed;
}

// Whether `message` is a 5-tuple whose second element is the reference `reference`, as a 'DOWN' message is.
bool monitor_message(ProcessContext &context, Word message, const ReferenceIdentity &reference) {
    const auto term = Term::from_word(message, context);
    if (!term || !term->is_tuple() || term->tuple_size_unchecked() != 5) {
        return false;
    }
    const auto element = term->tuple_element_unchecked(1);
    return element.is_reference() && element.reference_value_unchecked() == reference;
}

// demonitor(Ref, Options): stop the monitor; with flush, remove one {_, Ref, _, _, _} message when the monitor
// was no longer active; with info, whether it was.
TermResult<Term> demonitor_options_call(ProcessContext &context, const Term &reference, const ListArgument &options) {
    const auto parsed = demonitor_options(options);
    if (!reference.is_reference()) {
        bad_argument();
    }
    const auto identity = reference.reference_value_unchecked();
    const bool active = detail::Executor::demonitor(context, identity);
    if (!active && parsed.flush) {
        context.mailbox().remove([&](Word message) { return monitor_message(context, message, identity); });
    }
    return TermFactory(context).boolean(active || !parsed.info);
}

// demonitor(Ref): demonitor(Ref, []).
TermResult<Term> demonitor(ProcessContext &context, const Term &reference) {
    return demonitor_options_call(context, reference,
                                  ListArgument{.term = need(TermFactory(context).nil()), .elements = {}});
}

// link(Pid): true once linked; error:noproc for an ended process unless the caller traps exits.
TermResult<Term> link(ProcessContext &context, const Term &pid) {
    if (!pid.is_pid() && !pid.is_port()) {
        bad_argument();
    }
    if (!detail::Executor::of(context).link(context, pid.word())) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                             .payload = atom(context, "noproc"_atom).word()};
    }
    return atom(context, "true"_atom);
}

// Whether a term is a pid or port of another node, which a node that is not distributed has no link to and sends
// nothing to (plan 30 step DIST1).
bool external_process(const Term &value) { return value.is_external_pid() || value.is_external_port(); }

// unlink(Pid): true; the link, if any, has no effect from now on.
TermResult<Term> unlink(ProcessContext &context, const Term &pid) {
    if (external_process(pid)) {
        return atom(context, "true"_atom);
    }
    if (!pid.is_pid() && !pid.is_port()) {
        bad_argument();
    }
    detail::Executor::unlink(context, pid.word());
    return atom(context, "true"_atom);
}

// exit(Dest, Reason) and exit_signal(Dest, Reason): true after sending an exit signal to the process of a pid; a
// reference names no alias here and another node's pid no process reachable, so nothing is sent. exit/2 to the
// caller itself with reason normal ends it.
template <bool SelfNormal>
TermResult<Term> exit_signal(ProcessContext &context, const Term &destination, const Term &reason) {
    if (destination.is_pid() || destination.is_port()) {
        detail::Executor::of(context).exit(context, destination.word(), reason, SelfNormal);
    } else if (!destination.is_reference() && !detail::is_identity(destination)) {
        bad_argument();
    }
    return atom(context, "true"_atom);
}

// process_flag(min_heap_size, Size): the previous min_heap_size; it applies from the next collection.
TermResult<Term> min_heap_size(ProcessContext &context, const Term &value) {
    const auto size = small(value.word());
    if (!size || *size < 0) {
        bad_argument();
    }
    auto &heap = context.heap();
    const auto previous = heap.min_heap_words();
    heap.set_min_heap_words(static_cast<std::size_t>(*size));
    return TermFactory(context).integer(static_cast<std::int64_t>(previous));
}

// process_flag(save_calls, N): the previous setting, kept though no calls are saved (plan 21 step LIB9A).
TermResult<Term> save_calls(ProcessContext &context, const Term &value) {
    const auto calls = small(value.word());
    if (!calls || *calls < 0 || *calls > 10000) {
        bad_argument();
    }
    const auto previous = context.signals().set_save_calls(static_cast<std::size_t>(*calls));
    return TermFactory(context).integer(static_cast<std::int64_t>(previous));
}

// process_flag(trap_exit, Bool), (min_heap_size, Size) or (save_calls, N): the previous setting; every other flag is
// badarg.
TermResult<Term> process_flag(ProcessContext &context, const AtomArgument &flag, const Term &value) {
    const auto name = flag.term.atom_spelling_unchecked();
    if (name == "min_heap_size") {
        return min_heap_size(context, value);
    }
    if (name == "save_calls") {
        return save_calls(context, value);
    }
    const auto spelling = value.is_atom() ? value.atom_spelling_unchecked() : "";
    if (name != "trap_exit" || (spelling != "true" && spelling != "false")) {
        bad_argument();
    }
    return TermFactory(context).boolean(context.signals().set_trap_exit(spelling == "true"));
}

// is_process_alive(Pid): whether the process has not ended; badarg unless Pid is a pid.
TermResult<Term> is_process_alive(ProcessContext &context, const Term &pid) {
    if (!pid.is_pid()) {
        bad_argument();
    }
    return TermFactory(context).boolean(detail::Executor::alive(context, pid.word()));
}

// The pid a send goes to: a pid; the process registered as an atom (badarg when none is); for {Name, Node} the
// process registered as Name on this node, or 0 to drop the message, as for another node's pid. Anything else, a
// port of another node too, is badarg.
Word destination_pid(ProcessContext &context, const Term &destination) {
    if (destination.is_pid() || destination.is_port()) {
        return destination.word();
    }
    if (destination.is_external_pid()) {
        return 0;
    }

    const auto &executor = detail::Executor::of(context);
    if (destination.is_atom()) {
        const auto pid = executor.whereis(context, destination.word());
        if (pid == 0) {
            bad_argument();
        }
        return pid;
    }
    if (!remote_name(destination)) {
        bad_argument();
    }
    return local_name(destination) ? executor.whereis(context, destination.tuple_element_unchecked(0).word()) : 0;
}

// Deliver `message` to the process of `pid` unless it is 0; returns the message.
Term send_to(ProcessContext &context, Word pid, const Term &message) {
    if (TermTag{pid}.get_kind() == TermKind::local_port) {
        detail::Executor::of(context).port_request(context, pid, port_request(message));
    } else if (pid != 0) {
        if (const auto sent = detail::Executor::send(context, pid, message); !sent) {
            throw BuiltinFailure{.term = sent.error()};
        }
    }
    return message;
}

// Dest ! Msg and erlang:send(Dest, Msg): Msg, after delivering it to the process Dest names (nothing when that
// process has ended or a {Name, Node} names none).
TermResult<Term> send(ProcessContext &context, const Term &destination, const Term &message) {
    // A reference sends through an alias, or nowhere when it is none (step 41G2); another node's goes nowhere.
    if (destination.is_external_reference()) {
        return message;
    }
    if (destination.is_reference()) {
        if (const auto sent = detail::Executor::send_alias(context, destination.reference_value_unchecked(), message);
            !sent) {
            throw BuiltinFailure{.term = sent.error()};
        }
        return message;
    }
    return send_to(context, destination_pid(context, destination), message);
}

// register(Name, Pid): true; badarg for the name undefined, a name in use, a process that has a name or has ended.
TermResult<Term> register_name(ProcessContext &context, const AtomArgument &name, const Term &pid) {
    if (name.term.atom_spelling_unchecked() == "undefined" || (!pid.is_pid() && !pid.is_port()) ||
        !detail::Executor::of(context).register_name(context, name.term.word(), pid.word())) {
        bad_argument();
    }
    return atom(context, "true"_atom);
}

// unregister(Name): true; badarg when no process has the name.
TermResult<Term> unregister(ProcessContext &context, const AtomArgument &name) {
    if (!detail::Executor::of(context).unregister(context, name.term.word())) {
        bad_argument();
    }
    return atom(context, "true"_atom);
}

// whereis(Name): the pid registered as Name, or undefined.
TermResult<Term> whereis(ProcessContext &context, const AtomArgument &name) {
    const auto pid = detail::Executor::of(context).whereis(context, name.term.word());
    return pid != 0 ? Term::from_word(pid, context) : TermFactory(context).atom("undefined"_atom);
}

// registered(): the registered names.
TermResult<Term> registered(ProcessContext &context) {
    const auto names = detail::Executor::of(context).registered(context);
    TermFactory factory(context);
    return factory.list_words(names, need(factory.nil()));
}

constexpr std::array PROCESS_BUILTINS{
    typed_entry<self>("erlang", "self"),
    typed_entry<make_ref>("erlang", "make_ref"),
    typed_entry<spawn_fun>("erlang", "spawn"),
    typed_entry<spawn_call>("erlang", "spawn"),
    typed_entry<is_process_alive>("erlang", "is_process_alive"),
    typed_entry<spawn_link_fun>("erlang", "spawn_link"),
    typed_entry<spawn_link_call>("erlang", "spawn_link"),
    typed_entry<link>("erlang", "link"),
    typed_entry<unlink>("erlang", "unlink"),
    typed_entry<exit_signal<true>>("erlang", "exit"),
    typed_entry<exit_signal<false>>("erlang", "exit_signal"),
    typed_entry<process_flag>("erlang", "process_flag"),
    typed_entry<spawn_monitor_fun>("erlang", "spawn_monitor"),
    typed_entry<spawn_monitor_call>("erlang", "spawn_monitor"),
    typed_entry<monitor>("erlang", "monitor"),
    typed_entry<demonitor>("erlang", "demonitor"),
    typed_entry<demonitor_options_call>("erlang", "demonitor"),
    typed_entry<register_name>("erlang", "register"),
    typed_entry<unregister>("erlang", "unregister"),
    typed_entry<whereis>("erlang", "whereis"),
    typed_entry<registered>("erlang", "registered"),
    typed_entry<monitor3>("erlang", "monitor"),
    typed_entry<send>("erlang", "!"),
    typed_entry<send>("erlang", "send"),
};
} // namespace

TermResult<Term> send_message(ProcessContext &context, const Term &destination, const Term &message) {
    return send(context, destination, message);
}

bool remote_destination(const Term &destination) {
    return detail::is_identity(destination)
               ? !destination.is_pid() && !destination.is_port() && !destination.is_reference()
               : remote_name(destination) && !local_name(destination);
}

TermResult<Term> link_process(ProcessContext &context, const Term &pid) { return link(context, pid); }

TermResult<Term> send_exit(ProcessContext &context, const Term &destination, const Term &reason, bool self_normal) {
    return self_normal ? exit_signal<true>(context, destination, reason)
                       : exit_signal<false>(context, destination, reason);
}
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> process_builtins() noexcept { return builtins::PROCESS_BUILTINS; }
} // namespace clause::runtime
