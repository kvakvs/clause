#include "../builtins/support.hpp"
#include "../process/identities.hpp"
#include "../runtime_state.hpp"
#include "signal_messages.hpp"
#include <algorithm>
#include <array>
#include <new>

// The executor's port table (docs/ports.md): opening, output, closing, the port protocol of messages, and the links,
// monitors and exit signals of ports. A port's signals to processes hold no heap term when their target runs on
// another worker, so they wait for its slice to end (post) instead of making a builtin run again.
namespace clause::runtime::detail {
namespace {
// The data of an input message, a byte list or a binary, built in the heap of `process`; {eol, D} or {noeol, D} for
// a line or a line part.
TermResult<Term> input_data(ProcessContext &process, const PortEvent &event) {
    TermFactory factory(process);
    std::vector<Word> bytes;
    if (!event.binary) {
        bytes.reserve(event.bytes.size());
        for (const auto byte : event.bytes) {
            bytes.push_back(encode_integer(std::to_integer<std::int64_t>(byte)).value_or(0));
        }
    }
    const auto nil = factory.nil();
    const auto data = event.binary ? factory.binary(event.bytes) : (nil ? factory.list_words(bytes, *nil) : nil);
    if (!data || event.input == PortInput::Kind::data) {
        return data;
    }
    const auto tag = factory.atom(event.input == PortInput::Kind::eol ? "eol"_atom : "noeol"_atom);
    return tag ? factory.tuple(std::array{*tag, *data}) : tag;
}

// {Port, {data, Data}} built in the heap of `process`.
TermResult<Term> data_message(ProcessContext &process, const PortEvent &event) {
    TermFactory factory(process);
    const auto from = Term::from_word(event.port, process);
    const auto tag = factory.atom("data"_atom);
    const auto data = input_data(process, event);
    if (!from || !tag || !data) {
        return !from ? from : (!tag ? tag : data);
    }
    const auto body = factory.tuple(std::array{*tag, *data});
    return body ? factory.tuple(std::array{*from, *body}) : body;
}

// {Port, {exit_status, Status}} built in the heap of `process`.
TermResult<Term> status_message(ProcessContext &process, const PortEvent &event) {
    TermFactory factory(process);
    const auto from = Term::from_word(event.port, process);
    const auto tag = factory.atom("exit_status"_atom);
    const auto status = encode_integer(event.status);
    if (!from || !tag || !status) {
        return std::unexpected(!from ? from.error() : (!tag ? tag.error() : TermError::out_of_range));
    }
    const auto body = factory.tuple(std::array{*tag, Term::from_word(*status).value()});
    return body ? factory.tuple(std::array{*from, *body}) : body;
}

// {Port, Tag} built in the heap of `process`.
TermResult<Term> port_message(ProcessContext &process, Word port, AtomName tag) {
    TermFactory factory(process);
    const auto from = Term::from_word(port, process);
    const auto name = factory.atom(tag);
    return from && name ? factory.tuple(std::array{*from, *name}) : (from ? name : from);
}
} // namespace

bool Executor::is_port_word(Word word) noexcept { return TermTag{word}.get_kind() == TermKind::local_port; }

Port *Executor::open(Word port) const noexcept {
    if (!is_port_word(port)) {
        return nullptr;
    }
    const auto found = ports_.find(port_number(port));
    return found == ports_.end() ? nullptr : found->second.get();
}

Word Executor::open_port(ProcessContext &owner, std::unique_ptr<PortDriver> driver, PortOptions options,
                         std::string spelling) {
    const std::scoped_lock lock(mutex_);
    const auto number = runtime_.identity_numbers.issue_port();
    if (!number) {
        throw std::bad_alloc();
    }
    auto port = std::make_unique<Port>();
    port->number = *number;
    port->connected = pid_of(owner);
    port->links.push_back(port->connected);
    port->spelling = std::move(spelling);
    port->options = std::move(options);
    port->decoder = InputDecoder(port->options);
    port->driver = std::move(driver);
    const auto word = detail::port_word(*number);
    port->driver->attach(word);
    const auto input = port->options.input ? port->driver->input() : std::nullopt;
    const auto output = port->driver->queued_output();
    const auto child = port->driver->child();
    // A port that reads nothing has no end of input to wait for.
    port->input_ended = !input;
    owner.signals().link(word);
    try {
        ports_.emplace(*number, std::move(port));
        if (input) {
            io().read_descriptor(word, *input);
        }
        if (output) {
            io().write_descriptor(word, *output);
        }
        if (child) {
            io().watch_child(word, {.handle = *child});
        }
    } catch (...) {
        ports_.erase(*number);
        owner.signals().unlink(word);
        throw;
    }
    return word;
}

std::unique_ptr<PortDriver> Executor::socket_driver(bool udp) {
    const std::scoped_lock lock(mutex_);
    if (!sockets_) {
        sockets_ = std::make_unique<SocketService>(
            reactor(), [this](Word port, SocketEvent event) { socket_event(port, std::move(event)); });
    }
    return sockets_->driver(udp);
}

void Executor::socket_event(Word port, SocketEvent event) noexcept {
    const std::scoped_lock lock(mutex_);
    try {
        if (auto *open_port = open(port)) {
            open_port->pending.emplace_back(std::move(event));
            queue_port(*open_port);
        } else if (event.kind == SocketEvent::Kind::accepted) {
            accept_connection(port, std::move(event));
        } else if (auto *target = process(event.target)) {
            // A closed socket still answers the callers that waited on it.
            post(*target, PortEvent::term(port, std::move(event.value)));
        }
        drain();
    } catch (...) {
        // Only exhausted memory can fail building a port or a message of atoms, bytes and identities.
        fail_program();
    }
}

void Executor::accept_connection(Word listen, SocketEvent event) {
    auto *owner = process(event.target);
    if (!owner) {
        // The caller of accept has ended: the connection closes with its driver.
        return;
    }
    const auto number = runtime_.identity_numbers.issue_port();
    if (!number) {
        throw std::bad_alloc();
    }
    auto port = std::make_unique<Port>();
    port->number = *number;
    port->connected = event.target;
    port->links.push_back(event.target);
    port->spelling = "tcp_inet";
    port->options.binary = true;
    port->input_ended = true;
    port->driver = std::move(event.driver);
    const auto word = detail::port_word(*number);
    port->driver->attach(word);
    ports_.emplace(*number, std::move(port));
    owner->signals().link(word);
    // {clause_socket, Listen, ok} becomes {clause_socket, Listen, {ok, Socket}}.
    event.value.elements.back() = PortValue::of_tuple({PortValue::of_atom("ok"), PortValue::of_identity(word)});
    post(*owner, PortEvent::term(listen, std::move(event.value)));
}

Reactor &Executor::reactor() {
    if (!reactor_) {
        reactor_ = std::make_unique<Reactor>();
    }
    return *reactor_;
}

IoService &Executor::io() {
    if (!io_) {
        io_ = std::make_unique<IoService>(reactor(), [this](Word port, std::vector<PortInput> units, std::size_t read) {
            input(port, std::move(units), read);
        });
    }
    return *io_;
}

void Executor::input(Word port, std::vector<PortInput> units, std::size_t read) noexcept {
    const std::scoped_lock lock(mutex_);
    auto *open_port = open(port);
    if (!open_port) {
        return;
    }
    open_port->input += read;
    try {
        for (auto &unit : units) {
            if (unit.kind == PortInput::Kind::written) {
                written(*open_port, static_cast<std::size_t>(unit.status));
                continue;
            }
            open_port->held_bytes += unit.bytes.size();
            open_port->pending.emplace_back(std::move(unit));
        }
        if (open_port->held_bytes >= PORT_HELD_BYTES && !open_port->reading_paused) {
            open_port->reading_paused = true;
            io().pause(port, true);
        }
        if (!open_port->pending.empty()) {
            queue_port(*open_port);
        }
        drain();
    } catch (...) {
        // Only exhausted memory can fail queueing input.
        fail_program();
    }
}

void Executor::queue_port(Port &port) {
    if (port.queued) {
        return;
    }
    port_queue_.push_back(port.number);
    port.queued = true;
    const std::scoped_lock queues(queues_mutex_);
    wake_idle(queues_.size());
}

std::optional<Word> Executor::next_port() noexcept {
    if (port_queue_.empty()) {
        return std::nullopt;
    }
    if (const std::scoped_lock queues(queues_mutex_); !queues_[worker_ ? worker_index_ : 0]->processes.empty()) {
        // Processes and ports take turns, so neither can starve the other.
        port_turn_ = !port_turn_;
        if (port_turn_) {
            return std::nullopt;
        }
    }
    const auto number = port_queue_.front();
    port_queue_.pop_front();
    return number;
}

void Executor::run_port(Word number) {
    const auto word = detail::port_word(number);
    auto *port = open(word);
    if (!port) {
        return;
    }
    port->queued = false;
    auto reductions = PORT_TASK_REDUCTIONS;
    while (reductions > 0 && port_step(*port, reductions)) {
    }
    // A step may have closed the port: look it up again.
    if (auto *still = open(word)) {
        read_on(*still);
        if (!still->throttled && !(still->pending.empty() && still->units.empty())) {
            queue_port(*still);
        }
    }
    drain();
}

bool Executor::port_step(Port &port, std::size_t &reductions) {
    if (port.units.empty()) {
        if (port.pending.empty()) {
            return false;
        }
        auto work = std::move(port.pending.front());
        port.pending.pop_front();
        if (auto *event = std::get_if<SocketEvent>(&work)) {
            reductions -= std::min(reductions, PORT_MESSAGE_REDUCTIONS);
            socket_work(port, std::move(*event));
        } else {
            const auto &raw = std::get<PortInput>(work);
            port.held_bytes -= std::min(port.held_bytes, raw.bytes.size());
            frame(port, raw);
        }
        return true;
    }
    if (owner_full(port)) {
        return false;
    }
    auto unit = std::move(port.units.front());
    port.units.pop_front();
    reductions -= std::min(reductions, PORT_MESSAGE_REDUCTIONS + unit.bytes.size() / PORT_BYTES_PER_REDUCTION);
    return input(port, std::move(unit));
}

void Executor::frame(Port &port, const PortInput &raw) {
    if (raw.kind == PortInput::Kind::data || raw.kind == PortInput::Kind::end) {
        auto units = raw.kind == PortInput::Kind::data ? port.decoder.feed(raw.bytes) : port.decoder.finish();
        port.units.insert(port.units.end(), std::make_move_iterator(units.begin()),
                          std::make_move_iterator(units.end()));
    } else {
        port.units.push_back(raw);
    }
}

void Executor::socket_work(Port &port, SocketEvent event) {
    const auto word = detail::port_word(port.number);
    if (event.kind == SocketEvent::Kind::accepted) {
        accept_connection(word, std::move(event));
    } else if (auto *target = process(event.target == 0 ? port.connected : event.target)) {
        post(*target, PortEvent::term(word, std::move(event.value)));
    }
}

bool Executor::input(Port &port, PortInput unit) {
    if (unit.kind == PortInput::Kind::status) {
        return exited(port, unit.status);
    }
    if (unit.kind == PortInput::Kind::end || unit.kind == PortInput::Kind::error) {
        return input_end(port, unit);
    }
    if (auto *connected = process(port.connected)) {
        post(*connected, PortEvent::data(detail::port_word(port.number), std::move(unit), port.options.binary));
    }
    return true;
}

bool Executor::input_end(Port &port, const PortInput &unit) {
    if (unit.kind == PortInput::Kind::error) {
        auto reason = runtime_.atom_storage.intern(unit.reason);
        if (!reason) {
            throw std::bad_alloc();
        }
        close(port, *reason);
        return false;
    }
    port.input_ended = true;
    return port.options.exit_status && !port.exit_status ? true : finish_input(port);
}

bool Executor::exited(Port &port, std::int64_t status) {
    port.exit_status = status;
    return port.options.exit_status && port.input_ended ? finish_input(port) : true;
}

bool Executor::finish_input(Port &port) {
    const auto word = detail::port_word(port.number);
    auto *connected = process(port.connected);
    if (port.options.exit_status && connected) {
        post(*connected, PortEvent::exited(word, port.exit_status.value_or(0)));
    }
    if (port.options.eof) {
        if (connected) {
            post(*connected, PortEvent::message(PortEvent::Kind::eof, word));
        }
        return true;
    }
    auto normal = runtime_.atom_storage.intern("normal"_atom);
    if (!normal) {
        throw std::bad_alloc();
    }
    close(port, *normal);
    return false;
}

bool Executor::close_port(ProcessContext &caller, Word port) {
    const std::scoped_lock lock(mutex_);
    auto *open_port = open(port);
    if (!open_port) {
        return false;
    }
    const Running running(caller);
    close(*open_port, atom(caller, "normal"_atom));
    drain();
    return true;
}

PortOutcome Executor::command_port(ProcessContext &caller, Word port, std::vector<std::byte> data, bool nosuspend) {
    const std::scoped_lock lock(mutex_);
    auto *open_port = open(port);
    if (!open_port || !open_port->options.output) {
        return PortOutcome::not_open;
    }
    if (open_port->busy && !nosuspend) {
        suspend(caller, *open_port);
    }
    const Running running(caller);
    const auto outcome = write(caller, *open_port, std::move(data));
    drain();
    return outcome;
}

void Executor::suspend(ProcessContext &sender, Port &port) {
    port.suspended.push_back(pid_of(sender));
    schedule(sender).port_wait = detail::port_word(port.number);
    throw builtins::Blocked{};
}

void Executor::resume_senders(Port &port) {
    for (const auto pid : std::exchange(port.suspended, {})) {
        auto *sender = process(pid);
        if (!sender) {
            continue;
        }
        auto &state = schedule(*sender);
        // A sender whose slice has not ended yet is placed by after(), as it no longer waits.
        if (std::exchange(state.port_wait, 0) != 0 && !state.running) {
            push(*sender);
        }
    }
}

void Executor::written(Port &port, std::size_t bytes) {
    port.queued_output -= std::min(port.queued_output, bytes);
    if (port.busy && port.queued_output < port.options.busy_low) {
        port.busy = false;
        resume_senders(port);
    }
}

bool Executor::owner_full(Port &port) {
    auto *owner = process(port.connected);
    if (!owner) {
        return false;
    }
    auto &state = schedule(*owner);
    // A suspended or blocked owner cannot take messages before the port acts, so it always gets them.
    if (state.port_wait != 0 || state.blocked_on || (!state.running && owner->stack().waiting())) {
        return false;
    }
    const auto waiting = state.running ? state.events.size() : owner->mailbox().size();
    if (waiting < PORT_OWNER_MESSAGES) {
        return false;
    }
    port.throttled = true;
    state.throttling.push_back(port.number);
    return true;
}

void Executor::unthrottle(const std::vector<Word> &numbers) {
    for (const auto number : numbers) {
        if (auto *port = open(detail::port_word(number)); port && std::exchange(port->throttled, false)) {
            queue_port(*port);
        }
    }
}

void Executor::read_on(Port &port) {
    if (port.reading_paused && port.held_bytes < PORT_HELD_BYTES / 2) {
        port.reading_paused = false;
        io().pause(detail::port_word(port.number), false);
    }
}

PortOutcome Executor::write(ProcessContext &caller, Port &port, std::vector<std::byte> data) {
    const auto size = data.size();
    const auto output = framed(port.options, std::move(data));
    if (!output) {
        return PortOutcome::too_long;
    }
    if (port.driver->queued_output()) {
        if (port.busy) {
            return PortOutcome::busy;
        }
        port.queued_output += output->size();
        io().send(detail::port_word(port.number), *output);
        port.busy = port.queued_output >= port.options.busy_high;
    } else if (const auto written = port.driver->write(*output); !written) {
        close(port, atom(caller, written.error().reason));
        return PortOutcome::done;
    }
    port.output += size;
    return PortOutcome::done;
}

bool Executor::connect_port(Word port, const Term &owner_pid) {
    const std::scoped_lock lock(mutex_);
    const auto pid = owner_pid.word();
    auto *open_port = open(port);
    auto *owner = process(pid);
    if (!open_port || !owner) {
        return false;
    }
    open_port->connected = pid;
    unthrottle({open_port->number});
    if (!std::ranges::contains(open_port->links, pid)) {
        open_port->links.push_back(pid);
    }
    owner->signals().link(port);
    return true;
}

void Executor::port_request(ProcessContext &sender, Word port, const PortRequest &request) {
    const std::scoped_lock lock(mutex_);
    auto *open_port = open(port);
    if (!open_port) {
        return;
    }
    const Running running(sender);
    if (!serve(sender, *open_port, request)) {
        badsig(sender, *open_port);
    }
    drain();
}

bool Executor::serve(ProcessContext &sender, Port &port, const PortRequest &request) {
    if (request.kind == PortRequest::Kind::malformed || request.from != port.connected) {
        return false;
    }
    if (request.kind == PortRequest::Kind::command) {
        if (port.options.output && port.busy) {
            suspend(sender, port);
        }
        return port.options.output && write(sender, port, request.data) == PortOutcome::done;
    }
    if (request.kind == PortRequest::Kind::connect && !process(request.owner)) {
        return false;
    }
    answer(sender, port, request);
    return true;
}

void Executor::answer(ProcessContext &sender, Port &port, const PortRequest &request) {
    // A close or connect answers the sender, which is the connected process, after acting.
    auto *connected = process(port.connected);
    const auto word = detail::port_word(port.number);
    const auto close_request = request.kind == PortRequest::Kind::close;
    if (close_request) {
        close(port, atom(sender, "normal"_atom));
    } else {
        port.connected = request.owner;
    }
    if (connected) {
        post(*connected,
             PortEvent::message(close_request ? PortEvent::Kind::closed : PortEvent::Kind::connected, word));
    }
}

void Executor::badsig(ProcessContext &sender, Port &port) {
    if (auto *connected = process(port.connected)) {
        post(*connected, PortEvent::exit(detail::port_word(port.number), atom(sender, "badsig"_atom).word()));
    }
}

std::optional<PortInfo> Executor::port_info(Word port) const {
    const std::scoped_lock lock(mutex_);
    const auto *open_port = open(port);
    if (!open_port) {
        return std::nullopt;
    }
    PortInfo info{.name = open_port->spelling,
                  .links = open_port->links,
                  .id = open_port->number,
                  .connected = open_port->connected,
                  .input = open_port->input,
                  .output = open_port->output,
                  .os_pid = open_port->driver->os_pid(),
                  .monitored_by = {},
                  .registered = open_port->name,
                  .queue_size = open_port->queued_output};
    for (const auto &[reference, watcher] : open_port->watchers) {
        info.monitored_by.push_back(watcher.pid);
    }
    return info;
}

std::vector<Word> Executor::ports() const {
    const std::scoped_lock lock(mutex_);
    std::vector<Word> words;
    words.reserve(ports_.size());
    for (const auto &[number, port] : ports_) {
        words.push_back(detail::port_word(number));
    }
    return words;
}

std::optional<std::pair<std::vector<std::byte>, bool>>
Executor::control_port(ProcessContext &caller, Word port, std::span<const std::byte> data, std::uint32_t operation) {
    std::shared_ptr<PortDriver> driver;
    bool binary = false;
    {
        const std::scoped_lock lock(mutex_);
        const auto *open_port = open(port);
        if (!open_port || !open_port->driver->controllable()) {
            return std::nullopt;
        }
        driver = open_port->driver;
        binary = open_port->options.binary;
    }
    auto answer = driver->control(operation, data, pid_of(caller));
    if (!answer) {
        return std::nullopt;
    }
    return std::pair{std::move(*answer), binary};
}

void Executor::close(Port &port, const Term &reason) {
    // The port leaves the table first; its record and driver live until the signals went out.
    auto node = ports_.extract(port.number);
    const auto word = detail::port_word(port.number);
    if (io_) {
        io_->forget(word);
    }
    if (port.name != 0) {
        names_.erase(port.name);
    }
    // Suspended senders run their builtin again and find the port closed.
    resume_senders(port);
    for (const auto pid : port.links) {
        if (auto *target = process(pid)) {
            target->signals().unlink(word);
            link_closed(*target, word, reason);
        }
    }
    for (const auto &[reference, watcher] : port.watchers) {
        if (auto *target = process(watcher.pid)) {
            target->signals().demonitor(reference);
            monitor_closed(*target, word, reference, watcher.name, reason);
        }
    }
}

void Executor::link_closed(ProcessContext &target, Word port, const Term &reason) {
    if (busy(&target)) {
        post(target, PortEvent::exit(port, reason.word()));
    } else {
        signal(target, port, reason, SignalKind::link);
    }
}

void Executor::monitor_closed(ProcessContext &target, Word port, const ReferenceIdentity &reference, Word name,
                              const Term &reason) {
    if (busy(&target)) {
        post(target, PortEvent::down(port, reason.word(), reference, name));
    } else {
        deliver(target, down_message(target, reference, {.pid = port, .name = name}, reason));
    }
}

void Executor::port_exit(Port &port, Word from, const Term &reason, SignalKind kind) {
    if (kind == SignalKind::link && is_atom(reason, "normal") && from != port.connected) {
        std::erase(port.links, from);
        return;
    }
    if (kind != SignalKind::link && is_atom(reason, "kill")) {
        auto killed = runtime_.atom_storage.intern("killed"_atom);
        if (!killed) {
            throw std::bad_alloc();
        }
        close(port, *killed);
        return;
    }
    close(port, reason);
}

ProcessContext *Executor::busy_peer(const Port &port) noexcept {
    for (const auto pid : port.links) {
        if (auto *peer = process(pid); peer && busy(peer)) {
            return peer;
        }
    }
    for (const auto &[reference, watcher] : port.watchers) {
        if (auto *peer = process(watcher.pid); peer && busy(peer)) {
            return peer;
        }
    }
    return nullptr;
}

void Executor::post(ProcessContext &target, PortEvent event) {
    if (busy(&target)) {
        schedule(target).events.push_back(std::move(event));
    } else {
        apply(target, event);
    }
}

void Executor::apply(ProcessContext &target, const PortEvent &event) {
    if (target.generated_calls().failure()) {
        // The target has already ended.
        return;
    }
    const auto reason = Term::from_word(event.reason, target);
    switch (event.kind) {
    case PortEvent::Kind::exit:
        signal(target, event.port, reason.value(), SignalKind::link);
        break;
    case PortEvent::Kind::down:
        deliver_down(target, event, reason.value());
        break;
    case PortEvent::Kind::closed:
        deliver(target, port_message(target, event.port, "closed"_atom));
        break;
    case PortEvent::Kind::connected:
        deliver(target, port_message(target, event.port, "connected"_atom));
        break;
    case PortEvent::Kind::eof:
        deliver(target, port_message(target, event.port, "eof"_atom));
        break;
    case PortEvent::Kind::exit_status:
        deliver(target, status_message(target, event));
        break;
    case PortEvent::Kind::value:
        deliver(target, build_value(target, *event.value));
        break;
    case PortEvent::Kind::data:
        deliver(target, data_message(target, event));
        break;
    }
}

void Executor::deliver_down(ProcessContext &target, const PortEvent &event, const Term &reason) {
    if (event.reference) {
        deliver(target, down_message(target, *event.reference, {.pid = event.port, .name = event.name}, reason));
    }
}

bool Executor::link_port(ProcessContext &process, Word port) {
    auto *open_port = open(port);
    if (!open_port) {
        if (!process.signals().trap_exit()) {
            return false;
        }
        const Running running(process);
        deliver(process, exit_message(process, port, atom(process, "noproc"_atom)));
        return true;
    }
    const auto self = pid_of(process);
    if (!std::ranges::contains(open_port->links, self)) {
        open_port->links.push_back(self);
    }
    process.signals().link(port);
    return true;
}

void Executor::unlink_port(ProcessContext &process, Word port) noexcept {
    process.signals().unlink(port);
    if (auto *open_port = open(port)) {
        std::erase(open_port->links, pid_of(process));
    }
}

Term Executor::monitor_port(ProcessContext &watcher, Word port, Word name, const Term &reference) {
    const auto identity = reference.reference_value_unchecked();
    auto *open_port = open(port);
    if (!open_port) {
        const Running running(watcher);
        // A 'DOWN' of a port names the port, or {Name, Node}; one that names nothing is still a port monitor.
        const auto item = Signals::Monitor{.pid = port != 0 ? port : detail::port_word(0), .name = name};
        deliver(watcher, down_message(watcher, identity, item, atom(watcher, "noproc"_atom)));
        return reference;
    }
    watcher.signals().monitor(identity, port);
    open_port->watchers.emplace(identity, Signals::Monitor{.pid = pid_of(watcher), .name = name});
    return reference;
}

void Executor::exit_port(ProcessContext &sender, Word port, const Term &reason) {
    auto *open_port = open(port);
    if (!open_port) {
        return;
    }
    // A reason in the sender's heap can reach only processes that do not run elsewhere.
    if (!reason.is_atom()) {
        if (auto *peer = busy_peer(*open_port)) {
            block(sender, *peer);
        }
    }
    const Running running(sender);
    port_exit(*open_port, pid_of(sender), reason, SignalKind::exit);
    drain();
}

void Executor::notify_ports(ProcessContext &process, const Term &reason, const std::vector<Word> &links) {
    const auto from = pid_of(process);
    for (const auto word : links) {
        if (auto *port = open(word)) {
            std::erase(port->links, from);
            port_exit(*port, from, reason, SignalKind::link);
        }
    }
}
} // namespace clause::runtime::detail
