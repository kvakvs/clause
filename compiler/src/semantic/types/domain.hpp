#pragma once
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace clause::semantic::types {
enum class Kind : std::uint8_t {
    top,
    bottom,
    atom,
    integer,
    variable,
    range,
    application,
    tuple,
    list,
    map,
    record,
    bitstring,
    function,
    unary,
    binary,
    annotation,
    union_type,
    reference,
    // An inferred list of known elements at fixed positions: children are the elements, then the tail.
    positional
};

struct Id {
    // Graph identity prevents accidentally combining unrelated analysis owners.
    std::uint64_t owner;
    std::size_t index;
    auto operator<=>(const Id &) const = default;
};

struct Node {
    // Symbolic Erlang categories never imply a native representation or small integer.
    Kind kind;
    std::string name = {};
    std::string module = {};
    std::vector<Id> children = {};
    // Flags preserve any/exact, list cardinality, operators and structural field roles.
    std::vector<std::string> labels = {};
    auto operator<=>(const Node &) const = default;
};

// Unoptimized builds analyse far slower, so their work budgets are smaller to keep large programs compiling quickly.
#ifdef NDEBUG
inline constexpr std::size_t DEFAULT_SYNTAX_WORK = std::size_t{8'000'000};
inline constexpr std::size_t DEFAULT_TYPE_WORK = std::size_t{200'000};
#else
inline constexpr std::size_t DEFAULT_SYNTAX_WORK = std::size_t{1'000'000};
inline constexpr std::size_t DEFAULT_TYPE_WORK = std::size_t{16'000};
#endif

// A hash of a node from its kind, names, children and labels, for the graph's identity map. Added for hashed node
// interning (OPTC.C).
struct NodeHash {
    std::size_t operator()(const Node &node) const noexcept;
};

struct Limits {
    // Bound abstract-state growth; exhaustion widens to top and is observable by callers.
    std::size_t nodes = std::size_t{1024} * 1024;
    std::size_t union_members = 256;
    // Whole-program inference steps, shared by every function of one pass.
    std::size_t syntax_work = DEFAULT_SYNTAX_WORK;
    // Steps of one declared type's resolution, translation or expansion walk.
    std::size_t type_work = DEFAULT_TYPE_WORK;
};

class Graph {
  public:
    // Establish a fresh identity owner with permanent top/bottom nodes, even for zero budgets.
    explicit Graph(Limits limits = {});
    Graph(const Graph &) = delete;
    Graph &operator=(const Graph &) = delete;
    Graph(Graph &&) = delete;
    Graph &operator=(Graph &&) = delete;
    ~Graph() = default;
    // Intern equal descriptions; children must belong to this graph.
    Id intern(Node node);

    // Checked lookup; references are invalidated by subsequent graph growth. Inline, as every lattice operation reads
    // nodes (changed for hashed node interning, OPTC.C).
    const Node &get(const Id id) const {
        if (id.owner != owner_) [[unlikely]] {
            foreign();
        }
        return nodes_.at(id.index);
    }

    // These distinguished identities denote every term and the empty set respectively.
    Id top() const { return {owner_, 0}; }

    Id bottom() const { return {owner_, 1}; }

    // Join is commutative/idempotent; bounded unions widen conservatively to top.
    Id join(std::span<const Id> members);
    Id widen(Id previous, Id next);
    // Expose resource exhaustion separately from a naturally inferred top type.
    Id exhausted();

    bool widened() const { return widened_; }

    // How often exhaustion widened to top, so callers can tell which work caused it. Added for plan step 41K1.
    std::size_t widenings() const { return widenings_; }

    // The nodes interned so far. Added for --print-stage-times.
    std::size_t size() const { return nodes_.size(); }

    // Facts every lattice join reads (char() and any improper list), interned by the first join and kept here so
    // later joins do not intern them again. Added for the Gather constants (OPTC.A).
    struct JoinConstants {
        Id characters;
        Id any_improper;
    };

    const std::optional<JoinConstants> &join_constants() const { return join_constants_; }

    void keep_join_constants(const JoinConstants constants) { join_constants_ = constants; }

    // How many containers deep a node nests counted no further than `limit` (below NESTING_LIMITS), once known: nodes
    // never change, so the lattice keeps each answer here. Added for memoized nesting (OPTC.C).
    static constexpr std::size_t NESTING_LIMITS = 8;

    std::optional<std::size_t> nesting(const Id id, const std::size_t limit) const {
        if (limit >= NESTING_LIMITS || id.index >= nesting_.size() || nesting_[id.index][limit] == UNKNOWN_NESTING) {
            return std::nullopt;
        }
        return nesting_[id.index][limit];
    }

    void keep_nesting(Id id, std::size_t limit, std::size_t depth) const;

    const Limits &limits() const { return limits_; }

  private:
    // Reject an identity of another graph.
    [[noreturn]] static void foreign();

    // Stable IDs survive vector growth and cannot alias another graph's nodes.
    std::uint64_t owner_;
    Limits limits_;
    bool widened_ = false;
    std::size_t widenings_ = 0;
    // Added for the Gather constants (OPTC.A).
    std::optional<JoinConstants> join_constants_;
    // Known nestings by node and limit; a cache, so a const graph fills it. Added for memoized nesting (OPTC.C).
    static constexpr std::uint8_t UNKNOWN_NESTING = 0xff;
    mutable std::vector<std::array<std::uint8_t, NESTING_LIMITS>> nesting_;
    std::vector<Node> nodes_;
    // Hashed: found by one hash and usually one comparison (changed for hashed node interning, OPTC.C).
    std::unordered_map<Node, std::size_t, NodeHash> identities_;
};
} // namespace clause::semantic::types
