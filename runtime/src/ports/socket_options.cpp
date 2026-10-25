#include "socket_options.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

// Socket options and counters (docs/ports.md#sockets), created for inet:getopts/2 and inet:getstat/1,2. Operating
// system options go to setsockopt/getsockopt of the socket's handle; the options OTP's inet_drv keeps itself are
// stored with inet_drv's defaults.
namespace clause::runtime::detail::socket_options {
namespace {
#ifdef _WIN32
using OptionLength = int;
using Native = SOCKET;
#else
using OptionLength = socklen_t;
using Native = int;
#endif

// How an option's value is encoded: one 8-byte integer, linger's {OnOff, Seconds}, an address (family byte and its
// bytes), or a multicast membership (group and interface addresses).
enum class Kind : std::uint8_t { integer, linger, address, membership };

// One option: its code, encoding, the setsockopt level and name (level -1: kept by the driver), its default when
// kept, and whether a set is ignored (priority where the system has none, as OTP).
struct Option final {
    std::uint8_t code;
    Kind kind;
    int level;
    int name;
    std::int64_t initial;
    bool ignored = false;
};

constexpr std::uint8_t BUFFER = 32;
constexpr std::uint8_t RECBUF = 7;
constexpr int KEPT = -1;
constexpr std::int64_t DEFAULT_BUFFER = 9216;
// UDP's largest datagram, the most a recbuf raises the buffer of a UDP socket to.
constexpr std::int64_t UDP_MAX_BUFFER = 65536;

#ifdef SO_EXCLUSIVEADDRUSE
constexpr Option EXCLUSIVEADDRUSE{17, Kind::integer, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 0};
#else
constexpr Option EXCLUSIVEADDRUSE{17, Kind::integer, KEPT, 0, 0};
#endif
#ifdef SO_PRIORITY
constexpr Option PRIORITY{18, Kind::integer, SOL_SOCKET, SO_PRIORITY, 0};
#else
constexpr Option PRIORITY{18, Kind::integer, KEPT, 0, 0, true};
#endif
#if defined(SO_REUSEPORT) && !defined(_WIN32)
constexpr Option REUSEPORT{19, Kind::integer, SOL_SOCKET, SO_REUSEPORT, 0};
#else
constexpr Option REUSEPORT{19, Kind::integer, KEPT, 0, 0};
#endif

// Every option, by code (codes match clause_socket.erl).
constexpr std::array OPTIONS{
    Option{1, Kind::integer, SOL_SOCKET, SO_REUSEADDR, 0},
    Option{2, Kind::integer, SOL_SOCKET, SO_KEEPALIVE, 0},
    Option{3, Kind::integer, SOL_SOCKET, SO_DONTROUTE, 0},
    Option{4, Kind::linger, SOL_SOCKET, SO_LINGER, 0},
    Option{5, Kind::integer, SOL_SOCKET, SO_BROADCAST, 0},
    Option{6, Kind::integer, SOL_SOCKET, SO_SNDBUF, 0},
    Option{RECBUF, Kind::integer, SOL_SOCKET, SO_RCVBUF, 0},
    Option{8, Kind::integer, IPPROTO_TCP, TCP_NODELAY, 0},
    Option{9, Kind::integer, IPPROTO_IP, IP_TOS, 0},
    Option{10, Kind::integer, IPPROTO_IPV6, IPV6_TCLASS, 0},
    Option{11, Kind::integer, IPPROTO_IPV6, IPV6_V6ONLY, 0},
    Option{12, Kind::integer, IPPROTO_IP, IP_MULTICAST_TTL, 0},
    Option{13, Kind::integer, IPPROTO_IP, IP_MULTICAST_LOOP, 0},
    Option{14, Kind::address, IPPROTO_IP, IP_MULTICAST_IF, 0},
    Option{15, Kind::membership, IPPROTO_IP, IP_ADD_MEMBERSHIP, 0},
    Option{16, Kind::membership, IPPROTO_IP, IP_DROP_MEMBERSHIP, 0},
    // ttl: only as ancillary data of a send.
    Option{22, Kind::integer, IPPROTO_IP, IP_TTL, 0},
    EXCLUSIVEADDRUSE,
    PRIORITY,
    REUSEPORT,
    // reuseport_lb, nopush.
    Option{20, Kind::integer, KEPT, 0, 0},
    Option{21, Kind::integer, KEPT, 0, 0},
    // buffer, header, deliver (1 term, 0 port), exit_on_close, send_timeout (-1 infinity), send_timeout_close,
    // delay_send, high_watermark, low_watermark, high_msgq_watermark, low_msgq_watermark, packet_size,
    // show_econnreset, read_packets, recvtos, recvttl, recvtclass, debug, line_delimiter.
    Option{BUFFER, Kind::integer, KEPT, 0, DEFAULT_BUFFER},
    Option{33, Kind::integer, KEPT, 0, 0},
    Option{34, Kind::integer, KEPT, 0, 1},
    Option{35, Kind::integer, KEPT, 0, 1},
    Option{36, Kind::integer, KEPT, 0, -1},
    Option{37, Kind::integer, KEPT, 0, 0},
    Option{38, Kind::integer, KEPT, 0, 0},
    Option{39, Kind::integer, KEPT, 0, 8192},
    Option{40, Kind::integer, KEPT, 0, 4096},
    Option{41, Kind::integer, KEPT, 0, 8192},
    Option{42, Kind::integer, KEPT, 0, 4096},
    Option{43, Kind::integer, KEPT, 0, 0},
    Option{44, Kind::integer, KEPT, 0, 0},
    Option{45, Kind::integer, KEPT, 0, 5},
    Option{46, Kind::integer, KEPT, 0, 0},
    Option{47, Kind::integer, KEPT, 0, 0},
    Option{48, Kind::integer, KEPT, 0, 0},
    Option{49, Kind::integer, KEPT, 0, 0},
    Option{50, Kind::integer, KEPT, 0, '\n'},
};

const Option *find(std::uint8_t code) {
    const auto found = std::ranges::find(OPTIONS, code, &Option::code);
    return found == OPTIONS.end() ? nullptr : &*found;
}

// A big-endian signed integer of `size` bytes at `at`.
std::int64_t integer(std::span<const std::byte> bytes, std::size_t at = 0, std::size_t size = 8) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size && at + index < bytes.size(); ++index) {
        value = (value << 8) | std::to_integer<std::uint64_t>(bytes[at + index]);
    }
    return static_cast<std::int64_t>(value);
}

