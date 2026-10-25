// Synchronous Asio operations that take an error_code return nothing (BOOST_ASIO_NO_DEPRECATED, set by CMake, removes
// the deprecated overloads that return it too).
#include "sockets.hpp"
#include "io.hpp"
#include "socket_options.hpp"
#include <array>
#include <boost/asio.hpp>
#include <cstring>
#include <deque>
#include <exception>
#include <future>
#include <mutex>
#include <numeric>
#include <thread>

// Sockets as ports (docs/ports.md#sockets). Every socket's state lives on the I/O thread: port_control/3
// operations run there (the calling worker waits for their reply), completions continue there and report through
// the executor. Replies of operations start with a status byte: 0 ok, then the result; 1 error, then its reason.
namespace clause::runtime::detail {
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using udp = asio::ip::udp;

// The sockets' share of the reactor: its io_context and the gate deliveries pass.
class SocketService::Impl final {
  public:
    Impl(asio::io_context &io, Deliver deliver) : context(io), deliver_(std::move(deliver)) {}

    // Hand an event to the executor unless the service stopped.
    void deliver(Word port, SocketEvent event) {
        const std::scoped_lock lock(mutex_);
        if (open_) {
            deliver_(port, std::move(event));
        }
    }

    // Stop delivering; waits for a delivery in progress.
    void close() {
        const std::scoped_lock lock(mutex_);
        open_ = false;
    }

    asio::io_context &context;

  private:
    std::mutex mutex_;
    bool open_ = true;
    Deliver deliver_;
};

namespace {
using Bytes = std::vector<std::byte>;
using Service = SocketService::Impl;

// The port_control/3 operations of a socket port.
enum class SocketOperation : std::uint8_t {
    connect = 1,
    listen = 2,
    accept = 3,
    recv = 4,
    send = 5,
    setopts = 6,
    sockname = 7,
    peername = 8,
    shutdown = 9,
    udp_open = 10,
    udp_send = 11,
    udp_recv = 12,
    cancel = 13,
    resolve = 14,
    // The machine's host name (step 41R).
    hostname = 15,
    // Statistics, status flags, options and UDP connect, created for inet:getstat/2, inet:info/1,
    // inet:getopts/2 and gen_udp:connect/3.
    getstat = 16,
    getstatus = 17,
    getopt = 18,
    setopt = 19,
    udp_connect = 20,
    // Send one datagram at once with ancillary data (gen_udp:send/5).
    udp_send_now = 21,
    // Bind a stream socket before it listens or connects (socket:bind/2, plan 21 step LIB62).
    bind = 22,
};

// The status flags of getstatus, as inet_drv's: open, connected, connecting, listen, accepting.
enum StatusFlag : std::uint8_t { OPEN = 1, CONNECTED = 2, CONNECTING = 4, LISTEN = 8, ACCEPTING = 16 };

// How data reaches the owner: as messages ({active, true}, once for one message) or only through recv.
enum class Active : std::uint8_t { passive = 0, active = 1, once = 2 };

// A value of setopts/3 bytes that leaves an option unchanged.
constexpr std::uint8_t UNCHANGED = 255;

// A reply of success with `result`.
Bytes ok(std::span<const std::byte> result = {}) {
    Bytes reply{std::byte{0}};
    reply.insert(reply.end(), result.begin(), result.end());
    return reply;
}

// A reply of an error with its POSIX reason.
Bytes failure(std::string_view reason) {
    Bytes reply{std::byte{1}};
    std::ranges::transform(reason, std::back_inserter(reply), [](char c) { return static_cast<std::byte>(c); });
    return reply;
}

// The POSIX reason of a socket error, as OTP's inet names it.
std::string reason(const boost::system::error_code &error) {
    static const std::array<std::pair<std::errc, std::string_view>, 14> NAMES{{
        {std::errc::connection_refused, "econnrefused"},
        {std::errc::address_in_use, "eaddrinuse"},
        {std::errc::connection_reset, "econnreset"},
        {std::errc::connection_aborted, "econnaborted"},
        {std::errc::timed_out, "etimedout"},
        {std::errc::permission_denied, "eacces"},
        {std::errc::address_not_available, "eaddrnotavail"},
        {std::errc::network_unreachable, "enetunreach"},
        {std::errc::host_unreachable, "ehostunreach"},
        {std::errc::not_connected, "enotconn"},
        {std::errc::broken_pipe, "epipe"},
        {std::errc::invalid_argument, "einval"},
        {std::errc::address_family_not_supported, "eafnosupport"},
        {std::errc::message_size, "emsgsize"},
    }};
    if (error == asio::error::eof || error == asio::error::operation_aborted) {
        return "closed";
    }
#if defined(_WIN32)
    // ConnectEx reports a local address that collides with one still in use as ERROR_DUP_NAME.
    if (error.category() == boost::system::system_category() && error.value() == ERROR_DUP_NAME) {
        return "eaddrinuse";
    }
#endif
    const auto condition = error.default_error_condition();
    const auto found = std::ranges::find_if(
        NAMES, [&](const auto &name) { return condition == std::make_error_condition(name.first); });
    return found == NAMES.end() ? std::string("eio") : std::string(found->second);
}

// A big-endian integer of `size` bytes at `at`.
std::uint64_t number(std::span<const std::byte> bytes, std::size_t at, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size && at + index < bytes.size(); ++index) {
        value = (value << 8) | std::to_integer<std::uint64_t>(bytes[at + index]);
    }
    return value;
}

// An address encoded as a family byte (4 or 6) and its 4 or 16 bytes at `at`; `at` moves past it.
std::optional<asio::ip::address> address(std::span<const std::byte> bytes, std::size_t &at) {
    if (at >= bytes.size()) {
        return std::nullopt;
    }
    const auto family = std::to_integer<int>(bytes[at++]);
    const std::size_t size = family == 6 ? 16 : 4;
    if (at + size > bytes.size()) {
        return std::nullopt;
    }
    if (family == 6) {
        asio::ip::address_v6::bytes_type raw{};
        std::ranges::transform(bytes.subspan(at, size), raw.begin(),
                               [](std::byte b) { return std::to_integer<unsigned char>(b); });
        at += size;
        return asio::ip::address(asio::ip::address_v6(raw));
    }
    asio::ip::address_v4::bytes_type raw{};
    std::ranges::transform(bytes.subspan(at, size), raw.begin(),
                           [](std::byte b) { return std::to_integer<unsigned char>(b); });
    at += size;
    return asio::ip::address(asio::ip::address_v4(raw));
}

// The family byte, port and address bytes of an endpoint.
template <typename Endpoint> Bytes endpoint_bytes(const Endpoint &endpoint) {
    const auto address = endpoint.address();
    Bytes result{static_cast<std::byte>(address.is_v6() ? 6 : 4), static_cast<std::byte>(endpoint.port() >> 8),
                 static_cast<std::byte>(endpoint.port() & 0xff)};
    if (address.is_v6()) {
        for (const auto byte : address.to_v6().to_bytes()) {
            result.push_back(static_cast<std::byte>(byte));
        }
    } else {
        for (const auto byte : address.to_v4().to_bytes()) {
            result.push_back(static_cast<std::byte>(byte));
        }
    }
    return result;
}

// An address as Erlang's tuple of four bytes or eight 16-bit groups.
PortValue address_value(const asio::ip::address &address) {
    std::vector<PortValue> parts;
    if (address.is_v6()) {
        const auto raw = address.to_v6().to_bytes();
        for (std::size_t index = 0; index < raw.size(); index += 2) {
            parts.push_back(PortValue::of_integer((raw[index] << 8) | raw[index + 1]));
        }
    } else {
        for (const auto byte : address.to_v4().to_bytes()) {
            parts.push_back(PortValue::of_integer(byte));
        }
    }
    return PortValue::of_tuple(std::move(parts));
}

// The data mode of a socket: active mode, packet header bytes and binary or list data.
struct Mode {
    Active active = Active::active;
    std::size_t packet = 0;
    bool binary = false;

