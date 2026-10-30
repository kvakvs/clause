#pragma once
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace boost::asio {
class io_context;
} // namespace boost::asio

// Clause's epmd server over Boost.Asio (docs/distribution.md#epmd): the listeners and connections around the
// request rules of requests.hpp. Created for plan 30 step DIST3a, for clause-epmd and the bundled epmd.
namespace clause::runtime::epmd {
// How an epmd listens.
struct ServerOptions {
    // The TCP port, 4369 unless ERL_EPMD_PORT or -port says otherwise.
    std::uint16_t port_ = 4369;
    // The addresses to listen on, loopback added; empty listens on every address.
    std::vector<std::string> addresses_;
    // -relaxed_command_check.
    bool relaxed_ = false;
};

// A port number from 1 to 65535, from ERL_EPMD_PORT or -port.
std::optional<std::uint16_t> port_number(std::string_view text);

// The options ERL_EPMD_PORT, ERL_EPMD_ADDRESS and ERL_EPMD_RELAXED_COMMAND_CHECK give, or why one is invalid.
std::expected<ServerOptions, std::string> environment_options();

// A running epmd. Its handlers run on the io_context's thread; it ends with close() or an accepted KILL.
class Server final {
  public:
    class Impl;

    // Listen as `options` say, or describe why a listener could not open (an epmd on the port already). `killed`
    // runs on the io_context's thread after a KILL request stopped the server.
    static std::expected<Server, std::string> open(boost::asio::io_context &io, const ServerOptions &options,
                                                   std::function<void()> killed);

    // Stop listening and close every connection, ending their registrations; called on the io_context's thread.
    void close();

  private:
    explicit Server(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

    // Shared with the connections, whose handlers may outlive this handle.
    std::shared_ptr<Impl> impl_;
};
} // namespace clause::runtime::epmd
