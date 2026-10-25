#include "dns.hpp"
#include "../text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

// The DNS message codec of inet_res (plan 21 step LIB60B, docs/builtins.md#network-names): clause_net:dns_decode/1
// reads a message into OTP's #dns_rec{}, #dns_header{}, #dns_query{}, #dns_rr{}, #dns_rr_opt{} and #dns_rr_tsig{}
// tuples with inet_dns's values and formerr rules (class flags off, as inet_res decodes replies); dns_encode/1 writes
// one with name compression.
namespace clause::runtime::builtins::net::dns {
namespace {
// An RR type's number and name, as inet_dns names them.
struct TypeName {
    std::uint16_t number;
    std::string_view name;
};

constexpr std::array TYPES{
    TypeName{1, "a"},      TypeName{2, "ns"},       TypeName{3, "md"},      TypeName{4, "mf"},
    TypeName{5, "cname"},  TypeName{6, "soa"},      TypeName{7, "mb"},      TypeName{8, "mg"},
    TypeName{9, "mr"},     TypeName{10, "null"},    TypeName{11, "wks"},    TypeName{12, "ptr"},
    TypeName{13, "hinfo"}, TypeName{14, "minfo"},   TypeName{15, "mx"},     TypeName{16, "txt"},
    TypeName{28, "aaaa"},  TypeName{29, "loc"},     TypeName{33, "srv"},    TypeName{35, "naptr"},
    TypeName{41, "opt"},   TypeName{99, "spf"},     TypeName{100, "uinfo"}, TypeName{101, "uid"},
    TypeName{102, "gid"},  TypeName{103, "unspec"}, TypeName{250, "tsig"},  TypeName{251, "ixfr"},
    TypeName{252, "axfr"}, TypeName{253, "mailb"},  TypeName{254, "maila"}, TypeName{255, "any"},
    TypeName{256, "uri"},  TypeName{257, "caa"},
};

constexpr std::array CLASSES{
    TypeName{1, "in"}, TypeName{3, "chaos"}, TypeName{4, "hs"}, TypeName{254, "none"}, TypeName{255, "any"},
};

constexpr std::array OPCODES{
    TypeName{0, "query"}, TypeName{1, "iquery"}, TypeName{2, "status"}, TypeName{4, "notify"}, TypeName{5, "update"},
};

// The name of a number in a table, or empty for an unknown one.
template <std::size_t N> std::string_view name_of(const std::array<TypeName, N> &table, std::uint32_t number) {
    const auto found = std::ranges::find(table, number, &TypeName::number);
    return found == table.end() ? std::string_view() : found->name;
}

// The number of a name in a table, if it is one.
template <std::size_t N>
std::optional<std::uint16_t> number_of(const std::array<TypeName, N> &table, std::string_view name) {
    const auto found = std::ranges::find(table, name, &TypeName::name);
    return found == table.end() ? std::nullopt : std::optional(found->number);
}

// The message does not decode (inet_dns's formerr).
struct FormatError {};

[[noreturn]] void format_error() { throw FormatError{}; }

// Lower-case ASCII letters, as inet_db:tolower/1.
std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
    return text;
}

// Where a name walk is: the next byte, the end it may reach, whether it followed a pointer, its length so far.
struct NameWalk {
    std::size_t position = 0;
    std::size_t limit = 0;
    bool jumped = false;
    std::size_t length = 0;
};

// Where data lies in a message: its record's start (for TSIG), its first byte and its size.
struct Place {
    std::size_t start = 0;
    std::size_t at = 0;
    std::size_t size = 0;
};

// A record's type (an atom or its number) and its class and type numbers.
struct Kind {
    Term type;
    std::uint16_t class_number = 0;
    std::uint16_t type_number = 0;
};

// A reader over one message, positions counted from its start.
class Decoder final {
  public:
    Decoder(ProcessContext &context, std::vector<std::byte> buffer) : context_(context), buffer_(std::move(buffer)) {}

    Term message();

  private:
    std::uint8_t byte(std::size_t at) const { return std::to_integer<std::uint8_t>(buffer_.at(at)); }

    std::uint32_t u16(std::size_t at) const { return (static_cast<std::uint32_t>(byte(at)) << 8U) | byte(at + 1); }

    std::uint32_t u32(std::size_t at) const { return (u16(at) << 16U) | u16(at + 2); }

    // Fail unless `size` bytes lie between `at` and `end`.
    void need_bytes(std::size_t at, std::size_t size, std::size_t end) const {
        if (at + size > end) {
            format_error();
        }
    }