    // Apply setopts/connect/listen bytes at `at`: active, packet and binary, each UNCHANGED to keep it.
    void apply(std::span<const std::byte> bytes, std::size_t at) {
        const auto field = [&](std::size_t index) {
            return at + index < bytes.size() ? std::to_integer<std::uint8_t>(bytes[at + index]) : UNCHANGED;
        };
        constexpr std::array MODES{Active::passive, Active::active, Active::once};
        active = field(0) < MODES.size() ? MODES.at(field(0)) : active;
        packet = field(1) == UNCHANGED ? packet : field(1);
        binary = field(2) == UNCHANGED ? binary : field(2) != 0;
    }

    // The mode as setopts bytes: active, packet and binary.
    Bytes bytes() const {
        return {static_cast<std::byte>(active), static_cast<std::byte>(packet), static_cast<std::byte>(binary)};
    }

    // The input framing of the mode.
    PortOptions framing() const {
        PortOptions options;
        options.framing = packet == 0 ? Framing::stream : Framing::packet;
        options.packet_bytes = packet;
        return options;
    }
};

// A caller waiting for recv and the length it wants.
struct Waiting {
    Word caller = 0;
    std::size_t length = 0;
};

// One datagram received.
struct Datagram {
    udp::endpoint from;
    Bytes data;
};

class Socket;

// The driver of a socket port.
std::shared_ptr<PortDriver> socket_driver(const std::shared_ptr<Socket> &socket);

// A socket's state on the I/O thread.
class Socket final : public std::enable_shared_from_this<Socket> {
  public:
    Socket(std::shared_ptr<Service> service, bool udp) : service_(std::move(service)), udp_(udp) {}

    asio::io_context &context() noexcept { return service_->context; }

    // Learn the port's word.
    void attach(Word port) noexcept { port_ = port; }

    // Adopt an accepted connection with the listening socket's mode and options; it serves once its port exists.
    void adopt(tcp::socket socket, const Mode &mode, const socket_options::Options &options) {
        auto &stream = stream_.emplace(std::move(socket));
        mode_ = mode;
        options_ = options;
        // An option the connection refuses keeps the system's value, as the connection exists already.
        static_cast<void>(options_.apply(static_cast<socket_options::Handle>(stream.native_handle())));
        decoder_ = InputDecoder(mode_.framing());
        adopted_ = true;
    }

    // Whether this is an accepted connection, which starts serving when it learns its port.
    bool adopted() const noexcept { return adopted_; }

    // Run operation `operation` of `caller` on the I/O thread; the reply, or none for an unknown operation.
    std::optional<Bytes> operate(Word caller, SocketOperation operation, std::span<const std::byte> data);

    // The port closed: answer waiting callers with closed and close the socket once its queued output is sent.
    void close();

    // Continue reading and answering after an accepted connection got its port.
    void start() { serve(); }

