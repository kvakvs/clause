#include "../lists/list_walk.hpp"
#include "uri_map.hpp"
#include <algorithm>
#include <array>

// uri_string:parse/1 (plan 21 step LIB5G, docs/builtins.md#uri-string): one walker over RFC 3986's grammar driven by
// a delimiter table. OTP's results include where its parser stopped, so the walk keeps OTP's order: a URI with a
// scheme first (a failure after the scheme's colon is final), else a relative reference; userinfo@host when the
// userinfo walk succeeds, else the authority as a host; an IPv4 host that is not an address walked again as a name.
namespace clause::runtime::builtins::uri {
namespace {
using lists::no_clause;

// The component kinds the walk reads.
enum class Kind : std::uint8_t { segment, segment_nz_nc, query, fragment, port, reg_name, ipv6_end, userinfo };

// What a delimiter starts.
enum class Part : std::uint8_t { none, segment, path, port, host, query, fragment };

constexpr unsigned bit(Kind kind) { return 1U << static_cast<unsigned>(kind); }

// The delimiter table: the character, the kinds it delimits and the component it starts.
struct Delimiter {
    char32_t c;
    unsigned kinds;
    Part part;
};

constexpr unsigned ALL_KINDS = 0xFFU;

constexpr std::array<Delimiter, 6> DELIMITERS{{
    {'/', bit(Kind::segment) | bit(Kind::segment_nz_nc), Part::segment},
    {'/', bit(Kind::port) | bit(Kind::reg_name) | bit(Kind::ipv6_end), Part::path},
    {':', bit(Kind::reg_name) | bit(Kind::ipv6_end), Part::port},
    {'@', bit(Kind::userinfo), Part::host},
    {'?', ALL_KINDS & ~(bit(Kind::query) | bit(Kind::fragment) | bit(Kind::userinfo)), Part::query},
    {'#', ALL_KINDS & ~(bit(Kind::fragment) | bit(Kind::userinfo)), Part::fragment},
}};

Part delimiter(Kind kind, char32_t c) {
    const auto found = std::ranges::find_if(
        DELIMITERS, [&](const Delimiter &entry) { return entry.c == c && (entry.kinds & bit(kind)) != 0; });
    return found == DELIMITERS.end() ? Part::none : found->part;
}

bool allowed(Kind kind, char32_t c) {
    constexpr std::array<bool (*)(char32_t), 8> CLASSES{is_pchar, is_segment_nz_nc, is_query, is_fragment,
                                                        is_digit, is_reg_name,      is_ipv6,  is_userinfo};
    return CLASSES.at(static_cast<std::size_t>(kind))(c);
}

// A failure after the scheme's colon: the whole parse fails with it.
struct Final {
    Failure failure;
};

class Parser final {
  public:
    Parser(ProcessContext &context, Bytes input)
        : context_(context), input_(std::move(input)), empty_(binary_of(context, std::span<const std::byte>{})) {}

    Uri run() {
        if (input_.empty()) {
            Uri uri;
            uri.path = empty_;
            return uri;
        }
        try {
            return scheme_start();
        } catch (const Final &final) {
            throw final.failure;
        } catch (const Failure &) {
            return relative_part();
        }
    }

  private:
    // The input after a component's end, and the components found from it.
    struct Step {
        std::size_t tail;
        Uri uri;
    };

    std::size_t end() const { return input_.size(); }

    bool at(std::size_t pos, char c) const { return pos < end() && input_[pos] == static_cast<std::byte>(c); }

    // The code point at `pos`; function_clause at invalid UTF-8, as OTP's <<C/utf8, ...>> clauses.
    char32_t code(std::size_t pos, std::size_t &next) const {
        next = pos;
        const auto c = code_at(input_, next);
        if (!c) {
            no_clause();
        }
        return *c;
    }

    Term part(std::size_t from, std::size_t to) const {
        return binary_of(context_, std::span(input_).subspan(from, to - from));
    }

    // OTP's parsed/3: the input from `from` up to the delimiter `tail` follows; at the end, without a trailing
    // delimiter of `delimiters`.
    std::size_t parsed_end(std::size_t from, std::string_view delimiters, std::size_t tail) const {
        if (tail < end()) {
            return tail - 1;
        }
        if (from < end() && delimiters.find(static_cast<char>(input_.back())) != std::string_view::npos) {
            return end() - 1;
        }
        return end();
    }

    Term parsed(std::size_t from, std::string_view delimiters, std::size_t tail) const {
        return part(from, std::max(from, parsed_end(from, delimiters, tail)));
    }