    Term header();
    // The domain name at `at`, which moves past it (and its pointer); `end` bounds where it may start.
    std::string name(std::size_t &at, std::size_t end) const;
    bool name_step(NameWalk &walk, std::string &result, std::size_t &at) const;
    void follow_pointer(NameWalk &walk, std::size_t &at) const;
    void append_label(NameWalk &walk, std::string &result, std::size_t label) const;
    // A section of `count` entries from `at`, the position after it, and whether input ended before its count.
    std::vector<Term> queries(std::size_t &at, std::size_t count, bool &truncated);
    std::vector<Term> records(std::size_t &at, std::size_t count, bool &truncated);
    Term record(std::size_t &at);
    Term option(std::string_view domain, std::size_t at, std::size_t size, std::uint16_t class_number);
    Term signature(std::string_view domain, const Place &place);
    Term data(const Place &place, const Kind &kind);
    Term typed_data(std::size_t at, std::size_t end, std::string_view type);
    Term domain_data(std::size_t at, std::size_t end) const;
    Term address_data(std::size_t at, std::size_t end, std::string_view type);
    Term soa(std::size_t at, std::size_t end);
    Term pair_names(std::size_t at, std::size_t end);
    Term hinfo(std::size_t at, std::size_t end);
    Term priority_name(std::size_t at, std::size_t end, std::size_t fields);
    Term naptr(std::size_t at, std::size_t end);
    Term txt(std::size_t at, std::size_t end);
    Term uri(std::size_t at, std::size_t end);
    Term caa(std::size_t at, std::size_t end);
    Term loc(std::size_t at, std::size_t end);
    std::string character_string(std::size_t &at, std::size_t end) const;

    Term text(std::string_view chars) const;
    Term bytes(std::size_t at, std::size_t size) const;

    Term integer(std::int64_t value) const { return need(TermFactory(context_).integer(value)); }

    Term name_atom(std::string_view name) const { return need(TermFactory(context_).atom(AtomName{name})); }

    Term boolean(bool value) const { return need(TermFactory(context_).boolean(value)); }

    Term tuple(std::span<const Term> parts) const { return need(TermFactory(context_).tuple(parts)); }

    Term list(std::span<const Term> items) const { return need(TermFactory(context_).list(items)); }

    Term symbol(std::string_view name, std::uint32_t number) const {
        return name.empty() ? integer(number) : name_atom(name);
    }