void append(Bytes &bytes, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xff));
    }
}

// The IPv4 address of an address value at `at` (family byte 4 and four bytes), in network order.
std::optional<in_addr> ipv4(std::span<const std::byte> value, std::size_t at = 0) {
    if (value.size() < at + 5 || std::to_integer<int>(value[at]) != 4) {
        return std::nullopt;
    }
    in_addr address{};
    std::memcpy(&address, value.data() + at + 1, 4);
    return address;
}

// The reason of the last failed socket call.
std::string last_error() {
#ifdef _WIN32
    return WSAGetLastError() == WSAENOPROTOOPT ? "enoprotoopt" : "einval";
#else
    return errno == ENOPROTOOPT ? "enoprotoopt" : "einval";
#endif
}

std::string set_raw(Handle handle, const Option &option, const void *value, OptionLength size) {
    const auto status =
        ::setsockopt(static_cast<Native>(handle), option.level, option.name, static_cast<const char *>(value), size);
    return status == 0 ? std::string() : last_error();
}

// Set an operating system option on an open handle.
std::string set_system(Handle handle, const Option &option, std::span<const std::byte> value) {
    switch (option.kind) {
    case Kind::linger: {
        linger data{};
        data.l_onoff = static_cast<decltype(data.l_onoff)>(integer(value) != 0);
        data.l_linger = static_cast<decltype(data.l_linger)>(integer(value, 8));
        return set_raw(handle, option, &data, sizeof(data));
    }
    case Kind::address: {
        const auto address = ipv4(value);
        return address ? set_raw(handle, option, &*address, sizeof(*address)) : std::string("einval");
    }
    case Kind::membership: {
        const auto group = ipv4(value);
        const auto local = ipv4(value, 5);
        if (!group || !local) {
            return "einval";
        }
        ip_mreq request{};
        request.imr_multiaddr = *group;
        request.imr_interface = *local;
        return set_raw(handle, option, &request, sizeof(request));
    }
    case Kind::integer:
        break;
    }
    const auto number = static_cast<int>(integer(value));
    return set_raw(handle, option, &number, sizeof(number));
}

