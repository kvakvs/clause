#include "requests.hpp"
#include <optional>

namespace clause::runtime::epmd {
namespace {
// Request codes and reply tags of the epmd protocol.
constexpr char ALIVE2_REQ = 120;
constexpr char ALIVE2_X_RESP = 118;
constexpr char ALIVE2_RESP = 121;
constexpr char PORT_PLEASE2_REQ = 122;
constexpr char PORT2_RESP = 119;
constexpr char NAMES_REQ = 110;
constexpr char DUMP_REQ = 100;
constexpr char KILL_REQ = 107;
constexpr char STOP_REQ = 115;
// The first distribution version whose nodes get the 32-bit creation of ALIVE2_X_RESP.
constexpr std::uint16_t X_VERSION = 6;
// The longest node name: 255 characters of up to 4 UTF-8 bytes.
constexpr std::size_t MAX_NAME_BYTES = std::size_t{255} * 4;
// The fixed part of ALIVE2_REQ: code, port, type, protocol, two versions and the name length.
constexpr std::size_t ALIVE2_FIXED = 11;
// The creation OTP's epmd puts in a refused registration's reply.
constexpr std::uint32_t REFUSED_CREATION = 99;

// A reply, then `after`; `name` is the registered or stopped node's.
Outcome replied(std::string reply, After after = After::close, std::string name = {}) {
    return {.reply_ = std::move(reply), .after_ = after, .name_ = std::move(name)};
}

// The big-endian 16-bit field at `at`.
std::uint16_t field16(std::string_view packet, std::size_t at) {
    return static_cast<std::uint16_t>((static_cast<unsigned char>(packet[at]) << 8) |
                                      static_cast<unsigned char>(packet[at + 1]));
}

void put16(std::string &out, std::uint32_t value) {
    out += static_cast<char>((value >> 8) & 0xFF);
    out += static_cast<char>(value & 0xFF);
}

void put32(std::string &out, std::uint32_t value) {
    put16(out, value >> 16);
    put16(out, value & 0xFFFF);
}

// The node an ALIVE2_REQ describes, or nothing for a malformed request (closed without reply).
std::optional<Node> alive_request(std::string_view packet, std::uint64_t descriptor) {
    if (packet.size() <= ALIVE2_FIXED + 2) {
        return std::nullopt;
    }
    const std::size_t name_bytes = field16(packet, 9);
    if (name_bytes == 0 || name_bytes > MAX_NAME_BYTES || ALIVE2_FIXED + name_bytes + 2 > packet.size()) {
        return std::nullopt;
    }
    const auto extra_at = ALIVE2_FIXED + name_bytes + 2;
    const std::size_t extra_bytes = field16(packet, extra_at - 2);
    if (extra_at + extra_bytes > packet.size()) {
        return std::nullopt;
    }
    return Node{.name_ = std::string(packet.substr(ALIVE2_FIXED, name_bytes)),
                .port_ = field16(packet, 1),
                .type_ = static_cast<std::uint8_t>(packet[3]),
                .protocol_ = static_cast<std::uint8_t>(packet[4]),
                .highest_ = field16(packet, 5),
                .lowest_ = field16(packet, 7),
                .extra_ = std::string(packet.substr(extra_at, extra_bytes)),
                .descriptor_ = descriptor};
}

// The ALIVE2_X_RESP or ALIVE2_RESP of a registration: result 0 with the creation, or result 1.
std::string alive_reply(std::uint16_t highest, std::optional<std::uint32_t> creation) {
    std::string reply;
    reply += highest >= X_VERSION ? ALIVE2_X_RESP : ALIVE2_RESP;
    reply += static_cast<char>(creation ? 0 : 1);
    const auto value = creation.value_or(REFUSED_CREATION);
    if (highest >= X_VERSION) {
        put32(reply, value);
    } else {
        put16(reply, creation ? short_creation(value) : value);
    }
    return reply;
}

Outcome alive(Registry &registry, const Requester &requester, std::string_view packet) {
    auto node = alive_request(packet, requester.descriptor_);
    if (!requester.local_ || !node) {
        return {};
    }
    const auto highest = node->highest_;
    auto name = node->name_;
    const auto creation = registry.add(std::move(*node));
    if (!creation) {
        return replied(alive_reply(highest, std::nullopt), After::keep_reading);
    }
    return replied(alive_reply(highest, creation), After::registered, std::move(name));
}

// PORT2_RESP: the node's registration, or result 1 when the name is not registered.
Outcome port_please(const Registry &registry, std::string_view packet) {
    if (packet.size() < 2) {
        return {};
    }
    const auto *node = registry.find(packet.substr(1));
    std::string reply{PORT2_RESP};
    if (!node) {
        reply += '\1';
        return replied(std::move(reply));
    }
    reply += '\0';
    put16(reply, node->port_);
    reply += static_cast<char>(node->type_);
    reply += static_cast<char>(node->protocol_);
    put16(reply, node->highest_);
    put16(reply, node->lowest_);
    put16(reply, static_cast<std::uint32_t>(node->name_.size()));
    reply += node->name_;
    put16(reply, static_cast<std::uint32_t>(node->extra_.size()));
    reply += node->extra_;
    return replied(std::move(reply));
}

// NAMES_RESP and DUMP_RESP: the epmd's port, then the table's lines.
Outcome listing(const Settings &settings, const std::string &lines) {
    std::string reply;
    put32(reply, settings.port_);
    return replied(reply + lines);
}

// KILL_RESP: "OK" and the epmd stops, or "NO" while names are registered without -relaxed_command_check.
Outcome kill(const Registry &registry, const Settings &settings) {
    if (!settings.relaxed_ && !registry.empty()) {
        return replied("NO");
    }
    return replied("OK", After::kill);
}

// STOP_RESP: ignored without -relaxed_command_check. OTP's epmd answers an unknown name with "NOEXIST" followed by
// "STOPPED", so that is kept.
Outcome stop(const Registry &registry, const Settings &settings, std::string_view packet) {
    if (!settings.relaxed_ || packet.size() < 2) {
        return {};
    }
    const auto name = packet.substr(1);
    if (!registry.find(name)) {
        return replied("NOEXISTSTOPPED");
    }
    return replied("STOPPED", After::close, std::string(name));
}

// The requests only a peer on this host may make.
Outcome local_request(Registry &registry, const Settings &settings, std::string_view packet) {
    switch (packet[0]) {
    case DUMP_REQ:
        return listing(settings, registry.dump_text());
    case KILL_REQ:
        return kill(registry, settings);
    case STOP_REQ:
        return stop(registry, settings, packet);
    default:
        return {};
    }
}
} // namespace

Outcome handle_request(Registry &registry, const Settings &settings, const Requester &requester,
                       std::string_view packet) {
    if (packet.empty()) {
        return {};
    }
    switch (packet[0]) {
    case ALIVE2_REQ:
        return alive(registry, requester, packet);
    case PORT_PLEASE2_REQ:
        return port_please(registry, packet);
    case NAMES_REQ:
        return listing(settings, registry.names_text());
    default:
        return requester.local_ ? local_request(registry, settings, packet) : Outcome{};
    }
}
} // namespace clause::runtime::epmd
