// Synchronous Asio operations that take an error_code return nothing (BOOST_ASIO_NO_DEPRECATED, set by CMake).
#include "client.hpp"
#include <array>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <cstdio>
#include <optional>

namespace clause::runtime::epmd {
namespace {
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// The request codes the client sends.
constexpr char NAMES_REQ = 110;
constexpr char KILL_REQ = 107;

// A connection to the epmd on the loopback, IPv6 first as OTP's epmd tries; none after OTP's message.
std::optional<tcp::socket> connect_local(asio::io_context &io, std::uint16_t port) {
    for (const asio::ip::address &address :
         {asio::ip::address(asio::ip::address_v6::loopback()), asio::ip::address(asio::ip::address_v4::loopback())}) {
        tcp::socket socket(io);
        boost::system::error_code error;
        socket.connect(tcp::endpoint(address, port), error);
        if (!error) {
            return socket;
        }
    }
    std::fputs("epmd: Cannot connect to local epmd\n", stderr);
    return std::nullopt;
}

// Send a one-byte request; false after OTP's message when it cannot be written.
bool send_request(tcp::socket &socket, char code) {
    const std::array<char, 3> request{0, 1, code};
    boost::system::error_code error;
    asio::write(socket, asio::buffer(request), error);
    if (error) {
        std::fputs("epmd: Can't write to epmd\n", stdout);
        return false;
    }
    return true;
}

// Copy what the epmd sends until it closes the connection.
void copy_to_stdout(tcp::socket &socket) {
    std::array<char, 1024> buffer{};
    for (;;) {
        boost::system::error_code error;
        const auto count = socket.read_some(asio::buffer(buffer), error);
        if (error) {
            return;
        }
        std::fwrite(buffer.data(), 1, count, stdout);
    }
}
} // namespace

int print_names(std::uint16_t port) {
    asio::io_context io(1);
    auto socket = connect_local(io, port);
    if (!socket || !send_request(*socket, NAMES_REQ)) {
        return 1;
    }
    std::array<unsigned char, 4> head{};
    boost::system::error_code error;
    asio::read(*socket, asio::buffer(head), error);
    if (error) {
        std::fputs("epmd: no response from local epmd\n", stdout);
        return 1;
    }
    const auto epmd_port = (unsigned{head[0]} << 24) | (unsigned{head[1]} << 16) | (unsigned{head[2]} << 8) | head[3];
    std::printf("epmd: up and running on port %u with data:\n", epmd_port);
    copy_to_stdout(*socket);
    return 0;
}

int kill_epmd(std::uint16_t port) {
    asio::io_context io(1);
    auto socket = connect_local(io, port);
    if (!socket || !send_request(*socket, KILL_REQ)) {
        return 1;
    }
    std::array<char, 2> answer{};
    boost::system::error_code error;
    const auto count = asio::read(*socket, asio::buffer(answer), error);
    if (count == answer.size()) {
        const bool killed = answer[0] == 'O' && answer[1] == 'K';
        std::fputs(killed ? "Killed\n" : "Killing not allowed - living nodes in database.\n", stdout);
        return killed ? 0 : 1;
    }
    if (error != asio::error::eof) {
        std::fputs("epmd: failed to read answer from local epmd\n", stdout);
        return 1;
    }
    std::printf("epmd: local epmd responded with <%.*s>\n", static_cast<int>(count), answer.data());
    return 1;
}
} // namespace clause::runtime::epmd