// The value bytes of a SO_LINGER reading.
Bytes linger_value(const std::array<char, 16> &raw) {
    linger data{};
    std::memcpy(&data, raw.data(), sizeof(data));
    Bytes value;
    append(value, data.l_onoff != 0 ? 1 : 0);
    append(value, static_cast<std::uint64_t>(data.l_linger));
    return value;
}

// The value bytes of an IPv4 address reading.
Bytes address_value(const std::array<char, 16> &raw) {
    Bytes value{std::byte{4}};
    std::transform(raw.begin(), raw.begin() + 4, std::back_inserter(value),
                   [](char c) { return static_cast<std::byte>(c); });
    return value;
}

// The value bytes of an integer reading of `size` bytes: some options (IP_MULTICAST_TTL and IP_MULTICAST_LOOP on
// Linux) answer a single byte.
Bytes integer_value(const std::array<char, 16> &raw, OptionLength size) {
    int number = 0;
    if (size == 1) {
        number = static_cast<unsigned char>(raw[0]);
    } else {
        std::memcpy(&number, raw.data(), sizeof(number));
    }
    Bytes value;
    append(value, static_cast<std::uint64_t>(static_cast<std::int64_t>(number)));
    return value;
}

// Read an operating system option of an open handle as its value bytes.
std::optional<Bytes> get_system(Handle handle, const Option &option) {
    std::array<char, 16> raw{};
    // The option's own size: Windows refuses a larger buffer for some options (IP_MULTICAST_IF of a TCP socket).
    const auto exact = option.kind == Kind::linger ? sizeof(linger) : option.kind == Kind::address ? 4 : sizeof(int);
    auto size = static_cast<OptionLength>(exact);
    if (::getsockopt(static_cast<Native>(handle), option.level, option.name, raw.data(), &size) != 0) {
        // As OTP, a socket without a multicast interface (TCP on Windows) reads the any address.
        if (option.kind != Kind::address) {
            return std::nullopt;
        }
        raw.fill(0);
    }
    return option.kind == Kind::linger    ? linger_value(raw)
           : option.kind == Kind::address ? address_value(raw)
                                          : integer_value(raw, size);
}
} // namespace

std::optional<std::vector<Record>> records(std::span<const std::byte> bytes) {
    std::vector<Record> result;
    for (std::size_t at = 0; at < bytes.size();) {
        if (at + 2 > bytes.size()) {
            return std::nullopt;
        }
        const auto code = std::to_integer<std::uint8_t>(bytes[at]);
        const auto size = std::to_integer<std::size_t>(bytes[at + 1]);
        if (at + 2 + size > bytes.size()) {
            return std::nullopt;
        }
        const auto value = bytes.subspan(at + 2, size);
        result.push_back({code, Bytes(value.begin(), value.end())});
        at += 2 + size;
    }
    return result;
}

Bytes encode(const std::vector<Record> &records) {
    Bytes bytes;
    for (const auto &record : records) {
        bytes.push_back(static_cast<std::byte>(record.code));
        bytes.push_back(static_cast<std::byte>(record.value.size()));
        bytes.insert(bytes.end(), record.value.begin(), record.value.end());
    }
    return bytes;
}

