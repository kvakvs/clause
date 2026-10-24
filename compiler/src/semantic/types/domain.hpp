#pragma once
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace clause::semantic::types {
class LatticeTrace;

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

// The answers of lattice operations by a key packing the operation and its operands, with the joins each ran nested,
// in an open-addressed table (key 0 marks a free slot). Added for memoized lattice operations (OPTC.B).
class Answers {
  public:
    // The result's node index and the joins its computation ran nested (changed to 32 bits each, so a key and its
    // answer share one 16-byte slot, for allocation-free joins, OPTC.B part 2).
    struct Answer {
        std::uint32_t result;
        std::uint32_t joins;
    };

    const Answer *find(std::uint64_t key) const;
    void keep(std::uint64_t key, Answer answer);

  private:
    struct Slot {
        std::uint64_t key;
        Answer answer;
    };

    void grow();

    std::vector<Slot> slots_;
    std::size_t filled_ = 0;
};

// The identity map of a graph: open-addressed slots of a node's hash and its index in the graph's nodes, so each node
// is stored once and a probe compares hashes before nodes. Added for compact interning (OPTC.C).
class Identities {
  public:
    // The index of the node of `nodes` equal to `node`, whose hash is `hash`, if it was added.
    std::optional<std::uint32_t> find(const Node &node, std::uint32_t hash, std::span<const Node> nodes) const;
    void add(std::uint32_t hash, std::uint32_t index);

  private:
    struct Slot {
        std::uint32_t hash;
        std::uint32_t index;
    };

    static constexpr std::uint32_t FREE = UINT32_MAX;

    void grow();

    std::vector<Slot> slots_;
    std::size_t filled_ = 0;
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

    // How many lattice joins ran on this graph, nested ones included: the deterministic cost of inference work.
    // Added for cost-aware give-up (CONV.4).
    std::size_t joins() const { return joins_; }

    void count_join() { ++joins_; }

    // The lattice operations whose answers the graph keeps: a join of one member (added for allocation-free joins,
    // OPTC.B part 2) or two, meet and within. Added for memoized lattice operations (OPTC.B).
    enum class Operation : std::uint8_t { join_one, join, meet, within };

    // The answer of `operation` on `left` and `right` `depth` containers deep, from `compute` the first time: nodes
    // never change, so it holds for the graph's life. An answer counts the joins it ran again; one computed while a
    // budget widened to term() is not kept. Added for memoized lattice operations (OPTC.B).
    template <typename Compute> Id answer(Operation operation, Id left, Id right, std::size_t depth, Compute compute) {
        const auto key = answer_key(operation, left, right, depth);
        if (key == 0) {
            return compute();
        }
        if (const auto *known = answers_.find(key)) {
            joins_ += known->joins;
            return {owner_, known->result};
        }
        const auto joins = joins_;
        const auto widenings = widenings_;
        const auto result = compute();
        if (widenings_ == widenings && result.index <= UINT32_MAX && joins_ - joins <= UINT32_MAX) {
            answers_.keep(key, {static_cast<std::uint32_t>(result.index), static_cast<std::uint32_t>(joins_ - joins)});
        }
        return result;
    }

    // Where lattice operations are counted while a profile asks for it, else null. Added for the lattice operation
    // counts of --print-stage-times (INS.2).
    LatticeTrace *trace() const { return trace_; }

    void trace_with(LatticeTrace *trace) { trace_ = trace; }

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
    // The key of an answer, or 0 when its operands are too large to pack. Added for memoized lattice operations
    // (OPTC.B).
    static std::uint64_t answer_key(Operation operation, Id left, Id right, std::size_t depth);

    // Stable IDs survive vector growth and cannot alias another graph's nodes.
    std::uint64_t owner_;
    Limits limits_;
    bool widened_ = false;
    std::size_t widenings_ = 0;
    std::size_t joins_ = 0;
    // Added for the lattice operation counts of --print-stage-times (INS.2).
    LatticeTrace *trace_ = nullptr;
    // Added for memoized lattice operations (OPTC.B).
    Answers answers_;
    // Added for the Gather constants (OPTC.A).
    std::optional<JoinConstants> join_constants_;
    // Known nestings by node and limit; a cache, so a const graph fills it. Added for memoized nesting (OPTC.C).
    static constexpr std::uint8_t UNKNOWN_NESTING = 0xff;
    mutable std::vector<std::array<std::uint8_t, NESTING_LIMITS>> nesting_;
    std::vector<Node> nodes_;
    // Hashed: found by one hash and usually one comparison (changed for hashed node interning, OPTC.C, and to index
    // `nodes_` for compact interning).
    Identities identities_;
};
} // namespace clause::semantic::types
