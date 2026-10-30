// Synchronous Asio operations that take an error_code return nothing (BOOST_ASIO_NO_DEPRECATED, set by CMake).
#include "server.hpp"
#include "requests.hpp"
#include <array>
#include <boost/asio.hpp>
#include <charconv>
#include <chrono>
#include <optional>
#include <random>
#include <ranges>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace clause::runtime::epmd {
namespace {
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// A packet longer than this closes its connection.
constexpr std::size_t MAX_PACKET = 8192;
// A connection that holds no registration closes after this long, as OTP's epmd's -packet_timeout default.
constexpr auto PACKET_TIMEOUT = std::chrono::seconds(60);
// The bytes of a packet's length.
constexpr std::size_t LENGTH_BYTES = 2;

// The value of environment variable `name`, if it is set (epmd's variables are ASCII).
std::optional<std::string> variable(const char *name) {
#ifdef _WIN32
    std::array<char, 1024> value{};
    const auto size = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    return size == 0 || size >= value.size() ? std::nullopt : std::optional<std::string>(value.data());
#else
    const auto *value = std::getenv(name);
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}

// Whether `peer` is on this host: loopback, or the address the connection reached.
bool local_peer(const tcp::socket &socket) {
    boost::system::error_code error;
    const auto peer = socket.remote_endpoint(error).address();
    const auto self = socket.local_endpoint(error).address();
    return !error && (peer.is_loopback() || peer == self);
}

// The endpoints of `options`: every address of both families, else the listed ones with loopback added.
std::expected<std::vector<tcp::endpoint>, std::string> endpoints(const ServerOptions &options) {
    if (options.addresses_.empty()) {
        return std::vector{tcp::endpoint(tcp::v4(), options.port_), tcp::endpoint(tcp::v6(), options.port_)};
    }
    std::vector<tcp::endpoint> result;
    for (const auto &text : options.addresses_) {
        boost::system::error_code error;
        const auto address = asio::ip::make_address(text, error);
        if (error) {
            return std::unexpected("invalid address '" + text + "'");
        }
        result.emplace_back(address, options.port_);
    }
    const tcp::endpoint loopback(asio::ip::address_v4::loopback(), options.port_);
    if (std::ranges::find(result, loopback) == result.end()) {
        result.push_back(loopback);
    }
    return result;
}

// An acceptor listening on `endpoint` that no other process can share.
boost::system::error_code listen_on(tcp::acceptor &acceptor, const tcp::endpoint &endpoint) {
    boost::system::error_code error;
    acceptor.open(endpoint.protocol(), error);
    if (!error && endpoint.address().is_v6()) {
        acceptor.set_option(asio::ip::v6_only(true), error);
    }
#ifdef _WIN32
    if (!error) {
        acceptor.set_option(asio::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE>(true), error);
    }
#else
    if (!error) {
        acceptor.set_option(asio::socket_base::reuse_address(true), error);
    }
#endif
    if (!error) {
        acceptor.bind(endpoint, error);
    }
    if (!error) {
        acceptor.listen(asio::socket_base::max_listen_connections, error);
    }
    return error;
}

// Whether a listener failing with `error` is left out: IPv6 on a host without it, when every address is asked for.
bool optional_listener(const ServerOptions &options, const tcp::endpoint &endpoint,
                       const boost::system::error_code &error) {
    return options.addresses_.empty() && endpoint.address().is_v6() && error != asio::error::address_in_use;
}

// A random first creation, so the nodes of a restarted epmd get new ones.
std::uint32_t random_creation() {
    std::random_device device;
    return std::uniform_int_distribution<std::uint32_t>()(device);
}

class Session;
} // namespace

// The listeners, the node table and the connections that hold registrations.
class Server::Impl final : public std::enable_shared_from_this<Impl> {
  public:
    Impl(asio::io_context &io, const ServerOptions &options, std::function<void()> killed)
        : io_(io), settings_{.port_ = options.port_, .relaxed_ = options.relaxed_}, registry_(random_creation()),
          killed_(std::move(killed)) {}

