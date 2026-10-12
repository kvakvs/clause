#pragma once
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
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
inline constexpr std::size_t DEFAULT_SYNTAX_WORK = 10 * 1024 * 1024;
inline constexpr std::size_t DEFAULT_TYPE_WORK = 100000;
#else
inline constexpr std::size_t DEFAULT_SYNTAX_WORK = 200000;
inline constexpr std::size_t DEFAULT_TYPE_WORK = 20000;
#endif

struct Limits {
    // Bound abstract-state growth; exhaustion widens to top and is observable by callers.
    std::size_t nodes = 1024 * 1024;
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
    // Checked lookup; references are invalidated by subsequent graph growth.
    const Node &get(Id id) const;

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

    const Limits &limits() const { return limits_; }

  private:
    // Stable IDs survive vector growth and cannot alias another graph's nodes.
    std::uint64_t owner_;
    Limits limits_;
    bool widened_ = false;
    std::size_t widenings_ = 0;
    std::vector<Node> nodes_;
    std::map<Node, std::size_t> identities_;
};
} // namespace clause::semantic::types
