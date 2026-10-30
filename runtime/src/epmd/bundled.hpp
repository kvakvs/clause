#pragma once
#include <expected>
#include <memory>
#include <mutex>
#include <string>

// The bundled epmd (plan 30 step DIST3b, docs/distribution.md#epmd): the server of server.hpp on a runtime thread that
// runs no Erlang process, started when a registration finds no epmd and stopped with the runtime.
namespace clause::runtime::epmd {
class Bundled final {
  public:
    Bundled();
    Bundled(const Bundled &) = delete;
    Bundled &operator=(const Bundled &) = delete;
    Bundled(Bundled &&) = delete;
    Bundled &operator=(Bundled &&) = delete;
    // Close the server, ending its registrations, and join its thread.
    ~Bundled();

    // Listen as the environment says (ERL_EPMD_PORT, ERL_EPMD_ADDRESS) unless this epmd listens already. Another
    // epmd holding the port wins and counts as success; any other failure is described.
    std::expected<void, std::string> start();

  private:
    class Thread;

    // Serializes start() of several processes.
    std::mutex mutex_;
    // The io_context and its thread, created by the first start().
    std::unique_ptr<Thread> thread_;
};
} // namespace clause::runtime::epmd
