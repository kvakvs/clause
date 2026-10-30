#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The node table of Clause's epmd (docs/distribution.md#epmd), shared by the bundled epmd and clause-epmd. Created for
// plan 30 step DIST3a; it knows nothing of sockets, so the request rules stay testable on their own.
namespace clause::runtime::epmd {
// One registration, as its ALIVE2_REQ described the node.
struct Node {
    // The node's name before the `@`, UTF-8 as sent.
    std::string name_;
    // The node's distribution listen port.
    std::uint16_t port_ = 0;
    // 77 normal node, 72 hidden node.
    std::uint8_t type_ = 0;
    // 0 TCP over IPv4.
    std::uint8_t protocol_ = 0;
    // The distribution versions the node speaks.
    std::uint16_t highest_ = 0;
    std::uint16_t lowest_ = 0;
    // The request's Extra field, returned by PORT2_RESP.
    std::string extra_;
    // The node's creation, given at registration.
    std::uint32_t creation_ = 0;
    // The registering connection's descriptor, which DUMP shows.
    std::uint64_t descriptor_ = 0;
};

// The registered names, newest first, and the names whose nodes went away.
class Registry final {
  public:
    // A table whose first creation is `first_creation` (random in a server, so restarted epmds differ).
    explicit Registry(std::uint32_t first_creation);

    // Register `node` and return its creation; nothing when its name is registered already.
    std::optional<std::uint32_t> add(Node node);
    // End the registration of `name`; its name is remembered with its creation.
    void remove(std::string_view name);
    // The registration of `name`, if any.
    const Node *find(std::string_view name) const;

    // Whether no name is registered.
    bool empty() const noexcept { return active_.empty(); }

    // The lines of NAMES_RESP after the epmd port, newest name first.
    std::string names_text() const;
    // The lines of DUMP_RESP after the epmd port: registered names, then remembered ones.
    std::string dump_text() const;

  private:
    // The creation of a new registration of `name`: one more than its remembered creation, else the first one.
    std::uint32_t creation_for(std::string_view name);

    // Registered nodes, newest first.
    std::vector<Node> active_;
    // Names whose nodes went away, newest first, kept for their creations and DUMP.
    std::vector<Node> unused_;
    // The creation of a name never registered before.
    std::uint32_t first_creation_;
};

// The 16-bit creation of ALIVE2_RESP for a node that speaks only version 5: 1 to 3, as OTP's epmd gives.
std::uint16_t short_creation(std::uint32_t creation) noexcept;
} // namespace clause::runtime::epmd
