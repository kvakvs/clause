#ifndef _WIN32
#include "net.hpp"
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <ifaddrs.h>
#include <memory>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#ifdef __linux__
#include <linux/if_packet.h>
#include <net/if_arp.h>
#else
#include <net/if_dl.h>
#include <net/if_types.h>
#endif

// POSIX interfaces for clause_net (plan 21 step LIB60): getifaddrs(3) read as OTP's inet_drv does for
// inet:getifaddrs/0 and as OTP's prim_net does for net:getifaddrs/0; if_nameindex(3) for the names.
namespace clause::runtime::builtins::net {
namespace {
using Interfaces = std::unique_ptr<ifaddrs, decltype(&freeifaddrs)>;

// The system's interface list; none with `error` set when it cannot be read.
Interfaces interface_list(std::string &error) {
    ifaddrs *first = nullptr;
    if (::getifaddrs(&first) != 0) {
        error = errno == ENOMEM ? "enomem" : "einval";
        return {nullptr, &freeifaddrs};
    }
    return {first, &freeifaddrs};
}

bool is_ip(const sockaddr *address) {
    return address != nullptr && (address->sa_family == AF_INET || address->sa_family == AF_INET6);
}

IpAddress ip_of(const sockaddr *address) {
    IpAddress ip;
    if (address->sa_family == AF_INET6) {
        sockaddr_in6 v6{};
        std::memcpy(&v6, address, sizeof(v6));
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&v6.sin6_addr);
        ip.family = 6;
        ip.bytes.assign(bytes, bytes + 16);
        ip.scope_id = v6.sin6_scope_id;
    } else {
        sockaddr_in v4{};
        std::memcpy(&v4, address, sizeof(v4));
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&v4.sin_addr);
        ip.bytes.assign(bytes, bytes + 4);
    }
    return ip;
}

// The hardware address of a packet (Linux) or link (BSD) address, if `address` is one.
std::optional<std::vector<std::uint8_t>> hardware_of(const sockaddr *address) {
    if (address == nullptr) {
        return std::nullopt;
    }
#ifdef __linux__
    if (address->sa_family == AF_PACKET) {
        sockaddr_ll link{};
        std::memcpy(&link, address, sizeof(link));
        return std::vector<std::uint8_t>(link.sll_addr, link.sll_addr + link.sll_halen);
    }
#else
    if (address->sa_family == AF_LINK) {
        const auto *link = reinterpret_cast<const sockaddr_dl *>(address);
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(LLADDR(link));
        return std::vector<std::uint8_t>(bytes, bytes + link->sdl_alen);
    }
#endif
    return std::nullopt;
}

// Flag names of `flags` from `names`, in their order.
template <std::size_t Count>
std::vector<std::string_view> flag_names(unsigned flags,
                                         const std::array<std::pair<unsigned, std::string_view>, Count> &names) {
    std::vector<std::string_view> set;
    for (const auto &[bit, name] : names) {
        if ((flags & bit) != 0) {
            set.push_back(name);
        }
    }
    return set;
}

// inet_drv's flags, in prim_inet's order.
std::vector<std::string_view> inet_flags(unsigned flags) {
    constexpr std::array<std::pair<unsigned, std::string_view>, 6> NAMES{{
        {IFF_UP, "up"},
        {IFF_BROADCAST, "broadcast"},
        {IFF_LOOPBACK, "loopback"},
        {IFF_POINTOPOINT, "pointtopoint"},
        {IFF_RUNNING, "running"},
        {IFF_MULTICAST, "multicast"},
    }};
    return flag_names(flags, NAMES);
}

// prim_net's flags, in its order.
std::vector<std::string_view> net_flags(unsigned flags) {
    constexpr std::array<std::pair<unsigned, std::string_view>, 12> NAMES{{
        {IFF_UP, "up"},
        {IFF_BROADCAST, "broadcast"},
        {IFF_DEBUG, "debug"},
        {IFF_LOOPBACK, "loopback"},
        {IFF_POINTOPOINT, "pointopoint"},
#ifdef IFF_NOTRAILERS
        {IFF_NOTRAILERS, "notrailers"},
#else
        {0, "notrailers"},
#endif
        {IFF_RUNNING, "running"},
        {IFF_NOARP, "noarp"},
        {IFF_PROMISC, "promisc"},
#ifdef IFF_MASTER
        {IFF_MASTER, "master"},
        {IFF_SLAVE, "slave"},
#else
        {0, "master"},
        {0, "slave"},
#endif
        {IFF_MULTICAST, "multicast"},
    }};
    return flag_names(flags, NAMES);
}

// The inet_drv options of one interface record.
std::vector<InetOption> inet_options(const ifaddrs &record) {
    std::vector<InetOption> options;
    if (is_ip(record.ifa_addr)) {
        options.push_back({"addr", ip_of(record.ifa_addr), {}});
        if (is_ip(record.ifa_netmask)) {
            options.push_back({"netmask", ip_of(record.ifa_netmask), {}});
        }
        if ((record.ifa_flags & IFF_POINTOPOINT) != 0 && is_ip(record.ifa_dstaddr)) {
            options.push_back({"dstaddr", ip_of(record.ifa_dstaddr), {}});
        } else if ((record.ifa_flags & IFF_BROADCAST) != 0 && is_ip(record.ifa_broadaddr)) {
            options.push_back({"broadaddr", ip_of(record.ifa_broadaddr), {}});
        }
    } else if (auto hardware = hardware_of(record.ifa_addr); hardware && !hardware->empty()) {
        options.push_back({"hwaddr", {}, std::move(*hardware)});
    }
    return options;
}

