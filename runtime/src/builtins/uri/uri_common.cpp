#include "../lists/list_walk.hpp"
#include "../text.hpp"
#include "../unicode/chardata.hpp"
#include "uri.hpp"
#include <algorithm>
#include <array>

// uri_string's shared pieces (plan 21 step LIB5G): RFC 3986's character classes, OTP's strict address forms for
// hosts, conversions between list and binary text, percent coding and the error tuples.
namespace clause::runtime::builtins::uri {
namespace {
using lists::no_clause;

bool in(std::u32string_view set, char32_t c) { return set.find(c) != std::u32string_view::npos; }

// The value of a nonempty run of decimal digits; none for anything else.
std::optional<std::uint32_t> decimal(std::u32string_view digits) {
    if (digits.empty() || !std::ranges::all_of(digits, is_digit)) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (const auto c : digits) {
        value = value * 10 + (c - '0');
    }
    return value;
}

// Whether digits spell a decimal number up to 65535 (inet's scope id).
bool decimal16(std::u32string_view digits) {
    std::uint32_t value = 0;
    for (const auto c : digits) {
        if (!is_digit(c)) {
            return false;
        }
        value = value * 10 + (c - '0');
        if (value > 0xFFFF) {
            return false;
        }
    }
    return true;
}

// One field of a strict IPv4 address: "0", or at most three characters before a dot or the end, a sign allowed as
// list_to_integer/2 allows it, without a leading zero; where it ends.
std::optional<std::size_t> ipv4_field(std::u32string_view chars, std::size_t at) {
    const auto end = std::min(chars.find('.', at), chars.size());
    if (end == at + 1 && chars[at] == '0') {
        return end;
    }
    if (end == at || end - at > 3 || chars[at] == '0') {
        return std::nullopt;
    }
    const auto value = decimal(chars.substr(chars[at] == '+' ? at + 1 : at, end - at - (chars[at] == '+' ? 1 : 0)));
    return value && *value < 256 ? std::optional{end} : std::nullopt;
}

// One to four hex digits at `at`: their value and where they end; none without a digit.
struct HexField {
    std::uint32_t value;
    std::size_t end;
};

std::optional<HexField> hex_field(std::u32string_view chars, std::size_t at) {
    std::uint32_t value = 0;
    auto end = at;
    while (end < chars.size() && end - at < 4 && is_hex(chars[end])) {
        const auto c = chars[end];
        value = value * 16 + (is_digit(c) ? c - '0' : (c | 0x20U) - 'a' + 10);
        ++end;
    }
    return end == at ? std::nullopt : std::optional{HexField{.value = value, .end = end}};
}

// inet's strict IPv6 parser: fields before and after "::", an IPv4 tail, a %scope for fe80:: and ff02::.
class Ipv6 final {
  public:
    explicit Ipv6(std::u32string_view chars) : chars_(chars) {}

    bool valid() {
        if (chars_ == U"::") {
            return true;
        }
        if (chars_.starts_with(U"::")) {
            return after(2);
        }
        return before(0);
    }

  private:
    std::u32string_view rest(const HexField &field) const { return chars_.substr(field.end); }

    // A field before "::" at `at`, with before_.size() fields so far.
    bool before(std::size_t at) {
        const auto field = hex_field(chars_, at);
        if (!field) {
            return false;
        }
        const auto n = before_.size();
        const auto tail = rest(*field);
        before_.push_back(field->value);
        if (n == 7) {
            return tail.empty() || (tail.starts_with(U"%") && scoped(tail.substr(1), 8));
        }
        if (tail.starts_with(U"::")) {
            return gap(tail, field->end, n);
        }
        if (tail.starts_with(U":")) {
            return n <= 6 && before(field->end + 1);
        }
        return n == 6 && tail.starts_with(U".") && ipv4_address(chars_.substr(at));
    }

    // "::" after `n` + 1 fields ending at `end`: a scope, the end, or fields after it (a sixth field makes the second
    // colon start a field, which fails).
    bool gap(std::u32string_view tail, std::size_t end, std::size_t n) {
        if (tail.starts_with(U"::%")) {
            return n <= 6 && scoped(tail.substr(3), n + 1);
        }
        if (tail == U"::") {
            return n <= 6;
        }
        return n <= 5 ? after(end + 2) : n <= 6 && before(end + 1);
    }

