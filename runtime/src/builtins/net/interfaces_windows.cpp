#ifdef _WIN32
#include "net.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
// iphlpapi.h needs the socket headers first.
#include <iphlpapi.h>
// Programs link the runtime archive alone, so the archive names the IP helper library it needs.
#pragma comment(lib, "iphlpapi.lib")

// Windows' interfaces for clause_net (plan 21 step LIB60): GetAdaptersAddresses and GetIfEntry, read as OTP's
// inet_drv does for inet:getifaddrs/0 and as OTP's net module does for net:getifaddrs/0.
namespace clause::runtime::builtins::net {
namespace {
// The adapters of GetAdaptersAddresses with `flags`, kept in `buffer`; none when the call fails.
const IP_ADAPTER_ADDRESSES *adapters(std::vector<std::byte> &buffer, ULONG flags) {
    ULONG size = 16 * 1024;
    for (int attempt = 0; attempt < 17; ++attempt) {
        buffer.resize(size);
        auto *first = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
        const auto status = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, first, &size);
        if (status == NO_ERROR) {
            return first;
        }
        if (status != ERROR_BUFFER_OVERFLOW) {
            break;
        }
    }
    return nullptr;
}

std::string narrow(const wchar_t *text) {
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(size, 1)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.resize(std::strlen(result.c_str()));
    return result;
}

// The interface row of an index, if GetIfEntry has one.
std::optional<MIB_IFROW> interface_row(DWORD index) {
    MIB_IFROW row{};
    row.dwIndex = index;
    if (GetIfEntry(&row) != NO_ERROR) {
        return std::nullopt;
    }
    return row;
}

// The name of an interface: its row's name, else "Index: Description".
std::string row_name(const MIB_IFROW &row) {
    if (row.wszName[0] != 0) {
        return narrow(row.wszName);
    }
    std::string name = std::to_string(row.dwIndex) + ": ";
    name.append(reinterpret_cast<const char *>(row.bDescr), row.dwDescrLen);
    return name;
}

IpAddress ip_of(const SOCKET_ADDRESS &socket_address) {
    IpAddress ip;
    const auto *address = socket_address.lpSockaddr;
    if (address->sa_family == AF_INET6) {
        sockaddr_in6 v6{};
        std::memcpy(&v6, address, sizeof(v6));
        ip.family = 6;
        ip.bytes.assign(v6.sin6_addr.u.Byte, v6.sin6_addr.u.Byte + 16);
        ip.scope_id = v6.sin6_scope_id;
    } else {
        sockaddr_in v4{};
        std::memcpy(&v4, address, sizeof(v4));
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&v4.sin_addr);
        ip.bytes.assign(bytes, bytes + 4);
    }
    return ip;
}

// A netmask of the family of `like` with its first `bits` bits set.
IpAddress mask_of(const IpAddress &like, unsigned bits) {
    IpAddress mask;
    mask.family = like.family;
    mask.bytes.assign(like.bytes.size(), 0);
    for (unsigned bit = 0; bit < bits && bit < mask.bytes.size() * 8; ++bit) {
        mask.bytes[bit / 8] |= static_cast<std::uint8_t>(0x80U >> (bit % 8));
    }
    return mask;
}

// Whether `address` and `prefix` agree under `mask`.
bool masked_equal(const IpAddress &address, const IpAddress &prefix, const IpAddress &mask) {
    for (std::size_t index = 0; index < mask.bytes.size(); ++index) {
        if ((address.bytes[index] & mask.bytes[index]) != (prefix.bytes[index] & mask.bytes[index])) {
            return false;
        }
    }
    return true;
}

// The shortest prefix of the adapter containing `address`: its address and length, if one does.
std::optional<std::pair<IpAddress, unsigned>> shortest_prefix(const IpAddress &address,
                                                              const IP_ADAPTER_ADDRESSES &adapter, bool net_rules) {
    std::optional<std::pair<IpAddress, unsigned>> shortest;
    for (const auto *prefix = adapter.FirstPrefix; prefix != nullptr; prefix = prefix->Next) {
        const auto candidate = ip_of(prefix->Address);
        if (candidate.family != address.family) {
            continue;
        }
        // inet_drv shifts 32 bits for a /0 IPv4 prefix (no mask: an exact match); net's mask is empty then.
        const bool full = !net_rules && address.family == 4 && prefix->PrefixLength == 0;
        const auto mask = mask_of(address, full ? 32 : prefix->PrefixLength);
        if (masked_equal(address, candidate, mask) && (!shortest || prefix->PrefixLength < shortest->second)) {
            shortest = std::pair{candidate, prefix->PrefixLength};
        }
    }
    return shortest;
}

