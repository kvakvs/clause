#include "packets.hpp"
#include <algorithm>
#include <array>
#include <cctype>

// The http packet bodies of decode_packet/3 (plan 21 step LIB9C): OTP's packet_parse_http clause for clause, a
// request or status line, a header line with OTP's name canonicalization and known-header atoms, the end of the
// headers, or http_error with the whole line when it parses as none of them.
namespace clause::runtime::builtins::packets {
namespace {
using Bytes = std::span<const std::byte>;

// The methods OTP returns as atoms.
constexpr std::array<std::string_view, 7> METHODS{"OPTIONS", "GET", "HEAD", "POST", "PUT", "DELETE", "TRACE"};

// The headers OTP returns as atoms; a known header's bit is its index + 1.
constexpr std::array<std::string_view, 52> HEADERS{"Cache-Control",
                                                   "Connection",
                                                   "Date",
                                                   "Pragma",
                                                   "Transfer-Encoding",
                                                   "Upgrade",
                                                   "Via",
                                                   "Accept",
                                                   "Accept-Charset",
                                                   "Accept-Encoding",
                                                   "Accept-Language",
                                                   "Authorization",
                                                   "From",
                                                   "Host",
                                                   "If-Modified-Since",
                                                   "If-Match",
                                                   "If-None-Match",
                                                   "If-Range",
                                                   "If-Unmodified-Since",
                                                   "Max-Forwards",
                                                   "Proxy-Authorization",
                                                   "Range",
                                                   "Referer",
                                                   "User-Agent",
                                                   "Age",
                                                   "Location",
                                                   "Proxy-Authenticate",
                                                   "Public",
                                                   "Retry-After",
                                                   "Server",
                                                   "Vary",
                                                   "Warning",
                                                   "Www-Authenticate",
                                                   "Allow",
                                                   "Content-Base",
                                                   "Content-Encoding",
                                                   "Content-Language",
                                                   "Content-Length",
                                                   "Content-Location",
                                                   "Content-Md5",
                                                   "Content-Range",
                                                   "Content-Type",
                                                   "Etag",
                                                   "Expires",
                                                   "Last-Modified",
                                                   "Accept-Ranges",
                                                   "Set-Cookie",
                                                   "Set-Cookie2",
                                                   "X-Forwarded-For",
                                                   "Cookie",
                                                   "Keep-Alive",
                                                   "Proxy-Connection"};

// Header names longer than this keep their original spelling and are never known headers.
constexpr std::size_t MAX_NAME = 50;

unsigned char char_at(Bytes bytes, std::size_t at) { return std::to_integer<unsigned char>(bytes[at]); }

bool digit(unsigned char c) { return c >= '0' && c <= '9'; }

bool space(unsigned char c) { return c == ' ' || c == '\t'; }

// OTP's is_tspecial: controls, separators and space; 127 and the bytes above it are not separators but end tokens.
bool separator(unsigned char c) {
    constexpr std::string_view SEPARATORS = "()<>@,;:\\\"/[]?={} \t";
    if (c <= 32 || c >= 128) {
        return true;
    }
    return SEPARATORS.find(static_cast<char>(c)) != std::string_view::npos;
}

std::string_view text_of(Bytes bytes) { return {reinterpret_cast<const char *>(bytes.data()), bytes.size()}; }

// A cursor over the line's bytes: the position and how many bytes are left.
class Cursor final {
  public:
    explicit Cursor(Bytes bytes) : bytes_(bytes) {}

    bool done() const { return at_ >= bytes_.size(); }

    std::size_t left() const { return bytes_.size() - at_; }

    std::size_t at() const { return at_; }

    unsigned char peek() const { return char_at(bytes_, at_); }

    void skip(std::size_t count = 1) { at_ += count; }

    // Skip spaces and tabs; whether there was one.
    bool spaces() {
        const auto from = at_;
        while (!done() && space(peek())) {
            ++at_;
        }
        return at_ != from;
    }

    // Decimal digits as a number; none when there is no digit.
    std::optional<std::int64_t> number() {
        const auto from = at_;
        std::int64_t value = 0;
        for (; !done() && digit(peek()); ++at_) {
            value = 10 * value + (peek() - '0');
        }
        return at_ == from ? std::nullopt : std::optional{value};
    }

    bool starts(std::string_view prefix) const { return text_of(bytes_.subspan(at_)).starts_with(prefix); }

    Bytes rest() const { return bytes_.subspan(at_); }

    Bytes from(std::size_t start) const { return bytes_.subspan(start, at_ - start); }

  private:
    Bytes bytes_;
    std::size_t at_ = 0;
};

// Terms of one decode_packet/3 result: strings as lists or as sub-binaries of the packet's binary.
class Builder final {
  public:
    Builder(const string::Terms &terms, const Term &binary, Bytes whole, bool as_binary)
        : terms_(terms), binary_(binary), whole_(whole), as_binary_(as_binary) {}

