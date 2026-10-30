#include "executor.hpp"
#include "../builtins/support.hpp"
#include "../memory/message_buffer.hpp"
#include "../process/exits.hpp"
#include "../process/identities.hpp"
#include "../runtime_state.hpp"
#include "../terms/funs.hpp"
#include "cpus.hpp"
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

// The first capacity of the list of contexts waiting to be freed after the lock (process creation and teardown).
constexpr std::size_t RETIRED_CONTEXTS = 16;

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

void Executor::evacuate_parked() {
    const std::scoped_lock lock(mutex_, runtime_.contexts_mutex);
    for (const auto &[process, owner] : runtime_.contexts) {
        // A process suspended on a busy port is neither queued nor parked, and may stay so.
        const auto &state = owner->schedule();
        if (state.parked || (state.port_wait != 0 && !state.running)) {
            evacuate(*owner);
        }
    }
}

void Executor::evacuate(ProcessContext &process) {
    if (!runtime_.persistent_terms.behind(process)) {
        return;
    }
    process.heap().request_evacuation();
    // Between slices every word of the process is in its roots.
    const SafePoint safe(process.generated_calls());
    if (!process.heap().collect()) {
        // Tried again when it next runs, or at the next put or erase.
        process.heap().request_evacuation();
    }
}

bool Executor::start(ProcessContext &process, const FrameDescriptor &function) noexcept {
    const std::scoped_lock lock(mutex_);
    process.generated_calls().enter();
    if (!process.stack().start(function)) {
        return false;
    }
    try {
        // A spawned process starts on its parent's worker (run queues).
        auto &state = schedule(process);
        state.started = true;
        state.home = worker_ ? worker_index_ : 0;
        push(process);
        return true;
    } catch (const std::bad_alloc &) {
        schedule(process).started = false;
        process.generated_calls().fail_service(Status::out_of_memory);
        return false;
    }
}

template <typename Prepare>
TermResult<Term> Executor::create(ProcessContext &parent, Prepare prepare, const SpawnOptions &options) noexcept {
    auto &runtime = parent.runtime();
    // Nothing else reaches the child before it is published, so it is built and loaded outside the lock (process
    // creation and teardown).
    auto built = runtime.build_context(runtime_.options.process_heap, runtime_.options.process_stack);
    if (!built) {
        return std::unexpected(creation_error(built.error()));
    }
    auto &child = **built;
    child.generated_calls().enter();
    if (const auto words = options.min_heap_words.value_or(min_heap_words_.load(std::memory_order_relaxed))) {
        child.heap().set_min_heap_words(words);
    }
    const auto frame = prepare(child);
    const auto pid = frame ? TermFactory(parent).pid(child.identity()) : std::unexpected(frame.error());
    if (!pid) {
        runtime.free_context(std::move(*built));
        return pid;
    }
    if (*frame) {
        child.stack().start(*static_cast<const FrameDescriptor *>(*frame));
    }
    return !options.link && !options.monitor ? launch(parent, std::move(*built), *pid)
                                             : launch_linked(parent, std::move(*built), *pid, options);
}

TermResult<Term> Executor::launch_linked(ProcessContext &parent, std::unique_ptr<ProcessContext> built, const Term &pid,
                                         const SpawnOptions &options) noexcept {
    auto &child = *built;
    const std::scoped_lock lock(mutex_);
    if (const auto published = parent.runtime().publish_context(std::move(built)); !published) {
        return std::unexpected(creation_error(published.error()));
    }
    retire_arrays();
    // A spawned process inherits its parent's group leader (step 41G1).
    const auto leader = parent.signals().group_leader();
    child.signals().set_group_leader(leader != 0 ? leader : pid_of(parent));
    try {
        auto result = adopt(parent, child, pid, options);
        if (!result) {
            destroy(child);
        }
        return result;
    } catch (const std::bad_alloc &) {
        destroy(child);
        return std::unexpected(TermError::out_of_memory);
    }
}