    // Open the listeners, or describe the first that failed.
    std::expected<void, OpenFailure> listen(const ServerOptions &options);
    void close();

    // A request of a connection that holds no registration; a registration or a STOP takes effect at once.
    Outcome request(const std::shared_ptr<Session> &session, const Requester &requester, std::string_view packet);
    // The connection holding `name`'s registration closed.
    void ended(const std::string &name, const Session *session);
    // A KILL request was answered.
    void kill();

  private:
    void accept(tcp::acceptor &acceptor);

    asio::io_context &io_;
    Settings settings_;
    Registry registry_;
    // The listeners, owned through pointers since their accept handlers refer to them.
    std::vector<std::unique_ptr<tcp::acceptor>> acceptors_;
    // Every connection, closed with the server; expired entries are dropped as connections arrive.
    std::vector<std::weak_ptr<Session>> sessions_;
    // The connection holding each registered name.
    std::unordered_map<std::string, std::weak_ptr<Session>> holders_;
    std::function<void()> killed_;
    bool closed_ = false;
};

namespace {
// One connection: reads packets, answers them, and holds a registration until it closes.
class Session final : public std::enable_shared_from_this<Session> {
  public:
    Session(std::shared_ptr<Server::Impl> server, tcp::socket socket)
        : server_(std::move(server)), socket_(std::move(socket)), timer_(socket_.get_executor()),
          requester_{.local_ = local_peer(socket_),
                     .descriptor_ = static_cast<std::uint64_t>(socket_.native_handle())} {}

    // Start the packet timeout and read the first request.
    void start() {
        timer_.expires_after(PACKET_TIMEOUT);
        timer_.async_wait([self = shared_from_this()](const boost::system::error_code &error) {
            if (!error) {
                self->close();
            }
        });
        read();
    }

    // Close the connection; a registration it holds ends.
    void close() {
        if (closed_) {
            return;
        }
        closed_ = true;
        timer_.cancel();
        boost::system::error_code ignored;
        socket_.close(ignored);
        if (!name_.empty()) {
            server_->ended(name_, this);
        }
    }

  private:
    void read() {
        socket_.async_read_some(asio::buffer(chunk_),
                                [self = shared_from_this()](const boost::system::error_code &error, std::size_t bytes) {
                                    if (error) {
                                        self->close();
                                        return;
                                    }
                                    self->received(bytes);
                                });
    }

    // Collect a packet; any byte on a registered connection, or bytes past the packet, close it as in OTP.
    void received(std::size_t bytes) {
        input_.append(chunk_.data(), bytes);
        if (!name_.empty()) {
            close();
            return;
        }
        if (input_.size() < LENGTH_BYTES) {
            read();
            return;
        }
        const std::size_t length = (static_cast<unsigned char>(input_[0]) << 8) | static_cast<unsigned char>(input_[1]);
        if (length == 0 || length > MAX_PACKET || input_.size() > LENGTH_BYTES + length) {
            close();
        } else if (input_.size() < LENGTH_BYTES + length) {
            read();
        } else {
            answer(server_->request(shared_from_this(), requester_, std::string_view(input_).substr(LENGTH_BYTES)));
        }
    }

    // Write the reply of a request, then do what it asks.
    void answer(Outcome outcome) {
        input_.clear();
        if (outcome.after_ == After::registered) {
            name_ = outcome.name_;
            timer_.cancel();
        }
        if (outcome.reply_.empty()) {
            close();
            return;
        }
        output_ = std::move(outcome.reply_);
        asio::async_write(
            socket_, asio::buffer(output_),
            [self = shared_from_this(), after = outcome.after_](const boost::system::error_code &error, std::size_t) {
                self->written(error ? After::close : after);
            });
    }

    void written(After after) {
        switch (after) {
        case After::keep_reading:
        case After::registered:
            read();
            break;
        case After::kill:
            close();
            server_->kill();
            break;
        case After::close:
            close();
            break;
        }
    }

    std::shared_ptr<Server::Impl> server_;
    tcp::socket socket_;
    // The packet timeout of a connection that holds no registration.
    asio::steady_timer timer_;
    Requester requester_;
    // The bytes of the packet being read.
    std::string input_;
    // The reply being written.
    std::string output_;
    std::array<char, 1024> chunk_{};
    // The name this connection registered, empty until then.
    std::string name_;
    bool closed_ = false;
};
} // namespace

