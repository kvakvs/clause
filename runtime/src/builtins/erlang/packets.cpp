#include "packets.hpp"
#include "../typed.hpp"
#include <algorithm>
#include <array>
#include <climits>

// decode_packet/3 and the packet lengths of OTP's packet_parser (plan 21 step LIB9C): each type's header gives the
// length of its packet; lines end at a delimiter; http lines may continue on lines that start with a space.
namespace clause::runtime::builtins::packets {
namespace {
using Bytes = std::span<const std::byte>;

std::uint32_t byte_at(Bytes bytes, std::size_t at) { return std::to_integer<std::uint32_t>(bytes[at]); }

// Where an integer field of a header is: its offset and width in bytes.
struct Field {
    std::size_t at;
    std::size_t width;
};

// A big-endian unsigned integer field.
std::uint32_t big_endian(Bytes bytes, Field field) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < field.width; ++i) {
        value = (value << 8U) | byte_at(bytes, field.at + i);
    }
    return value;
}

std::uint32_t little_endian(Bytes bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 4; i > 0; --i) {
        value = (value << 8U) | byte_at(bytes, at + i - 1);
    }
    return value;
}

// A header of `header` bytes and a body of `body` bytes, unless the body exceeds packet_size (OTP's remain:).
std::optional<std::size_t> remain(std::size_t header, std::uint64_t body, const Limits &limits) {
    const auto most = limits.max_packet == 0 ? static_cast<std::uint64_t>(INT_MAX) - header : limits.max_packet;
    if (body > most) {
        return std::nullopt;
    }
    return header + body;
}

// Bytes with a length header of `width` bytes before the body.
std::optional<std::size_t> counted(Bytes bytes, std::size_t width, const Limits &limits) {
    if (bytes.size() < width) {
        return 0;
    }
    return remain(width, big_endian(bytes, {0, width}), limits);
}

// A line without its delimiter yet: an error past packet_size, the first line_length bytes once that many arrived.
std::optional<std::size_t> unterminated(Bytes bytes, const Limits &limits) {
    if (limits.max_packet != 0 && bytes.size() > limits.max_packet) {
        return std::nullopt;
    }
    if (limits.truncate != 0 && bytes.size() >= limits.truncate) {
        return limits.truncate;
    }
    return 0;
}

// [Data ... Delimiter], truncated at line_length; an error past packet_size.
std::optional<std::size_t> line_length(Bytes bytes, const Limits &limits) {
    const auto delimiter = std::ranges::find(bytes, limits.delimiter);
    if (delimiter == bytes.end()) {
        return unterminated(bytes, limits);
    }
    const auto length = static_cast<std::size_t>(delimiter - bytes.begin()) + 1;
    if (limits.max_packet != 0 && length > limits.max_packet) {
        return std::nullopt;
    }
    return limits.truncate != 0 && length > limits.truncate ? limits.truncate : length;
}

// The offset of an ASN.1 length field after its tag (a long tag continues while bit 8 is set); none until the tag is
// complete.
std::optional<std::size_t> asn1_tag_end(Bytes bytes) {
    std::size_t at = 1;
    if ((byte_at(bytes, 0) & 0x1FU) != 0x1FU) {
        return at;
    }
    auto left = bytes.size() - 1;
    for (; left != 0 && (byte_at(bytes, at) & 0x80U) != 0; ++at, --left) {
    }
    return left < 2 ? std::nullopt : std::optional{at + 1};
}

// ASN.1 BER: a tag, then a short or 1..4-byte long length.
std::optional<std::size_t> asn1_length(Bytes bytes, const Limits &limits) {
    const auto tag_end = bytes.size() < 2 ? std::nullopt : asn1_tag_end(bytes);
    if (!tag_end) {
        return 0;
    }
    auto at = *tag_end;
    auto left = bytes.size() - at;
    const auto first = byte_at(bytes, at);
    if ((first & 0x80U) == 0) {
        return remain(at + 1, first & 0x7FU, limits);
    }
    const std::size_t width = first & 0x7FU;
    ++at;
    --left;
    if (left < width) {
        return 0;
    }
    if (width > 4) {
        return std::nullopt;
    }
    return remain(at + width, width == 0 ? 0 : big_endian(bytes, {at, width}), limits);
}