    ProcessContext &context_;
    std::vector<std::byte> buffer_;
    std::uint16_t opcode_ = 0;
    // Whether the section being read has an OPT record already (RFC 6891 allows one).
    bool section_has_opt_ = false;
};

Term Decoder::text(std::string_view chars) const {
    std::vector<Term> items;
    items.reserve(chars.size());
    for (const auto c : chars) {
        items.push_back(integer(static_cast<unsigned char>(c)));
    }
    return list(items);
}

Term Decoder::bytes(std::size_t at, std::size_t size) const {
    return need(TermFactory(context_).binary(std::span(buffer_).subspan(at, size)));
}

// The labels of a name joined by dots, with dots and backslashes in labels escaped; "." for the root. A pointer goes
// to an earlier position past the header; the whole name stays below 255 bytes.
std::string Decoder::name(std::size_t &at, std::size_t end) const {
    std::string result;
    NameWalk walk{.position = at, .limit = end};
    while (!name_step(walk, result, at)) {
    }
    return result.empty() ? std::string(".") : result;
}

// One label or pointer of a name; true at its root label, whose end (or the first pointer's) `at` becomes.
bool Decoder::name_step(NameWalk &walk, std::string &result, std::size_t &at) const {
    if (walk.length >= 255) {
        format_error();
    }
    need_bytes(walk.position, 1, walk.limit);
    const auto label = byte(walk.position);
    if (label == 0) {
        if (!walk.jumped) {
            at = walk.position + 1;
        }
        return true;
    }
    if ((label & 0xC0U) == 0xC0U) {
        follow_pointer(walk, at);
    } else if ((label & 0xC0U) == 0) {
        append_label(walk, result, label);
    } else {
        format_error();
    }
    return false;
}

void Decoder::follow_pointer(NameWalk &walk, std::size_t &at) const {
    need_bytes(walk.position, 2, walk.limit);
    const auto pointer = u16(walk.position) & 0x3FFFU;
    if (pointer >= walk.position || pointer < 12) {
        format_error();
    }
    if (!walk.jumped) {
        at = walk.position + 2;
        walk.jumped = true;
    }
    walk.position = pointer;
    walk.limit = buffer_.size();
}

void Decoder::append_label(NameWalk &walk, std::string &result, std::size_t label) const {
    need_bytes(walk.position + 1, label, walk.limit);
    if (!result.empty()) {
        result.push_back('.');
    }
    for (std::size_t index = 0; index < label; ++index) {
        const auto c = static_cast<char>(byte(walk.position + 1 + index));
        if (c == '.' || c == '\\') {
            result.push_back('\\');
        }
        result.push_back(c);
    }
    walk.length += 1U + label;
    walk.position += 1U + label;
}

Term Decoder::header() {
    need_bytes(0, 12, buffer_.size());
    const auto flags = u16(2);
    opcode_ = static_cast<std::uint16_t>((flags >> 11U) & 0xFU);
    const auto bit = [&](unsigned shift) { return boolean(((flags >> shift) & 1U) != 0); };
    const std::array parts{name_atom("dns_header"),
                           integer(u16(0)),
                           bit(15),
                           symbol(name_of(OPCODES, opcode_), opcode_),
                           bit(10),
                           bit(9),
                           bit(8),
                           bit(7),
                           bit(6),
                           integer(flags & 0xFU)};
    return tuple(parts);
}

std::vector<Term> Decoder::queries(std::size_t &at, std::size_t count, bool &truncated) {
    std::vector<Term> items;
    for (; count > 0; --count) {
        if (at == buffer_.size()) {
            truncated = true;
            return items;
        }
        const auto domain = name(at, buffer_.size());
        need_bytes(at, 4, buffer_.size());
        const auto type = u16(at);
        const auto class_number = u16(at + 2);
        at += 4;
        const std::array parts{name_atom("dns_query"), text(domain), symbol(name_of(TYPES, type), type),
                               symbol(name_of(CLASSES, class_number), class_number), boolean(false)};
        items.push_back(tuple(parts));
    }
    return items;
}

std::vector<Term> Decoder::records(std::size_t &at, std::size_t count, bool &truncated) {
    std::vector<Term> items;
    section_has_opt_ = false;
    for (; count > 0; --count) {
        if (at == buffer_.size()) {
            truncated = true;
            return items;
        }
        items.push_back(record(at));
    }
    return items;
}

Term Decoder::record(std::size_t &at) {
    const auto start = at;
    const auto domain = name(at, buffer_.size());
    need_bytes(at, 10, buffer_.size());
    const auto type_number = static_cast<std::uint16_t>(u16(at));
    const auto class_number = static_cast<std::uint16_t>(u16(at + 2));
    const auto ttl = static_cast<std::int32_t>(u32(at + 4));
    const auto size = u16(at + 8);
    const auto data_at = at + 10;
    need_bytes(data_at, size, buffer_.size());
    at = data_at + size;
    if (type_number == 41) {
        if (section_has_opt_) {
            format_error();
        }
        section_has_opt_ = true;
        return option(domain, data_at - 6, size, class_number);
    }
    if (type_number == 250) {
        if (at != buffer_.size()) {
            format_error();
        }
        return signature(domain, Place{.start = start, .at = data_at, .size = size});
    }
    const auto type = symbol(name_of(TYPES, type_number), type_number);
    const std::array parts{name_atom("dns_rr"),
                           text(domain),
                           type,
                           symbol(name_of(CLASSES, class_number), class_number),
                           integer(0),
                           integer(std::max<std::int32_t>(0, ttl)),
                           data(Place{.at = data_at, .size = size},
                                Kind{.type = type, .class_number = class_number, .type_number = type_number}),
                           name_atom("undefined"),
                           list({}),
                           boolean(false)};
    return tuple(parts);
}

// #dns_rr_opt{}: the class is the UDP payload size, the TTL the extended rcode, version, DO bit and Z.
Term Decoder::option(std::string_view domain, std::size_t ttl_at, std::size_t size, std::uint16_t class_number) {
    const auto flags = u16(ttl_at + 2);
    const std::array parts{name_atom("dns_rr_opt"),
                           text(domain),
                           name_atom("opt"),
                           integer(class_number),
                           integer(byte(ttl_at)),
                           integer(byte(ttl_at + 1)),
                           integer(flags & 0x7FFFU),
                           bytes(ttl_at + 6, size),
                           boolean((flags & 0x8000U) != 0)};
    return tuple(parts);
}

// #dns_rr_tsig{}, the last record of a message.
Term Decoder::signature(std::string_view domain, const Place &place) {
    auto at = place.at;
    const auto end = place.at + place.size;
    const auto start = place.start;
    const auto algorithm = name(at, end);
    need_bytes(at, 10, end);
    const auto now = (static_cast<std::uint64_t>(u16(at)) << 32U) | u32(at + 2);
    const auto fudge = u16(at + 6);
    const auto mac_size = u16(at + 8);
    need_bytes(at + 10, mac_size + 6, end);
    const auto mac_at = at + 10;
    const auto after = mac_at + mac_size;
    const auto other_size = u16(after + 4);
    if (after + 6 + other_size != end) {
        format_error();
    }
    const std::array parts{name_atom("dns_rr_tsig"),
                           text(domain),
                           name_atom("tsig"),
                           integer(static_cast<std::int64_t>(start)),
                           text(algorithm),
                           integer(static_cast<std::int64_t>(now)),
                           integer(fudge),
                           bytes(mac_at, mac_size),
                           integer(u16(after)),
                           integer(u16(after + 2)),
                           bytes(after + 6, other_size)};
    return tuple(parts);
}

// The data of a record by its class and type; raw bytes for an unknown class or type, [] for an empty update.
Term Decoder::data(const Place &place, const Kind &kind) {
    const auto at = place.at;
    const auto size = place.size;
    const auto &type = kind.type;
    const auto class_number = kind.class_number;
    const auto type_number = kind.type_number;
    if (opcode_ == 5 && size == 0) {
        return list({});
    }
    const auto class_name = name_of(CLASSES, class_number);
    const auto type_name = name_of(TYPES, type_number);
    if (class_name == "in" && (type_name == "a" || type_name == "aaaa" || type_name == "wks")) {
        return address_data(at, at + size, type_name);
    }
    if (class_name.empty() || !type.is_atom()) {
        return bytes(at, size);
    }
    return typed_data(at, at + size, type_name);
}

Term Decoder::typed_data(std::size_t at, std::size_t end, std::string_view type) {
    constexpr std::array DOMAINS{"ns", "md", "mf", "cname", "mb", "mg", "mr", "ptr"};
    if (std::ranges::contains(DOMAINS, type)) {
        return domain_data(at, end);
    }
    using Read = Term (Decoder::*)(std::size_t, std::size_t);

    struct Reader {
        std::string_view type;
        Read read;
    };

    static constexpr std::array READERS{
        Reader{"soa", &Decoder::soa},     Reader{"minfo", &Decoder::pair_names}, Reader{"hinfo", &Decoder::hinfo},
        Reader{"naptr", &Decoder::naptr}, Reader{"txt", &Decoder::txt},          Reader{"spf", &Decoder::txt},
        Reader{"uri", &Decoder::uri},     Reader{"caa", &Decoder::caa},          Reader{"loc", &Decoder::loc},
    };
    if (const auto found = std::ranges::find(READERS, type, &Reader::type); found != READERS.end()) {
        return (this->*(found->read))(at, end);
    }
    if (type == "mx") {
        return priority_name(at, end, 1);
    }
    if (type == "srv") {
        return priority_name(at, end, 3);
    }
    return bytes(at, end - at);
}

Term Decoder::domain_data(std::size_t at, std::size_t end) const {
    auto position = at;
    const auto domain = name(position, end);
    if (position != end) {
        format_error();
    }
    return text(domain);
}

Term Decoder::address_data(std::size_t at, std::size_t end, std::string_view type) {
    const auto size = end - at;
    std::vector<Term> parts;
    if (type == "a" && size == 4) {
        for (std::size_t index = 0; index < 4; ++index) {
            parts.push_back(integer(byte(at + index)));
        }
        return tuple(parts);
    }
    if (type == "aaaa" && size == 16) {
        for (std::size_t index = 0; index < 16; index += 2) {
            parts.push_back(integer(u16(at + index)));
        }
        return tuple(parts);
    }
    if (type == "wks" && size >= 5) {
        const std::array address{integer(byte(at)), integer(byte(at + 1)), integer(byte(at + 2)),
                                 integer(byte(at + 3))};
        const std::array wks{tuple(address), integer(byte(at + 4)), bytes(at + 5, size - 5)};
        return tuple(wks);
    }
    format_error();
}

Term Decoder::soa(std::size_t at, std::size_t end) {
    auto position = at;
    const auto mname = name(position, end);
    const auto rname = name(position, end);
    if (position + 20 != end) {
        format_error();
    }
    const auto field = [&](std::size_t index, bool is_signed) {
        const auto value = u32(position + (index * 4));
        return integer(is_signed ? static_cast<std::int64_t>(static_cast<std::int32_t>(value)) : value);
    };
    const std::array parts{text(mname),    text(rname),    field(0, false), field(1, true),
                           field(2, true), field(3, true), field(4, false)};
    return tuple(parts);
}

Term Decoder::pair_names(std::size_t at, std::size_t end) {
    auto position = at;
    const auto first = name(position, end);
    const auto second = name(position, end);
    if (position != end) {
        format_error();
    }
    const std::array parts{text(first), text(second)};
    return tuple(parts);
}

std::string Decoder::character_string(std::size_t &at, std::size_t end) const {
    need_bytes(at, 1, end);
    const auto size = byte(at);
    need_bytes(at + 1, size, end);
    std::string result(reinterpret_cast<const char *>(buffer_.data() + at + 1), size);
    at += 1U + size;
    return result;
}

Term Decoder::hinfo(std::size_t at, std::size_t end) {
    auto position = at;
    const auto cpu = character_string(position, end);
    const auto os = character_string(position, end);
    if (position != end) {
        format_error();
    }
    const std::array parts{text(cpu), text(os)};
    return tuple(parts);
}

// MX {Prio, Domain} and SRV {Prio, Weight, Port, Domain}: 16-bit fields, then a name.
Term Decoder::priority_name(std::size_t at, std::size_t end, std::size_t fields) {
    need_bytes(at, fields * 2, end);
    std::vector<Term> parts;
    parts.reserve(fields + 1);
    for (std::size_t index = 0; index < fields; ++index) {
        parts.push_back(integer(u16(at + (index * 2))));
    }
    parts.push_back(domain_data(at + (fields * 2), end));
    return tuple(parts);
}

Term Decoder::naptr(std::size_t at, std::size_t end) {
    need_bytes(at, 4, end);
    auto position = at + 4;
    const auto flags = lower(character_string(position, end));
    const auto services = lower(character_string(position, end));
    const auto regexp = character_string(position, end);
    const auto regexp_chars = code_points(regexp);
    if (utf8(regexp_chars) != regexp) {
        format_error();
    }
    std::vector<Term> regexp_items;
    for (const auto code : regexp_chars) {
        regexp_items.push_back(integer(code));
    }
    const std::array parts{integer(u16(at)), integer(u16(at + 2)), text(flags),
                           text(services),   list(regexp_items),   domain_data(position, end)};
    return tuple(parts);
}

Term Decoder::txt(std::size_t at, std::size_t end) {
    std::vector<Term> strings;
    for (auto position = at; position < end;) {
        strings.push_back(text(character_string(position, end)));
    }
    return list(strings);
}

Term Decoder::uri(std::size_t at, std::size_t end) {
    if (end < at + 5) {
        format_error();
    }
    const std::string target(reinterpret_cast<const char *>(buffer_.data() + at + 4), end - at - 4);
    const std::array parts{integer(u16(at)), integer(u16(at + 2)), text(target)};
    return tuple(parts);
}

Term Decoder::caa(std::size_t at, std::size_t end) {
    need_bytes(at, 1, end);
    auto position = at + 1;
    const auto tag = lower(character_string(position, end));
    if (tag.empty() || tag.size() > 15) {
        format_error();
    }
    const std::string value(reinterpret_cast<const char *>(buffer_.data() + position), end - position);
    const std::array parts{integer(byte(at)), text(tag), text(value)};
    return tuple(parts);
}

// LOC (RFC 1876): {{Latitude, Longitude}, Altitude, Size, {HorizontalPrecision, VerticalPrecision}} in degrees and
// meters.
Term Decoder::loc(std::size_t at, std::size_t end) {
    if (end != at + 16 || byte(at) != 0) {
        format_error();
    }
    const auto size_of = [&](std::size_t index) {
        const auto field = byte(at + index);
        const auto base = field >> 4U;
        const auto exponent = field & 0xFU;
        if (base > 9 || exponent > 9) {
            format_error();
        }
        return std::round(base * std::pow(10.0, exponent)) / 100.0;
    };
    const auto real = [&](double value) { return need(TermFactory(context_).floating(value)); };
    const auto angle = [&](std::size_t offset) {
        return real((static_cast<double>(u32(at + offset)) - 2147483648.0) / 3600000.0);
    };
    const auto size = size_of(1);
    const auto horizontal = size_of(2);
    const auto vertical = size_of(3);
    const std::array position{angle(4), angle(8)};
    const std::array precision{real(horizontal), real(vertical)};
    const std::array parts{tuple(position), real((static_cast<double>(u32(at + 12)) - 10000000.0) / 100.0), real(size),
                           tuple(precision)};
    return tuple(parts);
}

Term Decoder::message() {
    const auto head = header();
    const auto truncated_flag = ((u16(2) >> 9U) & 1U) != 0;
    std::size_t at = 12;
    bool truncated = false;
    const auto qd = queries(at, u16(4), truncated);
    if (truncated && !truncated_flag) {
        format_error();
    }
    bool records_truncated = false;
    const auto an = records(at, u16(6), records_truncated);
    const auto ns = records(at, u16(8), records_truncated);
    const auto ar = records(at, u16(10), records_truncated);
    if (at != buffer_.size() || (records_truncated && !truncated_flag)) {
        format_error();
    }
    const std::array parts{name_atom("dns_rec"), head, list(qd), list(an), list(ns), list(ar)};
    return tuple(parts);
}
} // namespace