    // A string inside the packet.
    Term string(Bytes part) const {
        if (as_binary_) {
            return terms_.slice(binary_, static_cast<std::size_t>(part.data() - whole_.data()), part.size());
        }
        return chars(text_of(part));
    }

    // A string made here (a canonical header name, the "/" of a URI without a path).
    Term made(std::string_view text) const {
        if (as_binary_) {
            return terms_.binary(std::as_bytes(std::span(text.data(), text.size())));
        }
        return chars(text);
    }

    Term atom(std::string_view name) const { return terms_.atom(name); }

    Term integer(std::int64_t value) const { return terms_.integer(value); }

    Term tuple(std::initializer_list<Term> items) const { return terms_.tuple(std::span(items.begin(), items.size())); }

  private:
    Term chars(std::string_view text) const {
        std::vector<Term> codes;
        codes.reserve(text.size());
        for (const auto c : text) {
            codes.push_back(terms_.integer(static_cast<unsigned char>(c)));
        }
        return terms_.list(codes);
    }

    const string::Terms &terms_;
    Term binary_;
    Bytes whole_;
    bool as_binary_;
};

bool case_prefix(Bytes uri, std::string_view prefix) {
    if (uri.size() < prefix.size()) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), uri.begin(), [](char p, std::byte b) {
        const auto c = std::to_integer<unsigned char>(b);
        return static_cast<unsigned char>(std::tolower(c)) == static_cast<unsigned char>(p);
    });
}

// The port after `from` when only digits follow; undefined for none or 0.
Term port_term(const Builder &build, Bytes host, std::size_t from) {
    std::int64_t port = 0;
    std::size_t at = from;
    for (; at < host.size() && digit(char_at(host, at)); ++at) {
        port = 10 * port + (char_at(host, at) - '0');
    }
    return at == host.size() && port != 0 ? build.integer(port) : build.atom("undefined");
}

// {absoluteURI, Scheme, Host, Port, Path} of the URI after "http://" or "https://".
Term absolute_uri(const Builder &build, std::string_view scheme, Bytes uri) {
    const auto slash = std::ranges::find(uri, std::byte{'/'});
    const auto path = slash == uri.end() ? build.made("/") : build.string(uri.subspan(slash - uri.begin()));
    const auto host = uri.first(static_cast<std::size_t>(slash - uri.begin()));
    const auto colon = std::ranges::find(host, std::byte{':'});
    if (colon == host.end()) {
        return build.tuple(
            {build.atom("absoluteURI"), build.atom(scheme), build.string(host), build.atom("undefined"), path});
    }
    const auto close = std::ranges::find(host, std::byte{']'});
    if (!host.empty() && host[0] == std::byte{'['} && close != host.end()) {
        const auto bracketed = static_cast<std::size_t>(close - host.begin()) + 1;
        const auto last_colon = std::find(host.rbegin(), host.rend(), std::byte{':'});
        const bool port_follows = static_cast<std::size_t>(host.rend() - last_colon) == bracketed + 1;
        const auto port = port_follows ? port_term(build, host, bracketed + 1) : build.atom("undefined");
        return build.tuple(
            {build.atom("absoluteURI"), build.atom(scheme), build.string(host.first(bracketed)), port, path});
    }
    const auto at = static_cast<std::size_t>(colon - host.begin());
    return build.tuple({build.atom("absoluteURI"), build.atom(scheme), build.string(host.first(at)),
                        port_term(build, host, at + 1), path});
}

// The Request-URI term: '*', {abs_path, Path}, {absoluteURI, ...}, {scheme, Scheme, Rest} or the string itself.
Term uri_term(const Builder &build, Bytes uri) {
    if (uri.size() == 1 && uri[0] == std::byte{'*'}) {
        return build.atom("*");
    }
    if (uri.size() <= 1 || uri[0] == std::byte{'/'}) {
        return build.tuple({build.atom("abs_path"), build.string(uri)});
    }
    if (case_prefix(uri, "http://")) {
        return absolute_uri(build, "http", uri.subspan(7));
    }
    if (case_prefix(uri, "https://")) {
        return absolute_uri(build, "https", uri.subspan(8));
    }
    const auto colon = std::ranges::find(uri, std::byte{':'});
    if (colon == uri.end()) {
        return build.string(uri);
    }
    const auto at = static_cast<std::size_t>(colon - uri.begin());
    return build.tuple({build.atom("scheme"), build.string(uri.first(at)), build.string(uri.subspan(at + 1))});
}

// "HTTP/" Major "." Minor at the cursor; none when malformed.
std::optional<std::pair<std::int64_t, std::int64_t>> version(Cursor &line) {
    if (!line.starts("HTTP/")) {
        return std::nullopt;
    }
    line.skip(5);
    const auto major = line.number();
    if (!major || line.done() || line.peek() != '.') {
        return std::nullopt;
    }
    line.skip();
    const auto minor = line.number();
    if (!minor) {
        return std::nullopt;
    }
    return std::pair{*major, *minor};
}

