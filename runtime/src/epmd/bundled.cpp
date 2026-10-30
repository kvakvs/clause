// Synchronous Asio operations that take an error_code return nothing (BOOST_ASIO_NO_DEPRECATED, set by CMake).
#include "bundled.hpp"
#include "server.hpp"
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <exception>
#include <future>
#include <optional>
#include <thread>

namespace clause::runtime::epmd {
namespace asio = boost::asio;

// The thread the bundled epmd runs on; the server is opened, closed and served only there.
class Bundled::Thread final {
  public:
    Thread() : guard_(asio::make_work_guard(io_)), thread_([this] { run(); }) {}

    Thread(const Thread &) = delete;
    Thread &operator=(const Thread &) = delete;
    Thread(Thread &&) = delete;
    Thread &operator=(Thread &&) = delete;

    // Close the server; the io_context runs out of work once its connections closed.
    ~Thread() {
        try {
            asio::post(io_, [this] {
                if (server_) {
                    server_->close();
                }
            });
            guard_.reset();
            thread_.join();
        } catch (...) {
            // A failing join would leave the epmd thread serving destroyed state, as for the I/O thread.
            std::terminate();
        }
    }

    // Open a server on the thread unless one listens, and wait for the outcome.
    std::expected<void, std::string> open() {
        std::promise<std::expected<void, std::string>> outcome;
        asio::post(io_, [this, &outcome] { outcome.set_value(open_here()); });
        return outcome.get_future().get();
    }

  private:
    // Serve until the work guard goes and every connection closed; a handler that throws (out of memory) is dropped
    // and the others go on, as Asio recommends.
    void run() noexcept {
        for (;;) {
            try {
                io_.run();
                return;
            } catch (...) {
                continue;
            }
        }
    }

    std::expected<void, std::string> open_here() {
        if (listening_) {
            return {};
        }
        const auto options = environment_options();
        if (!options) {
            return std::unexpected(options.error());
        }
        auto server = Server::open(io_, *options, [this] { listening_ = false; });
        if (!server) {
            return server.error().in_use_ ? std::expected<void, std::string>{}
                                          : std::unexpected(server.error().message_);
        }
        server_ = std::move(*server);
        listening_ = true;
        return {};
    }

    asio::io_context io_{1};
    // Keeps the io_context running while no connection is open.
    asio::executor_work_guard<asio::io_context::executor_type> guard_;
    // The server last opened; a KILL request stops it, and the next start() opens another.
    std::optional<Server> server_;
    // Whether server_ listens; changed on the thread only.
    bool listening_ = false;
    // Declared last, so it starts once the members it uses exist.
    std::thread thread_;
};

Bundled::Bundled() = default;

Bundled::~Bundled() = default;

std::expected<void, std::string> Bundled::start() {
    const std::scoped_lock lock(mutex_);
    if (!thread_) {
        thread_ = std::make_unique<Thread>();
    }
    return thread_->open();
}
} // namespace clause::runtime::epmd