Term decode(ProcessContext &context, const Term &binary) {
    if (!binary.is_binary()) {
        bad_argument();
    }
    try {
        Decoder decoder(context, need(binary.binary_bytes()));
        return ok(context, decoder.message());
    } catch (const FormatError &) {
        return error(context, "formerr");
    }
}

namespace {
// A writer of one message with OTP's name compression: each encoded name suffix may be pointed to later.
class Encoder final {
  public:
    std::vector<std::uint8_t> message(const Term &record);

  private:
    void put16(std::uint32_t value) {
        out_.push_back(static_cast<std::uint8_t>(value >> 8U));
        out_.push_back(static_cast<std::uint8_t>(value));
    }

    void put32(std::uint32_t value) {
        put16(value >> 16U);
        put16(value & 0xFFFFU);
    }

    void header(const Term &header, const Term &record);
    void query(const Term &query);
    void resource(const Term &record);
    void name(const Term &domain);
    void data(const Term &data, std::string_view type);
    void domain_data(const Term &value, std::string_view type);
    void address_data(const Term &value, std::string_view type);
    void priority_data(const Term &value, std::string_view type);
    void txt_data(const Term &value, std::string_view type);
    void soa_data(const Term &value, std::string_view type);
    void pair_data(const Term &value, std::string_view type);
    void character_string(const Term &text);
    void iodata(const Term &data);