// GIOP: "GIOP", version, flags (bit 0: little-endian), message type, then the 4-byte size.
std::optional<std::size_t> cdr_length(Bytes bytes, const Limits &limits) {
    constexpr std::size_t HEADER = 12;
    if (bytes.size() < HEADER) {
        return 0;
    }
    constexpr std::array MAGIC{std::byte{'G'}, std::byte{'I'}, std::byte{'O'}, std::byte{'P'}};
    if (!std::equal(MAGIC.begin(), MAGIC.end(), bytes.begin())) {
        return std::nullopt;
    }
    const bool little = (byte_at(bytes, 6) & 1U) != 0;
    return remain(HEADER, little ? little_endian(bytes, 8) : big_endian(bytes, {8, 4}), limits);
}

// FastCGI: version 1, then the content length and padding of an 8-byte header.
std::optional<std::size_t> fcgi_length(Bytes bytes, const Limits &limits) {
    constexpr std::size_t HEADER = 8;
    if (bytes.size() < HEADER) {
        return 0;
    }
    if (byte_at(bytes, 0) != 1) {
        return std::nullopt;
    }
    return remain(HEADER, big_endian(bytes, {4, 2}) + byte_at(bytes, 6), limits);
}

// TPKT: version 3, then the 2-byte length of the whole packet.
std::optional<std::size_t> tpkt_length(Bytes bytes, const Limits &limits) {
    constexpr std::size_t HEADER = 4;
    if (bytes.size() < HEADER) {
        return 0;
    }
    const auto total = big_endian(bytes, {2, 2});
    if (byte_at(bytes, 0) != 3 || total < HEADER) {
        return std::nullopt;
    }
    return remain(HEADER, total - HEADER, limits);
}

// SSL/TLS: an SSLv2 client hello <<1:1, Len:15, 1:8, ...>> or a record <<Type:8, Version:16, Length:16>>.
std::optional<std::size_t> ssl_length(Bytes bytes, const Limits &limits) {
    constexpr std::size_t HEADER = 5;
    if (bytes.size() < HEADER) {
        return 0;
    }
    if ((byte_at(bytes, 0) & 0x80U) != 0 && byte_at(bytes, 2) == 1) {
        const auto length = big_endian(bytes, {0, 2}) & 0x7FFFU;
        if (length < 3) {
            return std::nullopt;
        }
        return remain(HEADER, length - 3, limits);
    }
    return remain(HEADER, big_endian(bytes, {3, 2}), limits);
}

bool space(Bytes bytes, std::size_t at) {
    return at < bytes.size() && (bytes[at] == std::byte{' '} || bytes[at] == std::byte{'\t'});
}

// A lone LF or CRLF: the empty line that ends http headers.
bool empty_line(Bytes bytes) {
    const auto newline = std::byte{'\n'};
    return (bytes.size() == 1 && bytes[0] == newline) ||
           (bytes.size() == 2 && bytes[0] == std::byte{'\r'} && bytes[1] == newline);
}

// The length of the line ending at the first newline from `from`, its newline included; none without one.
std::optional<std::size_t> line_end(Bytes bytes, std::size_t from) {
    const auto found = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(from), bytes.end(), std::byte{'\n'});
    if (found == bytes.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - bytes.begin()) + 1;
}

// Whether the header line ending at `length` continues on the next line (which starts with a space or tab).
bool continued(Bytes bytes, std::size_t length, bool header) {
    return header && length < bytes.size() && space(bytes, length) && length > 2;
}

// Where an http packet ends: at the first newline, for header lines at the first newline no continuation line
// follows; none when the bytes end first.
std::optional<std::size_t> http_end(Bytes bytes, bool header) {
    for (std::size_t from = 0;;) {
        const auto length = line_end(bytes, from);
        if (!length || !continued(bytes, *length, header)) {
            return length;
        }
        from = *length;
    }
}

// An http line and its continuation lines. packet_size, or line_length when it is 0, bounds the line; a last header
// line may still continue on bytes that have not arrived.
std::optional<std::size_t> http_length(Bytes bytes, const Limits &limits, bool header) {
    const auto n = bytes.size();
    if (empty_line(bytes)) {
        return n;
    }
    const std::uint64_t most = limits.max_packet != 0 ? limits.max_packet : limits.truncate;
    const auto end = http_end(bytes, header);
    if (!end) {
        return most != 0 && n >= most ? std::nullopt : std::optional<std::size_t>{0};
    }
    if (most != 0 && *end > most) {
        return std::nullopt;
    }
    return header && *end >= n ? std::optional<std::size_t>{0} : end;
}

std::optional<std::size_t> raw_length(Bytes bytes, const Limits & /*limits*/, bool & /*header*/) {
    return bytes.size();
}