  private:
    // The operations of every socket: data mode, cancellation, statistics, status and options; none for another.
    std::optional<Bytes> common_operation(Word caller, SocketOperation operation, std::span<const std::byte> data);
    std::optional<Bytes> stream_operation(Word caller, SocketOperation operation, std::span<const std::byte> data);
    std::optional<Bytes> datagram_operation(Word caller, SocketOperation operation, std::span<const std::byte> data);
    Bytes connect(Word caller, std::span<const std::byte> data);
    Bytes listen(std::span<const std::byte> data);
    Bytes accept(Word caller);
    Bytes recv(Word caller, std::size_t length);
    // Move the socket bind/2 bound into the listener (stream_ holds it).
    void adopt_bound(tcp::acceptor &listener, boost::system::error_code &error);
    // Open and bind a stream socket that listen or connect then uses (socket:bind/2).
    Bytes bind(std::span<const std::byte> data);
    Bytes send(std::span<const std::byte> data);
    Bytes names(bool peer);
    Bytes shutdown(std::span<const std::byte> data);
    Bytes cancel(Word caller);
    Bytes udp_open(std::span<const std::byte> data);
    Bytes udp_send(std::span<const std::byte> data);
    // Connect a UDP socket to one peer (gen_udp:connect/3).
    Bytes udp_connect(std::span<const std::byte> data);
    // Send one datagram at once with ancillary data set for it alone (gen_udp:send/5).
    Bytes udp_send_now(std::span<const std::byte> data);
    // The status flags, the socket type (0 stream, 1 datagram) and the family (4, 6 or 0 before opening).
    Bytes status() const;
    std::uint8_t status_flags() const;
    int family() const;
    // The values of the option codes in `data`.
    Bytes getopt(std::span<const std::byte> data);
    // Set the option records in `data`.
    Bytes setopt(std::span<const std::byte> data);
    // Take the option records after an open request's fields at `at`; an error reason, or empty.
    std::string preset(std::span<const std::byte> data, std::size_t at);
    // Open `socket` for `protocol` and apply the options taken so far; an error reason, or empty.
    template <typename AsioSocket, typename Protocol> std::string open(AsioSocket &socket, const Protocol &protocol);
    // Take the options after `at`, then open `socket`; an error reason, or empty.
    template <typename AsioSocket, typename Protocol>
    std::string open_with(AsioSocket &socket, const Protocol &protocol, std::span<const std::byte> data,
                          std::size_t at);
    // The native handle of the open socket, none before one is open.
    std::optional<socket_options::Handle> handle();
    // Queue `bytes` for writing to `to`, counting them as sent and pending.
    void queue_write(const udp::endpoint &to, Bytes bytes);
    // Start writing the front of the queue, calling `done` when written.
    template <typename Done> void start_write(const udp::endpoint &to, const Bytes &bytes, Done done);
    // sockname and peername of a UDP socket.
    Bytes datagram_names(udp::socket &datagram, bool peer) const;

    // Accept the next connection for the first waiting caller.
    void start_accept();
    // Accept completed: give the connection to the first waiting caller as a new port.
    void accepted(const boost::system::error_code &error, tcp::socket socket);
    // Write the queued output in order.
    void write_next();
    // Close the socket itself, gracefully for a connection.
    void release();
    // Answer waiting recv calls and deliver active messages from what was read; read more when wanted.
    void serve();
    // Serve stream data (TCP); true when something was delivered.
    bool serve_stream();
    // Serve datagrams (UDP); true when something was delivered.
    bool serve_datagrams();
    // Report the end of a TCP connection: closed to waiting recvs, tcp_closed (after tcp_error) when active.
    void report_end();
    // Start one read when messages or recvs want data.
    void read();
    // A TCP read completed.
    void read_done(const boost::system::error_code &error, std::size_t size);
    // A UDP receive completed.
    void receive_done(const boost::system::error_code &error, std::size_t size);
    // The next unit of TCP data for a recv of `length` bytes (0: what there is), if there is one.
    std::optional<Bytes> take(std::size_t length);
    // Send {clause_socket, Port, Reply} to `target`, or a message to the connected process (target 0).
    void reply(Word target, PortValue value);

    void message(PortValue value) { reply(0, std::move(value)); }

    // {clause_socket, Port, Reply}.
    PortValue answer(PortValue reply_value) const;

    // Data as the mode's binary or list.
    PortValue data(Bytes bytes) const { return PortValue::of_bytes(std::move(bytes), mode_.binary); }