    std::vector<std::uint8_t> out_;
    std::map<std::vector<std::string>, std::uint16_t> names_;
};

// A record's tag and tuple size.
struct Shape {
    std::string_view tag;
    std::size_t size = 0;
};

// A record field; badarg unless the tuple has the record's tag and size.
Term field(const Term &record, Shape shape, std::size_t index) {
    if (!record.is_tuple() || record.tuple_size_unchecked() != shape.size ||
        !record.tuple_element_unchecked(0).is_atom() ||
        record.tuple_element_unchecked(0).atom_spelling_unchecked() != shape.tag) {
        bad_argument();
    }
    return record.tuple_element_unchecked(index);
}

std::uint32_t unsigned_value(const Term &term, std::uint32_t limit) {
    const auto value = term.integer_value();
    if (!value || *value < 0 || *value > limit) {
        bad_argument();
    }
    return static_cast<std::uint32_t>(*value);
}

// A boolean, or 0 and 1, as one bit.
std::uint32_t flag(const Term &term) {
    if (term.is_atom()) {
        const auto spelling = term.atom_spelling_unchecked();
        if (spelling == "true" || spelling == "false") {
            return spelling == "true" ? 1 : 0;
        }
        bad_argument();
    }
    return unsigned_value(term, 1);
}

// A symbol of a table or its raw number.
template <std::size_t N> std::uint16_t symbol_value(const std::array<TypeName, N> &table, const Term &term) {
    if (term.is_atom()) {
        const auto number = number_of(table, term.atom_spelling_unchecked());
        if (!number) {
            bad_argument();
        }
        return *number;
    }
    return static_cast<std::uint16_t>(unsigned_value(term, 0xFFFF));
}

// The labels of a name so far: the finished ones, the current one and whether a backslash escapes the next character.
struct Labels {
    std::vector<std::string> done;
    std::string label;
    bool escaped = false;

