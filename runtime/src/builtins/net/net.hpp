#pragma once
#include "../support.hpp"
#include "../typed.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The clause_net builtins (plan 21 step LIB60, docs/builtins.md#network-names): name and service lookups and the
// interface lists of inet:getifaddrs/0 and net:getifaddrs/0, gathered per operating system as OTP's inet_drv and
// prim_net do and shaped here.
namespace clause::runtime::builtins::net {
// An address: family 4 or 6 and its bytes in network order, with the IPv6 scope.
struct IpAddress final {
    int family = 4;
    std::vector<std::uint8_t> bytes;
    std::uint32_t scope_id = 0;
};

// One option of an interface of inet:getifaddrs/0: addr, netmask, broadaddr or dstaddr with an address, or hwaddr
// with the hardware address bytes.
struct InetOption final {
    std::string_view name;
    IpAddress address;
    std::vector<std::uint8_t> hardware;
};

// One record of the system's interface list for inet:getifaddrs/0: its name, inet_drv's flags and options. Records
// of one name are merged by flags, as OTP's prim_inet does.
struct InetRecord final {
    std::string name;
    std::vector<std::string_view> flags;
    std::vector<InetOption> options;
};

// A socket address of net:getifaddrs/0: an IP address, a packet (Linux) or link (macOS) address with its fields
// as integers or atom names, or raw bytes (Windows' broadcast hardware address).
struct NetAddress final {
    enum class Kind : std::uint8_t { ip, packet, link, bytes };
    Kind kind = Kind::ip;
    IpAddress ip;
    // packet: protocol, ifindex, hatype, pkttype; link: index, type, nlen, alen, slen. Atom names when not empty.
    std::vector<std::pair<std::string_view, std::int64_t>> fields;
    std::vector<std::pair<std::string_view, std::string_view>> names;
    std::vector<std::uint8_t> data;
};

// One entry of net:getifaddrs/0.
struct NetEntry final {
    std::string name;
    std::vector<std::string_view> flags;
    std::optional<NetAddress> addr;
    std::optional<NetAddress> netmask;
    std::optional<NetAddress> broadaddr;
    std::optional<NetAddress> dstaddr;
};

// The system's interfaces in inet_drv's and prim_net's terms; empty with an error reason when they cannot be read.
std::vector<InetRecord> inet_records(std::string &error);
std::vector<NetEntry> net_entries(std::string &error);
// The system's interface indexes and names, by index.
std::vector<std::pair<std::uint32_t, std::string>> interface_names();

// clause_net:inet_getifaddrs/0, getifaddrs/0 and if_names/0 (interfaces.cpp).
Term inet_getifaddrs(ProcessContext &context);
Term getifaddrs(ProcessContext &context);
Term if_names(ProcessContext &context);

// A UTF-8 string as Erlang's list of characters.
Term chars(ProcessContext &context, std::string_view text);
// An address as Erlang's tuple of four bytes or eight 16-bit fields.
Term address_tuple(ProcessContext &context, const IpAddress &address);
// socket:sockaddr() of an address: #{family, addr, port} and, for IPv6, flowinfo and scope_id.
Term sockaddr_map(ProcessContext &context, const IpAddress &address, std::uint16_t port);
// {ok, Value} and {error, Reason}.
Term ok(ProcessContext &context, const Term &value);
Term error(ProcessContext &context, std::string_view reason);
} // namespace clause::runtime::builtins::net