    std::shared_ptr<Service> service_;
    bool udp_;
    std::atomic<Word> port_{0};
    std::optional<tcp::socket> stream_;
    std::optional<tcp::acceptor> listener_;
    std::optional<udp::socket> datagram_;
    // An accepted connection, set before its port exists and read by the executor once it does.
    bool adopted_ = false;
    Mode mode_;
    InputDecoder decoder_{PortOptions{}};
    // Stream bytes and packets read but not delivered yet.
    Bytes stream_bytes_;
    std::deque<Bytes> packets_;
    std::deque<Datagram> datagrams_;
    // Callers waiting for recv (with their length) and for accept, oldest first.
    std::deque<Waiting> recvs_;
    std::deque<Word> accepts_;
    // Queued TCP output, or UDP datagrams with their destinations.
    std::deque<std::pair<udp::endpoint, Bytes>> writes_;
    // Bytes queued and not written yet (send_pend), the traffic counters and the options beyond the data mode,
    // created for inet:getstat/2 and inet:getopts/2.
    std::uint64_t pending_ = 0;
    socket_options::Counters counters_;
    socket_options::Options options_;
    // A TCP connect is in progress; a UDP socket is connected to one peer (gen_udp:connect/3).
    bool connecting_ = false;
    // stream_ is a socket bound by socket:bind/2 that has neither listened nor connected yet (plan 21 step LIB62).
    bool bound_ = false;
    bool connected_ = false;
    bool writing_ = false;
    bool reading_ = false;
    bool accepting_ = false;
    // The connection ended (end of input or an error, kept in error_); the port closed.
    bool ended_ = false;
    bool reported_ = false;
    std::string error_;
    bool shut_ = false;
    std::array<std::byte, std::size_t{64} * 1024> buffer_{};
    udp::endpoint from_;
};

PortValue Socket::answer(PortValue reply_value) const {
    return PortValue::of_tuple(
        {PortValue::of_atom("clause_socket"), PortValue::of_identity(port_), std::move(reply_value)});
}

void Socket::reply(Word target, PortValue value) {
    service_->deliver(
        port_, SocketEvent{
                   .kind = SocketEvent::Kind::message, .target = target, .value = std::move(value), .driver = nullptr});
}

std::optional<Bytes> Socket::operate(Word caller, SocketOperation operation, std::span<const std::byte> data) {
    if (shut_) {
        return failure("closed");
    }
    if (auto answer = common_operation(caller, operation, data)) {
        return answer;
    }
    return udp_ ? datagram_operation(caller, operation, data) : stream_operation(caller, operation, data);
}

std::optional<Bytes> Socket::common_operation(Word caller, SocketOperation operation, std::span<const std::byte> data) {
    switch (operation) {
    case SocketOperation::setopts: {
        const auto previous = mode_.bytes();
        mode_.apply(data, 0);
        decoder_ = InputDecoder(mode_.framing());
        serve();
        return ok(previous);
    }
    case SocketOperation::cancel:
        return cancel(caller);
    case SocketOperation::getstat:
        return ok(counters_.bytes(pending_));
    case SocketOperation::getstatus:
        return status();
    case SocketOperation::getopt:
        return getopt(data);
    case SocketOperation::setopt:
        return setopt(data);
    default:
        return std::nullopt;
    }
}

// The address of the local end of an open socket, if it has one.
template <typename AsioSocket> std::optional<asio::ip::address> local_address(const std::optional<AsioSocket> &socket) {
    boost::system::error_code error;
    if (!socket || !socket->is_open()) {
        return std::nullopt;
    }
    const auto endpoint = socket->local_endpoint(error);
    return error ? std::nullopt : std::optional(endpoint.address());
}

Bytes Socket::status() const {
    return ok(std::array{static_cast<std::byte>(status_flags()), static_cast<std::byte>(udp_ ? 1 : 0),
                         static_cast<std::byte>(family())});
}

std::uint8_t Socket::status_flags() const {
    std::uint8_t flags = OPEN;
    if (listener_) {
        flags |= LISTEN | (accepts_.empty() ? 0 : ACCEPTING);
    }
    if (connecting_) {
        flags |= CONNECTING;
    } else if ((stream_ && stream_->is_open() && !bound_) || connected_) {
        flags |= CONNECTED;
    }
    return flags;
}

int Socket::family() const {
    auto local = local_address(datagram_);
    local = local ? local : local_address(listener_);
    local = local ? local : local_address(stream_);
    return !local ? 0 : local->is_v6() ? 6 : 4;
}

std::optional<socket_options::Handle> Socket::handle() {
    if (stream_ && stream_->is_open()) {
        return static_cast<socket_options::Handle>(stream_->native_handle());
    }
    if (listener_ && listener_->is_open()) {
        return static_cast<socket_options::Handle>(listener_->native_handle());
    }
    if (datagram_ && datagram_->is_open()) {
        return static_cast<socket_options::Handle>(datagram_->native_handle());
    }
    return std::nullopt;
}

Bytes Socket::getopt(std::span<const std::byte> data) {
    std::vector<socket_options::Record> found;
    for (const auto code : data) {
        auto record = options_.get(handle(), std::to_integer<std::uint8_t>(code));
        if (!record) {
            return failure("einval");
        }
        found.push_back(std::move(*record));
    }
    return ok(socket_options::encode(found));
}

Bytes Socket::setopt(std::span<const std::byte> data) {
    const auto records = socket_options::records(data);
    if (!records) {
        return failure("einval");
    }
    for (const auto &record : *records) {
        if (auto failed = options_.set(handle(), udp_, record); !failed.empty()) {
            return failure(failed);
        }
    }
    return ok();
}

std::string Socket::preset(std::span<const std::byte> data, std::size_t at) {
    const auto records = socket_options::records(data.subspan(std::min(at, data.size())));
    if (!records) {
        return "einval";
    }
    for (const auto &record : *records) {
        if (auto failed = options_.set(std::nullopt, udp_, record); !failed.empty()) {
            return failed;
        }
    }
    return {};
}

template <typename AsioSocket, typename Protocol>
std::string Socket::open(AsioSocket &socket, const Protocol &protocol) {
    boost::system::error_code error;
    socket.open(protocol, error);
    if (error) {
        return reason(error);
    }
    return options_.apply(static_cast<socket_options::Handle>(socket.native_handle()));
}

template <typename AsioSocket, typename Protocol>
std::string Socket::open_with(AsioSocket &socket, const Protocol &protocol, std::span<const std::byte> data,
                              std::size_t at) {
    auto failed = preset(data, at);
    return failed.empty() ? open(socket, protocol) : failed;
}

std::optional<Bytes> Socket::stream_operation(Word caller, SocketOperation operation, std::span<const std::byte> data) {
    switch (operation) {
    case SocketOperation::connect:
        return connect(caller, data);
    case SocketOperation::listen:
        return listen(data);
    case SocketOperation::accept:
        return accept(caller);
    case SocketOperation::recv:
        return recv(caller, number(data, 0, 4));
    case SocketOperation::bind:
        return bind(data);
    case SocketOperation::send:
        return send(data);
    case SocketOperation::sockname:
    case SocketOperation::peername:
        return names(operation == SocketOperation::peername);
    case SocketOperation::shutdown:
        return shutdown(data);
    default:
        return std::nullopt;
    }
}

std::optional<Bytes> Socket::datagram_operation(Word caller, SocketOperation operation,
                                                std::span<const std::byte> data) {
    switch (operation) {
    case SocketOperation::udp_open:
        return udp_open(data);
    case SocketOperation::udp_send:
        return udp_send(data);
    case SocketOperation::udp_recv:
        return recv(caller, 0);
    case SocketOperation::sockname:
    case SocketOperation::peername:
        return names(operation == SocketOperation::peername);
    case SocketOperation::udp_connect:
        return udp_connect(data);
    case SocketOperation::udp_send_now:
        return udp_send_now(data);
    default:
        return std::nullopt;
    }
}

Bytes Socket::connect(Word caller, std::span<const std::byte> data) {
    std::size_t at = 0;
    const auto target = address(data, at);
    if (!target || (stream_ && !bound_)) {
        return failure("einval");
    }
    const auto port = static_cast<unsigned short>(number(data, at, 2));
    mode_.apply(data, at + 2);
    decoder_ = InputDecoder(mode_.framing());
    const tcp::endpoint endpoint(*target, port);
    if (!bound_) {
        auto &opened = stream_.emplace(context());
        if (auto failed = open_with(opened, endpoint.protocol(), data, at + 5); !failed.empty()) {
            stream_.reset();
            return failure(failed);
        }
    }
    bound_ = false;
    auto &stream = *stream_;
    connecting_ = true;
    stream.async_connect(endpoint, [self = shared_from_this(), caller](const auto &error) {
        self->connecting_ = false;
        if (self->shut_) {
            return;
        }
        if (error) {
            self->ended_ = true;
            self->reported_ = true;
            self->reply(caller, self->answer(PortValue::of_tuple(
                                    {PortValue::of_atom("error"), PortValue::of_atom(reason(error))})));
            return;
        }
        self->reply(caller, self->answer(PortValue::of_atom("ok")));
        self->serve();
    });
    return ok();
}

// A socket bound by socket:bind/2 becomes the listener with its address and options.
void Socket::adopt_bound(tcp::acceptor &listener, boost::system::error_code &error) {
    auto &bound = *stream_;
    const auto protocol = bound.local_endpoint(error).protocol();
    const auto native = bound.release(error);
    stream_.reset();
    bound_ = false;
    if (!error) {
        listener.assign(protocol, native, error);
    }
}

Bytes Socket::listen(std::span<const std::byte> data) {
    std::size_t at = 0;
    const auto local = address(data, at);
    if (!local || listener_ || (stream_ && !bound_)) {
        return failure("einval");
    }
    const auto port = static_cast<unsigned short>(number(data, at, 2));
    const auto backlog = static_cast<int>(number(data, at + 2, 4));
    mode_.apply(data, at + 6);
    const tcp::endpoint endpoint(*local, port);
    auto &listener = listener_.emplace(context());
    boost::system::error_code error;
    if (bound_ && stream_) {
        adopt_bound(listener, error);
    } else if (auto failed = open_with(listener, endpoint.protocol(), data, at + 9); !failed.empty()) {
        listener_.reset();
        return failure(failed);
    } else {
        listener.bind(endpoint, error);
    }
    if (!error) {
        listener.listen(backlog, error);
    }
    if (error) {
        listener_.reset();
        return failure(reason(error));
    }
    return ok();
}

Bytes Socket::bind(std::span<const std::byte> data) {
    std::size_t at = 0;
    const auto local = address(data, at);
    if (!local || listener_ || stream_) {
        return failure("einval");
    }
    const tcp::endpoint endpoint(*local, static_cast<unsigned short>(number(data, at, 2)));
    mode_.apply(data, at + 2);
    auto &stream = stream_.emplace(context());
    auto failed = open_with(stream, endpoint.protocol(), data, at + 5);
    boost::system::error_code error;
    if (failed.empty()) {
        stream.bind(endpoint, error);
        failed = error ? reason(error) : std::string();
    }
    if (!failed.empty()) {
        stream_.reset();
        return failure(failed);
    }
    bound_ = true;
    return ok();
}

Bytes Socket::accept(Word caller) {
    if (!listener_) {
        return failure("einval");
    }
    accepts_.push_back(caller);
    start_accept();
    return ok();
}

void Socket::start_accept() {
    if (accepting_ || accepts_.empty() || !listener_) {
        return;
    }
    accepting_ = true;
    listener_->async_accept([self = shared_from_this()](const auto &error, tcp::socket socket) {
        self->accepted(error, std::move(socket));
    });
}

void Socket::accepted(const boost::system::error_code &error, tcp::socket socket) {
    accepting_ = false;
    if (shut_ || accepts_.empty()) {
        return;
    }
    if (error == asio::error::operation_aborted) {
        start_accept();
        return;
    }
    const auto caller = accepts_.front();
    accepts_.pop_front();
    if (error) {
        reply(caller, answer(PortValue::of_tuple({PortValue::of_atom("error"), PortValue::of_atom(reason(error))})));
    } else {
        auto connection = std::make_shared<Socket>(service_, false);
        connection->adopt(std::move(socket), mode_, options_);
        auto driver = socket_driver(connection);
        // The connection starts serving once the listening port's task gave it a port (SocketDriver::attach).
        service_->deliver(port_, SocketEvent{.kind = SocketEvent::Kind::accepted,
                                             .target = caller,
                                             .value = answer(PortValue::of_atom("ok")),
                                             .driver = std::move(driver)});
    }
    start_accept();
}

Bytes Socket::recv(Word caller, std::size_t length) {
    // As in OTP, only a passive socket receives on request.
    if (mode_.active != Active::passive) {
        return failure("einval");
    }
    recvs_.push_back(Waiting{.caller = caller, .length = length});
    serve();
    return ok();
}

Bytes Socket::send(std::span<const std::byte> data) {
    if (!stream_ || ended_) {
        return failure("closed");
    }
    auto framed_data = framed(mode_.framing(), Bytes(data.begin(), data.end()));
    if (!framed_data) {
        return failure("emsgsize");
    }
    queue_write(udp::endpoint(), std::move(*framed_data));
    return ok();
}

void Socket::queue_write(const udp::endpoint &to, Bytes bytes) {
    counters_.sent(bytes.size());
    pending_ += bytes.size();
    writes_.emplace_back(to, std::move(bytes));
    write_next();
}

void Socket::write_next() {
    if (writing_ || writes_.empty()) {
        return;
    }
    writing_ = true;
    auto &[to, bytes] = writes_.front();
    const auto done = [self = shared_from_this()](const boost::system::error_code &error, std::size_t) {
        self->writing_ = false;
        self->pending_ -= self->writes_.front().second.size();
        self->writes_.pop_front();
        if (error) {
            self->writes_.clear();
            self->pending_ = 0;
        }
        if (self->shut_ && self->writes_.empty()) {
            self->release();
            return;
        }
        self->write_next();
    };
    start_write(to, bytes, done);
}

template <typename Done> void Socket::start_write(const udp::endpoint &to, const Bytes &bytes, Done done) {
    if (datagram_ && connected_) {
        datagram_->async_send(asio::buffer(bytes), done);
    } else if (datagram_) {
        datagram_->async_send_to(asio::buffer(bytes), to, done);
    } else if (stream_) {
        asio::async_write(*stream_, asio::buffer(bytes), done);
    }
}

// The reply of an endpoint query that set `error`.
template <typename Endpoint> Bytes endpoint_reply(const Endpoint &endpoint, const boost::system::error_code &error) {
    return error ? failure(reason(error)) : ok(endpoint_bytes(endpoint));
}

Bytes Socket::datagram_names(udp::socket &datagram, bool peer) const {
    boost::system::error_code error;
    if (!peer) {
        return endpoint_reply(datagram.local_endpoint(error), error);
    }
    return connected_ ? endpoint_reply(datagram.remote_endpoint(error), error) : failure("enotconn");
}

Bytes Socket::names(bool peer) {
    if (datagram_) {
        return datagram_names(*datagram_, peer);
    }
    boost::system::error_code error;
    if (listener_) {
        return peer ? failure("enotconn") : endpoint_reply(listener_->local_endpoint(error), error);
    }
    if (!stream_) {
        return failure(udp_ ? "einval" : "enotconn");
    }
    const auto endpoint = peer ? stream_->remote_endpoint(error) : stream_->local_endpoint(error);
    return endpoint_reply(endpoint, error);
}

Bytes Socket::shutdown(std::span<const std::byte> data) {
    if (!stream_) {
        return failure("enotconn");
    }
    const auto how = number(data, 0, 1);
    boost::system::error_code error;
    stream_->shutdown(how == 0   ? tcp::socket::shutdown_receive
                      : how == 1 ? tcp::socket::shutdown_send
                                 : tcp::socket::shutdown_both,
                      error);
    return error ? failure(reason(error)) : ok();
}

Bytes Socket::cancel(Word caller) {
    std::erase_if(recvs_, [&](const auto &waiting) { return waiting.caller == caller; });
    std::erase(accepts_, caller);
    if (accepts_.empty() && accepting_ && listener_) {
        boost::system::error_code error;
        listener_->cancel(error);
    }
    // Every message the socket sent the caller before arrives before this one.
    reply(caller, answer(PortValue::of_atom("cancelled")));
    return ok();
}

Bytes Socket::udp_open(std::span<const std::byte> data) {
    std::size_t at = 0;
    const auto local = address(data, at);
    if (!local || datagram_) {
        return failure("einval");
    }
    const auto port = static_cast<unsigned short>(number(data, at, 2));
    mode_.apply(data, at + 2);
    const udp::endpoint endpoint(*local, port);
    auto &datagram = datagram_.emplace(context());
    if (auto failed = open_with(datagram, endpoint.protocol(), data, at + 5); !failed.empty()) {
        datagram_.reset();
        return failure(failed);
    }
    boost::system::error_code error;
    datagram.bind(endpoint, error);
    if (error) {
        datagram_.reset();
        return failure(reason(error));
    }
    serve();
    return ok();
}

Bytes Socket::udp_connect(std::span<const std::byte> data) {
    std::size_t at = 0;
    const auto target = address(data, at);
    if (!target || !datagram_) {
        return failure("einval");
    }
    boost::system::error_code error;
    datagram_->connect(udp::endpoint(*target, static_cast<unsigned short>(number(data, at, 2))), error);
    if (error) {
        return failure(reason(error));
    }
    connected_ = true;
    return ok();
}

Bytes Socket::udp_send(std::span<const std::byte> data) {
    std::size_t at = 2;
    const auto port = static_cast<unsigned short>(number(data, 0, 2));
    const auto target = address(data, at);
    if (!target || !datagram_) {
        return failure("einval");
    }
    Bytes bytes(data.begin() + static_cast<std::ptrdiff_t>(at), data.end());
    const udp::endpoint to(*target, port);
    // As OTP's send/2, an unconnected socket sends to the any address and port 0, which the system refuses at once.
    if (!connected_ && port == 0 && target->is_unspecified()) {
        boost::system::error_code error;
        datagram_->send_to(asio::buffer(bytes), to, 0, error);
        return error ? failure(reason(error)) : ok();
    }
    queue_write(to, std::move(bytes));
    return ok();
}

// The ancillary data records of a send; none when malformed or, as OTP on Windows, when there are any.
std::optional<std::vector<socket_options::Record>> ancillary_records(std::span<const std::byte> bytes) {
    auto records = socket_options::records(bytes);
#if defined(_WIN32)
    if (records && !records->empty()) {
        return std::nullopt;
    }
#endif
    return records;
}

// Set each record for one send: the previous values to restore, or none (with the ones set restored) when one
// cannot be set.
std::optional<std::vector<socket_options::Record>> exchange_all(socket_options::Handle handle,
                                                                const std::vector<socket_options::Record> &records) {
    std::vector<socket_options::Record> previous;
    for (const auto &record : records) {
        auto old = socket_options::exchange(handle, record);
        if (!old) {
            static_cast<void>(exchange_all(handle, previous));
            return std::nullopt;
        }
        previous.push_back(std::move(*old));
    }
    return previous;
}

Bytes Socket::udp_send_now(std::span<const std::byte> data) {
    std::size_t at = 2;
    const auto port = static_cast<unsigned short>(number(data, 0, 2));
    const auto target = address(data, at);
    const auto size = number(data, at, 2);
    const auto native = handle();
    if (!target || !datagram_ || !native || at + 2 + size > data.size()) {
        return failure("einval");
    }
    auto &datagram = *datagram_;
    const auto ancillary = ancillary_records(data.subspan(at + 2, size));
    const auto previous = ancillary ? exchange_all(*native, *ancillary) : std::nullopt;
    if (!previous) {
        return failure("einval");
    }
    const auto bytes = data.subspan(at + 2 + size);
    boost::system::error_code error;
    datagram.send_to(asio::buffer(bytes.data(), bytes.size()), udp::endpoint(*target, port), 0, error);
    static_cast<void>(exchange_all(*native, *previous));
    if (error) {
        return failure(reason(error));
    }
    counters_.sent(bytes.size());
    return ok();
}

std::optional<Bytes> Socket::take(std::size_t length) {
    if (mode_.packet != 0) {
        if (packets_.empty()) {
            return std::nullopt;
        }
        auto packet = std::move(packets_.front());
        packets_.pop_front();
        return packet;
    }
    const auto size = length == 0 ? stream_bytes_.size() : length;
    if (stream_bytes_.empty() || stream_bytes_.size() < size) {
        return std::nullopt;
    }
    Bytes taken(stream_bytes_.begin(), stream_bytes_.begin() + static_cast<std::ptrdiff_t>(size));
    stream_bytes_.erase(stream_bytes_.begin(), stream_bytes_.begin() + static_cast<std::ptrdiff_t>(size));
    return taken;
}

bool Socket::serve_stream() {
    if (!recvs_.empty()) {
        auto taken = take(recvs_.front().length);
        if (!taken) {
            return false;
        }
        const auto caller = recvs_.front().caller;
        recvs_.pop_front();
        reply(caller, answer(PortValue::of_tuple({PortValue::of_atom("ok"), data(std::move(*taken))})));
        return true;
    }
    if (mode_.active == Active::passive) {
        return false;
    }
    auto taken = take(0);
    if (!taken) {
        return false;
    }
    message(PortValue::of_tuple({PortValue::of_atom("tcp"), PortValue::of_identity(port_), data(std::move(*taken))}));
    mode_.active = mode_.active == Active::once ? Active::passive : mode_.active;
    return true;
}

bool Socket::serve_datagrams() {
    if (datagrams_.empty() || (recvs_.empty() && mode_.active == Active::passive)) {
        return false;
    }
    auto datagram = std::move(datagrams_.front());
    datagrams_.pop_front();
    const auto from = address_value(datagram.from.address());
    const auto port = PortValue::of_integer(datagram.from.port());
    if (!recvs_.empty()) {
        const auto caller = recvs_.front().caller;
        recvs_.pop_front();
        reply(caller, answer(PortValue::of_tuple({PortValue::of_atom("ok"),
                                                  PortValue::of_tuple({from, port, data(std::move(datagram.data))})})));
        return true;
    }
    message(PortValue::of_tuple(
        {PortValue::of_atom("udp"), PortValue::of_identity(port_), from, port, data(std::move(datagram.data))}));
    mode_.active = mode_.active == Active::once ? Active::passive : mode_.active;
    return true;
}

void Socket::report_end() {
    while (!recvs_.empty()) {
        const auto waiting = recvs_.front();
        recvs_.pop_front();
        reply(waiting.caller, answer(PortValue::of_tuple({PortValue::of_atom("error"), PortValue::of_atom("closed")})));
    }
    if (reported_ || mode_.active == Active::passive) {
        return;
    }
    reported_ = true;
    if (!error_.empty()) {
        message(PortValue::of_tuple(
            {PortValue::of_atom("tcp_error"), PortValue::of_identity(port_), PortValue::of_atom(error_)}));
    }
    message(PortValue::of_tuple({PortValue::of_atom("tcp_closed"), PortValue::of_identity(port_)}));
}

void Socket::serve() {
    if (shut_ || port_ == 0) {
        return;
    }
    while (udp_ ? serve_datagrams() : serve_stream()) {
    }
    // A recv the data left cannot satisfy is answered at the end too.
    if (ended_ && ((stream_bytes_.empty() && packets_.empty()) || !recvs_.empty())) {
        report_end();
    }
    read();
}

void Socket::read() {
    const bool wanted = mode_.active != Active::passive || !recvs_.empty();
    if (reading_ || ended_ || !wanted) {
        return;
    }
    if (udp_ && datagram_) {
        reading_ = true;
        datagram_->async_receive_from(
            asio::buffer(buffer_), from_,
            [self = shared_from_this()](const auto &error, std::size_t size) { self->receive_done(error, size); });
    } else if (!udp_ && stream_ && stream_->is_open()) {
        reading_ = true;
        stream_->async_read_some(
            asio::buffer(buffer_),
            [self = shared_from_this()](const auto &error, std::size_t size) { self->read_done(error, size); });
    }
}

void Socket::read_done(const boost::system::error_code &error, std::size_t size) {
    reading_ = false;
    if (shut_) {
        return;
    }
    if (error) {
        ended_ = true;
        error_ = error == asio::error::eof || error == asio::error::operation_aborted ? std::string() : reason(error);
        serve();
        return;
    }
    const auto chunk = std::span(buffer_).first(size);
    counters_.received(size);
    if (mode_.packet == 0) {
        stream_bytes_.insert(stream_bytes_.end(), chunk.begin(), chunk.end());
    } else {
        for (auto &unit : decoder_.feed(chunk)) {
            packets_.push_back(std::move(unit.bytes));
        }
    }
    serve();
}

void Socket::receive_done(const boost::system::error_code &error, std::size_t size) {
    reading_ = false;
    if (shut_ || error) {
        return;
    }
    counters_.received(size);
    datagrams_.push_back({from_, Bytes(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(size))});
    serve();
}

void Socket::close() {
    shut_ = true;
    for (const auto caller : std::exchange(accepts_, {})) {
        reply(caller, answer(PortValue::of_tuple({PortValue::of_atom("error"), PortValue::of_atom("closed")})));
    }
    for (const auto &waiting : std::exchange(recvs_, {})) {
        reply(waiting.caller, answer(PortValue::of_tuple({PortValue::of_atom("error"), PortValue::of_atom("closed")})));
    }
    if (!writing_) {
        release();
    }
}

void Socket::release() {
    boost::system::error_code error;
    if (stream_) {
        stream_->shutdown(tcp::socket::shutdown_send, error);
        stream_->close(error);
    }
    if (listener_) {
        listener_->close(error);
    }
    if (datagram_) {
        datagram_->close(error);
    }
}

// Run `work` on the I/O thread and wait for its result; the caller is a worker without the executor's lock.
template <typename Work> auto on_socket_thread(asio::io_context &context, Work work) {
    std::packaged_task<decltype(work())()> task(std::move(work));
    auto result = task.get_future();
    asio::post(context, [&task] { task(); });
    return result.get();
}

// Append the family byte (4 or 6) and the address bytes of one address name lookup found.
void append_address(Bytes &addresses, const addrinfo &entry) {
    const bool v6 = entry.ai_family == AF_INET6;
    std::array<std::byte, 16> bytes{};
    if (v6) {
        sockaddr_in6 address{};
        std::memcpy(&address, entry.ai_addr, sizeof(address));
        std::memcpy(bytes.data(), &address.sin6_addr, 16);
    } else {
        sockaddr_in address{};
        std::memcpy(&address, entry.ai_addr, sizeof(address));
        std::memcpy(bytes.data(), &address.sin_addr, 4);
    }
    addresses.push_back(static_cast<std::byte>(v6 ? 6 : 4));
    addresses.insert(addresses.end(), bytes.begin(), bytes.begin() + (v6 ? 16 : 4));
}

// resolve: the addresses of a host name of the family byte (4 or 6), each encoded as family and bytes. It runs on
// the calling worker, as name lookup blocks.
Bytes resolve(std::span<const std::byte> data) {
    if (data.empty()) {
        return failure("einval");
    }
    const auto family = std::to_integer<int>(data[0]);
    std::string host(data.size() - 1, '\0');
    std::ranges::transform(data.subspan(1), host.begin(), [](std::byte b) { return static_cast<char>(b); });
    addrinfo hints{};
    hints.ai_family = family == 6 ? AF_INET6 : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0) {
        return failure("nxdomain");
    }
    const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> entries(found, &freeaddrinfo);
    Bytes addresses;
    for (const auto *entry = entries.get(); entry != nullptr; entry = entry->ai_next) {
        if (entry->ai_family == hints.ai_family) {
            append_address(addresses, *entry);
        }
    }
    return addresses.empty() ? failure("nxdomain") : ok(addresses);
}