    void add(char c) {
        if (escaped) {
            label.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '.') {
            done.push_back(std::exchange(label, {}));
        } else {
            label.push_back(c);
        }
    }
};

// The labels of a name, with backslash escapes resolved; none for "" and ".".
std::vector<std::string> labels(const Term &domain) {
    Labels walk;
    auto rest = domain;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        walk.add(static_cast<char>(unsigned_value(rest.head_unchecked(), 255)));
    }
    auto &result = walk.done;
    auto &label = walk.label;
    if (!rest.is_nil()) {
        bad_argument();
    }
    if (!label.empty()) {
        result.push_back(label);
    }
    if (result.size() == 1 && result.front().empty()) {
        result.clear();
    }
    return result;
}

void Encoder::name(const Term &domain) {
    auto parts = labels(domain);
    const auto start = out_.size();
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const std::vector suffix(parts.begin() + static_cast<std::ptrdiff_t>(index), parts.end());
        if (const auto found = names_.find(suffix); found != names_.end()) {
            put16(0xC000U | found->second);
            return;
        }
        if (parts[index].empty() || parts[index].size() > 63) {
            bad_argument();
        }
        if (out_.size() < 0x4000) {
            names_.emplace(suffix, static_cast<std::uint16_t>(out_.size()));
        }
        out_.push_back(static_cast<std::uint8_t>(parts[index].size()));
        out_.insert(out_.end(), parts[index].begin(), parts[index].end());
    }
    out_.push_back(0);
    if (out_.size() - start > 255) {
        bad_argument();
    }
}