    Uri scheme_start() {
        std::size_t next = 0;
        const auto c = code(0, next);
        if (!is_alpha(c)) {
            invalid_char(context_, c);
        }
        auto [tail, uri] = scheme(next, Uri{});
        with_path(uri);
        uri.scheme = part(0, std::max(next, parsed_end(next, ":", tail)));
        return uri;
    }

    Step scheme(std::size_t pos, const Uri &uri) {
        while (true) {
            if (at(pos, ':')) {
                try {
                    return {pos + 1, hier(pos + 1, uri).uri};
                } catch (const Failure &failure) {
                    throw Final{failure};
                }
            }
            if (pos == end()) {
                fail(context_, "invalid_uri", std::span<const std::byte>{});
            }
            std::size_t next = 0;
            const auto c = code(pos, next);
            if (!is_scheme(c)) {
                invalid_char(context_, c);
            }
            pos = next;
        }
    }

    Step hier(std::size_t pos, const Uri &uri) {
        if (at(pos, '/') && at(pos + 1, '/')) {
            return authority(pos + 2, uri);
        }
        if (pos == end()) {
            return {end(), uri};
        }
        return component(pos, uri, Kind::segment, is_pchar);
    }

    // After "scheme:" or at a relative reference's start: a path, a query or a fragment.
    Step component(std::size_t pos, const Uri &uri, Kind kind, bool (*first)(char32_t)) {
        if (at(pos, '/')) {
            return path_part(pos, pos + 1, Kind::segment, uri);
        }
        if (at(pos, '?')) {
            return delimited(Part::query, pos + 1, uri);
        }
        if (at(pos, '#')) {
            return delimited(Part::fragment, pos + 1, uri);
        }
        std::size_t next = 0;
        const auto c = code(pos, next);
        if (!first(c)) {
            invalid_char(context_, c);
        }
        return path_part(pos, next, kind, uri);
    }

    Uri relative_part() {
        if (at(0, '/') && at(1, '/')) {
            auto uri = authority(2, Uri{}).uri;
            with_path(uri);
            return uri;
        }
        auto uri = component(0, Uri{}, Kind::segment_nz_nc, is_segment_nz_nc).uri;
        if (at(0, '?') || at(0, '#')) {
            with_path(uri);
        }
        return uri;
    }

    // userinfo@host when the userinfo walk succeeds, else the authority as a host from its start.
    Step authority(std::size_t pos, const Uri &uri) {
        try {
            auto [tail, found] = walk(Kind::userinfo, pos, uri);
            found.userinfo = parsed(pos, "?#@", tail);
            return {pos, found};
        } catch (const Failure &) {
            return host_part(pos, uri);
        }
    }

    Step host_part(std::size_t pos, const Uri &uri) {
        auto [tail, found] = host(pos, uri);
        found.host = without_brackets(pos, std::max(pos, parsed_end(pos, ":?#/", tail)));
        return {pos, found};
    }

    Term without_brackets(std::size_t from, std::size_t to) const {
        if (from < to && at(from, '[')) {
            return at(to - 1, ']') && to - 1 > from ? part(from + 1, to - 1) : part(from + 1, to);
        }
        return part(from, to);
    }

    // A bracketed IPv6 address, an IPv4 address (a registered name when it is not one) or a registered name.
    Step host(std::size_t pos, const Uri &uri) {
        if (at(pos, '[')) {
            return ipv6(pos + 1, uri);
        }
        if (pos < end() && is_digit(std::to_integer<char32_t>(input_[pos]))) {
            try {
                return ipv4(pos, uri);
            } catch (const Failure &) {
                return walk(Kind::reg_name, pos, uri);
            }
        }
        return walk(Kind::reg_name, pos, uri);
    }

    Step ipv4(std::size_t pos, const Uri &uri) {
        std::u32string address;
        while (pos < end()) {
            std::size_t next = 0;
            const auto c = code(pos, next);
            const auto part = delimiter(Kind::reg_name, c);
            if (part != Part::none) {
                checked(address, false);
                return delimited(part, next, uri);
            }
            if (c != '.' && !is_digit(c)) {
                invalid_char(context_, c);
            }
            address.push_back(c);
            pos = next;
        }
        checked(address, false);
        return {end(), uri};
    }