// The classful length of an IPv4 address without a prefix: inet_drv tests bits 23..21 (its quirk), net the first
// byte; IPv6 is /128.
unsigned classful(const IpAddress &address, bool net_rules) {
    if (address.family == 6) {
        return 128;
    }
    const auto first = address.bytes[0];
    const auto second = address.bytes[1];
    // Classes A, B and C in order; class D and above are /32.
    const std::array classes = net_rules ? std::array{first < 128, first < 192, first < 224}
                                         : std::array{(second & 0x80) == 0, (second & 0x40) == 0, (second & 0x20) == 0};
    const auto found = std::ranges::find(classes, true);
    return 8 * static_cast<unsigned>(found - classes.begin() + 1);
}

// The netmask of a unicast address and the prefix address (or the address itself) it was found with.
struct Netmask final {
    IpAddress mask;
    IpAddress prefix;
};

// The broadcast address of a prefix address under its mask.
IpAddress broadcast_of(const Netmask &netmask) {
    auto broadcast = netmask.prefix;
    for (std::size_t index = 0; index < broadcast.bytes.size(); ++index) {
        broadcast.bytes[index] |= static_cast<std::uint8_t>(~netmask.mask.bytes[index]);
    }
    return broadcast;
}

// The netmask and prefix address of one unicast address of an adapter.
Netmask netmask_of(const IpAddress &address, const IP_ADAPTER_ADDRESSES &adapter, bool net_rules) {
    const auto prefix = shortest_prefix(address, adapter, net_rules);
    const auto length = prefix ? prefix->second : classful(address, net_rules);
    return {mask_of(address, length), prefix ? prefix->first : address};
}

constexpr ULONG INET_FLAGS = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER |
                             GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_SKIP_MULTICAST;

// inet_drv's flags of an adapter, in prim_inet's order.
std::vector<std::string_view> inet_flags(const IP_ADAPTER_ADDRESSES &adapter, const MIB_IFROW &row) {
    bool multicast = (adapter.Flags & IP_ADAPTER_NO_MULTICAST) == 0;
    const bool broadcast = row.dwType == IF_TYPE_ETHERNET_CSMACD;
    const bool loopback = row.dwType == IF_TYPE_SOFTWARE_LOOPBACK;
    multicast = multicast && broadcast;
    const bool up = row.dwAdminStatus != 0;
    const auto status = row.dwOperStatus;
    const bool pointtopoint = up && (status == IF_OPER_STATUS_CONNECTING || status == IF_OPER_STATUS_CONNECTED);
    const bool running = up && (status == IF_OPER_STATUS_CONNECTED || status == IF_OPER_STATUS_OPERATIONAL);
    std::vector<std::string_view> flags;
    for (const auto &[set, name] : std::array{std::pair{up, "up"}, std::pair{broadcast, "broadcast"},
                                              std::pair{loopback, "loopback"}, std::pair{pointtopoint, "pointtopoint"},
                                              std::pair{running, "running"}, std::pair{multicast, "multicast"}}) {
        if (set) {
            flags.emplace_back(name);
        }
    }
    return flags;
}

// The inet_drv options of one adapter's unicast addresses and hardware address.
std::vector<InetOption> inet_options(const IP_ADAPTER_ADDRESSES &adapter, const MIB_IFROW &row, bool broadcast) {
    std::vector<InetOption> options;
    for (const auto *unicast = adapter.FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
        const auto address = ip_of(unicast->Address);
        const auto netmask = netmask_of(address, adapter, false);
        options.push_back({"addr", address, {}});
        options.push_back({"netmask", netmask.mask, {}});
        if (broadcast && address.family == 4) {
            options.push_back({"broadaddr", broadcast_of(netmask), {}});
        }
    }
    if (row.dwPhysAddrLen != 0) {
        options.push_back({"hwaddr", {}, {row.bPhysAddr, row.bPhysAddr + row.dwPhysAddrLen}});
    }
    return options;
}

// OTP's net module's flags of an adapter, sorted (with its "runnning" for a connected line).
std::vector<std::string_view> net_flags(const IP_ADAPTER_ADDRESSES &adapter, const MIB_IFROW &row) {
    std::vector<std::string_view> flags;
    if ((adapter.Flags & IP_ADAPTER_NO_MULTICAST) == 0) {
        flags.emplace_back("multicast");
    }
    if (row.dwType == IF_TYPE_ETHERNET_CSMACD) {
        flags.emplace_back("broadcast");
    } else if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) {
        flags.emplace_back("loopback");
    }
    if (row.dwAdminStatus == MIB_IF_ADMIN_STATUS_UP) {
        flags.emplace_back("up");
        if (row.dwOperStatus == IF_OPER_STATUS_CONNECTING) {
            flags.emplace_back("pointtopoint");
        } else if (row.dwOperStatus == IF_OPER_STATUS_CONNECTED) {
            flags.insert(flags.end(), {"runnning", "pointtopoint"});
        } else if (row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL) {
            flags.emplace_back("running");
        }
    }
    std::ranges::sort(flags);
    return flags;
}