std::expected<void, OpenFailure> Server::Impl::listen(const ServerOptions &options) {
    const auto wanted = endpoints(options);
    if (!wanted) {
        return std::unexpected(OpenFailure{.message_ = wanted.error()});
    }
    for (const auto &endpoint : *wanted) {
        auto acceptor = std::make_unique<tcp::acceptor>(io_);
        if (const auto error = listen_on(*acceptor, endpoint)) {
            if (optional_listener(options, endpoint, error)) {
                continue;
            }
            return std::unexpected(OpenFailure{.message_ = "cannot listen on " + endpoint.address().to_string() +
                                                           " port " + std::to_string(endpoint.port()) + ": " +
                                                           error.message(),
                                               .in_use_ = error == asio::error::address_in_use});
        }
        acceptors_.push_back(std::move(acceptor));
    }
    for (const auto &acceptor : acceptors_) {
        accept(*acceptor);
    }
    return {};
}

void Server::Impl::accept(tcp::acceptor &acceptor) {
    acceptor.async_accept(
        [self = shared_from_this(), &acceptor](const boost::system::error_code &error, tcp::socket socket) {
            if (self->closed_) {
                return;
            }
            if (!error) {
                std::erase_if(self->sessions_, [](const auto &session) { return session.expired(); });
                auto session = std::make_shared<Session>(self, std::move(socket));
                self->sessions_.push_back(session);
                session->start();
            }
            self->accept(acceptor);
        });
}

Outcome Server::Impl::request(const std::shared_ptr<Session> &session, const Requester &requester,
                              std::string_view packet) {
    auto outcome = handle_request(registry_, settings_, requester, packet);
    if (outcome.after_ == After::registered) {
        holders_[outcome.name_] = session;
    } else if (!outcome.name_.empty()) {
        // A STOP request ends the node's registration by closing its connection.
        const auto holder = holders_.find(outcome.name_);
        if (const auto open = holder == holders_.end() ? nullptr : holder->second.lock()) {
            open->close();
        }
    }
    return outcome;
}

void Server::Impl::ended(const std::string &name, const Session *session) {
    const auto holder = holders_.find(name);
    if (holder != holders_.end() && holder->second.lock().get() == session) {
        holders_.erase(holder);
        registry_.remove(name);
    }
}

void Server::Impl::kill() {
    close();
    if (killed_) {
        killed_();
    }
}

void Server::Impl::close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    for (const auto &acceptor : acceptors_) {
        boost::system::error_code ignored;
        acceptor->close(ignored);
    }
    for (const auto &session : sessions_) {
        if (const auto open = session.lock()) {
            open->close();
        }
    }
}

std::optional<std::uint16_t> port_number(std::string_view text) {
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

std::expected<ServerOptions, std::string> environment_options() {
    ServerOptions options;
    if (const auto port = variable("ERL_EPMD_PORT")) {
        const auto number = port_number(*port);
        if (!number) {
            return std::unexpected("invalid ERL_EPMD_PORT '" + *port + "'");
        }
        options.port_ = *number;
    }
    if (const auto addresses = variable("ERL_EPMD_ADDRESS")) {
        for (const auto part : std::views::split(*addresses, ',')) {
            if (!part.empty()) {
                options.addresses_.emplace_back(part.begin(), part.end());
            }
        }
    }
    options.relaxed_ = variable("ERL_EPMD_RELAXED_COMMAND_CHECK").has_value();
    return options;
}

std::expected<Server, OpenFailure> Server::open(asio::io_context &io, const ServerOptions &options,
                                                std::function<void()> killed) {
    auto impl = std::make_shared<Impl>(io, options, std::move(killed));
    if (auto listening = impl->listen(options); !listening) {
        impl->close();
        return std::unexpected(listening.error());
    }
    return Server(std::move(impl));
}

void Server::close() { impl_->close(); }
} // namespace clause::runtime::epmd