// A socket answer as a port_control/3 reply, whose buffer type binaries take over (small replies only, copied once).
std::optional<ByteBuffer> reply_of(std::optional<Bytes> answer) {
    return answer ? std::optional{ByteBuffer(answer->begin(), answer->end())} : std::nullopt;
}

// hostname: this machine's host name, from the socket layer (plan step 41R); runs on the calling worker.
Bytes hostname() {
    std::array<char, 256> name{};
    if (gethostname(name.data(), static_cast<int>(name.size() - 1)) != 0) {
        return failure("einval");
    }
    const std::string_view text(name.data());
    Bytes bytes(text.size());
    std::ranges::transform(text, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return ok(bytes);
}

class SocketDriver final : public PortDriver {
  public:
    explicit SocketDriver(std::shared_ptr<Socket> socket) noexcept : socket_(std::move(socket)) {}

    SocketDriver(const SocketDriver &) = delete;
    SocketDriver &operator=(const SocketDriver &) = delete;
    SocketDriver(SocketDriver &&) = delete;
    SocketDriver &operator=(SocketDriver &&) = delete;

    // The port closed: close the socket on its thread, without waiting.
    ~SocketDriver() override {
        try {
            asio::post(socket_->context(), [socket = socket_] { socket->close(); });
        } catch (...) {
            // Only exhausted memory fails posting; as in ERTS, the program cannot go on without memory.
            std::terminate();
        }
    }

    // Sockets send through port_control/3, never as plain port output.
    std::expected<void, DriverError> write(std::span<const std::byte>) override {
        return std::unexpected(DriverError{"einval"});
    }

    bool controllable() const noexcept override { return true; }

    std::optional<ByteBuffer> control(std::uint32_t operation, std::span<const std::byte> data, Word caller) override {
        const auto kind = static_cast<SocketOperation>(operation);
        if (kind == SocketOperation::resolve) {
            return reply_of(resolve(data));
        }
        if (kind == SocketOperation::hostname) {
            return reply_of(hostname());
        }
        Bytes copy(data.begin(), data.end());
        return reply_of(on_socket_thread(socket_->context(), [socket = socket_, caller, kind, copy] {
            return socket->operate(caller, kind, copy);
        }));
    }

    // Learn the port's word; an accepted connection then starts serving on the I/O thread.
    void attach(Word port) noexcept override {
        socket_->attach(port);
        if (!socket_->adopted()) {
            return;
        }
        try {
            asio::post(socket_->context(), [socket = socket_] { socket->start(); });
        } catch (...) {
            // Only exhausted memory fails posting; as in ERTS, the program cannot go on without memory.
            std::terminate();
        }
    }

  private:
    std::shared_ptr<Socket> socket_;
};

std::shared_ptr<PortDriver> socket_driver(const std::shared_ptr<Socket> &socket) {
    return std::make_shared<SocketDriver>(socket);
}
} // namespace

SocketService::SocketService(Reactor &reactor, Deliver deliver)
    : impl_(std::make_shared<Impl>(reactor.context(), std::move(deliver))) {}

SocketService::~SocketService() { impl_->close(); }

std::unique_ptr<PortDriver> SocketService::driver(bool udp) {
    return std::make_unique<SocketDriver>(std::make_shared<Socket>(impl_, udp));
}
} // namespace clause::runtime::detail
