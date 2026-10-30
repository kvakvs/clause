#pragma once
#include <clause/runtime/terms.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

// Connection entries of other nodes, node monitors (DIST2c) and node status subscriptions (plan 30 step DIST2b,
// docs/distribution.md#connections): the data the executor keeps under its lock, as OTP's dist table and nodes
// monitors keep it.
namespace clause::runtime::detail {
// OTP's distribution flags (dist.h) that the runtime reads or reports through erts_internal:get_dflags/0.
namespace dflags {
inline constexpr std::uint64_t PUBLISHED = 0x1;
inline constexpr std::uint64_t ATOM_CACHE = 0x2;
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