TermResult<Term> Executor::launch(ProcessContext &parent, std::unique_ptr<ProcessContext> built,
                                  const Term &pid) noexcept {
    auto &child = *built;
    // A spawned process inherits its parent's group leader (step 41G1); the running parent's own cannot change
    // meanwhile, as group_leader/2 waits for its slice to end.
    const auto leader = parent.signals().group_leader();
    child.signals().set_group_leader(leader != 0 ? leader : pid_of(parent));
    child.schedule().started = true;
    if (const auto published = parent.runtime().publish_context(std::move(built)); !published) {
        return std::unexpected(creation_error(published.error()));
    }
    try {
        push(child);
        return pid;
    } catch (const std::bad_alloc &) {
        const std::scoped_lock lock(mutex_);
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
        const auto identity = reference.transform([](const Term &term) { return term.reference_value_unchecked(); });
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
        schedule(child).started = true;
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
    return runtime_.processes.find(pid_number(pid));
}

bool Executor::alive(ProcessContext &context, Word pid) noexcept {
    const std::scoped_lock lock(of(context).mutex_);
    return find(context, pid) != nullptr;
}

TermResult<void> Executor::send(ProcessContext &sender, Word pid, const Term &message) {
    auto &executor = of(sender);
    if (pid == sender.self) {
        // The sender's own heap holds the message already.
        sender.mailbox().deliver(message.word());
        return {};
    }
    if (TermTag{pid}.get_kind() != TermKind::local_pid) {
        return {};
    }
    // The message's graph is walked once, before any lock (single message walk).
    OutgoingMessage outgoing(sender);
    if (const auto walked = outgoing.walk(message); !walked) {
        return walked;
    }
    const auto posting = executor.posting_arrivals(sender, pid);
    if (!posting) {
        return {};
    }
    return *posting != nullptr ? executor.send_posted(sender, {.pid = pid, .arrivals = **posting}, outgoing)
                               : executor.send_locked(sender, pid, outgoing);
}

std::optional<Arrivals *> Executor::posting_arrivals(ProcessContext &sender, Word pid) const noexcept {
    // A receiver parked at the last send to it most likely is again, and its waker locks anyway: no lookup then. Only
    // workers look up without the lock, as only their slice boundaries keep what they find allocated (quiescent
    // reclamation).
    auto &hint = sender.mailbox().send_hint();
    if (!worker_ || (hint.pid == pid && hint.parked)) {
        return nullptr;
    }
    auto *arrivals = runtime_.processes.arrivals(pid_number(pid));
    if (arrivals == nullptr) {
        return std::nullopt;
    }
    // Only a parked receiver needs the lock to be woken, so it gets the message copied straight into its heap; any
    // other takes it from its arrivals (lock-free posts).
    hint = {.pid = pid, .parked = arrivals->parked()};
    return hint.parked ? nullptr : arrivals;
}

TermResult<void> Executor::send_posted(ProcessContext &sender, PostedReceiver receiver, OutgoingMessage &message) {
    // A receiver that is not parked takes the message from its arrivals; no executor lock unless the post finds it
    // parked meanwhile and wakes it (lock-free posts).
    auto posted = message.arrival();
    if (!posted) {
        return std::unexpected(posted.error());
    }
    if (!receiver.arrivals.post(std::move(*posted))) {
        return {};
    }
    const std::scoped_lock lock(mutex_);
    auto *parked = find(sender, receiver.pid);
    return parked && parked->schedule().parked ? wake(*parked) : TermResult<void>{};
}

TermResult<void> Executor::send_locked(ProcessContext &sender, Word pid, OutgoingMessage &message) {
    const std::scoped_lock lock(mutex_);
    auto *found = find(sender, pid);
    if (!found) {
        return {};
    }
    auto &receiver = *found;
    sender.mailbox().send_hint() = {.pid = pid, .parked = receiver.schedule().parked};
    // It began a slice meanwhile, or earlier messages still wait among its arrivals: this one goes behind them, never
    // past them, and the sender copies no backlog under the lock.
    if (busy(&receiver) || receiver.mailbox().arrivals().pending()) {
        auto posted = message.arrival();
        if (!posted) {
            return std::unexpected(posted.error());
        }
        post(receiver, std::move(*posted));
        return {};
    }
    const auto copy = message.copy_to(receiver);
    if (!copy) {
        return std::unexpected(copy.error());
    }
    receiver.mailbox().deliver(copy->word());
    return wake(receiver);
}

TermResult<void> Executor::arrive(ProcessContext &receiver, Word message) {
    // Arrival buffers came before it.
    if (const auto taken = MessageBuffer::take_arrivals(receiver); !taken) {
        return taken;
    }
    receiver.mailbox().deliver(message);
    return wake(receiver);
}

Schedule &Executor::schedule(ProcessContext &process) { return process.schedule(); }

bool Executor::busy(ProcessContext *process) const noexcept {
    return process != running_ && process->schedule().running;
}

void Executor::block(ProcessContext &process, ProcessContext &target) {
    schedule(process).blocked_on = &target;
    schedule(process).blocked_pid = target.self;
    throw builtins::Blocked{};
}

void Executor::push(ProcessContext &process, bool front) {
    auto &state = schedule(process);
    if (state.holds > 0) {
        state.ready = true;
        return;
    }
    const std::scoped_lock queues(queues_mutex_);
    const auto home = place(state.home);
    state.home = home;
    auto &queue = queues_[home]->processes;
    if (front) {
        // Woken blockers run ahead of a handed-over process too; they are pushed under the executor lock, which guards
        // it (handed-over wakes).
        auto &handed = queues_[home]->handed;
        if (auto *next = handed.load(std::memory_order_relaxed)) {
            queue.push_front(next);
            handed.store(nullptr, std::memory_order_relaxed);
        }
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
    if (queue.runnable() > 1) {
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
    const std::scoped_lock queues(queues_mutex_);
    for (const auto &queue : queues_) {
        queue->wake.notify_all();
    }
}

std::size_t Executor::queued() const noexcept {
    const std::scoped_lock queues(queues_mutex_);
    std::size_t count = 0;
    for (const auto &queue : queues_) {
        count += queue->runnable();
    }
    return count;
}

ProcessContext *Executor::steal(std::size_t thief) {
    RunQueue *victim = nullptr;
    for (std::size_t index = 0; index < queues_.size(); ++index) {
        auto &queue = *queues_[index];
        // A sleeping worker's only process is taken too: this worker runs it before that one wakes.
        const auto spare = queue.idle ? std::size_t{1} : std::size_t{2};
        if (index != thief && !queue.processes.empty() && queue.runnable() >= spare &&
            (!victim || queue.runnable() > victim->runnable())) {
            victim = &queue;
        }
    }
    if (!victim) {
        return nullptr;
    }
    auto *process = victim->processes.pop_back();
    schedule(*process).home = thief;
    return process;
}

bool Executor::balance_due(std::size_t index) noexcept {
    // ERTS migrates processes between schedulers periodically; this compares one neighbor every few slices.
    constexpr std::size_t BALANCE_SLICES = 4;
    auto &own = *queues_[index];
    if (++own.since_balance < BALANCE_SLICES || queues_.size() < 2) {
        return false;
    }
    own.since_balance = 0;
    return true;
}

void Executor::balance(std::size_t index) {
    auto &own = *queues_[index];
    auto &neighbor = *queues_[(index + 1) % queues_.size()];
    if (!neighbor.processes.empty() && neighbor.runnable() >= own.runnable() + 2) {
        auto *process = neighbor.processes.pop_back();
        schedule(*process).home = index;
        own.processes.push_back(process);
    }
}

bool Executor::hand_over(ProcessContext &process) noexcept {
    // Only the worker itself hands over, while nothing else waits in its queue: the process would have been the
    // first one queued anyway, so the order of the run queue stays as it was (handed-over wakes).
    const auto &state = schedule(process);
    if (!worker_ || state.home != worker_index_ || state.holds > 0) {
        return false;
    }
    auto &own = *queues_[worker_index_];
    if (own.handed.load(std::memory_order_relaxed) != nullptr || own.processes.seen_size() != 0) {
        return false;
    }
    own.handed.store(&process, std::memory_order_relaxed);
    return true;
}

ProcessContext *Executor::handed_over(std::size_t index) {
    auto &own = *queues_[index];
    auto *handed = own.handed.load(std::memory_order_relaxed);
    if (handed == nullptr) {
        return nullptr;
    }
    // The neighbor is compared under the run queues' lock only when it may have a process to give.
    if (balance_due(index) && queues_[(index + 1) % queues_.size()]->processes.seen_size() >= 2) {
        const std::scoped_lock queues(queues_mutex_);
        balance(index);
    }
    // Only the executor lock's holders change it.
    own.handed.store(nullptr, std::memory_order_relaxed);
    return handed;
}

ProcessContext *Executor::next_process(std::size_t index) {
    if (balance_due(index)) {
        balance(index);
    }
    auto &own = queues_[index]->processes;
    return own.empty() ? steal(index) : own.pop_front();
}

TermResult<void> Executor::wake(ProcessContext &receiver) {
    if (!receiver.stack().waiting()) {
        return {};
    }
    try {
        if (!hand_over(receiver)) {
            push(receiver);
        }
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
    receiver.stack().wake();
    receiver.mailbox().arrivals().unpark();
    schedule(receiver).parked = false;
    cancel(receiver);
    return {};
}

void Executor::cancel(ProcessContext &process) noexcept { wheel_.cancel(process.schedule().timer); }

void Executor::expire(ProcessContext &process) {
    process.mailbox().arrivals().unpark();
    schedule(process).parked = false;
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
    // A message that arrived since the process last looked lets it run again instead (arrival buffers).
    if (!process.mailbox().arrivals().park()) {
        process.stack().wake();
        push(process);
        return;
    }
    schedule(process).parked = true;
    // A retirement may have come while it ran, after evacuate_parked last looked (persistent term reclamation).
    evacuate(process);
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
    auto &seen = queues_[index]->seen;
    seen.store(reclaim_epoch_.load());
    Garbage garbage;
    while (!finished_) {
        try {
            if (const auto port = next_port()) {
                run_port(*port);
                continue;
            }
            auto *next = next_or_sleep(index, lock, garbage);
            if (!next) {
                continue;
            }
            auto &process = *next;
            schedule(process).running = true;
            ++slices_;
            lock.unlock();
            garbage.free();
            running_ = &process;
            prepare(process);
            const bool ended = process.stack().run(SLICE_REDUCTIONS);
            running_ = nullptr;
            lock.lock();
            // A slice boundary: nothing this worker found without the lock is held any more (quiescent reclamation).
            seen.store(reclaim_epoch_.load());
            after(process, ended);
            collect(garbage);
        } catch (const std::bad_alloc &) {
            fail_program();
        } catch (...) {
            // A failing mutex or condition variable leaves the executor without a consistent state.
            std::terminate();
        }
    }
    seen.store(QUIESCENT);
    // The program ends: freed under the lock.
    garbage.free();
    worker_ = false;
    wake_all();
}

ProcessContext *Executor::next_or_sleep(std::size_t index, std::unique_lock<std::mutex> &lock, Garbage &garbage) {
    if (auto *handed = handed_over(index)) {
        return handed;
    }
    std::unique_lock queues(queues_mutex_);
    if (auto *next = next_process(index)) {
        return next;
    }
    if (!garbage.empty()) {
        // Freed before sleeping, outside both locks; the worker looks again after.
        queues.unlock();
        lock.unlock();
        garbage.free();
        lock.lock();
        return nullptr;
    }
    // Sleeps holding only the run queues' lock, which every push takes: a spawn that queues without the executor's
    // lock cannot slip in between the look and the wait.
    auto &queue = *queues_[index];
    queue.idle = true;
    queue.seen.store(QUIESCENT);
    lock.unlock();
    queue.wake.wait(queues);
    queue.idle = false;
    queues.unlock();
    lock.lock();
    queue.seen.store(reclaim_epoch_.load());
    return nullptr;
}

void Executor::retire(std::unique_ptr<Reclaimable> retired) noexcept {
    if (!retired) {
        return;
    }
    auto *last = retired.release();
    last->stamp = reclaim_epoch_.fetch_add(1) + 1;
    (retired_last_ != nullptr ? retired_last_->next : retired_first_) = last;
    retired_last_ = last;
    ++retired_count_;
}

void Executor::retire_arrays() noexcept {
    for (auto &array : runtime_.processes.take_retired()) {
        retire(std::move(array));
    }
}

Reclaimable *Executor::reclaimable() noexcept {
    // Retirements reclaimed together, so the workers' epochs are read once per batch.
    constexpr std::size_t RECLAIM_BATCH = 64;
    if (retired_count_ < RECLAIM_BATCH) {
        return nullptr;
    }
    auto oldest = QUIESCENT;
    for (const auto &queue : queues_) {
        oldest = std::min(oldest, queue->seen.load());
    }
    Reclaimable *passed = nullptr;
    Reclaimable *tail = nullptr;
    while (retired_first_ != nullptr && retired_first_->stamp <= oldest) {
        auto *next = std::exchange(retired_first_, retired_first_->next);
        next->next = nullptr;
        (tail != nullptr ? tail->next : passed) = next;
        tail = next;
        --retired_count_;
    }
    if (retired_first_ == nullptr) {
        retired_last_ = nullptr;
    }
    return passed;
}

void Executor::free_reclaimed(Reclaimable *list) noexcept {
    while (list != nullptr) {
        delete std::exchange(list, list->next);
    }
}

void Executor::prepare(ProcessContext &process) const {
    // A process that may hold retiring persistent terms copies them out at this slice's first safepoint (persistent
    // term reclamation).
    if (runtime_.persistent_terms.behind(process)) {
        process.heap().request_evacuation();
    }
}

void Executor::settle(ProcessContext &process, ProcessContext *blocked_on) {
    // A process suspended on a busy port stays out of the queue until the port lets it go.
    if (schedule(process).port_wait == 0) {
        place(process, blocked_on);
    } else {
        // A retirement may have come while it ran (persistent term reclamation).
        evacuate(process);
    }
}

void Executor::after(ProcessContext &process, bool ended) {
    auto &state = schedule(process);
    state.running = false;
    const auto blockers = std::exchange(state.blockers, {});
    // The process waited for may have ended since: only its pid finds it then, and never again.
    auto *blocked_on = std::exchange(state.blocked_on, nullptr);
    if (blocked_on != nullptr && find(process, std::exchange(state.blocked_pid, 0)) != blocked_on) {
        blocked_on = nullptr;
    }
    const auto throttled = std::exchange(state.throttling, {});
    release(process);
    // The process is placed before its blockers act: a message they deliver must find it parked, not mid-way.
    if (failed_ && &process == main_) {
        fail_main();
    } else if (ended) {
        finish(process);
    } else {
        hold(blockers, process);
        settle(process, blocked_on);
        for (const auto &event : std::exchange(state.events, {})) {
            apply(process, event);
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
        state.blocked_pid = 0;
        if (std::exchange(state.ending, false)) {
            finish(blocker);
        } else {
            push(blocker, true);
        }
    }
}

void Executor::place(ProcessContext &process, ProcessContext *blocked_on) {
    if (blocked_on && busy(blocked_on)) {
        schedule(*blocked_on).blockers.push_back(&process);
        schedule(process).blocked_on = blocked_on;
        schedule(process).blocked_pid = blocked_on->self;
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
    // Bound workers of --scheduler-cpus; empty leaves them unbound.
    const std::span<const std::uint32_t> cpus = main.runtime().impl_->options.scheduler_cpus;
    {
        const std::scoped_lock lock(mutex_, queues_mutex_);
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
            helpers.emplace_back([this, count, cpus] {
                bind_worker(cpus, count);
                work(count);
            });
        } catch (const std::exception &) {
            // Run on the workers started so far.
            break;
        }
    }
    bind_worker(cpus, 0);
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
    auto &runtime = process.runtime();
    auto context = runtime.unpublish_context(process);
    retire(context->mailbox().release_arrivals());
    retire_arrays();
    if (retired_contexts_.size() == retired_contexts_.capacity()) {
        try {
            retired_contexts_.reserve(std::max(RETIRED_CONTEXTS, 2 * retired_contexts_.capacity()));
        } catch (const std::bad_alloc &) {
            // Freed under the lock instead.
            runtime.free_context(std::move(context));
            return;
        }
    }
    retired_contexts_.push_back(std::move(context));
}

Executor::~Executor() {
    if (reactor_) {
        reactor_->stop();
    }
    free_reclaimed(retired_first_);
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
    for (auto &context : std::exchange(retired_contexts_, {})) {
        auto &runtime = context->runtime();
        runtime.free_context(std::move(context));
    }
    // No worker runs any more.
    free_reclaimed(std::exchange(retired_first_, nullptr));
    retired_last_ = nullptr;
    retired_count_ = 0;
    // The wheel links timers embedded in the schedules: unlink them while the schedules still exist.
    wheel_.clear();
    std::vector<ProcessContext *> started;
    {
        const std::scoped_lock contexts(runtime_.contexts_mutex);
        for (const auto &[process, owner] : runtime_.contexts) {
            if (owner->schedule().started && owner.get() != finished_) {
                started.push_back(owner.get());
            }
        }
    }
    for (auto *process : started) {
        process->runtime().destroy_context(process);
    }
    // The program's outcome stays with the host, unscheduled.
    if (finished_ != nullptr) {
        finished_->schedule() = Schedule{};
    }
    {
        const std::scoped_lock queues(queues_mutex_);
        for (const auto &queue : queues_) {
            queue->processes.clear();
            queue->handed.store(nullptr, std::memory_order_relaxed);
        }
    }
    port_queue_.clear();
    ending_.clear();
    stopped_.clear();
    names_.clear();
    forget_distribution();
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
    text.reserve(512);
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
