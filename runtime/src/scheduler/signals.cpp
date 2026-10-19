#include "../builtins/support.hpp"
#include "../process/exits.hpp"
#include "../terms/service_errors.hpp"
#include "signal_messages.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/runtime.hpp>
#include <new>
#include <utility>

// Links, monitors and exit signals (docs/processes.md#links, #monitors, #workers). The executor acts on a signal at
// once under its lock: links, monitors and names of any process, exits and messages of a process that does not run
// on another worker (a builtin aimed at one runs again after its slice). A process a signal ends is finished before
// the sending builtin returns, or once the peers it must signal have left their slices.
namespace clause::runtime::detail {
namespace {
using abi::v1::ErrorReason;

// What a 'DOWN' message names, built in the heap of `process`: the pid or port, or {Name, nonode@nohost} for a
// monitor made with a registered name.
TermResult<Term> monitored_item(ProcessContext &process, const Signals::Monitor &item) {
    if (item.name == 0) {
        return Term::from_word(item.pid, process);
    }
    TermFactory factory(process);
    const auto name = Term::from_word(item.name, process);
    const auto node = factory.atom(LOCAL_NODE);
    return name && node ? factory.tuple(std::array{*name, *node}) : (name ? node : name);
}
} // namespace

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

Term atom(ProcessContext &process, const AtomName name) {
    auto atom = TermFactory(process).atom(name);
    if (!atom) {
        throw std::bad_alloc();
    }
    return *atom;
}

TermResult<Term> exit_message(ProcessContext &process, Word from, const Term &reason) {
    TermFactory factory(process);
    const auto tag = factory.atom("EXIT"_atom);
    const auto pid = Term::from_word(from, process);
    const auto copy = reason.copy_to(process.heap());
    if (!tag || !pid || !copy) {
        return !tag ? tag : (!pid ? pid : copy);
    }
    return factory.tuple(std::array{*tag, *pid, *copy});
}

TermResult<Term> down_message(ProcessContext &process, const ReferenceIdentity &reference, const Signals::Monitor &item,
                              const Term &reason) {
    TermFactory factory(process);
    const auto type = TermTag{item.pid}.get_kind() == TermKind::local_port ? "port" : "process";
    // monitor/3's {tag, Tag} replaces 'DOWN' (step 41G2).
    const std::array parts{item.tag ? item.tag->copy_to(process) : factory.atom("DOWN"_atom),
                           factory.reference(reference), factory.atom(type), monitored_item(process, item),
                           reason.copy_to(process.heap())};
    const auto failed = std::ranges::find_if(parts, [](const auto &part) { return !part.has_value(); });
    if (failed != parts.end()) {
        return *failed;
    }
    return factory.tuple(std::array{*parts[0], *parts[1], *parts[2], *parts[3], *parts[4]});
}

bool Executor::link(ProcessContext &process, Word pid) {
    const std::scoped_lock lock(mutex_);
    if (is_port_word(pid)) {
        return link_port(process, pid);
    }
    const auto self = pid_of(process);
    if (pid == self) {
        return true;
    }
    auto *target = find(process, pid);
    if (!target) {
        if (!process.signals().trap_exit()) {
            return false;
        }
        const Running running(process);
        deliver(process, exit_message(process, pid, atom(process, "noproc"_atom)));
        return true;
    }
    process.signals().link(pid);
    target->signals().link(self);
    return true;
}

void Executor::unlink(ProcessContext &process, Word pid) noexcept {
    auto &executor = of(process);
    const std::scoped_lock lock(executor.mutex_);
    if (is_port_word(pid)) {
        executor.unlink_port(process, pid);
        return;
    }
    process.signals().unlink(pid);
    if (auto *target = find(process, pid)) {
        target->signals().unlink(pid_of(process));
    }
}

Term Executor::monitor(ProcessContext &watcher, Word pid, Word name, bool port, const MonitorOptions &options) {
    const std::scoped_lock lock(mutex_);
    pid = name != 0 ? lookup(watcher, name) : pid;
    const auto reference = TermFactory(watcher).make_reference();
    const auto identity = reference.transform([](const Term &term) { return term.reference_value_unchecked(); });
    if (!identity) {
        throw std::bad_alloc();
    }
    if (port) {
        return monitor_port(watcher, is_port_word(pid) ? pid : 0, name, *reference);
    }
    pid = is_port_word(pid) ? 0 : pid;
    auto *target = pid != 0 ? find(watcher, pid) : nullptr;
    if (options.alias) {
        add_alias(*identity, pid_of(watcher), *options.alias);
    }
    if (target == &watcher) {
        return *reference;
    }
    if (!target) {
        const Running running(watcher);
        deliver(watcher, down_message(watcher, *identity, {.pid = pid, .name = name, .tag = options.tag},
                                      atom(watcher, "noproc"_atom)));
        forget_alias(*identity, false);
        return *reference;
    }
    watcher.signals().monitor(*identity, pid);
    target->signals().watch(*identity, {.pid = pid_of(watcher), .name = name, .tag = options.tag});
    return *reference;
}

bool Executor::demonitor(ProcessContext &watcher, const ReferenceIdentity &reference) noexcept {
    auto &executor = of(watcher);
    const std::scoped_lock lock(executor.mutex_);
    executor.forget_alias(reference, false);
    const auto pid = watcher.signals().demonitor(reference);
    if (!pid) {
        return false;
    }
    if (auto *port = executor.open(*pid)) {
        port->watchers.erase(reference);
    } else if (auto *target = find(watcher, *pid)) {
        target->signals().unwatch(reference);
    }
    return true;
}

void Executor::exit(ProcessContext &sender, Word pid, const Term &reason, bool self_normal) {
    const std::scoped_lock lock(mutex_);
    if (is_port_word(pid)) {
        exit_port(sender, pid, reason);
        return;
    }
    auto *target = find(sender, pid);
    if (!target) {
        return;
    }
    if (busy(target)) {
        block(sender, *target);
    }
    const Running running(sender);
    signal(*target, pid_of(sender), reason,
           self_normal && target == &sender ? SignalKind::self_exit : SignalKind::exit);
    drain();
}

void Executor::signal(ProcessContext &target, Word from, const Term &reason, SignalKind kind) {
    if (target.generated_calls().failure()) {
        // The target has already ended.
        return;
    }
    if (kind != SignalKind::link && is_atom(reason, "kill")) {
        end(target, atom(target, "killed"_atom));
    } else if (target.signals().trap_exit()) {
        deliver(target, exit_message(target, from, reason));
    } else if (!is_atom(reason, "normal") || kind == SignalKind::self_exit) {
        end(target, reason);
    }
}

void Executor::deliver(ProcessContext &process, const TermResult<Term> &message) {
    if (!message) {
        process.generated_calls().fail_service(term_status(message.error()));
        retire(process);
        return;
    }
    process.mailbox().deliver(message->word());
    if (const auto woken = wake(process); !woken) {
        throw std::bad_alloc();
    }
}

void Executor::end(ProcessContext &target, const Term &reason) {
    auto &calls = target.generated_calls();
    if (const auto copy = reason.copy_to(target.heap())) {
        calls.fail({.code = CallError::exited, .reason = ErrorReason::raised_exit, .value = *copy});
    } else {
        calls.fail_service(term_status(copy.error()));
    }
    retire(target);
}

void Executor::retire(ProcessContext &process) {
    if (&process != running_) {
        withdraw(process);
        ending_.push_back(&process);
    }
}

void Executor::withdraw(ProcessContext &process) {
    for (const auto &queue : queues_) {
        std::erase(queue->processes, &process);
    }
    if (parked_.erase(&process) != 0) {
        cancel(process);
    }
    auto &state = schedule(process);
    if (const auto waited = schedules_.find(std::exchange(state.blocked_on, nullptr)); waited != schedules_.end()) {
        std::erase(waited->second.blockers, &process);
    }
    state.ready = false;
    release(process);
}

void Executor::finish(ProcessContext &process) {
    release_suspends(process);
    if (&process == main_ || ends_program(process)) {
        stop(process);
        return;
    }
    if (auto *peer = busy_peer(process)) {
        // Signal the peer once its slice ended; until then the ended process waits among its blockers.
        auto &state = schedule(process);
        state.ending = true;
        state.blocked_on = peer;
        schedule(*peer).blockers.push_back(&process);
        return;
    }
    if (!notify(process)) {
        stop(process);
        return;
    }
    report_exit(process);
    destroy(process);
}

ProcessContext *Executor::busy_link(ProcessContext &process, Word link) noexcept {
    if (const auto *port = open(link)) {
        return busy_peer(*port);
    }
    auto *peer = find(process, link);
    return peer && busy(peer) ? peer : nullptr;
}

ProcessContext *Executor::busy_peer(ProcessContext &process) noexcept {
    for (const auto pid : process.signals().links()) {
        if (auto *peer = busy_link(process, pid)) {
            return peer;
        }
    }
    for (const auto &[reference, watcher] : process.signals().watchers()) {
        if (auto *peer = find(process, watcher.pid); peer && busy(peer)) {
            return peer;
        }
    }
    return nullptr;
}

bool Executor::notify(ProcessContext &process) {
    auto &signals = process.signals();
    names_.erase(signals.name());
    // Tables go first, as in OTP: a monitor's 'DOWN' message finds them deleted or inherited.
    release_tables(process);
    for (const auto &[reference, monitored] : signals.take_monitors()) {
        if (auto *port = open(monitored.pid)) {
            port->watchers.erase(reference);
        } else if (auto *target = find(process, monitored.pid)) {
            target->signals().unwatch(reference);
        }
    }
    const auto links = signals.take_links();
    const auto watchers = signals.take_watchers();
    if (links.empty() && watchers.empty()) {
        return true;
    }
    const auto reason = exit_reason(process);
    if (!reason) {
        auto &calls = process.generated_calls();
        calls.clear();
        calls.fail_service(term_status(reason.error()));
        return false;
    }
    notify(process, *reason, links, watchers);
    return true;
}

void Executor::notify(ProcessContext &process, const Term &reason, const std::vector<Word> &links,
                      const Signals::Monitors &watchers) {
    const auto from = pid_of(process);
    notify_ports(process, reason, links);
    for (const auto pid : links) {
        if (auto *target = find(process, pid)) {
            target->signals().unlink(from);
            signal(*target, from, reason, SignalKind::link);
        }
    }
    for (const auto &[reference, monitor] : watchers) {
        auto *watcher = find(process, monitor.pid);
        if (watcher && !watcher->generated_calls().failure()) {
            watcher->signals().demonitor(reference);
            forget_alias(reference, false);
            deliver(*watcher,
                    down_message(*watcher, reference, {.pid = from, .name = monitor.name, .tag = monitor.tag}, reason));
        }
    }
}

bool Executor::register_name(ProcessContext &context, Word name, Word pid) {
    const std::scoped_lock lock(mutex_);
    if (auto *port = open(pid)) {
        if (port->name != 0 || lookup(context, name) != 0) {
            return false;
        }
        names_.insert_or_assign(name, pid);
        port->name = name;
        return true;
    }
    auto *process = find(context, pid);
    if (!process || process->signals().name() != 0 || lookup(context, name) != 0) {
        return false;
    }
    names_.insert_or_assign(name, pid);
    process->signals().set_name(name);
    return true;
}

bool Executor::unregister(ProcessContext &context, Word name) noexcept {
    const std::scoped_lock lock(mutex_);
    const auto pid = lookup(context, name);
    if (pid == 0) {
        return false;
    }
    names_.erase(name);
    if (auto *port = open(pid)) {
        port->name = 0;
    } else {
        find(context, pid)->signals().set_name(0);
    }
    return true;
}

Word Executor::whereis(ProcessContext &context, Word name) const noexcept {
    const std::scoped_lock lock(mutex_);
    return lookup(context, name);
}

Word Executor::lookup(ProcessContext &context, Word name) const noexcept {
    const auto found = names_.find(name);
    // A host may release a registered context outside the executor: only a live process or open port counts.
    if (found == names_.end()) {
        return 0;
    }
    return find(context, found->second) || open(found->second) ? found->second : 0;
}

std::vector<Word> Executor::registered(ProcessContext &context) const {
    const std::scoped_lock lock(mutex_);
    std::vector<Word> names;
    for (const auto &[name, pid] : names_) {
        if (find(context, pid) || open(pid)) {
            names.push_back(name);
        }
    }
    return names;
}

void Executor::stop(ProcessContext &process) {
    if (!finished_) {
        finished_ = &process;
        wake_all();
    } else {
        stopped_.push_back(&process);
    }
}

void Executor::drain() {
    while (!ending_.empty()) {
        auto &process = *ending_.front();
        ending_.pop_front();
        finish(process);
    }
}

void Executor::fail_program() noexcept {
    if (finished_ || !main_) {
        return;
    }
    failed_ = true;
    if (!busy(main_)) {
        fail_main();
    }
}

void Executor::fail_main() noexcept {
    try {
        withdraw(*main_);
    } catch (const std::bad_alloc &) {
        // The main process ends the program now; queues are released by clear().
        for (const auto &queue : queues_) {
            queue->processes.clear();
        }
    }
    auto &calls = main_->generated_calls();
    calls.clear();
    calls.fail_service(abi::v1::Status::out_of_memory);
    finished_ = main_;
    wake_all();
}
} // namespace clause::runtime::detail