    // A field after "::" at `at`.
    bool after(std::size_t at) {
        const auto field = hex_field(chars_, at);
        if (!field) {
            return false;
        }
        const auto n = before_.size() + after_.size();
        const auto tail = rest(*field);
        if (tail.starts_with(U".")) {
            return n <= 5 && ipv4_address(chars_.substr(at));
        }
        after_.push_back(field->value);
        if (tail.starts_with(U"%")) {
            return n <= 6 && scoped(tail.substr(1), n + 1);
        }
        if (tail.empty()) {
            return n <= 6;
        }
        return tail.starts_with(U":") && n <= 5 && after(field->end + 1);
    }

    // A link-local or multicast address with its %scope ("0" and a decimal id up to 65535, anything else 0).
    bool scoped(std::u32string_view scope, std::size_t n) const {
        if (scope.starts_with(U"0") && !decimal16(scope.substr(1))) {
            return false;
        }
        std::vector<std::uint32_t> address(before_);
        address.insert(address.end(), 8 - n, 0);
        address.insert(address.end(), after_.begin(), after_.end());
        return address.size() >= 2 && (address[0] == 0xFE80 || address[0] == 0xFF02) && address[1] == 0;
    }

    std::u32string_view chars_;
    std::vector<std::uint32_t> before_;
    std::vector<std::uint32_t> after_;
};
} // namespace

[[noreturn]] void fail(ProcessContext &context, std::string_view reason, const Term &term) {
    static_cast<void>(context);
    throw Failure{.reason = reason, .term = term};
}

[[noreturn]] void fail(ProcessContext &context, std::string_view reason, std::span<const std::byte> bytes) {
    fail(context, reason, binary_of(context, bytes));
}

[[noreturn]] void invalid_char(ProcessContext &context, char32_t code) {
    fail(context, "invalid_uri", codes_list(context, std::u32string(1, code)));
}

Term error_tuple(ProcessContext &context, const Failure &failure) {
    const std::array items{atom(context, "error"), atom(context, failure.reason), failure.term};
    return need(TermFactory(context).tuple(items));
}

[[noreturn]] void throw_failure(ProcessContext &context, const Failure &failure) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_throw, .payload = error_tuple(context, failure).word()};
}