template <std::size_t Width> std::optional<std::size_t> counted_length(Bytes bytes, const Limits &limits, bool &) {
    return counted(bytes, Width, limits);
}

// Sun RPC record marking: 4 bytes, the top bit marking the last fragment.
std::optional<std::size_t> sunrm_length(Bytes bytes, const Limits &limits, bool & /*header*/) {
    if (bytes.size() < 4) {
        return 0;
    }
    return remain(4, big_endian(bytes, {0, 4}) & 0x7FFFFFFFU, limits);
}

// A length rule without the http state.
template <std::optional<std::size_t> (*Rule)(Bytes, const Limits &)>
std::optional<std::size_t> stateless(Bytes bytes, const Limits &limits, bool & /*header*/) {
    return Rule(bytes, limits);
}

std::optional<std::size_t> http_start(Bytes bytes, const Limits &limits, bool &header) {
    return http_length(bytes, limits, header);
}

// The httph types read header lines from the first byte.
std::optional<std::size_t> http_header(Bytes bytes, const Limits &limits, bool &header) {
    header = true;
    return http_length(bytes, limits, header);
}

using LengthRule = std::optional<std::size_t> (*)(Bytes, const Limits &, bool &);

// The length rule of each packet type, in Type order.
constexpr std::array<LengthRule, 15> RULES{
    raw_length,
    counted_length<1>,
    counted_length<2>,
    counted_length<4>,
    stateless<asn1_length>,
    sunrm_length,
    stateless<cdr_length>,
    stateless<fcgi_length>,
    stateless<line_length>,
    stateless<tpkt_length>,
    http_start,
    http_header,
    stateless<ssl_length>,
    http_start,
    http_header,
};
} // namespace

std::optional<std::size_t> packet_length(Type type, Bytes bytes, const Limits &limits, bool &header) {
    return RULES.at(static_cast<std::size_t>(type))(bytes, limits, header);
}
} // namespace clause::runtime::builtins::packets

