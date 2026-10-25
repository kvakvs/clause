#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The socket options of inet:getopts/2 and setopts/2 beyond the data mode, and the statistics of inet:getstat/1,2
// (docs/ports.md#sockets). Created for inet:getopts/2 and inet:getstat/1,2.
namespace clause::runtime::detail::socket_options {
using Bytes = std::vector<std::byte>;

// A native socket handle (SOCKET on Windows, a descriptor elsewhere).
using Handle = std::uintptr_t;

// One option of a getopt/setopt request: <<Code:8, Size:8, Value:Size/binary>>; codes match clause_socket.erl.
struct Record final {
    std::uint8_t code = 0;
    Bytes value;
};

// The records of `bytes`, or none when they are malformed.
std::optional<std::vector<Record>> records(std::span<const std::byte> bytes);

// Records as request or reply bytes.
Bytes encode(const std::vector<Record> &records);

// The options a socket keeps beyond the operating system's: their values, and the operating system options set so
// far, which an accepted connection takes over from its listening socket.
class Options final {
  public:
    // Set one option on `handle` (none when the socket is not open yet); an error reason, or empty.
    std::string set(std::optional<Handle> handle, bool udp, const Record &record);
    // Apply every option set so far to a newly opened `handle`; an error reason, or empty.
    std::string apply(Handle handle);
    // The value of option `code`, read from `handle` for an operating system option; none when unknown.
    std::optional<Record> get(std::optional<Handle> handle, std::uint8_t code) const;

  private:
    // Keep the value of an option the driver keeps itself (none for one whose set is ignored).
    void keep(const Record &record, bool ignored);
    // As inet_drv, a receive buffer raises the user-level buffer unless that was set (UDP: at most a datagram).
    void raise_buffer(std::int64_t size, bool udp);

    // Values of the options the driver keeps itself, by code.
    std::map<std::uint8_t, std::int64_t> stored_;
    // Operating system options set so far, in order.
    std::vector<Record> system_;
    // The buffer option was set explicitly, so receive buffers no longer raise it.
    bool buffer_set_ = false;
};

// Set an operating system option of `handle` for one send only, without keeping it (gen_udp:send/5's ancillary
// data): the previous value to restore it with, or none when it cannot be set.
std::optional<Record> exchange(Handle handle, const Record &record);

// A socket's traffic counters, updated as inet_drv does (running averages over packets).
class Counters final {
  public:
    // Count `size` bytes received as one packet.
    void received(std::size_t size) noexcept;
    // Count `size` bytes sent as one packet.
    void sent(std::size_t size) noexcept;
    // recv_oct, recv_cnt, recv_max, recv_avg, recv_dvi, send_oct, send_cnt, send_max, send_avg and `pending`, each
    // as 8 bytes.
    Bytes bytes(std::uint64_t pending) const;

  private:
    std::uint64_t recv_oct_ = 0;
    std::uint64_t recv_cnt_ = 0;
    std::uint64_t recv_max_ = 0;
    double recv_avg_ = 0;
    double recv_dvi_ = 0;
    std::uint64_t send_oct_ = 0;
    std::uint64_t send_cnt_ = 0;
    std::uint64_t send_max_ = 0;
    double send_avg_ = 0;
};
} // namespace clause::runtime::detail::socket_options