void Encoder::iodata(const Term &data) {
    if (data.is_binary()) {
        for (const auto b : need(data.binary_bytes())) {
            out_.push_back(std::to_integer<std::uint8_t>(b));
        }
        return;
    }
    auto rest = data;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto head = rest.head_unchecked();
        if (head.is_integer()) {
            out_.push_back(static_cast<std::uint8_t>(unsigned_value(head, 255)));
        } else {
            iodata(head);
        }
    }
    if (!rest.is_nil() && !rest.is_binary()) {
        bad_argument();
    }
    if (rest.is_binary()) {
        iodata(rest);
    }
}

void Encoder::character_string(const Term &text) {
    const auto size_at = out_.size();
    out_.push_back(0);
    iodata(text);
    const auto size = out_.size() - size_at - 1;
    if (size > 255) {
        bad_argument();
    }
    out_[size_at] = static_cast<std::uint8_t>(size);
}

void Encoder::header(const Term &header, const Term &record) {
    put16(unsigned_value(field(header, Shape{"dns_header", 10}, 1), 0xFFFF));
    const auto opcode = symbol_value(OPCODES, header.tuple_element_unchecked(3));
    out_.push_back(static_cast<std::uint8_t>((flag(header.tuple_element_unchecked(2)) << 7U) | ((opcode & 0xFU) << 3U) |
                                             (flag(header.tuple_element_unchecked(4)) << 2U) |
                                             (flag(header.tuple_element_unchecked(5)) << 1U) |
                                             flag(header.tuple_element_unchecked(6))));
    out_.push_back(static_cast<std::uint8_t>((flag(header.tuple_element_unchecked(7)) << 7U) |
                                             (flag(header.tuple_element_unchecked(8)) << 6U) |
                                             (unsigned_value(header.tuple_element_unchecked(9), 15))));
    for (std::size_t section = 2; section <= 5; ++section) {
        std::size_t count = 0;
        for (auto rest = record.tuple_element_unchecked(section); rest.is_cons(); rest = rest.tail_unchecked()) {
            ++count;
        }
        put16(static_cast<std::uint32_t>(count));
    }
}

void Encoder::query(const Term &query) {
    name(field(query, Shape{"dns_query", 5}, 1));
    put16(symbol_value(TYPES, query.tuple_element_unchecked(2)));
    put16(symbol_value(CLASSES, query.tuple_element_unchecked(3)));
}

void Encoder::resource(const Term &record) {
    const auto tag =
        record.is_tuple() && record.tuple_size_unchecked() > 0 ? record.tuple_element_unchecked(0) : record;
    if (tag.is_atom() && tag.atom_spelling_unchecked() == "dns_rr_opt") {
        name(field(record, Shape{"dns_rr_opt", 9}, 1));
        put16(41);
        put16(unsigned_value(record.tuple_element_unchecked(3), 0xFFFF));
        out_.push_back(static_cast<std::uint8_t>(unsigned_value(record.tuple_element_unchecked(4), 255)));
        out_.push_back(static_cast<std::uint8_t>(unsigned_value(record.tuple_element_unchecked(5), 255)));
        put16((flag(record.tuple_element_unchecked(8)) << 15U) |
              unsigned_value(record.tuple_element_unchecked(6), 0x7FFF));
        const auto size_at = out_.size();
        put16(0);
        iodata(record.tuple_element_unchecked(7));
        const auto size = out_.size() - size_at - 2;
        out_[size_at] = static_cast<std::uint8_t>(size >> 8U);
        out_[size_at + 1] = static_cast<std::uint8_t>(size);
        return;
    }
    name(field(record, Shape{"dns_rr", 10}, 1));
    const auto type = record.tuple_element_unchecked(2);
    const auto type_number = symbol_value(TYPES, type);
    const auto class_term = record.tuple_element_unchecked(3);
    put16(type_number);
    put16(symbol_value(CLASSES, class_term));
    const auto ttl = record.tuple_element_unchecked(5).integer_value();
    if (!ttl) {
        bad_argument();
    }
    put32(static_cast<std::uint32_t>(*ttl));
    const auto size_at = out_.size();
    put16(0);
    const auto raw = !class_term.is_atom() || !type.is_atom();
    data(record.tuple_element_unchecked(6), raw ? std::string_view() : type.atom_spelling_unchecked());
    const auto size = out_.size() - size_at - 2;
    out_[size_at] = static_cast<std::uint8_t>(size >> 8U);
    out_[size_at + 1] = static_cast<std::uint8_t>(size);
}

