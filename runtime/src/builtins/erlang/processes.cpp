#include "../typed.hpp"
#include "ports.hpp"
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
Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

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
    if (!destination.is_tuple() || destination.tuple_size().value_or(0) != 2) {
        return false;
    }
    return need(destination.tuple_element(0)).is_atom() && need(destination.tuple_element(1)).is_atom();
}

// Whether {Name, Node} names a process of this node.
bool local_name(const Term &destination) {
    return need(destination.tuple_element(1)).atom_spelling().value_or("") == detail::LOCAL_NODE;
}

// The name (an atom word) a monitor item gives: an atom or {Name, nonode@nohost}; badarg for anything else.
Word monitored_name(const Term &item) {
    if (item.is_atom()) {
        return item.word();
    }
    if (!remote_name(item) || !local_name(item)) {
        bad_argument();
    }
    return need(item.tuple_element(0)).word();
}

// monitor(process | port, Item): a reference; the caller gets {'DOWN', Ref, Type, Item, Reason} when the process
// ends or the port closes. Item is a pid or port of the type, or a registered name as Name or {Name, nonode@nohost}.
TermResult<Term> monitor(ProcessContext &context, const AtomArgument &type, const Term &item) {
    const bool port = type.spelling == "port";
    if ((!port && type.spelling != "process") || (port ? item.is_pid() : item.is_port())) {
        bad_argument();
    }
    auto &executor = detail::Executor::of(context);
    if (item.is_pid() || item.is_port()) {
        return executor.monitor(context, item.word(), 0, port);
    }
    return executor.monitor(context, 0, monitored_name(item), port);
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
        const auto spelling = option.is_atom() ? option.atom_spelling().value_or("") : "";
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
    if (!term || !term->is_tuple() || term->tuple_size().value_or(0) != 5) {
        return false;
    }
    const auto element = term->tuple_element(1);
    return element && element->is_reference() && element->reference_value() == reference;
}

// demonitor(Ref, Options): stop the monitor; with flush, remove one {_, Ref, _, _, _} message when the monitor
// was no longer active; with info, whether it was.
TermResult<Term> demonitor_options_call(ProcessContext &context, const Term &reference, const ListArgument &options) {
    const auto parsed = demonitor_options(options);
    const auto identity =
        reference.is_reference() ? reference.reference_value() : std::unexpected(TermError::wrong_type);
    if (!identity) {
        bad_argument();
    }
    const bool active = detail::Executor::demonitor(context, *identity);
    if (!active && parsed.flush) {
        context.mailbox().remove([&](Word message) { return monitor_message(context, message, *identity); });
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
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = atom(context, "noproc").word()};
    }
    return atom(context, "true");
}

// unlink(Pid): true; the link, if any, has no effect from now on.
TermResult<Term> unlink(ProcessContext &context, const Term &pid) {
    if (!pid.is_pid() && !pid.is_port()) {
        bad_argument();
    }
    detail::Executor::unlink(context, pid.word());
    return atom(context, "true");
}

// exit(Dest, Reason) and exit_signal(Dest, Reason): true after sending an exit signal to the process of a pid; a
// reference names no alias here, so nothing is sent. exit/2 to the caller itself with reason normal ends it.
template <bool SelfNormal>
TermResult<Term> exit_signal(ProcessContext &context, const Term &destination, const Term &reason) {
    if (destination.is_pid() || destination.is_port()) {
        detail::Executor::of(context).exit(context, destination.word(), reason, SelfNormal);
    } else if (!destination.is_reference()) {
        bad_argument();
    }
    return atom(context, "true");
}

// process_flag(trap_exit, Bool): the previous setting; every other flag is badarg.
TermResult<Term> process_flag(ProcessContext &context, const AtomArgument &flag, const Term &value) {
    const auto spelling = value.is_atom() ? value.atom_spelling().value_or("") : "";
    if (flag.term.atom_spelling().value_or("") != "trap_exit" || (spelling != "true" && spelling != "false")) {
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
// process registered as Name on this node, or 0 to drop the message. Anything else is badarg.
Word destination_pid(ProcessContext &context, const Term &destination) {
    if (destination.is_pid() || destination.is_port()) {
        return destination.word();
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
    return local_name(destination) ? executor.whereis(context, need(destination.tuple_element(0)).word()) : 0;
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
    return send_to(context, destination_pid(context, destination), message);
}

// register(Name, Pid): true; badarg for the name undefined, a name in use, a process that has a name or has ended.
TermResult<Term> register_name(ProcessContext &context, const AtomArgument &name, const Term &pid) {
    if (name.term.atom_spelling().value_or("") == "undefined" || (!pid.is_pid() && !pid.is_port()) ||
        !detail::Executor::of(context).register_name(context, name.term.word(), pid.word())) {
        bad_argument();
    }
    return atom(context, "true");
}

// unregister(Name): true; badarg when no process has the name.
TermResult<Term> unregister(ProcessContext &context, const AtomArgument &name) {
    if (!detail::Executor::of(context).unregister(context, name.term.word())) {
        bad_argument();
    }
    return atom(context, "true");
}

// whereis(Name): the pid registered as Name, or undefined.
TermResult<Term> whereis(ProcessContext &context, const AtomArgument &name) {
    const auto pid = detail::Executor::of(context).whereis(context, name.term.word());
    return pid != 0 ? Term::from_word(pid, context) : TermFactory(context).atom("undefined");
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
    typed_entry<send>("erlang", "!"),
    typed_entry<send>("erlang", "send"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> process_builtins() noexcept { return builtins::PROCESS_BUILTINS; }
} // namespace clause::runtime
