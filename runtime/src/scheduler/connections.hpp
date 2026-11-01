#pragma once
#include <clause/runtime/terms.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Connection entries of other nodes, node monitors (DIST2c), the controller data path (DIST2d) and node status
// subscriptions (plan 30 step DIST2b,
// docs/distribution.md#connections): the data the executor keeps under its lock, as OTP's dist table and nodes
// monitors keep it.
namespace clause::runtime::detail {
class MessageBuffer;

// OTP's distribution flags (dist.h) that the runtime reads or reports through erts_internal:get_dflags/0.
namespace dflags {
inline constexpr std::uint64_t PUBLISHED = 0x1;
inline constexpr std::uint64_t ATOM_CACHE = 0x2;
// A node takes process monitors only with both (plan 30 step DIST4b3b1); EXIT_PAYLOAD sends their exit reasons as
// payloads.
inline constexpr std::uint64_t DIST_MONITOR = 0x8;
inline constexpr std::uint64_t DIST_MONITOR_NAME = 0x20;
inline constexpr std::uint64_t EXIT_PAYLOAD = 0x40'0000;
// A node takes SPAWN_REQUEST only with it (plan 30 step DIST5b2).
inline constexpr std::uint64_t SPAWN = std::uint64_t{0x1} << 32;
inline constexpr std::uint64_t HIDDEN_ATOM_CACHE = 0x40;
inline constexpr std::uint64_t DIST_HDR_ATOM_CACHE = 0x2000;
inline constexpr std::uint64_t FRAGMENTS = 0x800000;
// OTP 26's mandatory set: EXTENDED_REFERENCES, FUN_TAGS, NEW_FUN_TAGS, EXTENDED_PIDS_PORTS, EXPORT_PTR_TAG,
// BIT_BINARIES, NEW_FLOATS, UTF8_ATOMS, MAP_TAG, BIG_CREATION, HANDSHAKE_23, UNLINK_ID and V4_NC.
inline constexpr std::uint64_t MANDATORY = 0x4'0307'0F94;
// What Clause offers (docs/distribution.md#protocol): the mandatory set, DIST_MONITOR, DIST_MONITOR_NAME,
// UNICODE_IO, SMALL_ATOM_TAGS, SEND_SENDER, BIG_SEQTRACE_LABELS, EXIT_PAYLOAD, MANDATORY_25_DIGEST, SPAWN and ALIAS;
// not the atom cache, fragments, ALTACT_SIG or NATIVE_RECORDS.
inline constexpr std::uint64_t DEFAULT = MANDATORY | 0x8 | 0x20 | 0x1000 | 0x4000 | 0x8'0000 | 0x10'0000 | 0x40'0000 |
                                         0x400'0000 | (std::uint64_t{0x1} << 32) | (std::uint64_t{0x8} << 32);
inline constexpr std::uint64_t REJECTABLE = DIST_HDR_ATOM_CACHE | HIDDEN_ATOM_CACHE | FRAGMENTS | ATOM_CACHE;
inline constexpr std::uint64_t STRICT_ORDER = DIST_HDR_ATOM_CACHE;
} // namespace dflags

// A refused dist_ctrl_* call, as OTP's: notsup when the caller has no part in a connection, badarg for another handle
// or a bad argument (plan 30 step DIST2d).
enum class DataPathError : std::uint8_t { notsup, badarg };

// What became of a packet for another node (plan 30 step DIST4b3a): queued on its connection, dropped, or not sent
// because the node is not connected and the send would have to connect it (send/3's noconnect).
enum class DistSend : std::uint8_t { queued, dropped, noconnect };

// The bytes OTP 29's dist_get_stat/1 counts for each queued packet besides its own, as observed: more for one queued
// while the connection was pending.
inline constexpr std::size_t PACKET_OVERHEAD = 160;
inline constexpr std::size_t PENDING_PACKET_OVERHEAD = 174;

// The controller data path of an up connection (plan 30 step DIST2d, docs/distribution.md#controller-data-path),
// reset when it goes down.
struct DataPath final {
    // The process dist_ctrl_put_data/2 takes data from: the controller, or dist_ctrl_input_handler/2's; 0 for a
    // process of another node, from which no data comes.
    Word input_handler = 0;
    // Encoded packets dist_ctrl_get_data/1 hands the controller, oldest first: messages since DIST4b3a, also while
    // the connection is pending.
    std::deque<std::vector<std::byte>> output;
    // How many packets at the front of `output` were queued while the connection was pending.
    std::size_t pending_packets = 0;
    // Whether the controller asked for a dist_data message when a packet is queued.
    bool notify = false;
    // dist_ctrl_set_opt(DHandle, get_size, Bool): dist_ctrl_get_data/1 returns {Size, Packet}.
    bool get_size = false;
    // dist_get_stat/1's counts: dist_ctrl_put_data/2 calls and packets taken.
    std::uint64_t received = 0;
    std::uint64_t sent = 0;