// The data of a record by its type: addresses, names, priorities and strings; iodata as it is for raw types.
void Encoder::data(const Term &value, std::string_view type) {
    using Write = void (Encoder::*)(const Term &, std::string_view);

    struct Writer {
        std::string_view type;
        Write write;
    };

    static constexpr std::array WRITERS{
        Writer{"ns", &Encoder::domain_data},   Writer{"md", &Encoder::domain_data},
        Writer{"mf", &Encoder::domain_data},   Writer{"cname", &Encoder::domain_data},
        Writer{"mb", &Encoder::domain_data},   Writer{"mg", &Encoder::domain_data},
        Writer{"mr", &Encoder::domain_data},   Writer{"ptr", &Encoder::domain_data},
        Writer{"a", &Encoder::address_data},   Writer{"aaaa", &Encoder::address_data},
        Writer{"mx", &Encoder::priority_data}, Writer{"srv", &Encoder::priority_data},
        Writer{"txt", &Encoder::txt_data},     Writer{"spf", &Encoder::txt_data},
        Writer{"soa", &Encoder::soa_data},     Writer{"hinfo", &Encoder::pair_data},
        Writer{"minfo", &Encoder::pair_data},
    };
    const auto found = std::ranges::find(WRITERS, type, &Writer::type);
    if (found == WRITERS.end()) {
        iodata(value);
    } else {
        (this->*(found->write))(value, type);
    }
}

// A tuple of `size` elements; badarg for anything else.
void tuple_of(const Term &value, std::size_t size) {
    if (!value.is_tuple() || value.tuple_size_unchecked() != size) {
        bad_argument();
    }
}

void Encoder::domain_data(const Term &value, std::string_view) { name(value); }

void Encoder::address_data(const Term &value, std::string_view type) {
    const bool four = type == "a";
    tuple_of(value, four ? 4 : 8);
    for (std::size_t index = 0; index < value.tuple_size_unchecked(); ++index) {
        const auto part = unsigned_value(value.tuple_element_unchecked(index), four ? 255 : 0xFFFF);
        if (four) {
            out_.push_back(static_cast<std::uint8_t>(part));
        } else {
            put16(part);
        }
    }
}

// MX {Prio, Name} and SRV {Prio, Weight, Port, Name}.
void Encoder::priority_data(const Term &value, std::string_view type) {
    const std::size_t fields = type == "mx" ? 1 : 3;
    tuple_of(value, fields + 1);
    for (std::size_t index = 0; index < fields; ++index) {
        put16(unsigned_value(value.tuple_element_unchecked(index), 0xFFFF));
    }
    name(value.tuple_element_unchecked(fields));
}

void Encoder::txt_data(const Term &value, std::string_view) {
    for (auto rest = value; rest.is_cons(); rest = rest.tail_unchecked()) {
        character_string(rest.head_unchecked());
    }
}

void Encoder::soa_data(const Term &value, std::string_view) {
    tuple_of(value, 7);
    name(value.tuple_element_unchecked(0));
    name(value.tuple_element_unchecked(1));
    for (std::size_t index = 2; index < 7; ++index) {
        const auto number = value.tuple_element_unchecked(index).integer_value();
        if (!number) {
            bad_argument();
        }
        put32(static_cast<std::uint32_t>(*number));
    }
}

// HINFO {Cpu, Os} as character strings, MINFO {Rm, Em} as names.
void Encoder::pair_data(const Term &value, std::string_view type) {
    tuple_of(value, 2);
    for (std::size_t index = 0; index < 2; ++index) {
        if (type == "hinfo") {
            character_string(value.tuple_element_unchecked(index));
        } else {
            name(value.tuple_element_unchecked(index));
        }
    }
}

std::vector<std::uint8_t> Encoder::message(const Term &record) {
    header(field(record, Shape{"dns_rec", 6}, 1), record);
    for (auto rest = record.tuple_element_unchecked(2); rest.is_cons(); rest = rest.tail_unchecked()) {
        query(rest.head_unchecked());
    }
    for (std::size_t section = 3; section <= 5; ++section) {
        for (auto rest = record.tuple_element_unchecked(section); rest.is_cons(); rest = rest.tail_unchecked()) {
            resource(rest.head_unchecked());
        }
    }
    return std::move(out_);
}
} // namespace

Term encode(ProcessContext &context, const Term &record) {
    Encoder encoder;
    const auto bytes = encoder.message(record);
    return need(TermFactory(context).binary(std::as_bytes(std::span(bytes))));
}
} // namespace clause::runtime::builtins::net::dns