namespace clause::runtime::builtins::packets {
namespace {
// The roles of decode_packet/3's arguments (typed.hpp's Named).
struct TypeRole;
struct OptionsRole;
using TypeArg = Named<TypeRole>;
using OptionsArg = Named<OptionsRole>;

// The packet types by atom; 0, 1, 2 and 4 are integers.
constexpr std::array<std::pair<std::string_view, Type>, 12> TYPE_NAMES{{
    {"raw", Type::raw},
    {"asn1", Type::asn1},
    {"sunrm", Type::sunrm},
    {"cdr", Type::cdr},
    {"fcgi", Type::fcgi},
    {"line", Type::line},
    {"tpkt", Type::tpkt},
    {"http", Type::http},
    {"httph", Type::httph},
    {"http_bin", Type::http_bin},
    {"httph_bin", Type::httph_bin},
    {"ssl_tls", Type::ssl_tls},
}};

// decode_packet's Type: 0, 1, 2, 4 or a type atom; badarg otherwise.
Type type_of(const TypeArg &type) {
    if (const auto number = small(type.term.word())) {
        constexpr std::array<std::pair<std::int64_t, Type>, 4> COUNTS{
            {{0, Type::raw}, {1, Type::one}, {2, Type::two}, {4, Type::four}}};
        const auto found = std::ranges::find(COUNTS, *number, &std::pair<std::int64_t, Type>::first);
        if (found != COUNTS.end()) {
            return found->second;
        }
    }
    const auto name = type.term.is_atom() ? type.term.atom_spelling_unchecked() : std::string_view{};
    const auto found = std::ranges::find(TYPE_NAMES, name, &std::pair<std::string_view, Type>::first);
    if (found == TYPE_NAMES.end()) {
        bad_argument();
    }
    return found->second;
}

// An option {Name, N} with N in 0..2^32-1; badarg for anything else.
std::pair<std::string_view, std::uint32_t> option_of(const Term &option) {
    if (!option.is_tuple() || option.tuple_size_unchecked() != 2 || !option.tuple_element_unchecked(0).is_atom()) {
        bad_argument();
    }
    const auto value = small(option.tuple_element_unchecked(1).word());
    if (!value || *value < 0 || *value > 0xFFFFFFFFLL) {
        bad_argument();
    }
    return {option.tuple_element_unchecked(0).atom_spelling_unchecked(), static_cast<std::uint32_t>(*value)};
}

// One option; badarg for another option, or a delimiter for a type other than line or above 255.
void apply_option(Limits &limits, Type type, const Term &option) {
    const auto [name, number] = option_of(option);
    if (name == "packet_size") {
        limits.max_packet = number;
    } else if (name == "line_length") {
        limits.truncate = number;
    } else if (name == "line_delimiter" && type == Type::line && number <= 255) {
        limits.delimiter = static_cast<std::byte>(number);
    } else {
        bad_argument();
    }
}

Limits limits_of(const OptionsArg &options, Type type) {
    Limits limits;
    auto rest = options.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        apply_option(limits, type, rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return limits;
}

// {ssl_tls, [], ContentType, {Major, Minor}, Data}; an SSLv2 client hello becomes a handshake record.
Term ssl_term(const string::Terms &terms, std::span<const std::byte> packet) {
    const auto at = [&](std::size_t index) { return std::to_integer<std::uint32_t>(packet[index]); };
    const bool hello = (at(0) & 0x80U) != 0 && at(2) == 1;
    std::vector<std::byte> data;
    if (hello) {
        const auto length = packet.size() - 3;
        data = {std::byte{1}, static_cast<std::byte>(length >> 16U), static_cast<std::byte>(length >> 8U),
                static_cast<std::byte>(length)};
    }
    const auto body = packet.subspan(hello ? 3 : 5);
    data.insert(data.end(), body.begin(), body.end());
    const std::array version{terms.integer(at(hello ? 3 : 1)), terms.integer(at(hello ? 4 : 2))};
    const std::array items{terms.atom("ssl_tls"_atom), terms.nil(), terms.integer(hello ? 22 : at(0)),
                           terms.tuple(version), terms.binary(data)};
    return terms.tuple(items);
}

bool http_type(Type type) {
    return type == Type::http || type == Type::httph || type == Type::http_bin || type == Type::httph_bin;
}

// The term of a whole packet: the parsed http line or ssl record, else its body without the length header.
Term body_term(const string::Terms &terms, const Term &binary, std::span<const std::byte> packet, Type type,
               bool header) {
    if (http_type(type)) {
        return http_term(terms, binary, packet, header, type == Type::http_bin || type == Type::httph_bin);
    }
    if (type == Type::ssl_tls) {
        return ssl_term(terms, packet);
    }
    constexpr std::array<std::size_t, 4> HEADERS{0, 1, 2, 4};
    const auto skipped = type <= Type::four ? HEADERS.at(static_cast<std::size_t>(type)) : 0;
    const auto padding = type == Type::fcgi ? std::to_integer<std::size_t>(packet[6]) : 0;
    return terms.slice(binary, skipped, packet.size() - skipped - padding);
}

Term tagged(const string::Terms &terms, AtomName tag, const Term &value) {
    const std::array items{terms.atom(tag), value};
    return terms.tuple(items);
}

// decode_packet(Type, Bin, Options): {ok, Packet, Rest}, {more, Length | undefined} or {error, invalid}.
Term decode_packet(ProcessContext &context, const TypeArg &type_arg, const Term &binary, const OptionsArg &options) {
    const auto type = type_of(type_arg);
    if (!binary.is_bitstring() || !options.term.is_list()) {
        bad_argument();
    }
    const auto limits = limits_of(options, type);
    if (!binary.is_binary()) {
        bad_argument();
    }
    const auto bytes = need(binary.binary_bytes());
    const string::Terms terms(context);
    bool header = false;
    const auto length = packet_length(type, bytes, limits, header);
    if (!length) {
        return tagged(terms, "error"_atom, terms.atom("invalid"_atom));
    }
    if (*length == 0 || *length > bytes.size()) {
        const auto needed =
            *length == 0 ? terms.atom("undefined"_atom) : terms.integer(static_cast<std::int64_t>(*length));
        return tagged(terms, "more"_atom, needed);
    }
    const auto packet = std::span(bytes).first(*length);
    const std::array items{terms.atom("ok"_atom), body_term(terms, binary, packet, type, header),
                           terms.slice(binary, *length, bytes.size() - *length)};
    return terms.tuple(items);
}

constexpr std::array PACKET_BUILTINS{
    typed_entry<decode_packet>("erlang", "decode_packet"),
};
} // namespace
} // namespace clause::runtime::builtins::packets

namespace clause::runtime {
std::span<const BuiltinEntry> packet_builtins() noexcept { return builtins::packets::PACKET_BUILTINS; }
} // namespace clause::runtime