    // dist_get_stat/1's pending output: the bytes queued, each packet with OTP's overhead.
    std::size_t queued() const noexcept;
    // Take the oldest packet (dist_ctrl_get_data/1); the output must not be empty.
    std::vector<std::byte> take();
};

// A monitor a process of this node holds on a process of the connection's node (plan 30 step DIST4b3b1): the
// watcher's pid word, the monitored pid in the external format or the registered name it was made with, and
// monitor/3's tag.
struct RemoteTarget final {
    Word watcher = 0;
    std::string pid;
    Word name = 0;
    std::shared_ptr<const MessageBuffer> tag = nullptr;
};

// A monitor a process of the connection's node holds on a process of this node (plan 30 step DIST4b3b1): the
// monitored pid word and the name it was made with or 0, and the watcher's pid and the monitor's reference in the
// external format.
struct RemoteWatcher final {
    Word target = 0;
    Word name = 0;
    std::string watcher;
    std::string reference;
};

// The process monitors between this node and the connection's node (plan 30 step DIST4b3b1), ended when the
// connection goes down. Each side's Signals entry names the node's atom in place of a pid.
struct ProcessMonitors final {
    // Monitors processes here hold, by their references.
    std::map<ReferenceIdentity, RemoteTarget> targets;
    // Monitors on processes here, by the key of the target's watcher entry, and those keys by the external format of
    // the monitor's reference.
    std::map<ReferenceIdentity, RemoteWatcher> watchers;
    std::map<std::string, ReferenceIdentity> keys;
};

// A MONITOR_P or DEMONITOR_P from the connection's node, as read (plan 30 step DIST4b3b1): the watcher (that node's
// pid), the monitored process of this node (a pid, a registered name, or a pid of an earlier incarnation) and the
// reference; terms of the controller's heap.
struct RemoteMonitor final {
    Term watcher;
    Term watched;
    Term reference;
};

// A MONITOR_P_EXIT or PAYLOAD_MONITOR_P_EXIT from the connection's node, as read: the reference of a monitor held
// here, its watcher (a pid) and the exit reason; terms of the controller's heap.
struct RemoteDown final {
    Term reference;
    Term watcher;
    Term reason;
};

// A link between a process of this node and a process of the connection's node (plan 30 step DIST5a): the other pid
// in the external format, and the id of the UNLINK_ID that unlink/1 sent while it waits for UNLINK_ID_ACK, else 0. A
// link being unlinked drops the exits that come for it and is not shown, as OTP's.
struct RemoteLink final {
    std::string pid;
    std::uint64_t unlinking = 0;
};

// The links between this node and the connection's node by the pid word of the process here, oldest first, ended
// when the connection goes down. Each such process's Signals links name the node's atom once (plan 30 step DIST5a).
using RemoteLinks = std::map<Word, std::vector<RemoteLink>>;

// A link signal from the connection's node, as read (plan 30 step DIST5a): the operation, that node's pid, the pid
// here (or of an earlier incarnation of this node), an UNLINK_ID's id and an exit's reason; terms of the
// controller's heap.
struct RemoteLinkSignal final {
    enum class Kind : std::uint8_t { link, unlink, unlink_ack, link_exit, exit };
    Kind kind = Kind::link;
    Term from;
    Term to;
    std::uint64_t id = 0;
    Term reason;
};

// One other node's connection entry, as OTP's dist entry: idle, pending (new_connection/1 or auto-connect) or up
// (setnode/3).
struct Connection final {
    enum class State : std::uint8_t { idle, pending, up };
    State state = State::idle;
    // The controller process's pid word while up, else 0.
    Word controller = 0;
    // The flags and creation setnode/3 gave the connection while up.
    std::uint64_t flags = 0;
    std::uint32_t creation = 0;
    // The connection id: each pending connection advances it, within 24 bits as OTP's.
    std::uint32_t id = 0;
    // The number of the reference every dist handle of the entry holds, as OTP's magic reference of the entry.
    std::uint64_t handle = 0;
    // When the entry last changed state; nodes/1 lists each state's entries oldest first, as OTP's lists come out.
    std::uint64_t stamp = 0;
    // monitor_node/2,3's monitors of the node (plan 30 step DIST2c): each process's count, one {nodedown, Node} per
    // count when the connection goes down or is aborted.
    std::map<Word, std::size_t> monitors;
    // The data path while up (plan 30 step DIST2d).
    DataPath data;
    // Process monitors to and from the node, made while pending or up (plan 30 step DIST4b3b1).
    ProcessMonitors processes;
    // Links to and from the node's processes, made while pending or up (plan 30 step DIST5a).
    RemoteLinks links;