// prim_net's name of an interface type, as net's hatype (ether and loopback for its two common ones).
std::string_view hardware_type(DWORD type) {
    struct Name {
        DWORD type;
        std::string_view name;
    };

    constexpr std::array NAMES{
        Name{IF_TYPE_OTHER, "other"},
        Name{IF_TYPE_ETHERNET_CSMACD, "ether"},
        Name{IF_TYPE_ISO88025_TOKENRING, "iso88025_tokenring"},
        Name{IF_TYPE_FDDI, "fddi"},
        Name{IF_TYPE_PPP, "ppp"},
        Name{IF_TYPE_SOFTWARE_LOOPBACK, "loopback"},
        Name{IF_TYPE_ATM, "atm"},
        Name{IF_TYPE_IEEE80211, "ieee80211"},
        Name{IF_TYPE_TUNNEL, "tunnel"},
        Name{IF_TYPE_IEEE1394, "ieee1394"},
    };
    const auto found = std::ranges::find(NAMES, type, &Name::type);
    return found == NAMES.end() ? std::string_view("other") : found->name;
}

// net's packet entry of an adapter: its hardware address, with the fake broadcast address unless loopback.
NetEntry packet_entry(const std::string &name, const std::vector<std::string_view> &flags, const MIB_IFROW &row) {
    const auto type = hardware_type(row.dwType);
    NetAddress address;
    address.kind = NetAddress::Kind::packet;
    address.fields = {{"protocol", 0}, {"ifindex", row.dwIndex}};
    address.names = {{"hatype", type}, {"pkttype", "host"}};
    address.data.assign(row.bPhysAddr, row.bPhysAddr + row.dwPhysAddrLen);
    if (type == "loopback" && address.data.empty()) {
        address.data.assign(6, 0);
    }
    NetEntry entry{name, flags, address, std::nullopt, std::nullopt, std::nullopt};
    if (type != "loopback") {
        entry.broadaddr = NetAddress{NetAddress::Kind::bytes, {}, {}, {}, std::vector<std::uint8_t>(6, 0xff)};
    }
    return entry;
}

NetAddress ip_entry(const IpAddress &address) { return NetAddress{NetAddress::Kind::ip, address, {}, {}, {}}; }

// net's entries of one adapter: its packet entry, then its unicast addresses last first.
void net_adapter(const IP_ADAPTER_ADDRESSES &adapter, const MIB_IFROW &row, std::vector<NetEntry> &entries) {
    const auto name = row.wszName[0] != 0 ? narrow(row.wszName) : narrow(adapter.Description);
    const auto flags = net_flags(adapter, row);
    entries.push_back(packet_entry(name, flags, row));
    const bool broadcast = std::ranges::find(flags, "broadcast") != flags.end();
    std::vector<NetEntry> unicasts;
    for (const auto *unicast = adapter.FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
        const auto address = ip_of(unicast->Address);
        const auto netmask = netmask_of(address, adapter, true);
        NetEntry entry{name, flags, ip_entry(address), ip_entry(netmask.mask), std::nullopt, std::nullopt};
        if (broadcast && address.family == 4) {
            entry.broadaddr = ip_entry(broadcast_of(netmask));
        }
        unicasts.push_back(std::move(entry));
    }
    entries.insert(entries.end(), std::make_move_iterator(unicasts.rbegin()), std::make_move_iterator(unicasts.rend()));
}
} // namespace

std::vector<InetRecord> inet_records(std::string &error) {
    std::vector<std::byte> buffer;
    const auto *first = adapters(buffer, INET_FLAGS);
    std::vector<InetRecord> records;
    if (first == nullptr) {
        error = "enotsup";
        return records;
    }
    for (const auto *adapter = first; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->IfIndex == 0) {
            continue;
        }
        const auto row = interface_row(adapter->IfIndex);
        if (!row) {
            break;
        }
        auto flags = inet_flags(*adapter, *row);
        const bool broadcast = row->dwType == IF_TYPE_ETHERNET_CSMACD;
        records.push_back({row_name(*row), std::move(flags), inet_options(*adapter, *row, broadcast)});
    }
    return records;
}

std::vector<NetEntry> net_entries(std::string &error) {
    std::vector<std::byte> buffer;
    const auto *first = adapters(buffer, GAA_FLAG_INCLUDE_PREFIX);
    std::vector<NetEntry> entries;
    if (first == nullptr) {
        error = "enotsup";
        return entries;
    }
    for (const auto *adapter = first; adapter != nullptr; adapter = adapter->Next) {
        if (const auto row = interface_row(adapter->IfIndex)) {
            net_adapter(*adapter, *row, entries);
        }
    }
    return entries;
}

std::vector<std::pair<std::uint32_t, std::string>> interface_names() {
    std::vector<std::byte> buffer;
    std::vector<std::pair<std::uint32_t, std::string>> names;
    const auto *first = adapters(buffer, GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_FRIENDLY_NAME);
    for (const auto *adapter = first; adapter != nullptr; adapter = adapter->Next) {
        if (const auto row = interface_row(adapter->IfIndex)) {
            names.emplace_back(adapter->IfIndex, narrow(row->wszName));
        }
    }
    return names;
}
} // namespace clause::runtime::builtins::net
#endif
