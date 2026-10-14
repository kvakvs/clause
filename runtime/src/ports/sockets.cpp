// Synchronous Asio operations that take an error_code return nothing (BOOST_ASIO_NO_DEPRECATED, set by CMake, removes
// the deprecated overloads that return it too).
#include "sockets.hpp"
#include "io.hpp"
#include <array>
#include <boost/asio.hpp>
#include <cstring>
#include <deque>
#include <exception>
#include <future>
#include <mutex>
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
};

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
    static const std::array<std::pair<std::errc, std::string_view>, 13> NAMES{{
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

    // Adopt an accepted connection with the listening socket's mode; it serves once its port exists.
    void adopt(tcp::socket socket, const Mode &mode) {
        stream_.emplace(std::move(socket));
        mode_ = mode;
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
    std::optional<Bytes> stream_operation(Word caller, SocketOperation operation, std::span<const std::byte> data);
    std::optional<Bytes> datagram_operation(Word caller, SocketOperation operation, std::span<const std::byte> data);
    Bytes connect(Word caller, std::span<const std::byte> data);
    Bytes listen(std::span<const std::byte> data);
    Bytes accept(Word caller);
    Bytes recv(Word caller, std::size_t length);
    Bytes send(std::span<const std::byte> data);
    Bytes names(bool peer);
    Bytes shutdown(std::span<const std::byte> data);
    Bytes cancel(Word caller);
    Bytes udp_open(std::span<const std::byte> data);
    Bytes udp_send(std::span<const std::byte> data);

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
    std::deque<std::pair<Word, std::size_t>> recvs_;
    std::deque<Word> accepts_;
    // Queued TCP output, or UDP datagrams with their destinations.
    std::deque<std::pair<udp::endpoint, Bytes>> writes_;
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
    if (operation == SocketOperation::setopts) {
        const auto previous = mode_.bytes();
        mode_.apply(data, 0);
        decoder_ = InputDecoder(mode_.framing());
        serve();
        return ok(previous);
    }
    if (operation == SocketOperation::cancel) {
        return cancel(caller);
    }
    return udp_ ? datagram_operation(caller, operation, data) : stream_operation(caller, operation, data);
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
        return names(false);
    default:
        return std::nullopt;
    }
}

Bytes Socket::connect(Word caller, std::span<const std::byte> data) {
    std::size_t at = 0;
    const auto target = address(data, at);
    if (!target || stream_) {
        return failure("einval");
    }
    const auto port = static_cast<unsigned short>(number(data, at, 2));
    mode_.apply(data, at + 2);
    decoder_ = InputDecoder(mode_.framing());
    stream_.emplace(context());
    stream_->async_connect(tcp::endpoint(*target, port), [self = shared_from_this(), caller](const auto &error) {
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

Bytes Socket::listen(std::span<const std::byte> data) {
    std::size_t at = 0;
    const auto local = address(data, at);
    if (!local || listener_ || stream_) {
        return failure("einval");
    }
    const auto port = static_cast<unsigned short>(number(data, at, 2));
    const auto backlog = static_cast<int>(number(data, at + 2, 4));
    const bool reuse = number(data, at + 6, 1) != 0;
    mode_.apply(data, at + 7);
    const tcp::endpoint endpoint(*local, port);
    boost::system::error_code error;
    listener_.emplace(context());
    listener_->open(endpoint.protocol(), error);
    if (!error && reuse) {
        listener_->set_option(tcp::acceptor::reuse_address(true), error);
    }
    if (!error) {
        listener_->bind(endpoint, error);
    }
    if (!error) {
        listener_->listen(backlog, error);
    }
    if (error) {
        listener_.reset();
        return failure(reason(error));
    }
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
        connection->adopt(std::move(socket), mode_);
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
    recvs_.emplace_back(caller, length);
    serve();
    return ok();
}

Bytes Socket::send(std::span<const std::byte> data) {
    if (!stream_ || ended_) {
        return failure("closed");
    }
    const auto framed_data = framed(mode_.framing(), Bytes(data.begin(), data.end()));
    if (!framed_data) {
        return failure("emsgsize");
    }
    writes_.emplace_back(udp::endpoint(), *framed_data);
    write_next();
    return ok();
}

void Socket::write_next() {
    if (writing_ || writes_.empty()) {
        return;
    }
    writing_ = true;
    auto &[to, bytes] = writes_.front();
    const auto done = [self = shared_from_this()](const boost::system::error_code &error, std::size_t) {
        self->writing_ = false;
        self->writes_.pop_front();
        if (error) {
            self->writes_.clear();
        }
        if (self->shut_ && self->writes_.empty()) {
            self->release();
            return;
        }
        self->write_next();
    };
    if (datagram_) {
        datagram_->async_send_to(asio::buffer(bytes), to, done);
    } else if (stream_) {
        asio::async_write(*stream_, asio::buffer(bytes), done);
    }
}

// The reply of an endpoint query that set `error`.
template <typename Endpoint> Bytes endpoint_reply(const Endpoint &endpoint, const boost::system::error_code &error) {
    return error ? failure(reason(error)) : ok(endpoint_bytes(endpoint));
}

Bytes Socket::names(bool peer) {
    boost::system::error_code error;
    if (datagram_) {
        return endpoint_reply(datagram_->local_endpoint(error), error);
    }
    if (listener_) {
        return endpoint_reply(listener_->local_endpoint(error), error);
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
    std::erase_if(recvs_, [&](const auto &waiting) { return waiting.first == caller; });
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
    boost::system::error_code error;
    datagram_.emplace(context());
    datagram_->open(endpoint.protocol(), error);
    if (!error) {
        datagram_->bind(endpoint, error);
    }
    if (error) {
        datagram_.reset();
        return failure(reason(error));
    }
    serve();
    return ok();
}

Bytes Socket::udp_send(std::span<const std::byte> data) {
    std::size_t at = 2;
    const auto port = static_cast<unsigned short>(number(data, 0, 2));
    const auto target = address(data, at);
    if (!target || !datagram_) {
        return failure("einval");
    }
    writes_.emplace_back(udp::endpoint(*target, port),
                         Bytes(data.begin() + static_cast<std::ptrdiff_t>(at), data.end()));
    write_next();
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
        auto taken = take(recvs_.front().second);
        if (!taken) {
            return false;
        }
        const auto caller = recvs_.front().first;
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
        const auto caller = recvs_.front().first;
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
        reply(recvs_.front().first,
              answer(PortValue::of_tuple({PortValue::of_atom("error"), PortValue::of_atom("closed")})));
        recvs_.pop_front();
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
    if (ended_ && stream_bytes_.empty() && packets_.empty()) {
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
    datagrams_.push_back({from_, Bytes(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(size))});
    serve();
}

void Socket::close() {
    shut_ = true;
    for (const auto caller : std::exchange(accepts_, {})) {
        reply(caller, answer(PortValue::of_tuple({PortValue::of_atom("error"), PortValue::of_atom("closed")})));
    }
    for (const auto &[caller, length] : std::exchange(recvs_, {})) {
        reply(caller, answer(PortValue::of_tuple({PortValue::of_atom("error"), PortValue::of_atom("closed")})));
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

    std::optional<Bytes> control(std::uint32_t operation, std::span<const std::byte> data, Word caller) override {
        const auto kind = static_cast<SocketOperation>(operation);
        if (kind == SocketOperation::resolve) {
            return resolve(data);
        }
        if (kind == SocketOperation::hostname) {
            return hostname();
        }
        Bytes copy(data.begin(), data.end());
        return on_socket_thread(socket_->context(),
                                [socket = socket_, caller, kind, copy] { return socket->operate(caller, kind, copy); });
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
