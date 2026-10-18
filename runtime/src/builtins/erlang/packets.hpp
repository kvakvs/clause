#pragma once
#include "../string/string.hpp"
#include <cstdint>
#include <optional>
#include <span>

// decode_packet/3 (plan 21 step LIB9C, docs/builtins.md#erlang-module): OTP's packet_parser rules, the packet length
// of every packet type and the body parsing of the http and ssl_tls types, in packets.cpp and packet_http.cpp.
namespace clause::runtime::builtins::packets {
// The packet types decode_packet/3 takes, as OTP's inet driver numbers them.
enum class Type : std::uint8_t {
    raw,
    one,
    two,
    four,
    asn1,
    sunrm,
    cdr,
    fcgi,
    line,
    tpkt,
    http,
    httph,
    ssl_tls,
    http_bin,
    httph_bin
};

// The options of decode_packet/3: packet_size, line_length (0 = no limit) and line_delimiter.
struct Limits {
    std::uint32_t max_packet = 0;
    std::uint32_t truncate = 0;
    std::byte delimiter{'\n'};
};

// The length of the first packet of `bytes`: its total size, 0 when more bytes are needed to know it, none when the
// bytes are no valid packet. `header` turns true for the httph types (OTP's parser state).
std::optional<std::size_t> packet_length(Type type, std::span<const std::byte> bytes, const Limits &limits,
                                         bool &header);

// The term of one http line, as OTP's http callbacks build it: http_request, http_response, http_header, http_eoh
// or http_error. Strings are lists, or sub-binaries of `binary` for the _bin types.
Term http_term(const string::Terms &terms, const Term &binary, std::span<const std::byte> line, bool header,
               bool as_binary);
} // namespace clause::runtime::builtins::packets
