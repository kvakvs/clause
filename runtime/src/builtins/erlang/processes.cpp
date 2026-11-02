#include "processes.hpp"
#include "../../terms/identities.hpp"
#include "../typed.hpp"
#include "dist_messages.hpp"
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

// Whether a process monitor's item names another node's process (plan 30 step DIST4b3b1): its pid, or {Name, Node}
// of another node, which is badarg while this node is not alive, as OTP's.
bool remote_process(const Term &item) {
    if (item.is_external_pid()) {
        return true;
    }
    if (!remote_name(item) || local_name(item)) {
        return false;
    }
    if (!detail::this_node_named()) {
        bad_argument();
    }
    return true;
}

// Whether a monitor's type, item and options fit together: no pid for port, no port for process, options for process.
bool monitor_fits(const AtomArgument &type, const Term &item, const detail::MonitorOptions &options) {
    if (type.spelling == "port") {
        return !item.is_pid() && !options.alias && !options.tag;
    }
    return type.spelling == "process" && !item.is_port();
}

// monitor(process | port, Item[, Options]): a reference; the caller gets {'DOWN', Ref, Type, Item, Reason} (or
// {Tag, ...} with {tag, Tag}) when the process ends or the port closes. Item is a pid or port of the type, or a
// registered name as Name or {Name, Node}; another node's process is monitored over its connection. Options apply to
// process monitors (step 41G2).
TermResult<Term> monitor_with(ProcessContext &context, const AtomArgument &type, const Term &item,
                              const detail::MonitorOptions &options) {
    if (!monitor_fits(type, item, options)) {
        bad_argument();
    }
    const bool port = type.spelling == "port";
    auto &executor = detail::Executor::of(context);
    if (!port && remote_process(item)) {
        return executor.monitor_remote(context, item, options);
    }
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
bool monitor_message(ProcessContext &context, Word message, const Term &reference) {
    const auto term = Term::from_word(message, context);
    if (!term || !term->is_tuple() || term->tuple_size_unchecked() != 5) {
        return false;
    }
    const auto element = term->tuple_element_unchecked(1);
    if (element.is_reference() && reference.is_reference()) {
        return element.reference_value_unchecked() == reference.reference_value_unchecked();
    }
    return element.exactly_equal(reference).value_or(false);
}

bool earlier_incarnation(ProcessContext &context, const Term &identity);

// Whether demonitor/1,2 may take `reference`: this node's, or one of an earlier incarnation of it, which monitors
// nothing (OTP's demonitor answers false for it, DIST5e); another node's is badarg.
bool demonitor_reference(ProcessContext &context, const Term &reference) {
    return reference.is_reference() || (reference.is_external_reference() && earlier_incarnation(context, reference));
}

// demonitor(Ref, Options): stop the monitor; with flush, remove one {_, Ref, _, _, _} message when the monitor
// was no longer active; with info, whether it was.
TermResult<Term> demonitor_options_call(ProcessContext &context, const Term &reference, const ListArgument &options) {
    const auto parsed = demonitor_options(options);
    if (!demonitor_reference(context, reference)) {
        bad_argument();
    }
    const bool active =
        reference.is_reference() && detail::Executor::demonitor(context, reference.reference_value_unchecked());
    if (!active && parsed.flush) {
        // A 'DOWN' of another node's process is posted among the arrivals (DIST4b3b1).
        need(detail::MessageBuffer::take_arrivals(context));
        context.mailbox().remove([&](Word message) { return monitor_message(context, message, reference); });
    }
    return TermFactory(context).boolean(active || !parsed.info);
}

// demonitor(Ref): demonitor(Ref, []).
TermResult<Term> demonitor(ProcessContext &context, const Term &reference) {
    return demonitor_options_call(context, reference,
                                  ListArgument{.term = need(TermFactory(context).nil()), .elements = {}});
}

// Whether another node's pid, port or reference names an earlier incarnation of this node (plan 30 step DIST5a).
bool earlier_incarnation(ProcessContext &context, const Term &identity) {
    return detail::is_this_node(need(Term::from_word(need(detail::ExternalAccess::read(identity)).node, context)));
}

// Whether the caller may link to an identity of another node: a pid, or a port of an earlier incarnation of this
// node; other nodes' ports are badarg (plan 30 step DIST5a).
bool remote_linkable(ProcessContext &context, const Term &pid) {
    return pid.is_external_pid() || (pid.is_external_port() && earlier_incarnation(context, pid));
}

// link(Pid): true once linked; error:noproc for an ended process unless the caller traps exits. Another node's
// process is linked over its connection (plan 30 step DIST5a).
TermResult<Term> link(ProcessContext &context, const Term &pid) {
    auto &executor = detail::Executor::of(context);
    bool linked = false;
    if (pid.is_pid() || pid.is_port()) {
        linked = executor.link(context, pid.word());
    } else if (remote_linkable(context, pid)) {
        linked = executor.link_remote(context, pid);
    } else {
        bad_argument();
    }
    if (!linked) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                             .payload = atom(context, "noproc"_atom).word()};
    }
    return atom(context, "true"_atom);
}