    Step ipv6(std::size_t pos, const Uri &uri) {
        std::u32string address;
        while (!at(pos, ']')) {
            if (pos == end()) {
                fail(context_, "invalid_uri", std::span<const std::byte>{});
            }
            std::size_t next = 0;
            const auto c = code(pos, next);
            if (!is_ipv6(c)) {
                invalid_char(context_, c);
            }
            address.push_back(c);
            pos = next;
        }
        checked(address, true);
        return walk(Kind::ipv6_end, pos + 1, uri);
    }

    // {error, invalid_uri, Address} unless the characters are a strict address of the family.
    void checked(const std::u32string &address, bool six) const {
        if (!(six ? ipv6_address(address) : ipv4_address(address))) {
            fail(context_, "invalid_uri", codes_list(context_, address));
        }
    }

    // The characters of one component kind until a delimiter hands over to the next component.
    Step walk(Kind kind, std::size_t pos, const Uri &uri) {
        while (pos < end()) {
            std::size_t next = 0;
            const auto c = code(pos, next);
            const auto part = delimiter(kind, c);
            if (part == Part::segment) {
                kind = Kind::segment;
            } else if (part != Part::none) {
                return delimited(part, next, uri);
            } else if (!allowed(kind, c)) {
                invalid_char(context_, c);
            }
            pos = next;
        }
        if (kind == Kind::userinfo) {
            fail(context_, "invalid_uri", std::span<const std::byte>{});
        }
        return {end(), uri};
    }

    // The component after a delimiter, added to the components.
    Step delimited(Part part, std::size_t pos, const Uri &uri) {
        switch (part) {
        case Part::path:
            return path_part(pos - 1, pos, Kind::segment, uri);
        case Part::host:
            return host_part(pos, uri);
        case Part::port:
            return port_part(pos, uri);
        default:
            break;
        }
        const bool query = part == Part::query;
        auto [tail, found] = walk(query ? Kind::query : Kind::fragment, pos, uri);
        (query ? found.query : found.fragment) = parsed(pos, "#", tail);
        return {pos, found};
    }

    Step port_part(std::size_t pos, const Uri &uri) {
        auto [tail, found] = walk(Kind::port, pos, uri);
        const auto digits = bytes_of(parsed(pos, ":?#/", tail));
        if (digits.empty()) {
            found.port = atom(context_, "undefined"_atom);
        } else if (digits.size() <= 5) {
            std::string text(reinterpret_cast<const char *>(digits.data()), digits.size());
            found.port = need(TermFactory(context_).integer(std::stoll(text)));
        } else {
            fail(context_, "invalid_uri", digits);
        }
        return {pos, found};
    }

    // A path from `first` (its first character) through the walk from `pos`.
    Step path_part(std::size_t first, std::size_t pos, Kind kind, const Uri &uri) {
        auto [tail, found] = walk(kind, pos, uri);
        found.path = part(first, std::max(pos, parsed_end(pos, "?#", tail)));
        return {pos, found};
    }

    void with_path(Uri &uri) const {
        if (!uri.path) {
            uri.path = empty_;
        }
    }

    ProcessContext &context_;
    Bytes input_;
    // The empty binary of a missing path.
    Term empty_;
};

Term parse1(ProcessContext &context, const Term &string) { return parse(context, string); }

constexpr std::array URI_PARSE_BUILTINS{typed_entry<parse1>("uri_string", "parse")};
} // namespace

// parse(URIString): a map of binaries, or of lists for a list.
Term parse(ProcessContext &context, const Term &string) {
    if (!string.is_binary() && !string.is_list()) {
        no_clause();
    }
    const auto bytes = string.is_binary() ? std::optional{bytes_of(string)} : chardata_bytes(context, string);
    if (!bytes) {
        no_clause();
    }
    try {
        return Parser(context, *bytes).run().term(context, string.is_list());
    } catch (const Failure &failure) {
        return error_tuple(context, failure);
    }
}

Term Uri::term(ProcessContext &context, bool lists) const {
    std::vector<std::pair<Term, Term>> entries;
    const auto add = [&](std::string_view key, const std::optional<Term> &value) {
        if (!value) {
            return;
        }
        const bool text = lists && value->is_binary();
        entries.emplace_back(atom(context, key), text ? characters_to_list(context, *value) : *value);
    };
    add("scheme", scheme);
    add("userinfo", userinfo);
    add("host", host);
    add("port", port);
    add("path", path);
    add("query", query);
    add("fragment", fragment);
    return need(TermFactory(context).map(entries));
}
} // namespace clause::runtime::builtins::uri

namespace clause::runtime {
std::span<const BuiltinEntry> uri_parse_builtins() noexcept { return builtins::uri::URI_PARSE_BUILTINS; }
} // namespace clause::runtime
