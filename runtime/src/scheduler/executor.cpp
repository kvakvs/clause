#include "executor.hpp"
#include "../builtins/support.hpp"
#include "../memory/message_buffer.hpp"
#include "../process/exits.hpp"
#include "../process/identities.hpp"
#include "../runtime_state.hpp"
#include "../terms/funs.hpp"
#include <array>
#include <chrono>
#include <clause/abi/equality.hpp>
#include <clause/abi/modules.hpp>
#include <cstdio>
#include <exception>
#include <new>
#include <optional>
#include <string>
#include <thread>

namespace clause::runtime::detail {
namespace {
using abi::v1::FrameDescriptor;
using abi::v1::Status;

// The term error of a failed context creation.
TermError creation_error(Status status) {
    return status == Status::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}
} // namespace

Word pid_of(ProcessContext &process) noexcept {
    return TermFactory(process).pid(process.identity()).transform(&Term::word).value_or(0);
}

bool ends_program(ProcessContext &process) noexcept {
    const auto &failure = process.generated_calls().failure();
    return failure && failure->code != CallError::erlang_exception && failure->code != CallError::exited;
}

Executor &Executor::of(ProcessContext &context) noexcept { return context.runtime().impl_->executor; }

bool Executor::start(ProcessContext &process, const FrameDescriptor &function) noexcept {
    const std::scoped_lock lock(mutex_);
    process.generated_calls().enter();
    if (!process.stack().start(function)) {
        return false;
    }
    try {
        // A spawned process starts on its parent's worker (run queues).
        schedules_[&process].home = worker_ ? worker_index_ : 0;
        push(process);
        return true;
    } catch (const std::bad_alloc &) {
        schedules_.erase(&process);
        process.generated_calls().fail_service(Status::out_of_memory);
        return false;
    }
}

template <typename Prepare>
TermResult<Term> Executor::create(ProcessContext &parent, Prepare prepare, const SpawnOptions &options) noexcept {
    const std::scoped_lock lock(mutex_);
    auto &runtime = parent.runtime();
    const auto created = runtime.create_context();
    if (!created) {
        return std::unexpected(creation_error(created.error()));
    }
    auto &child = **created;
    // A spawned process inherits its parent's group leader (step 41G1).
    const auto leader = parent.signals().group_leader();
    child.signals().set_group_leader(leader != 0 ? leader : pid_of(parent));
    child.generated_calls().enter();
    if (options.min_heap_words) {
        child.heap().set_min_heap_words(*options.min_heap_words);
    }
    const auto frame = prepare(child);
    if (!frame) {
        runtime.destroy_context(&child);
        return std::unexpected(frame.error());
    }
    if (*frame) {
        child.stack().start(*static_cast<const FrameDescriptor *>(*frame));
    }
    const auto pid = TermFactory(parent).pid(child.identity());
    try {
        auto result = pid ? adopt(parent, child, *pid, options) : pid;
        if (!result) {
            destroy(child);
        }
        return result;
    } catch (const std::bad_alloc &) {
        destroy(child);
        return std::unexpected(TermError::out_of_memory);
    }
}

namespace {
// The reference of a spawn's monitor: spawn_request's request id (step 41G2), or a new one.
TermResult<Term> monitor_reference(ProcessContext &parent, const SpawnOptions &options) {
    TermFactory factory(parent);
    return options.monitor_reference ? factory.reference(*options.monitor_reference) : factory.make_reference();
}
} // namespace

void Executor::watch_child(ProcessContext &parent, ProcessContext &child, const ReferenceIdentity &monitor,
                           const MonitorOptions &options) {
    parent.signals().monitor(monitor, pid_of(child));
    child.signals().watch(monitor, {.pid = pid_of(parent), .tag = options.tag});
    if (options.alias) {
        add_alias(monitor, pid_of(parent), *options.alias);
    }
}

TermResult<Term> Executor::adopt(ProcessContext &parent, ProcessContext &child, const Term &pid,
                                 const SpawnOptions &options) {
    auto result = TermResult<Term>(pid);
    std::optional<ReferenceIdentity> monitor;
    if (options.monitor) {
        const auto reference = monitor_reference(parent, options);
        const auto identity = reference.and_then([](const Term &term) { return term.reference_value(); });
        if (!identity) {
            return std::unexpected(identity.error());
        }
        result = TermFactory(parent).tuple(std::array{pid, *reference});
        monitor = *identity;
    }
    if (!result) {
        return result;
    }
    try {
        schedules_[&child] = {};
        if (options.link) {
            parent.signals().link(pid.word());
            child.signals().link(pid_of(parent));
        }
        if (monitor) {
            watch_child(parent, child, *monitor, options.monitor_options);
        }
        push(child);
    } catch (...) {
        parent.signals().unlink(pid.word());
        if (monitor) {
            parent.signals().demonitor(*monitor);
        }
        throw;
    }
    return result;
}

TermResult<Term> Executor::spawn(ProcessContext &parent, const Term &fun, const SpawnOptions &options) noexcept {
    return create(
        parent,
        [&](ProcessContext &child) -> TermResult<const void *> {
            const auto copy = fun.copy_to(child.heap());
            if (!copy) {
                return std::unexpected(copy.error());
            }
            return apply_list_service(child, copy->word(), abi::v1::empty_list, child.stack().registers());
        },
        options);
}

TermResult<Term> Executor::spawn(ProcessContext &parent, const InitialCall &call,
                                 const SpawnOptions &options) noexcept {
    return create(
        parent,
        [&](ProcessContext &child) -> TermResult<const void *> {
            const auto copy = call.arguments.copy_to(child.heap());
            if (!copy) {
                return std::unexpected(copy.error());
            }
            return call_list_service(child, call.module.word(), call.function.word(), copy->word(),
                                     child.stack().registers());
        },
        options);
}

ProcessContext *Executor::find(ProcessContext &context, Word pid) noexcept { return of(context).process(pid); }

ProcessContext *Executor::process(Word pid) const noexcept {
    if (TermTag{pid}.get_kind() != TermKind::local_pid) {
        return nullptr;
    }
    const auto &processes = runtime_.processes;
    const auto found = processes.find(pid_number(pid));
    return found == processes.end() ? nullptr : found->second;
}

bool Executor::alive(ProcessContext &context, Word pid) noexcept {
    const std::scoped_lock lock(of(context).mutex_);
    return find(context, pid) != nullptr;
}

TermResult<void> Executor::send(ProcessContext &sender, Word pid, const Term &message) {
    auto &executor = of(sender);
    // A large message is copied into a buffer of its own before the lock, then handed over (message hand-off).
    auto large = MessageBuffer::large(message, sender, MessageBuffer::ADOPTED_WORDS)
                     ? MessageBuffer::copy_of(message, sender)
                     : TermResult<std::shared_ptr<const MessageBuffer>>{nullptr};
    if (!large) {
        return std::unexpected(large.error());
    }
    const std::scoped_lock lock(executor.mutex_);
    auto *found = find(sender, pid);
    if (!found) {
        return {};
    }
    auto &receiver = *found;
    if (*large) {
        return executor.hand_over(receiver, *large);
    }
    if (executor.busy(&receiver)) {
        // Buffered sends: a message to a process running on another worker waits in a buffer of its own and is
        // delivered, in order, when that slice ends, instead of blocking the sender until then.
        const auto buffered = MessageBuffer::copy_of(message, sender);
        if (!buffered) {
            return std::unexpected(buffered.error());
        }
        executor.post(receiver, *buffered);
        return {};
    }
    const auto copy = message.copy_to(receiver.heap());
    if (!copy) {
        return std::unexpected(copy.error());
    }
    receiver.mailbox().deliver(copy->word());
    return executor.wake(receiver);
}

TermResult<void> Executor::hand_over(ProcessContext &receiver, const std::shared_ptr<const MessageBuffer> &message) {
    if (busy(&receiver)) {
        post(receiver, message);
        return {};
    }
    const auto taken = MessageBuffer::take(message, receiver);
    if (!taken) {
        return std::unexpected(taken.error());
    }
    receiver.mailbox().deliver(taken->word());
    return wake(receiver);
}

Executor::Schedule &Executor::schedule(ProcessContext &process) { return schedules_[&process]; }

bool Executor::busy(ProcessContext *process) const noexcept {
    const auto found = schedules_.find(process);
    return process != running_ && found != schedules_.end() && found->second.running;
}

void Executor::block(ProcessContext &process, ProcessContext &target) {
    schedule(process).blocked_on = &target;
    throw builtins::Blocked{};
}

void Executor::push(ProcessContext &process, bool front) {
    auto &state = schedule(process);
    if (state.holds > 0) {
        state.ready = true;
        return;
    }
    const auto home = place(state.home);
    state.home = home;
    auto &queue = queues_[home]->processes;
    if (front) {
        queue.push_front(&process);
    } else {
        queue.push_back(&process);
    }
    signal_queued(home);
}

std::size_t Executor::place(std::size_t home) const noexcept {
    home = std::min(home, queues_.size() - 1);
    if (worker_ && home == worker_index_) {
        return home;
    }
    // A process that a running process wakes, whose worker sleeps with nothing queued, joins the waker's worker: a
    // chain of messages stays on one worker instead of waking another per hop, and stealing spreads work again once
    // a queue holds two. Any other waker (a port task, the timer or I/O thread, another worker between slices) hands
    // a process whose worker is busy to an idle worker, which runs it at once.
    const auto &queue = *queues_[home];
    if (worker_ && running_ != nullptr) {
        return queue.idle && queue.processes.empty() ? worker_index_ : home;
    }
    return queue.idle ? home : idle_worker().value_or(home);
}

void Executor::signal_queued(std::size_t home) {
    // Lazy wakeups, as ERTS keeps processes that talk to each other on one scheduler: a busy worker takes its next
    // queued process itself after the current slice, so another worker is woken only for more than that one.
    auto &queue = *queues_[home];
    if (queue.idle && !(worker_ && worker_index_ == home)) {
        queue.wake.notify_one();
    }
    if (queue.processes.size() > 1) {
        wake_idle(home);
    }
}

std::optional<std::size_t> Executor::idle_worker() const noexcept {
    for (std::size_t index = 0; index < queues_.size(); ++index) {
        if (queues_[index]->idle && queues_[index]->processes.empty()) {
            return index;
        }
    }
    return std::nullopt;
}

void Executor::wake_idle(std::size_t except) {
    for (std::size_t index = 0; index < queues_.size(); ++index) {
        if (index != except && queues_[index]->idle) {
            queues_[index]->wake.notify_one();
            return;
        }
    }
}

void Executor::wake_all() {
    for (const auto &queue : queues_) {
        queue->wake.notify_all();
    }
}

std::size_t Executor::queued() const noexcept {
    std::size_t count = 0;
    for (const auto &queue : queues_) {
        count += queue->processes.size();
    }
    return count;
}

ProcessContext *Executor::steal(std::size_t thief) {
    RunQueue *victim = nullptr;
    for (std::size_t index = 0; index < queues_.size(); ++index) {
        auto &queue = *queues_[index];
        // A sleeping worker's only process is taken too: this worker runs it before that one wakes.
        const auto spare = queue.idle ? std::size_t{1} : std::size_t{2};
        if (index != thief && queue.processes.size() >= spare &&
            (!victim || queue.processes.size() > victim->processes.size())) {
            victim = &queue;
        }
    }
    if (!victim) {
        return nullptr;
    }
    auto *process = victim->processes.back();
    victim->processes.pop_back();
    schedule(*process).home = thief;
    return process;
}

void Executor::balance(std::size_t index) {
    // ERTS migrates processes between schedulers periodically; this compares one neighbor every few slices.
    constexpr std::size_t BALANCE_SLICES = 4;
    auto &own = *queues_[index];
    if (++own.since_balance < BALANCE_SLICES || queues_.size() < 2) {
        return;
    }
    own.since_balance = 0;
    auto &neighbor = *queues_[(index + 1) % queues_.size()];
    if (neighbor.processes.size() >= own.processes.size() + 2) {
        auto *process = neighbor.processes.back();
        neighbor.processes.pop_back();
        schedule(*process).home = index;
        own.processes.push_back(process);
    }
}

ProcessContext *Executor::next_process(std::size_t index) {
    balance(index);
    auto &own = queues_[index]->processes;
    if (own.empty()) {
        return steal(index);
    }
    auto *process = own.front();
    own.pop_front();
    return process;
}

TermResult<void> Executor::wake(ProcessContext &receiver) {
    if (!receiver.stack().waiting()) {
        return {};
    }
    try {
        push(receiver);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
    receiver.stack().wake();
    parked_.erase(&receiver);
    cancel(receiver);
    return {};
}

void Executor::cancel(ProcessContext &process) noexcept {
    if (const auto found = schedules_.find(&process); found != schedules_.end()) {
        wheel_.cancel(found->second.timer);
    }
}

void Executor::expire(ProcessContext &process) {
    parked_.erase(&process);
    process.stack().wake();
    push(process);
}

std::uint64_t Executor::tick_of(const std::chrono::steady_clock::time_point deadline) const noexcept {
    if (deadline <= epoch_) {
        return 0;
    }
    const auto elapsed = std::chrono::ceil<std::chrono::milliseconds>(deadline - epoch_);
    return static_cast<std::uint64_t>(elapsed.count());
}

void Executor::park(ProcessContext &process) {
    parked_.insert(&process);
    if (const auto deadline = process.mailbox().deadline()) {
        auto &timer = schedule(process).timer;
        timer.owner = &process;
        const auto earliest = wheel_.next();
        wheel_.arm(timer, tick_of(*deadline));
        // The timer thread sleeps until the earliest slot; an earlier one makes it plan again.
        if (!earliest || timer.tick < *earliest) {
            timer_signal_.notify_one();
        }
    }
}

void Executor::tick(std::unique_lock<std::mutex> &lock) {
    const auto next = wheel_.next();
    if (!next) {
        timer_signal_.wait(lock);
        return;
    }
    const auto wake = epoch_ + std::chrono::milliseconds(*next);
    if (timer_signal_.wait_until(lock, wake) == std::cv_status::no_timeout) {
        return;
    }
    // One clock reading per wake: the wheel moves to the tick it shows and fires what is due.
    ++clock_reads_;
    const auto reached = std::chrono::floor<std::chrono::milliseconds>(std::chrono::steady_clock::now() - epoch_);
    for (auto *timer : wheel_.advance(static_cast<std::uint64_t>(reached.count()))) {
        if (timer->message) {
            fire(*static_cast<MessageTimer *>(timer->owner));
        } else {
            expire(*static_cast<ProcessContext *>(timer->owner));
        }
    }
}

void Executor::time() noexcept {
    std::unique_lock lock(mutex_);
    while (!timers_stop_) {
        try {
            tick(lock);
        } catch (const std::bad_alloc &) {
            fail_program();
        } catch (...) {
            std::terminate();
        }
    }
}

void Executor::work(const std::size_t index) noexcept {
    worker_ = true;
    worker_index_ = index;
    std::unique_lock lock(mutex_);
    while (!finished_) {
        try {
            if (const auto port = next_port()) {
                run_port(*port);
                continue;
            }
            auto *next = next_process(index);
            if (!next) {
                auto &queue = *queues_[index];
                queue.idle = true;
                queue.wake.wait(lock);
                queue.idle = false;
                continue;
            }
            auto &process = *next;
            schedule(process).running = true;
            ++slices_;
            lock.unlock();
            running_ = &process;
            const bool ended = process.stack().run(SLICE_REDUCTIONS);
            running_ = nullptr;
            lock.lock();
            after(process, ended);
        } catch (const std::bad_alloc &) {
            fail_program();
        } catch (...) {
            // A failing mutex or condition variable leaves the executor without a consistent state.
            std::terminate();
        }
    }
    worker_ = false;
    wake_all();
}

void Executor::after(ProcessContext &process, bool ended) {
    auto &state = schedule(process);
    state.running = false;
    const auto blockers = std::exchange(state.blockers, {});
    auto *blocked_on = std::exchange(state.blocked_on, nullptr);
    const auto throttled = std::exchange(state.throttling, {});
    release(process);
    // The process is placed before its blockers act: a message they deliver must find it parked, not mid-way.
    if (failed_ && &process == main_) {
        fail_main();
    } else if (ended) {
        finish(process);
    } else {
        hold(blockers, process);
        // A process suspended on a busy port stays out of the queue until the port lets it go.
        if (state.port_wait == 0) {
            place(process, blocked_on);
        }
        for (const auto &event : std::exchange(state.events, {})) {
            apply(process, event);
        }
        for (const auto &message : std::exchange(state.messages, {})) {
            deliver(process, MessageBuffer::take(message, process));
        }
    }
    resume(blockers);
    unthrottle(throttled);
    drain();
}

void Executor::hold(const std::vector<ProcessContext *> &blockers, ProcessContext &holder) {
    for (auto *blocker : blockers) {
        auto &state = schedule(*blocker);
        if (!state.ending) {
            state.holding.push_back(pid_of(holder));
            ++schedule(holder).holds;
        }
    }
}

void Executor::resume(const std::vector<ProcessContext *> &blockers) {
    // Pushing to the front in reverse keeps the blockers in the order they blocked.
    for (auto at = blockers.rbegin(); at != blockers.rend(); ++at) {
        auto &blocker = **at;
        auto &state = schedule(blocker);
        state.blocked_on = nullptr;
        if (std::exchange(state.ending, false)) {
            finish(blocker);
        } else {
            push(blocker, true);
        }
    }
}

void Executor::place(ProcessContext &process, ProcessContext *blocked_on) {
    // The process waited for may have ended since; its address is then only compared, never used.
    if (blocked_on && busy(blocked_on)) {
        schedule(*blocked_on).blockers.push_back(&process);
        schedule(process).blocked_on = blocked_on;
    } else if (process.stack().waiting()) {
        park(process);
    } else {
        push(process);
    }
}

void Executor::release(ProcessContext &process) {
    for (const auto pid : std::exchange(schedule(process).holding, {})) {
        auto *held = find(process, pid);
        if (!held) {
            continue;
        }
        auto &state = schedule(*held);
        if (state.holds == 0) {
            continue;
        }
        --state.holds;
        if (state.holds == 0 && std::exchange(state.ready, false)) {
            push(*held);
        }
    }
}

ProcessContext &Executor::run(ProcessContext &main) noexcept {
    {
        const std::scoped_lock lock(mutex_);
        main_ = &main;
        finished_ = nullptr;
        failed_ = false;
    }
    std::optional<std::thread> timers;
    try {
        timers.emplace([this] { time(); });
    } catch (const std::exception &) {
        // Without the timer thread receive timeouts cannot expire: the program fails like an allocation failure.
        const std::scoped_lock lock(mutex_);
        fail_program();
    }
    std::vector<std::thread> helpers;
    const auto workers = main.runtime().impl_->options.schedulers;
    {
        const std::scoped_lock lock(mutex_);
        while (queues_.size() < workers) {
            try {
                queues_.push_back(std::make_unique<RunQueue>());
            } catch (const std::bad_alloc &) {
                break; // Run on the queues there are.
            }
        }
    }
    for (std::size_t count = 1; count < std::min(workers, queues_.size()); ++count) {
        try {
            helpers.emplace_back([this, count] { work(count); });
        } catch (const std::exception &) {
            // Run on the workers started so far.
            break;
        }
    }
    work(0);
    for (auto &helper : helpers) {
        helper.join();
    }
    {
        const std::scoped_lock lock(mutex_);
        timers_stop_ = true;
        timer_signal_.notify_one();
    }
    if (timers) {
        timers->join();
    }
    {
        const std::scoped_lock lock(mutex_);
        timers_stop_ = false;
        if (runtime_.profile) {
            runtime_.profile->scheduling({.slices = slices_, .clock_reads = clock_reads_});
        }
    }
    return *finished_;
}

void Executor::destroy(ProcessContext &process) noexcept {
    cancel(process);
    drop_timers(pid_of(process));
    drop_aliases(pid_of(process));
    schedules_.erase(&process);
    process.runtime().destroy_context(&process);
}

Executor::~Executor() {
    if (reactor_) {
        reactor_->stop();
    }
}

void Executor::clear() noexcept {
    // The I/O thread and the Windows blocking readers stop outside the lock: a delivery in progress waits for it.
    // Workers have stopped, so nothing else creates or replaces the services meanwhile.
    Reactor *reactor = nullptr;
    IoService *io = nullptr;
    {
        const std::scoped_lock lock(mutex_);
        reactor = reactor_.get();
        io = io_.get();
    }
    if (reactor) {
        reactor->stop();
    }
    if (io) {
        io->close();
    }

    // The services are destroyed after the lock (stopping one takes the I/O gate, which the I/O thread holds while it
    // takes this lock), in member order reversed: I/O, sockets, then the reactor's io_context.
    struct Services {
        std::unique_ptr<Reactor> reactor;
        std::unique_ptr<SocketService> sockets;
        std::unique_ptr<IoService> io;
    } services;

    const std::scoped_lock lock(mutex_);
    // The wheel links timers embedded in the schedules: unlink them while the schedules still exist.
    wheel_.clear();
    for (const auto &[process, state] : schedules_) {
        if (process != finished_) {
            process->runtime().destroy_context(process);
        }
    }
    schedules_.clear();
    for (const auto &queue : queues_) {
        queue->processes.clear();
    }
    port_queue_.clear();
    parked_.clear();
    ending_.clear();
    stopped_.clear();
    names_.clear();
    // Drivers go before the services.
    ports_.clear();
    services = {std::move(reactor_), std::move(sockets_), std::move(io_)};
    main_ = finished_ = nullptr;
    failed_ = false;
}
} // namespace clause::runtime::detail

namespace clause::runtime {
namespace {
// Append the spelling of a compiled atom slot without touching runtime atom tables.
void append_atom(std::string &text, const abi::v1::ModuleDescriptor &module, const std::size_t slot) {
    if (slot < module.atom_count && module.atoms[slot].spelling) {
        text.append(module.atoms[slot].spelling, module.atoms[slot].size);
    }
}

// The Erlang frames of `process`, one `  module:function/arity` line each, innermost first.
std::string erlang_stack(ProcessContext &process, std::size_t &count) {
    std::string text;
    process.stack().named_frames([&](const abi::v1::FrameDescriptor &frame) {
        text += "  ";
        append_atom(text, *frame.module, frame.module_atom);
        text += ':';
        append_atom(text, *frame.module, frame.function_atom);
        text += '/' + std::to_string(frame.arity) + '\n';
        ++count;
    });
    return text;
}
} // namespace

std::size_t debug_erlang_stack() noexcept {
    auto *process = detail::Executor::running();
    std::size_t count = 0;
    try {
        // One write, so a debugger forwarding the program's output never splits the stack.
        const auto text = process ? erlang_stack(*process, count) : "clause: no Erlang process runs on this thread\n";
        static_cast<void>(std::fwrite(text.data(), 1, text.size(), stderr));
        static_cast<void>(std::fflush(stderr));
    } catch (...) {
        return 0;
    }
    return count;
}
} // namespace clause::runtime
