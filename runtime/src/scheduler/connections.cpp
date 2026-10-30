#include "connections.hpp"
#include "../process/exits.hpp"
#include "../terms/identities.hpp"
#include "signal_messages.hpp"
#include <algorithm>
#include <array>
#include <new>

// Connection entries of other nodes and node status messages (plan 30 step DIST2b, docs/distribution.md#connections):
// the executor keeps the table under its lock, as OTP's dist table; a controller's end takes its connection down and
// net_kernel's end takes every connection and then the node's name (OTP's erts_do_net_exits).
namespace clause::runtime::detail {
namespace {
// The connection id is 24 bits, as OTP's ERTS_DIST_CON_ID_MASK.
constexpr std::uint32_t CONNECTION_ID_MASK = 0xFF'FFFF;

// Whether a subscription with `options` hears of a node of that visibility: the plain form only of visible nodes.
bool wants(unsigned options, bool visible) noexcept {
    const auto types = options & (node_status::VISIBLE | node_status::HIDDEN);
    if (!visible) {
        return (types & node_status::HIDDEN) != 0;
    }
    return types == 0 || (types & node_status::VISIBLE) != 0;
}

// The kind nodes/1,2 list an entry as for `kinds`, or none when they leave it out.
std::optional<ListedNode::Kind> listed_kind(const Connection &connection, NodeKinds kinds) noexcept {
    using Kind = ListedNode::Kind;
    if (connection.state != Connection::State::up) {
        return kinds.known ? std::optional{Kind::known} : std::nullopt;
    }
    const auto visible = connection.visible();
    if (!(visible ? kinds.visible : kinds.hidden)) {
        return std::nullopt;
    }
    return visible ? Kind::visible : Kind::hidden;
}

// Append the entries in `state` that nodes/1,2 list as `group` for `kinds`, oldest change first, leaving out this
// node's name.
void append_group(std::vector<ListedNode> &listed, const ConnectionTable &table, Connection::State state,
                  ListedNode::Kind group, NodeKinds kinds) {
    for (const auto node : table.in_state(state)) {
        const auto *connection = table.find(node);
        if (listed_kind(*connection, kinds) != group || is_this_node(TermAccess::make(node))) {
            continue;
        }
        const auto up = group != ListedNode::Kind::known;
        listed.push_back({.node = node, .kind = group, .id = up ? std::optional{connection->id} : std::nullopt});
    }
}

// A term or the failure to build it, as std::bad_alloc: the executor's operations throw on allocation failure.
Term built(const TermResult<Term> &term) {
    if (!term) {
        throw std::bad_alloc();
    }
    return *term;
}

// The Info entries of a status message for `options`, in OTP's list order: connection_id, nodedown_reason, node_type.
std::vector<std::pair<Term, Term>> status_info(ProcessContext &builder, const NodeStatus &status, unsigned options) {
    TermFactory factory(builder);
    std::vector<std::pair<Term, Term>> info;
    if ((options & node_status::CONNECTION_ID) != 0) {
        const auto id = status.id ? factory.integer(*status.id) : factory.atom("undefined"_atom);
        info.emplace_back(atom(builder, "connection_id"_atom), built(id));
    }
    if (!status.up && (options & node_status::DOWN_REASON) != 0) {
        info.emplace_back(atom(builder, "nodedown_reason"_atom), status.reason);
    }
    if ((options & (node_status::VISIBLE | node_status::HIDDEN)) != 0) {
        info.emplace_back(atom(builder, "node_type"_atom),
                          atom(builder, status.visible ? "visible"_atom : "hidden"_atom));
    }
    return info;
}

// The message a subscription with `options` gets: {What, Node}, or {What, Node, Info} with Info a map or a list.
Term status_message(ProcessContext &builder, const NodeStatus &status, unsigned options) {
    TermFactory factory(builder);
    const auto what = atom(builder, status.up ? "nodeup"_atom : "nodedown"_atom);
    const auto node = built(Term::from_word(status.node, builder));
    if (options == 0) {
        return built(factory.tuple(std::array{what, node}));
    }
    const auto info = status_info(builder, status, options);
    if ((options & node_status::INFO_MAP) != 0) {
        return built(factory.tuple(std::array{what, node, built(factory.map(info))}));
    }
    std::vector<Term> items;
    items.reserve(info.size());
    for (const auto &[key, value] : info) {
        items.push_back(built(factory.tuple(std::array{key, value})));
    }
    return built(factory.tuple(std::array{what, node, built(factory.list(items))}));
}

// A message built in `builder`'s heap as a buffer that can be posted to any process.
std::shared_ptr<const MessageBuffer> buffered(ProcessContext &builder, const Term &message) {
    auto buffer = MessageBuffer::copy_of(message, builder);
    if (!buffer) {
        throw std::bad_alloc();
    }
    return std::move(*buffer);
}

// A controller's exit reason as nodedown_reason, as OTP's: {shutdown, Reason} unwrapped, normal as
// connection_closed.
Term down_reason(ProcessContext &controller) {
    auto reason = built(exit_reason(controller));
    if (reason.is_tuple() && reason.tuple_size_unchecked() == 2 &&
        is_atom(reason.tuple_element_unchecked(0), "shutdown")) {
        reason = reason.tuple_element_unchecked(1);
    }
    return is_atom(reason, "normal") ? atom(controller, "connection_closed"_atom) : reason;
}
} // namespace

Connection *ConnectionTable::find(Word node) noexcept {
    const auto found = entries_.find(node);
    return found == entries_.end() ? nullptr : &found->second;
}

const Connection *ConnectionTable::find(Word node) const noexcept {
    const auto found = entries_.find(node);
    return found == entries_.end() ? nullptr : &found->second;
}

Connection &ConnectionTable::entry(Word node, std::uint64_t handle) {
    if (auto *found = find(node)) {
        return *found;
    }
    handles_.emplace(handle, node);
    return entries_.emplace(node, Connection{.handle = handle, .stamp = ++clock_}).first->second;
}

std::optional<Word> ConnectionTable::node_of_handle(std::uint64_t handle) const noexcept {
    const auto found = handles_.find(handle);
    return found == handles_.end() ? std::nullopt : std::optional{found->second};
}

std::optional<Word> ConnectionTable::controlled_by(Word pid) const noexcept {
    const auto found = controllers_.find(pid);
    return found == controllers_.end() ? std::nullopt : std::optional{found->second};
}

void ConnectionTable::pend(Connection &connection) noexcept {
    connection.state = Connection::State::pending;
    connection.id = (connection.id + 1) & CONNECTION_ID_MASK;
    connection.stamp = ++clock_;
}

void ConnectionTable::connect(Word node, Word controller, const ChannelOptions &options) {
    controllers_.emplace(controller, node);
    auto &connection = entries_.at(node);
    connection.state = Connection::State::up;
    connection.controller = controller;
    connection.flags = options.flags;
    connection.creation = options.creation;
    connection.stamp = ++clock_;
}

void ConnectionTable::drop(Connection &connection) noexcept {
    controllers_.erase(connection.controller);
    connection.state = Connection::State::idle;
    connection.controller = 0;
    connection.flags = 0;
    connection.stamp = ++clock_;
}

std::vector<Word> ConnectionTable::in_state(Connection::State state) const {
    std::vector<std::pair<std::uint64_t, Word>> stamped;
    for (const auto &[node, connection] : entries_) {
        if (connection.state == state) {
            stamped.emplace_back(connection.stamp, node);
        }
    }
    std::ranges::sort(stamped);
    std::vector<Word> nodes;
    nodes.reserve(stamped.size());
    for (const auto &[stamp, node] : stamped) {
        nodes.push_back(node);
    }
    return nodes;
}

std::size_t ConnectionTable::subscribe(Word pid, unsigned options, bool on) {
    const auto found = std::ranges::find_if(subscriptions_, [&](const auto &subscription) {
        return subscription.pid == pid && subscription.options == options;
    });
    if (found == subscriptions_.end()) {
        if (on) {
            subscriptions_.push_back({.pid = pid, .options = options, .count = 1});
        }
        return 0;
    }
    const auto count = found->count;
    if (on) {
        ++found->count;
    } else {
        subscriptions_.erase(found);
    }
    return count;
}

void ConnectionTable::unsubscribe(Word pid) noexcept {
    std::erase_if(subscriptions_, [pid](const auto &subscription) { return subscription.pid == pid; });
}

void ConnectionTable::clear() noexcept {
    entries_.clear();
    handles_.clear();
    controllers_.clear();
    subscriptions_.clear();
}

Connection &Executor::entry_of(ProcessContext &context, Word node) {
    if (auto *found = connections_.find(node)) {
        return *found;
    }
    const auto reference = built(TermFactory(context).make_reference());
    return connections_.entry(node, reference_number(reference).value());
}

TermResult<Term> Executor::handle_term(ProcessContext &context, const Connection &connection) {
    TermFactory factory(context);
    const auto id = factory.integer(connection.id);
    const auto reference = factory.reference_number(connection.handle);
    if (!id || !reference) {
        return id ? reference : id;
    }
    return factory.tuple(std::array{*id, *reference});
}

std::optional<Term> Executor::new_connection(ProcessContext &context, const Term &node) {
    const std::scoped_lock lock(mutex_);
    if (is_this_node(node)) {
        return std::nullopt;
    }
    auto &connection = entry_of(context, node.word());
    if (connection.state == Connection::State::idle) {
        connections_.pend(connection);
    }
    return built(handle_term(context, connection));
}

std::optional<bool> Executor::abort_connection(const Term &node, const DistHandle &handle) {
    const std::scoped_lock lock(mutex_);
    const auto named = connections_.node_of_handle(handle.reference);
    auto *connection = named && *named == node.word() && !is_this_node(node) ? connections_.find(*named) : nullptr;
    if (connection == nullptr) {
        return std::nullopt;
    }
    if (connection->id != handle.id) {
        return true;
    }
    if (connection->state == Connection::State::pending) {
        connections_.drop(*connection);
    }
    return connection->state != Connection::State::up;
}

bool Executor::kernel_ready(ProcessContext &context) const {
    const std::scoped_lock lock(mutex_);
    return node_owner_ != 0 && lookup(context, atom(context, "net_kernel"_atom).word()) == node_owner_;
}

std::optional<Term> Executor::connect(ProcessContext &context, const Term &node, Word controller,
                                      const ChannelOptions &options) {
    const auto kernel_name = atom(context, "net_kernel"_atom).word();
    const std::scoped_lock lock(mutex_);
    const auto kernel = lookup(context, kernel_name);
    if (kernel == 0 || kernel != node_owner_ || is_this_node(node)) {
        return std::nullopt;
    }
    // A failed setnode/3 leaves an idle entry, as OTP's finds or inserts it first.
    auto &connection = entry_of(context, node.word());
    auto *target = find(context, controller);
    if (connection.state != Connection::State::pending || target == nullptr || ending(*target) ||
        controller == node_owner_ || connections_.controlled_by(controller)) {
        return std::nullopt;
    }
    connections_.connect(node.word(), controller, options);
    TermFactory factory(context);
    const std::array parts{atom(context, "dist_ctrlr"_atom), built(Term::from_word(controller, context)), node,
                           built(factory.pid(context.identity()))};
    post(*process(kernel), buffered(context, built(factory.tuple(parts))));
    announce(context,
             {.up = true, .node = node.word(), .id = connection.id, .visible = connection.visible(), .reason = {}});
    return built(handle_term(context, connection));
}

std::vector<ListedNode> Executor::list_nodes(NodeKinds kinds) const {
    using Kind = ListedNode::Kind;
    using State = Connection::State;
    // OTP's order: pending and idle entries as known, then up entries, hidden before visible.
    static constexpr std::array GROUPS{std::pair{State::pending, Kind::known}, std::pair{State::idle, Kind::known},
                                       std::pair{State::up, Kind::hidden}, std::pair{State::up, Kind::visible}};
    const std::scoped_lock lock(mutex_);
    std::vector<ListedNode> listed;
    for (const auto &[state, group] : GROUPS) {
        append_group(listed, connections_, state, group, kinds);
    }
    return listed;
}

std::size_t Executor::subscribe(ProcessContext &context, unsigned options, bool on) {
    const std::scoped_lock lock(mutex_);
    return connections_.subscribe(pid_of(context), options, on);
}

void Executor::announce(ProcessContext &builder, const NodeStatus &status) {
    for (const auto &subscription : connections_.subscriptions()) {
        auto *receiver = process(subscription.pid);
        if (receiver == nullptr || !wants(subscription.options, status.visible)) {
            continue;
        }
        const auto message = buffered(builder, status_message(builder, status, subscription.options));
        for (std::size_t copy = 0; copy < subscription.count; ++copy) {
            post(*receiver, message);
        }
    }
}

void Executor::announce_named(ProcessContext &builder) {
    announce(builder,
             {.up = true, .node = built(this_node_atom(builder)).word(), .id = {}, .visible = true, .reason = {}});
}

void Executor::end_distribution(ProcessContext &process) {
    const auto pid = pid_of(process);
    connections_.unsubscribe(pid);
    if (const auto node = connections_.controlled_by(pid)) {
        connection_down(process, *node);
    }
    if (node_owner_ != 0 && node_owner_ == pid) {
        kernel_down(process);
    }
}

void Executor::connection_down(ProcessContext &controller, Word node) {
    auto &connection = *connections_.find(node);
    const NodeStatus status{.up = false,
                            .node = node,
                            .id = connection.id,
                            .visible = connection.visible(),
                            .reason = down_reason(controller)};
    connections_.drop(connection);
    announce(controller, status);
    unname_when_done(controller);
}

void Executor::kernel_down(ProcessContext &kernel) {
    node_owner_ = 0;
    const auto reason = built(exit_reason(kernel));
    node_down_reason_ =
        is_atom(reason, "no_network") ? reason.word() : atom(kernel, "net_kernel_terminated"_atom).word();
    for (const auto node : connections_.in_state(Connection::State::pending)) {
        connections_.drop(*connections_.find(node));
    }
    // OTP kills hidden controllers before visible ones, the latest connected first.
    auto up = connections_.in_state(Connection::State::up);
    std::ranges::reverse(up);
    std::ranges::stable_partition(up, [&](Word node) { return !connections_.find(node)->visible(); });
    for (const auto node : up) {
        auto *controller = process(connections_.find(node)->controller);
        if (controller != nullptr && !controller->generated_calls().failure()) {
            end(*controller, atom(*controller, "killed"_atom));
        }
    }
    unname_when_done(kernel);
}

void Executor::unname_when_done(ProcessContext &builder) {
    if (node_owner_ != 0 || node_down_reason_ == 0 || !connections_.controllers().empty()) {
        return;
    }
    const NodeStatus status{.up = false,
                            .node = built(this_node_atom(builder)).word(),
                            .id = {},
                            .visible = true,
                            .reason = built(Term::from_word(node_down_reason_, builder))};
    node_down_reason_ = 0;
    unname_this_node();
    announce(builder, status);
}

ProcessContext *Executor::busy_controller(ProcessContext &kernel) noexcept {
    if (node_owner_ == 0 || node_owner_ != pid_of(kernel)) {
        return nullptr;
    }
    for (const auto &[pid, node] : connections_.controllers()) {
        if (auto *controller = process(pid); controller && busy(controller)) {
            return controller;
        }
    }
    return nullptr;
}

void Executor::forget_distribution() noexcept {
    connections_.clear();
    // This executor holds the name while net_kernel lives or its connections end.
    if (node_owner_ != 0 || node_down_reason_ != 0) {
        node_owner_ = 0;
        node_down_reason_ = 0;
        unname_this_node();
    }
}
} // namespace clause::runtime::detail