bool is_alpha(char32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

bool is_digit(char32_t c) { return c >= '0' && c <= '9'; }

bool is_hex(char32_t c) { return is_digit(c) || ((c | 0x20U) >= 'a' && (c | 0x20U) <= 'f' && c < 128); }

bool is_unreserved(char32_t c) { return in(U"-._~", c) || is_alpha(c) || is_digit(c); }

bool is_sub_delim(char32_t c) { return in(U"!$&'()*+,;=", c); }

bool is_reserved(char32_t c) { return in(U":/?#[]@", c) || is_sub_delim(c); }

bool is_scheme(char32_t c) { return in(U"+-.", c) || is_alpha(c) || is_digit(c); }

bool is_userinfo(char32_t c) { return in(U"%:", c) || is_unreserved(c) || is_sub_delim(c); }

bool is_reg_name(char32_t c) { return c == '%' || is_unreserved(c) || is_sub_delim(c); }

bool is_pchar(char32_t c) { return in(U"%:@", c) || is_unreserved(c) || is_sub_delim(c); }

bool is_segment_nz_nc(char32_t c) { return in(U"%@", c) || is_unreserved(c) || is_sub_delim(c); }

bool is_query(char32_t c) { return in(U"/?", c) || is_pchar(c); }

bool is_fragment(char32_t c) { return in(U"/?", c) || is_pchar(c); }

bool is_ipv6(char32_t c) { return in(U":.", c) || is_hex(c); }

bool is_host(char32_t c) { return c == ':' || is_unreserved(c) || is_sub_delim(c); }

bool is_path(char32_t c) { return c == '/' || is_pchar(c); }

bool ipv4_address(std::u32string_view chars) {
    std::size_t at = 0;
    for (int field = 0; field < 4; ++field) {
        const auto end = ipv4_field(chars, at);
        if (!end) {
            return false;
        }
        if (field == 3) {
            return *end == chars.size();
        }
        if (*end + 1 >= chars.size()) {
            return false;
        }
        at = *end + 1;
    }
    return false;
}

bool ipv6_address(std::u32string_view chars) { return Ipv6(chars).valid(); }

Term binary_of(ProcessContext &context, std::span<const std::byte> bytes) {
    return need(TermFactory(context).binary(bytes));
}

Term binary_of(ProcessContext &context, std::string_view text) {
    return binary_of(context, std::as_bytes(std::span(text)));
}

Bytes bytes_of(const Term &binary) { return need(binary.binary_bytes()); }

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

std::optional<char32_t> code_at(std::span<const std::byte> bytes, std::size_t &at) { return next_utf8(bytes, at); }

std::optional<Bytes> chardata_bytes(ProcessContext &context, const Term &data) {
    const auto codes = chardata_codes(context, data);
    if (!codes) {
        return std::nullopt;
    }
    std::string text;
    for (const auto code : *codes) {
        encode(code, text);
    }
    const auto bytes = std::as_bytes(std::span(text));
    return Bytes(bytes.begin(), bytes.end());
}

std::optional<std::u32string> chardata_codes(ProcessContext &context, const Term &data) {
    auto converted = unicode::convert(context, data, unicode::Encoding{}, false);
    if (converted.outcome != unicode::Conversion::Outcome::done) {
        return std::nullopt;
    }
    return std::move(converted.codes);
}

std::optional<Term> utf8_list(ProcessContext &context, std::span<const std::byte> bytes) {
    const auto codes = decode_utf8(bytes);
    return codes ? std::optional{codes_list(context, *codes)} : std::nullopt;
}

Term codes_list(ProcessContext &context, std::u32string_view codes) {
    std::vector<Term> items;
    items.reserve(codes.size());
    for (const auto code : codes) {
        items.push_back(need(TermFactory(context).integer(static_cast<std::int64_t>(code))));
    }
    return need(TermFactory(context).list(items));
}

void percent_encode(std::string &out, std::span<const std::byte> bytes) {
    constexpr std::string_view HEX = "0123456789ABCDEF";
    for (const auto b : bytes) {
        const auto value = std::to_integer<unsigned>(b);
        out.push_back('%');
        out.push_back(HEX[value >> 4U]);
        out.push_back(HEX[value & 15U]);
    }
}

std::optional<std::uint8_t> escape_value(std::uint8_t high, std::uint8_t low) {
    const auto value = [](std::uint8_t c) {
        return is_digit(c) ? c - '0' : ((c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10);
    };
    if (!is_hex(high) || !is_hex(low)) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value(high) * 16 + value(low));
}

Bytes text_bytes(ProcessContext &context, const Term &text) {
    if (text.is_binary()) {
        return bytes_of(text);
    }
    if (!text.is_list()) {
        no_clause();
    }
    const auto bytes = chardata_bytes(context, text);
    if (!bytes) {
        no_clause();
    }
    return *bytes;
}

std::string encode_bytes(ProcessContext &context, std::span<const std::byte> bytes, bool (*keep)(char32_t),
                         std::u32string_view safe) {
    std::string out;
    for (std::size_t at = 0; at < bytes.size();) {
        const auto start = at;
        const auto code = code_at(bytes, at);
        if (!code) {
            fail(context, "invalid_input", bytes.subspan(start));
        }
        if (keep(*code) || in(safe, *code)) {
            out.push_back(static_cast<char>(*code & 0xFFU));
        } else {
            percent_encode(out, bytes.subspan(start, at - start));
        }
    }
    return out;
}

namespace {
// A conversion's result, or OTP's {error | incomplete, Converted, Rest} tuple with Converted in the result's form.
Term converted_result(ProcessContext &context, const unicode::Conversion &converted, bool binary) {
    std::string text;
    for (const auto code : converted.codes) {
        encode(code, text);
    }
    auto done = binary ? binary_of(context, text) : codes_list(context, converted.codes);
    if (converted.outcome == unicode::Conversion::Outcome::done) {
        return done;
    }
    const auto tag = converted.outcome == unicode::Conversion::Outcome::error ? "error" : "incomplete";
    const std::array items{atom(context, tag), done, converted.rest};
    return need(TermFactory(context).tuple(items));
}
} // namespace

Term characters_to_list(ProcessContext &context, const Term &data) {
    return converted_result(context, unicode::convert(context, data, unicode::Encoding{}, false), false);
}

Term characters_to_binary(ProcessContext &context, const Term &data) {
    return converted_result(context, unicode::convert(context, data, unicode::Encoding{}, false), true);
}

Term encoded_as(ProcessContext &context, const Term &text, const std::string &bytes) {
    const auto binary = binary_of(context, bytes);
    return text.is_binary() ? binary : characters_to_list(context, binary);
}
} // namespace clause::runtime::builtins::uri