Term version_term(const Builder &build, std::pair<std::int64_t, std::int64_t> version) {
    return build.tuple({build.integer(version.first), build.integer(version.second)});
}

// Status-Line: {http_response, {Major, Minor}, Status, Phrase}.
std::optional<Term> response(const Builder &build, Cursor &line) {
    const auto parsed = version(line);
    if (!parsed || !line.spaces()) {
        return std::nullopt;
    }
    const auto status = line.number().value_or(0);
    if (!line.spaces() && !line.done()) {
        return std::nullopt;
    }
    return build.tuple(
        {build.atom("http_response"), version_term(build, *parsed), build.integer(status), build.string(line.rest())});
}

// The method of a Request-Line, a token followed by a space: an atom for a known one, else a string.
std::optional<Term> method_of(const Builder &build, Cursor &line) {
    while (!line.done() && !separator(line.peek())) {
        line.skip();
    }
    const auto method = line.from(0);
    if (line.done() || method.empty() || !space(line.peek())) {
        return std::nullopt;
    }
    const auto known = std::ranges::find(METHODS, text_of(method));
    return known == METHODS.end() ? build.string(method) : build.atom(*known);
}

// The Request-URI after the method's spaces; none when it is empty.
std::optional<Bytes> uri_of(Cursor &line) {
    line.spaces();
    const auto start = line.at();
    while (!line.done() && !space(line.peek())) {
        line.skip();
    }
    const auto uri = line.from(start);
    return uri.empty() ? std::nullopt : std::optional{uri};
}

// The version that ends a Request-Line: HTTP/0.9 when there is none.
std::optional<std::pair<std::int64_t, std::int64_t>> request_version(Cursor &line) {
    line.spaces();
    if (line.done()) {
        return std::pair<std::int64_t, std::int64_t>{0, 9};
    }
    return line.left() >= 8 ? version(line) : std::nullopt;
}

// Request-Line: {http_request, Method, Uri, {Major, Minor}}.
std::optional<Term> request(const Builder &build, Cursor &line) {
    const auto method = method_of(build, line);
    const auto uri = method ? uri_of(line) : std::nullopt;
    const auto parsed = uri ? request_version(line) : std::nullopt;
    if (!parsed) {
        return std::nullopt;
    }
    return build.tuple({build.atom("http_request"), *method, uri_term(build, *uri), version_term(build, *parsed)});
}

// A header name with OTP's case: upper case first and after each '-', lower case elsewhere.
std::string canonical(Bytes name) {
    std::string text;
    bool upper = true;
    for (const auto b : name) {
        auto c = std::to_integer<unsigned char>(b);
        if (upper) {
            c = static_cast<unsigned char>(std::toupper(c));
            upper = false;
        } else {
            c = static_cast<unsigned char>(std::tolower(c));
            upper = c == '-';
        }
        text.push_back(static_cast<char>(c));
    }
    return text;
}

// A header line: {http_header, Bit, Name, OriginalName, Value}; http_eoh for the empty line.
std::optional<Term> header_line(const Builder &build, Cursor &line) {
    if (line.done()) {
        return build.atom("http_eoh");
    }
    while (!separator(line.peek())) {
        line.skip();
        if (line.done()) {
            return std::nullopt;
        }
    }
    if (line.peek() != ':') {
        return std::nullopt;
    }
    const auto original = line.from(0);
    line.skip();
    line.spaces();
    auto bit = build.integer(0);
    auto name = build.string(original);
    if (original.size() <= MAX_NAME) {
        const auto text = canonical(original);
        const auto known = std::ranges::find(HEADERS, std::string_view{text});
        bit = build.integer(known == HEADERS.end() ? 0 : (known - HEADERS.begin()) + 1);
        name = known == HEADERS.end() ? build.made(text) : build.atom(*known);
    }
    return build.tuple({build.atom("http_header"), bit, name, build.string(original), build.string(line.rest())});
}

// The line without its CRLF or LF.
Bytes trimmed(Bytes line) {
    if (line.size() >= 2 && line[line.size() - 2] == std::byte{'\r'}) {
        return line.first(line.size() - 2);
    }
    if (!line.empty() && line.back() == std::byte{'\n'}) {
        return line.first(line.size() - 1);
    }
    return line;
}
} // namespace

Term http_term(const string::Terms &terms, const Term &binary, Bytes line, bool header, bool as_binary) {
    const Builder build(terms, binary, line, as_binary);
    Cursor cursor(trimmed(line));
    std::optional<Term> parsed;
    if (header) {
        parsed = header_line(build, cursor);
    } else if (cursor.starts("HTTP/")) {
        parsed = response(build, cursor);
    } else {
        parsed = request(build, cursor);
    }
    return parsed ? *parsed : build.tuple({build.atom("http_error"), build.string(line)});
}
} // namespace clause::runtime::builtins::packets