#ifdef __linux__
// prim_net's name of a hardware type, or empty for one it gives as a number.
std::string_view hardware_type(unsigned type) {
    switch (type) {
    case ARPHRD_ETHER:
        return "ether";
    case ARPHRD_LOOPBACK:
        return "loopback";
    case ARPHRD_NONE:
        return "none";
    default:
        return {};
    }
}

// prim_net's packet address of a Linux hardware address record.
NetAddress hardware_address(const sockaddr *address) {
    sockaddr_ll link{};
    std::memcpy(&link, address, sizeof(link));
    NetAddress result;
    result.kind = NetAddress::Kind::packet;
    result.fields = {{"protocol", ntohs(link.sll_protocol)}, {"ifindex", link.sll_ifindex}};
    if (const auto type = hardware_type(link.sll_hatype); type.empty()) {
        result.fields.emplace_back("hatype", link.sll_hatype);
    } else {
        result.names.emplace_back("hatype", type);
    }
    result.names.emplace_back("pkttype", link.sll_pkttype == PACKET_HOST ? "host" : "otherhost");
    result.data.assign(link.sll_addr, link.sll_addr + link.sll_halen);
    return result;
}

constexpr int HARDWARE_FAMILY = AF_PACKET;
#else
// prim_net's link address of a BSD hardware address record.
NetAddress hardware_address(const sockaddr *address) {
    const auto *link = reinterpret_cast<const sockaddr_dl *>(address);
    NetAddress result;
    result.kind = NetAddress::Kind::link;
    result.fields = {
        {"index", link->sdl_index}, {"nlen", link->sdl_nlen}, {"alen", link->sdl_alen}, {"slen", link->sdl_slen}};
    const auto type = link->sdl_type == IFT_ETHER  ? std::string_view("ether")
                      : link->sdl_type == IFT_LOOP ? std::string_view("loop")
                                                   : std::string_view("other");
    result.names.emplace_back("type", type);
    const auto *data = reinterpret_cast<const std::uint8_t *>(link->sdl_data);
    const auto size =
        link->sdl_len > offsetof(sockaddr_dl, sdl_data) ? link->sdl_len - offsetof(sockaddr_dl, sdl_data) : 0;
    result.data.assign(data, data + size);
    return result;
}

constexpr int HARDWARE_FAMILY = AF_LINK;
#endif

// prim_net's packet or link address of a hardware address record.
std::optional<NetAddress> net_hardware(const sockaddr *address) {
    if (address == nullptr || address->sa_family != HARDWARE_FAMILY) {
        return std::nullopt;
    }
    return hardware_address(address);
}

std::optional<NetAddress> net_ip(const sockaddr *address) {
    if (!is_ip(address)) {
        return std::nullopt;
    }
    return NetAddress{NetAddress::Kind::ip, ip_of(address), {}, {}, {}};
}
} // namespace

std::vector<InetRecord> inet_records(std::string &error) {
    std::vector<InetRecord> records;
    const auto list = interface_list(error);
    for (const auto *record = list.get(); record != nullptr; record = record->ifa_next) {
        records.push_back({record->ifa_name, inet_flags(record->ifa_flags), inet_options(*record)});
    }
    return records;
}

std::vector<NetEntry> net_entries(std::string &error) {
    std::vector<NetEntry> entries;
    const auto list = interface_list(error);
    for (const auto *record = list.get(); record != nullptr; record = record->ifa_next) {
        NetEntry entry{record->ifa_name, net_flags(record->ifa_flags), std::nullopt, std::nullopt, std::nullopt,
                       std::nullopt};
        entry.addr = is_ip(record->ifa_addr) ? net_ip(record->ifa_addr) : net_hardware(record->ifa_addr);
        entry.netmask = net_ip(record->ifa_netmask);
        if ((record->ifa_flags & IFF_POINTOPOINT) != 0) {
            entry.dstaddr = net_ip(record->ifa_dstaddr);
        } else if ((record->ifa_flags & IFF_BROADCAST) != 0) {
            entry.broadaddr =
                is_ip(record->ifa_addr) ? net_ip(record->ifa_broadaddr) : net_hardware(record->ifa_broadaddr);
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<std::pair<std::uint32_t, std::string>> interface_names() {
    std::vector<std::pair<std::uint32_t, std::string>> names;
    const std::unique_ptr<struct if_nameindex, decltype(&if_freenameindex)> list(if_nameindex(), &if_freenameindex);
    for (const auto *entry = list.get(); entry != nullptr && entry->if_index != 0; ++entry) {
        names.emplace_back(entry->if_index, entry->if_name);
    }
    return names;
}
} // namespace clause::runtime::builtins::net
#endif