    // Whether the connection is up and published (visible), not hidden.
    bool visible() const noexcept { return state == State::up && (flags & dflags::PUBLISHED) != 0; }
};

// A dist handle {ConnId, Ref} as numbers: the connection id and the reference's number.
struct DistHandle final {
    std::uint32_t id = 0;
    std::uint64_t reference = 0;
};

// setnode/3's {Flags, Creation}.
struct ChannelOptions final {
    std::uint64_t flags = 0;
    std::uint32_t creation = 0;
};

// What nodes/1,2 ask for: up visible or hidden entries, and known ones (idle or pending).
struct NodeKinds final {
    bool visible = false;
    bool hidden = false;
    bool known = false;
};

// An entry as nodes/1,2 list it: its node name atom word, its kind and its connection id while up.
struct ListedNode final {
    enum class Kind : std::uint8_t { visible, hidden, known };
    Word node = 0;
    Kind kind = Kind::known;
    std::optional<std::uint32_t> id;
};

// A node coming up or going down, as node status messages tell it: the node name atom word, the connection id (none
// for this node), whether it is visible, and nodedown's reason.
struct NodeStatus final {
    bool up = false;
    Word node = 0;
    std::optional<std::uint32_t> id;
    bool visible = true;
    Term reason;
};

// Options of a node status subscription (process_flag(monitor_nodes, ...)), as OTP's bits: 0 is the plain
// {nodeup, Node} form for visible nodes.
namespace node_status {
inline constexpr unsigned VISIBLE = 1U << 0;
inline constexpr unsigned HIDDEN = 1U << 1;
inline constexpr unsigned DOWN_REASON = 1U << 2;
inline constexpr unsigned INFO_MAP = 1U << 3;
inline constexpr unsigned CONNECTION_ID = 1U << 4;
} // namespace node_status

// One process's subscription with one option set, taken `count` times.
struct NodeSubscription final {
    Word pid = 0;
    unsigned options = 0;
    std::size_t count = 0;
};

// The entries by node name atom word, and the subscriptions in the order they were made (OTP's delivery order).
class ConnectionTable final {
  public:
    // The entry of `node`, or null.
    Connection *find(Word node) noexcept;
    const Connection *find(Word node) const noexcept;
    // The entry of `node`, created idle with `handle` as its reference number when there is none.
    Connection &entry(Word node, std::uint64_t handle);
    // The node of a dist handle's reference number, or none.
    std::optional<Word> node_of_handle(std::uint64_t handle) const noexcept;
    // The node whose connection `pid` controls, or none.
    std::optional<Word> controlled_by(Word pid) const noexcept;
    // State changes of the entry of `node`: pending with the next connection id, up under `controller`, or idle
    // again.
    void pend(Connection &connection) noexcept;
    void connect(Word node, Word controller, const ChannelOptions &options);
    void drop(Connection &connection) noexcept;
    // A node monitor of `pid` once more, or one fewer when it has one (plan 30 step DIST2c).
    static void monitor(Connection &connection, Word pid);
    static void demonitor(Connection &connection, Word pid) noexcept;
    // End every node monitor of an ended process.
    void forget_monitors(Word pid) noexcept;
    // The data path's connections (plan 30 step DIST2d). The up connection `pid` controls when `handle` is its
    // current one; notsup when it controls none, badarg for another handle.
    std::expected<Connection *, DataPathError> controlled(Word pid, const std::optional<DistHandle> &handle) noexcept;
    // `connection`, a handle's entry, when it is up with `pid` as its input handler; badarg for no entry, else notsup
    // when `pid` is no connection's input handler, badarg when another's.
    std::expected<Connection *, DataPathError> input_of(Connection *connection, Word pid) noexcept;
    // The entry of a handle's reference, or null; with `current`, only while the handle's id is the entry's.
    Connection *of_reference(std::uint64_t reference) noexcept;
    Connection *of_handle(const DistHandle &handle, bool current) noexcept;
    // The node words of the entries in `state`, oldest change first.
    std::vector<Word> in_state(Connection::State state) const;

    // The controllers of up connections and their nodes.
    const std::map<Word, Word> &controllers() const noexcept { return controllers_; }

    // Subscribe `pid` with `options` once more, or end that subscription; the count it had.
    std::size_t subscribe(Word pid, unsigned options, bool on);
    // End every subscription of an ended process.
    void unsubscribe(Word pid) noexcept;

    const std::vector<NodeSubscription> &subscriptions() const noexcept { return subscriptions_; }

    // Forget everything (the runtime stops).
    void clear() noexcept;

  private:
    std::map<Word, Connection> entries_;
    // Entries by their handle's reference number, and by their up connection's controller pid.
    std::map<std::uint64_t, Word> handles_;
    std::map<Word, Word> controllers_;
    std::vector<NodeSubscription> subscriptions_;
    // The last stamp given.
    std::uint64_t clock_ = 0;
};
} // namespace clause::runtime::detail
