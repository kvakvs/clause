#include "../text.hpp"
#include "dns.hpp"
#include "net.hpp"
#include <array>
#include <cstring>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// Name and service lookups of clause_net (plan 21 step LIB60, docs/builtins.md#network-names): the system's
// gethostname, getaddrinfo, getnameinfo, getservbyname and getservbyport with prim_net's results and error names.
// The library's net and inet modules check the arguments and pass text as UTF-8 binaries.
namespace clause::runtime::builtins::net {
namespace {
using Text = Named<struct TextRole>;
using Port = Named<struct PortRole>;
using Scope = Named<struct ScopeRole>;
using Flags = Named<struct FlagsRole>;
using Service = Named<struct ServiceRole>;
using Protocol = Named<struct ProtocolRole>;

// The bytes of a binary argument as text, or none for `undefined`; badarg for anything else.
std::optional<std::string> optional_text(const Term &term) {
    if (term.is_atom() && need(term.atom_spelling()) == "undefined") {
        return std::nullopt;
    }
    if (!term.is_binary()) {
        bad_argument();
    }
    const auto bytes = need(term.binary_bytes());
    std::string text(bytes.size(), '\0');
    std::ranges::transform(bytes, text.begin(), [](std::byte b) { return static_cast<char>(b); });
    return text;
}

// prim_net's names of getaddrinfo and getnameinfo errors; einval for another.
std::string_view lookup_error(int code) {
    struct Name {
        int code;
        std::string_view name;
    };

    static const std::array NAMES{
#if defined(EAI_ADDRFAMILY)
        Name{EAI_ADDRFAMILY, "eaddrfamily"},
#endif
        Name{EAI_AGAIN, "eagain"},
        Name{EAI_BADFLAGS, "ebadflags"},
        Name{EAI_FAIL, "efail"},
        Name{EAI_FAMILY, "efamily"},
        Name{EAI_MEMORY, "emem"},
#if defined(EAI_NODATA)
        Name{EAI_NODATA, "enodata"},
#endif
#if !defined(_WIN32)
        // Windows' EAI_NONAME is another error's number, which prim_net leaves to the default.
        Name{EAI_NONAME, "enoname"},
#endif
        Name{EAI_SERVICE, "eservice"},
        Name{EAI_SOCKTYPE, "esocktype"},
#if defined(EAI_SYSTEM)
        Name{EAI_SYSTEM, "esystem"},
#endif
    };
    const auto found = std::ranges::find(NAMES, code, &Name::code);
    return found == NAMES.end() ? std::string_view("einval") : found->name;
}

// The address and port of a sockaddr of family AF_INET or AF_INET6.
std::pair<IpAddress, std::uint16_t> ip_of(const sockaddr *address) {
    IpAddress ip;
    if (address->sa_family == AF_INET6) {
        sockaddr_in6 v6{};
        std::memcpy(&v6, address, sizeof(v6));
        ip.family = 6;
        ip.bytes.resize(16);
        std::memcpy(ip.bytes.data(), &v6.sin6_addr, 16);
        ip.scope_id = v6.sin6_scope_id;
        return {ip, ntohs(v6.sin6_port)};
    }
    sockaddr_in v4{};
    std::memcpy(&v4, address, sizeof(v4));
    ip.bytes.resize(4);
    std::memcpy(ip.bytes.data(), &v4.sin_addr, 4);
    return {ip, ntohs(v4.sin_port)};
}

// prim_net's names of a socket type and protocol; the number for another.
Term socket_type(ProcessContext &context, int type) {
    constexpr std::array<std::string_view, 6> TYPES{"any", "stream", "dgram", "raw", "rdm", "seqpacket"};
    return type >= 0 && static_cast<std::size_t>(type) < TYPES.size()
               ? need(TermFactory(context).atom(AtomName{TYPES.at(static_cast<std::size_t>(type))}))
               : need(TermFactory(context).integer(type));
}

Term protocol_name(ProcessContext &context, int protocol) {
    const auto name = protocol == IPPROTO_IP    ? std::string_view("ip")
                      : protocol == IPPROTO_TCP ? std::string_view("tcp")
                      : protocol == IPPROTO_UDP ? std::string_view("udp")
                                                : std::string_view();
    return name.empty() ? need(TermFactory(context).integer(protocol))
                        : need(TermFactory(context).atom(AtomName{name}));
}

// One address info map of getaddrinfo: #{family, socktype, protocol, addr}.
Term address_info(ProcessContext &context, const addrinfo &entry) {
    const auto [ip, port] = ip_of(entry.ai_addr);
    const std::array entries{
        std::pair{atom(context, "family"_atom), atom(context, ip.family == 6 ? "inet6"_atom : "inet"_atom)},
        std::pair{atom(context, "socktype"_atom), socket_type(context, entry.ai_socktype)},
        std::pair{atom(context, "protocol"_atom), protocol_name(context, entry.ai_protocol)},
        std::pair{atom(context, "addr"_atom), sockaddr_map(context, ip, port)},
    };
    return need(TermFactory(context).map(entries));
}

// clause_net:gethostname(): {ok, Name} of this machine.
Term gethostname(ProcessContext &context) {
    std::array<char, 256> name{};
    if (::gethostname(name.data(), static_cast<int>(name.size() - 1)) != 0) {
        return error(context, "einval");
    }
    return ok(context, chars(context, name.data()));
}

// clause_net:getaddrinfo(Host, Service): {ok, [AddressInfo]} for UTF-8 binaries or undefined.
Term getaddrinfo(ProcessContext &context, const Text &host, const Service &service) {
    const auto name = optional_text(host.term);
    const auto port = optional_text(service.term);
    addrinfo *found = nullptr;
    const auto status = ::getaddrinfo(name ? name->c_str() : nullptr, port ? port->c_str() : nullptr, nullptr, &found);
    if (status != 0) {
        return error(context, lookup_error(status));
    }
    const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> entries(found, &freeaddrinfo);
    std::vector<Term> infos;
    for (const auto *entry = entries.get(); entry != nullptr; entry = entry->ai_next) {
        if (entry->ai_family == AF_INET || entry->ai_family == AF_INET6) {
            infos.push_back(address_info(context, *entry));
        }
    }
    return ok(context, need(TermFactory(context).list(infos)));
}

// clause_net:getnameinfo(Address, Port, ScopeId, Flags): {ok, #{host, service}} of an address's 4 or 16 bytes and
// the NI_* flags the library encoded (bits 1 namereqd, 2 dgram, 4 nofqdn, 8 numerichost, 16 numericserv).
Term getnameinfo(ProcessContext &context, const BinaryArgument &address, const Port &port_term, const Scope &scope_term,
                 const Flags &flags_term) {
    const auto port = need(port_term.term.integer_value());
    const auto scope = need(scope_term.term.integer_value());
    const auto bits = need(flags_term.term.integer_value());
    constexpr std::array<int, 5> FLAGS{NI_NAMEREQD, NI_DGRAM, NI_NOFQDN, NI_NUMERICHOST, NI_NUMERICSERV};
    int flags = 0;
    for (std::size_t index = 0; index < FLAGS.size(); ++index) {
        flags |= (bits >> index) & 1 ? FLAGS.at(index) : 0;
    }
    sockaddr_storage storage{};
    socklen_t size = 0;
    if (address.bytes.size() == 16) {
        sockaddr_in6 v6{};
        v6.sin6_family = AF_INET6;
        v6.sin6_port = htons(static_cast<std::uint16_t>(port));
        v6.sin6_scope_id = static_cast<std::uint32_t>(scope);
        std::memcpy(&v6.sin6_addr, address.bytes.data(), 16);
        std::memcpy(&storage, &v6, sizeof(v6));
        size = sizeof(v6);
    } else if (address.bytes.size() == 4) {
        sockaddr_in v4{};
        v4.sin_family = AF_INET;
        v4.sin_port = htons(static_cast<std::uint16_t>(port));
        std::memcpy(&v4.sin_addr, address.bytes.data(), 4);
        std::memcpy(&storage, &v4, sizeof(v4));
        size = sizeof(v4);
    } else {
        bad_argument();
    }
    std::array<char, NI_MAXHOST> host{};
    std::array<char, NI_MAXSERV> service{};
    const auto status = ::getnameinfo(reinterpret_cast<const ::sockaddr *>(&storage), size, host.data(),
                                      static_cast<socklen_t>(host.size()), service.data(),
                                      static_cast<socklen_t>(service.size()), flags);
    if (status != 0) {
        return error(context, lookup_error(status));
    }
    const std::array entries{
        std::pair{atom(context, "host"_atom), chars(context, host.data())},
        std::pair{atom(context, "service"_atom), chars(context, service.data())},
    };
    return ok(context, need(TermFactory(context).map(entries)));
}

// clause_net:getservbyname(Name, Protocol): {ok, Port}, or {error, einval} for an unknown service.
Term getservbyname(ProcessContext &context, const Text &name, const Protocol &protocol) {
    const auto service = optional_text(name.term);
    const auto kind = optional_text(protocol.term);
    const auto *entry = service ? ::getservbyname(service->c_str(), kind ? kind->c_str() : nullptr) : nullptr;
    return entry == nullptr
               ? error(context, "einval")
               : ok(context, need(TermFactory(context).integer(ntohs(static_cast<std::uint16_t>(entry->s_port)))));
}

// clause_net:getservbyport(Port, Protocol): {ok, Name}, or {error, einval} for an unknown port.
Term getservbyport(ProcessContext &context, std::int64_t port, const Protocol &protocol) {
    const auto kind = optional_text(protocol.term);
    if (port < 0 || port > 65535) {
        return error(context, "einval");
    }
    const auto *entry = ::getservbyport(htons(static_cast<std::uint16_t>(port)), kind ? kind->c_str() : nullptr);
    return entry == nullptr ? error(context, "einval") : ok(context, chars(context, entry->s_name));
}

constexpr std::array NET_BUILTINS{
    typed_entry<gethostname>("clause_net", "gethostname"),
    typed_entry<getaddrinfo>("clause_net", "getaddrinfo"),
    typed_entry<getnameinfo>("clause_net", "getnameinfo"),
    typed_entry<getservbyname>("clause_net", "getservbyname"),
    typed_entry<getservbyport>("clause_net", "getservbyport"),
    typed_entry<inet_getifaddrs>("clause_net", "inet_getifaddrs"),
    typed_entry<getifaddrs>("clause_net", "getifaddrs"),
    typed_entry<if_names>("clause_net", "if_names"),
    typed_entry<dns::decode>("clause_net", "dns_decode"),
    typed_entry<dns::encode>("clause_net", "dns_encode"),
};
} // namespace

Term chars(ProcessContext &context, std::string_view text) {
    const auto codes = code_points(text);
    std::vector<Term> list;
    list.reserve(codes.size());
    for (const auto code : codes) {
        list.push_back(need(TermFactory(context).integer(code)));
    }
    return need(TermFactory(context).list(list));
}

Term address_tuple(ProcessContext &context, const IpAddress &address) {
    std::vector<Term> parts;
    if (address.family == 6) {
        for (std::size_t index = 0; index + 1 < address.bytes.size(); index += 2) {
            parts.push_back(need(TermFactory(context).integer((address.bytes[index] << 8) | address.bytes[index + 1])));
        }
    } else {
        for (const auto byte : address.bytes) {
            parts.push_back(need(TermFactory(context).integer(byte)));
        }
    }
    return need(TermFactory(context).tuple(parts));
}

Term sockaddr_map(ProcessContext &context, const IpAddress &address, std::uint16_t port) {
    TermFactory factory(context);
    const auto port_term = need(factory.integer(port));
    if (address.family != 6) {
        const std::array entries{
            std::pair{atom(context, "family"_atom), atom(context, "inet"_atom)},
            std::pair{atom(context, "port"_atom), port_term},
            std::pair{atom(context, "addr"_atom), address_tuple(context, address)},
        };
        return need(factory.map(entries));
    }
    const std::array entries{
        std::pair{atom(context, "family"_atom), atom(context, "inet6"_atom)},
        std::pair{atom(context, "port"_atom), port_term},
        std::pair{atom(context, "addr"_atom), address_tuple(context, address)},
        std::pair{atom(context, "flowinfo"_atom), need(factory.integer(0))},
        std::pair{atom(context, "scope_id"_atom), need(factory.integer(address.scope_id))},
    };
    return need(factory.map(entries));
}

Term ok(ProcessContext &context, const Term &value) {
    const std::array parts{atom(context, "ok"_atom), value};
    return need(TermFactory(context).tuple(parts));
}

Term error(ProcessContext &context, std::string_view reason) {
    const std::array parts{atom(context, "error"_atom), need(TermFactory(context).atom(AtomName{reason}))};
    return need(TermFactory(context).tuple(parts));
}
} // namespace clause::runtime::builtins::net

namespace clause::runtime {
std::span<const BuiltinEntry> clause_net_builtins() noexcept { return builtins::net::NET_BUILTINS; }
} // namespace clause::runtime