std::string Options::set(std::optional<Handle> handle, bool udp, const Record &record) {
    const auto *option = find(record.code);
    if (option == nullptr) {
        return "einval";
    }
    if (option->level == KEPT) {
        keep(record, option->ignored);
        return {};
    }
    if (handle) {
        if (auto failed = set_system(*handle, *option, record.value); !failed.empty()) {
            return failed;
        }
    }
    if (option->kind != Kind::membership) {
        std::erase_if(system_, [&](const Record &earlier) { return earlier.code == record.code; });
        system_.push_back(record);
    }
    if (record.code == RECBUF) {
        raise_buffer(integer(record.value), udp);
    }
    return {};
}

void Options::keep(const Record &record, bool ignored) {
    if (!ignored) {
        stored_[record.code] = integer(record.value);
    }
    buffer_set_ = buffer_set_ || record.code == BUFFER;
}

void Options::raise_buffer(std::int64_t size, bool udp) {
    if (buffer_set_) {
        return;
    }
    const auto current = stored_.contains(BUFFER) ? stored_[BUFFER] : DEFAULT_BUFFER;
    stored_[BUFFER] = udp && size > UDP_MAX_BUFFER ? UDP_MAX_BUFFER : std::max(size, current);
}

std::string Options::apply(Handle handle) {
    for (const auto &record : system_) {
        if (auto failed = set_system(handle, *find(record.code), record.value); !failed.empty()) {
            return failed;
        }
    }
    return {};
}

std::optional<Record> Options::get(std::optional<Handle> handle, std::uint8_t code) const {
    const auto *option = find(code);
    if (option == nullptr || option->kind == Kind::membership) {
        return std::nullopt;
    }
    if (option->level != KEPT) {
        auto value = handle ? get_system(*handle, *option) : std::nullopt;
        return value ? std::optional<Record>(Record{code, std::move(*value)}) : std::nullopt;
    }
    const auto found = stored_.find(code);
    Bytes value;
    append(value, static_cast<std::uint64_t>(found == stored_.end() ? option->initial : found->second));
    return Record{code, std::move(value)};
}

std::optional<Record> exchange(Handle handle, const Record &record) {
    const auto *option = find(record.code);
    if (option == nullptr || option->level == KEPT || option->kind != Kind::integer) {
        return std::nullopt;
    }
    auto previous = get_system(handle, *option);
    if (!previous || !set_system(handle, *option, record.value).empty()) {
        return std::nullopt;
    }
    return Record{record.code, std::move(*previous)};
}

void Counters::received(std::size_t size) noexcept {
    const auto length = static_cast<double>(size);
    recv_oct_ += size;
    ++recv_cnt_;
    recv_max_ = std::max<std::uint64_t>(recv_max_, size);
    recv_avg_ += (length - recv_avg_) / static_cast<double>(recv_cnt_);
    recv_dvi_ += ((length - recv_avg_) - recv_dvi_) / static_cast<double>(recv_cnt_);
}

void Counters::sent(std::size_t size) noexcept {
    send_oct_ += size;
    ++send_cnt_;
    send_max_ = std::max<std::uint64_t>(send_max_, size);
    send_avg_ += (static_cast<double>(size) - send_avg_) / static_cast<double>(send_cnt_);
}

Bytes Counters::bytes(std::uint64_t pending) const {
    // As inet_drv, counts and averages are 32-bit unsigned values; the deviation is its magnitude.
    const auto word = [](double value) { return static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)); };
    Bytes result;
    for (const auto value :
         {recv_oct_, recv_cnt_ & 0xffffffff, recv_max_ & 0xffffffff, word(recv_avg_), word(std::abs(recv_dvi_)),
          send_oct_, send_cnt_ & 0xffffffff, send_max_ & 0xffffffff, word(send_avg_), pending & 0xffffffff}) {
        append(result, value);
    }
    return result;
}
} // namespace clause::runtime::detail::socket_options