// unlink(Pid): true; the link, if any, has no effect from now on.
TermResult<Term> unlink(ProcessContext &context, const Term &pid) {
    if (pid.is_external_pid()) {
        if (!earlier_incarnation(context, pid)) {
            detail::Executor::of(context).unlink_remote(context, pid);
        }
    } else if (pid.is_pid() || pid.is_port()) {
        detail::Executor::unlink(context, pid.word());
    } else if (!remote_linkable(context, pid)) {
        bad_argument();
    }
    return atom(context, "true"_atom);
}

// exit(Dest, Reason) and exit_signal(Dest, Reason): true after sending an exit signal to the process of a pid; a
// reference names no alias here, and an alias of another node gets nothing without ALTACT_SIG. Another node's pid
// gets EXIT2 (plan 30 step DIST5a), its port is badarg. exit/2 to the caller itself with reason normal ends it.
template <bool SelfNormal>
TermResult<Term> exit_signal(ProcessContext &context, const Term &destination, const Term &reason) {
    if (destination.is_pid() || destination.is_port()) {
        detail::Executor::of(context).exit(context, destination.word(), reason, SelfNormal);
    } else if (destination.is_external_pid() && !earlier_incarnation(context, destination)) {
        detail::Executor::of(context).exit_remote(context, destination, reason);
    } else if (!destination.is_reference() && !destination.is_external_reference() &&
               !remote_linkable(context, destination)) {
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

// The spelling of an atom, or empty for any other term.
std::string_view spelling(const Term &term) { return term.is_atom() ? term.atom_spelling_unchecked() : ""; }

// The bit of a boolean monitor_nodes option: `bit` for true, nothing for false; none for any other value.
std::optional<unsigned> boolean_bit(std::string_view value, unsigned bit) {
    if (value != "true" && value != "false") {
        return std::nullopt;
    }
    return value == "true" ? bit : 0U;
}

// The subscription bits one monitor_nodes option adds (plan 30 step DIST2b); none for an invalid one.
std::optional<unsigned> option_bits(std::string_view key, std::string_view value) {
    namespace status = detail::node_status;
    if (key == "connection_id") {
        return boolean_bit(value, status::CONNECTION_ID);
    }
    if (key == "nodedown_reason") {
        return boolean_bit(value, status::DOWN_REASON);
    }
    if (key != "node_type" || (value != "visible" && value != "hidden" && value != "all")) {
        return std::nullopt;
    }
    return (value == "hidden" ? 0U : status::VISIBLE) | (value == "visible" ? 0U : status::HIDDEN);
}

// The subscription bits of monitor_nodes options, as OTP's erts_monitor_nodes reads them: [] is the plain form, a
// map gives an info map unless it holds list => true. None for anything else.
std::optional<unsigned> node_status_options(const Term &options) {
    if (options.is_nil()) {
        return 0U;
    }
    if (!options.is_map()) {
        return std::nullopt;
    }
    auto bits = detail::node_status::INFO_MAP;
    for (const auto &[key, value] : need(options.map_entries())) {
        if (spelling(key) == "list" && spelling(value) == "true") {
            bits &= ~detail::node_status::INFO_MAP;
            continue;
        }
        const auto added = option_bits(spelling(key), spelling(value));
        if (!added) {
            return std::nullopt;
        }
        bits |= *added;
    }
    return bits;
}

// The options of a monitor_nodes flag: [] for monitor_nodes, Options for {monitor_nodes, Options}; none for any other
// flag.
std::optional<Term> monitor_nodes_options(ProcessContext &context, const Term &flag) {
    if (flag.is_tuple() && flag.tuple_size_unchecked() == 2 &&
        spelling(flag.tuple_element_unchecked(0)) == "monitor_nodes") {
        return flag.tuple_element_unchecked(1);
    }
    return spelling(flag) == "monitor_nodes" ? std::optional{need(TermFactory(context).nil())} : std::nullopt;
}

// process_flag(monitor_nodes, Bool) or ({monitor_nodes, Options}, Bool) (plan 30 step DIST2b,
// docs/distribution.md#node-status-messages): subscribe to node status messages with the options' bits once more, or
// end that subscription; the count it had.
TermResult<Term> monitor_nodes(ProcessContext &context, std::optional<unsigned> bits, const Term &value) {
    if (!value.is_boolean() || !bits) {
        bad_argument();
    }
    const auto count = detail::Executor::of(context).subscribe(context, *bits, value.boolean_value_unchecked());
    return TermFactory(context).integer(static_cast<std::int64_t>(count));
}

using Flag = Named<struct FlagRole>;

// process_flag(trap_exit | async_dist, Bool), (min_heap_size, Size), (save_calls, N) or monitor_nodes: the previous
// setting; every other flag is badarg.
TermResult<Term> process_flag(ProcessContext &context, const Flag &flag, const Term &value) {
    if (const auto options = monitor_nodes_options(context, flag.term)) {
        return monitor_nodes(context, node_status_options(*options), value);
    }
    const auto name = spelling(flag.term);
    if (name == "min_heap_size") {
        return min_heap_size(context, value);
    }
    if (name == "save_calls") {
        return save_calls(context, value);
    }
    const auto setting = spelling(value);
    if ((name != "trap_exit" && name != "async_dist") || (setting != "true" && setting != "false")) {
        bad_argument();
    }
    auto &signals = context.signals();
    const bool on = setting == "true";
    return TermFactory(context).boolean(name == "trap_exit" ? signals.set_trap_exit(on) : signals.set_async_dist(on));
}

// is_process_alive(Pid): whether the process has not ended; badarg unless Pid is a pid.
TermResult<Term> is_process_alive(ProcessContext &context, const Term &pid) {
    if (!pid.is_pid()) {
        bad_argument();
    }
    return TermFactory(context).boolean(detail::Executor::alive(context, pid.word()));
}

// The pid a send on this node goes to: a pid; the process registered as an atom (badarg when none is) or as Name of
// {Name, Node} with this node's name (0 when none is, to drop the message). Anything else, a port of another node too,
// is badarg.
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
    if (!remote_name(destination) || !local_name(destination)) {
        bad_argument();
    }
    return executor.whereis(context, destination.tuple_element_unchecked(0).word());
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
// process has ended or a {Name, Node} names none). A message to another node's process goes on its connection (plan
// 30 step DIST4b3a).
TermResult<Term> send(ProcessContext &context, const Term &destination, const Term &message) {
    if (remote_destination(destination)) {
        send_remote(context, {.destination = destination, .message = message});
        return message;
    }
    // A reference sends through an alias, or nowhere when it is none (step 41G2).
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
    return destination.is_external_pid() || destination.is_external_reference() ||
           (remote_name(destination) && !local_name(destination));
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
