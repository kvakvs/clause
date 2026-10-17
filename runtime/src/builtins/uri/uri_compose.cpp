#include "../lists/list_walk.hpp"
#include "../unicode/chardata.hpp"
#include "../unicode/graphemes.hpp"
#include "uri_map.hpp"
#include <algorithm>
#include <array>

// uri_string's recompose/1, normalize/1,2 and resolve/2,3 (plan 21 step LIB5G, docs/builtins.md#uri-string), over
// maps as uri_string.erl walks them: each component's list or binary form decides the result's, and OTP's errors
// are answered, raised or thrown where OTP's are.
namespace clause::runtime::builtins::uri {
namespace {
using lists::no_clause;

constexpr std::array<std::string_view, 7> COMPONENTS{"scheme", "userinfo", "host", "port", "path", "query", "fragment"};

bool atom_is(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The components holding text normalize/1 decodes.
bool text_key(const Term &key) {
    return atom_is(key, "userinfo") || atom_is(key, "host") || atom_is(key, "path") || atom_is(key, "query") ||
           atom_is(key, "fragment");
}

std::optional<std::int64_t> small_code(const Term &c) {
    const auto value = c.is_integer() ? c.integer_value() : std::unexpected(TermError::wrong_type);
    return value ? std::optional{*value} : std::nullopt;
}

// Whether an element is a character of the class.
bool code_in(const Term &c, bool (*test)(char32_t)) {
    const auto code = small_code(c);
    return code && *code >= 0 && *code < 0x110000 && test(static_cast<char32_t>(*code));
}

// The elements of a proper list; badarg otherwise (OTP's ++ on its left operand).
std::vector<Term> elements(const Term &list) {
    std::vector<Term> items;
    Term rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return items;
}

// Left ++ Right.
Term appended(ProcessContext &context, const Term &left, const Term &right) {
    return need(TermFactory(context).list(elements(left), right));
}

Term chars(ProcessContext &context, std::string_view text) {
    return codes_list(context, std::u32string(text.begin(), text.end()));
}

Term joined_binary(ProcessContext &context, const Term &left, const Term &right) {
    auto bytes = bytes_of(left);
    const auto tail = bytes_of(right);
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    return binary_of(context, bytes);
}

// uri_string's concat/2: binaries joined, a list with anything as a list; function_clause before the first part.
Term concat(ProcessContext &context, const std::optional<Term> &left, const Term &right) {
    if (!left) {
        no_clause();
    }
    if (left->is_binary() && right.is_binary()) {
        return joined_binary(context, *left, right);
    }
    if (left->is_binary() && right.is_list()) {
        return appended(context, characters_to_list(context, *left), right);
    }
    if (left->is_list()) {
        return appended(context, *left, right.is_binary() ? characters_to_list(context, right) : right);
    }
    no_clause();
}

Term prefixed(ProcessContext &context, std::string_view prefix, const Term &text) {
    if (text.is_binary()) {
        return joined_binary(context, binary_of(context, prefix), text);
    }
    if (text.is_list()) {
        return appended(context, chars(context, prefix), text);
    }
    no_clause();
}

// uri_string's encode/2.
Term encode(ProcessContext &context, const Term &text, bool (*keep)(char32_t)) {
    if (!text.is_binary() && !text.is_list()) {
        no_clause();
    }
    return encoded_as(context, text, encode_bytes(context, text_bytes(context, text), keep));
}

// Whether every character of a scheme is in its class (a list's elements all checked, as lists:all/2 does).
bool scheme_valid(const Term &scheme) {
    if (scheme.is_binary()) {
        const auto bytes = bytes_of(scheme);
        return std::ranges::all_of(bytes, [](std::byte b) { return is_scheme(std::to_integer<char32_t>(b)); });
    }
    bool valid = true;
    Term rest = scheme;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        valid = valid && code_in(rest.head_unchecked(), is_scheme);
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return valid;
}

// The scheme followed by ":"; {error, invalid_scheme, Scheme} when empty or holding a character outside the class.
Term scheme_text(ProcessContext &context, const Term &scheme) {
    if (scheme.is_nil() || (scheme.is_binary() && bytes_of(scheme).empty()) || !scheme_valid(scheme)) {
        fail(context, "invalid_scheme", scheme);
    }
    return concat(context, scheme, binary_of(context, ":"));
}

// The code points of a flat list of characters; none for anything else.
std::optional<std::u32string> flat_codes(const Term &list) {
    std::u32string codes;
    Term rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto code = small_code(rest.head_unchecked());
        if (!code || *code < 0 || *code >= 0x110000) {
            return std::nullopt;
        }
        codes.push_back(static_cast<char32_t>(*code));
    }
    return rest.is_nil() ? std::optional{codes} : std::nullopt;
}

// OTP's registered_name/1: whether every character is in reg-name, up to the first that is not.
bool registered_name(const Term &list) {
    Term rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (!code_in(rest.head_unchecked(), is_reg_name)) {
            return false;
        }
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return true;
}

// A host as it is when it is an IPv4 address or a registered name, bracketed when IPv6, else percent-encoded.
Term host_text(ProcessContext &context, const Term &host) {
    if (host.is_nil()) {
        return host;
    }
    const auto list = host.is_binary() ? characters_to_list(context, host) : host;
    const auto codes = flat_codes(list);
    if (codes && ipv6_address(*codes)) {
        return host.is_binary() ? prefixed(context, "[", joined_binary(context, host, binary_of(context, "]")))
                                : appended(context, appended(context, chars(context, "["), host), chars(context, "]"));
    }
    if ((codes && ipv4_address(*codes)) || registered_name(list)) {
        return host;
    }
    return encode(context, host, is_reg_name);
}

// A path made absolute under an authority: OTP's flat_path/1 then absolute_path/1.
Term absolute_path(ProcessContext &context, const Term &path) {
    auto flat = path.is_binary() ? path : characters_to_list(context, path);
    if (flat.is_binary()) {
        const auto bytes = bytes_of(flat);
        return bytes.empty() || bytes[0] == std::byte{'/'} ? flat : prefixed(context, "/", flat);
    }
    if (flat.is_nil() || (flat.is_cons() && small_code(flat.head_unchecked()) == '/')) {
        return flat;
    }
    if (!flat.is_cons()) {
        no_clause();
    }
    return need(TermFactory(context).cons(need(TermFactory(context).integer('/')), flat));
}

// The URI string of a valid map, built component by component.
class Recomposed final {
  public:
    Recomposed(ProcessContext &context, const Fields &fields) : context_(context), fields_(fields) {}

    Term run() {
        if (const auto scheme = fields_.get("scheme")) {
            uri_ = scheme_text(context_, *scheme);
        }
        if (const auto userinfo = fields_.get("userinfo")) {
            after(prefixed(context_, "//", encode(context_, *userinfo, is_userinfo)));
        }
        host();
        port();
        path();
        if (const auto query = fields_.get("query")) {
            uri_ = concat(context_, uri_, prefixed(context_, "?", encode(context_, *query, is_query)));
        }
        if (const auto fragment = fields_.get("fragment")) {
            after(prefixed(context_, "#", encode(context_, *fragment, is_fragment)));
        }
        return *uri_;
    }

  private:
    void after(const Term &part) { uri_ = uri_ ? concat(context_, uri_, part) : part; }

    void host() {
        if (const auto host = fields_.get("host")) {
            const auto text = host_text(context_, *host);
            uri_ = uri_ ? concat(context_, uri_, prefixed(context_, fields_.has("userinfo") ? "@" : "//", text))
                        : prefixed(context_, "//", text);
        }
    }

    void port() {
        const auto port = fields_.get("port");
        if (!port) {
            return;
        }
        if (atom_is(*port, "undefined")) {
            uri_ = concat(context_, uri_, binary_of(context_, ":"));
            return;
        }
        if (!port->is_integer()) {
            bad_argument();
        }
        uri_ = concat(context_, uri_, binary_of(context_, ":" + need(port->integer_decimal())));
    }

    void path() {
        const auto path = *fields_.get("path");
        if (!uri_) {
            uri_ = encode(context_, path, is_path);
        } else if (fields_.has("host")) {
            uri_ = concat(context_, uri_, encode(context_, absolute_path(context_, path), is_path));
        } else {
            uri_ = concat(context_, uri_, encode(context_, path, is_path));
        }
    }

    ProcessContext &context_;
    const Fields &fields_;
    std::optional<Term> uri_;
};

// OTP's to_binary/3 and to_list/2 (utf8 in and out): {error, invalid_input, Rest} thrown for bad characters.
Term checked(ProcessContext &context, const Term &converted) {
    if (converted.is_tuple()) {
        fail(context, "invalid_input", need(converted.tuple_element(2)));
    }
    return converted;
}

std::string text_of(const Bytes &bytes) { return {reinterpret_cast<const char *>(bytes.data()), bytes.size()}; }

// A text's bytes changed by `change`, in its list or binary form; function_clause for anything else.
template <typename Change> Term rewritten(ProcessContext &context, const Term &text, Change change) {
    if (text.is_binary()) {
        return binary_of(context, change(text_of(bytes_of(text))));
    }
    if (!text.is_list()) {
        no_clause();
    }
    const auto bytes = checked(context, characters_to_binary(context, text));
    return checked(context, characters_to_list(context, binary_of(context, change(text_of(bytes_of(bytes))))));
}

std::string lowered(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
    return text;
}

// Percent-encoded unreserved characters decoded, the other escapes upper-cased; {error, invalid_utf8, Bytes} when
// the result is not UTF-8.
// The escape at `at`: its byte when unreserved, else the escape with upper-case digits; {error,
// invalid_percent_encoding, <<"%HL">>} for a bad one.
void unreserved_escape(ProcessContext &context, const std::string &text, std::size_t at, std::string &out) {
    const auto high = static_cast<std::uint8_t>(text[at + 1]);
    const auto low = static_cast<std::uint8_t>(text[at + 2]);
    const auto byte = escape_value(high, low);
    if (!byte) {
        fail(context, "invalid_percent_encoding", binary_of(context, text.substr(at, 3)));
    }
    if (is_unreserved(*byte)) {
        out.push_back(static_cast<char>(*byte));
        return;
    }
    const auto upper = [](std::uint8_t c) { return static_cast<char>(c >= 'a' && c <= 'f' ? c - 32 : c); };
    out += {'%', upper(high), upper(low)};
}

std::string decoded_unreserved(ProcessContext &context, const std::string &text) {
    std::string out;
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] != '%' || at + 2 >= text.size()) {
            out.push_back(text[at]);
        } else {
            unreserved_escape(context, text, at, out);
            at += 2;
        }
    }
    if (!utf8_list(context, std::as_bytes(std::span(out)))) {
        fail(context, "invalid_utf8", binary_of(context, out));
    }
    return out;
}

// normalize_decode/1: a list goes through characters_to_binary/1 (function_clause on its error tuple).
Term normalize_decoded(ProcessContext &context, const Term &text) {
    const auto decode = [&](const std::string &bytes) { return decoded_unreserved(context, bytes); };
    if (text.is_binary()) {
        return binary_of(context, decode(text_of(bytes_of(text))));
    }
    if (!text.is_list()) {
        no_clause();
    }
    const auto bytes = characters_to_binary(context, text);
    if (!bytes.is_binary()) {
        no_clause();
    }
    return characters_to_list(context, binary_of(context, decode(text_of(bytes_of(bytes)))));
}

// One dot-segment rule of RFC 3986's remove_dot_segments at the input's start: the prefix it removes, what replaces
// it, whether the whole input must match and whether the output loses its last segment.
struct DotRule {
    std::string_view prefix;
    std::string_view replacement;
    bool whole;
    bool drop;
};

constexpr std::array<DotRule, 8> DOT_RULES{{
    {"../", "", false, false},
    {"./", "", false, false},
    {"/./", "/", false, false},
    {"/.", "/", true, false},
    {"/../", "/", false, true},
    {"/..", "/", true, true},
    {".", "", true, false},
    {"..", "", true, false},
}};

// The output without its last segment.
void drop_segment(std::string &out) {
    const auto slash = out.rfind('/');
    out.erase(slash == std::string::npos ? 0 : slash);
}

// The first segment of the input, up to the next slash, moved to the output.
void move_segment(std::string &in, std::string &out) {
    const auto next = in.find('/', 1);
    const auto length = next == std::string::npos ? in.size() : next;
    out.append(in, 0, length);
    in.erase(0, length);
}

// RFC 3986's remove_dot_segments over a path's bytes.
std::string dot_segments(std::string in) {
    std::string out;
    while (!in.empty()) {
        const auto rule = std::ranges::find_if(DOT_RULES, [&](const DotRule &rule) {
            return rule.whole ? in == rule.prefix : in.starts_with(rule.prefix);
        });
        if (rule == DOT_RULES.end()) {
            move_segment(in, out);
            continue;
        }
        in.replace(0, rule->prefix.size(), rule->replacement);
        if (rule->drop) {
            drop_segment(out);
        }
    }
    return out;
}

// The scheme's name as text; none without one or for a list of other than ASCII characters.
std::optional<std::string> scheme_name(const Fields &fields) {
    const auto scheme = fields.get("scheme");
    if (scheme && scheme->is_binary()) {
        return text_of(bytes_of(*scheme));
    }
    if (!scheme || !scheme->is_list()) {
        return std::nullopt;
    }
    const auto codes = flat_codes(*scheme);
    if (!codes || !std::ranges::all_of(*codes, [](char32_t c) { return c < 128; })) {
        return std::nullopt;
    }
    return std::string(codes->begin(), codes->end());
}

// The default port of a scheme uri_string knows; none for the others.
std::optional<int> default_port(const std::optional<std::string> &name) {
    constexpr std::array<std::pair<std::string_view, int>, 6> PORTS{
        {{"http", 80}, {"https", 443}, {"ftp", 21}, {"ssh", 22}, {"sftp", 22}, {"tftp", 69}}};
    const auto known = std::ranges::find_if(PORTS, [&](const auto &entry) { return name && *name == entry.first; });
    return known == PORTS.end() ? std::nullopt : std::optional{known->second};
}

// An empty http or https path made "/".
void root_path(ProcessContext &context, Fields &fields) {
    const auto path = fields.get("path");
    if (path && (path->is_nil() || (path->is_binary() && bytes_of(*path).empty()))) {
        fields.set("path", path->is_nil() ? chars(context, "/") : binary_of(context, "/"));
    }
}

// The scheme's default port dropped; an empty http or https path made "/".
void by_scheme(ProcessContext &context, Fields &fields) {
    const auto name = scheme_name(fields);
    const auto known = default_port(name);
    if (!known) {
        return;
    }
    const auto port = fields.get("port");
    if (port && small_code(*port) == *known) {
        fields.erase("port");
    }
    if (name && name->starts_with("http")) {
        root_path(context, fields);
    }
}

void remove_dots(ProcessContext &context, Fields &fields) {
    fields.set("path", without_dots(context, fields.get("path").value_or(binary_of(context, ""))));
}

// OTP's normalize_map: scheme and host lower-cased, escapes decoded, an undefined port and a default one dropped,
// dot segments removed.
Term normalized_map(ProcessContext &context, const Term &map) {
    Fields fields(context, map);
    for (auto &[key, value] : fields.entries()) {
        if (atom_is(key, "scheme") || atom_is(key, "host")) {
            value = rewritten(context, value, lowered);
        }
    }
    for (auto &[key, value] : fields.entries()) {
        if (text_key(key)) {
            value = normalize_decoded(context, value);
        }
    }
    if (const auto port = fields.get("port"); port && atom_is(*port, "undefined")) {
        fields.erase("port");
    }
    by_scheme(context, fields);
    remove_dots(context, fields);
    return fields.term();
}

bool return_map(const Term &options) {
    return options.is_cons() && atom_is(options.head_unchecked(), "return_map") && options.tail_unchecked().is_nil();
}

// parse/1 of a string: the map, or none with its {error, ...} answer in `error`.
std::optional<Term> parsed_map(ProcessContext &context, const Term &string, Term &error) {
    auto parsed = parse(context, string);
    if (parsed.is_map()) {
        return parsed;
    }
    error = parsed;
    return std::nullopt;
}

Term normalize2(ProcessContext &context, const UriArg &uri, const OptionsArg &options) {
    if (!options.term.is_nil() && !return_map(options.term)) {
        no_clause();
    }
    Term map = uri.term;
    if (!uri.term.is_map()) {
        Term error = uri.term;
        const auto parsed = parsed_map(context, uri.term, error);
        if (!parsed) {
            return error;
        }
        map = *parsed;
    }
    try {
        const auto normalized = normalized_map(context, map);
        return options.term.is_nil() ? recompose(context, normalized) : normalized;
    } catch (const Failure &failure) {
        return error_tuple(context, failure);
    }
}

Term normalize1(ProcessContext &context, const UriArg &uri) {
    return normalize2(context, uri, {need(TermFactory(context).nil())});
}

// The bytes of a path as iolist_to_binary/1 gives them (badarg for anything else).
Bytes iolist_bytes(ProcessContext &context, const Term &term) {
    if (term.is_binary()) {
        return bytes_of(term);
    }
    const auto converted =
        unicode::convert(context, term, unicode::Encoding{.kind = unicode::Encoding::Kind::latin1}, true);
    if (converted.outcome != unicode::Conversion::Outcome::done) {
        bad_argument();
    }
    Bytes bytes;
    for (const auto code : converted.codes) {
        bytes.push_back(static_cast<std::byte>(code));
    }
    return bytes;
}

// OTP's base_directory/1: the characters before the last grapheme that is a slash, or none.
std::optional<std::u32string> base_directory(const std::u32string &codes) {
    std::optional<std::size_t> last;
    for (std::size_t at = 0; at < codes.size();) {
        const auto length = unicode::grapheme_length(codes, at);
        if (length == 1 && codes[at] == '/') {
            last = at;
        }
        at += length;
    }
    return last ? std::optional{codes.substr(0, *last)} : std::nullopt;
}

// A relative path appended to the base path's directory, or to "/" under an authority with an empty path.
Term merged_path(ProcessContext &context, const Term &path, const Fields &base) {
    const auto base_path = base.get("path");
    if (!base_path) {
        no_clause();
    }
    const auto slash = need(TermFactory(context).integer('/'));
    const bool empty = iolist_bytes(context, *base_path).empty();
    if (base.has("host") && empty) {
        return path.is_binary() ? prefixed(context, "/", path)
                                : characters_to_list(context, need(TermFactory(context).list(std::array{slash, path})));
    }
    const auto chars = characters_to_list(context, *base_path);
    const auto codes = flat_codes(chars);
    if (!codes) {
        bad_argument();
    }
    const auto directory = base_directory(*codes);
    if (!directory) {
        return path;
    }
    const std::array parts{codes_list(context, *directory), slash, path};
    const auto data = need(TermFactory(context).list(parts));
    return path.is_binary() ? characters_to_binary(context, data) : characters_to_list(context, data);
}

enum class PathType : std::uint8_t { empty, absolute, relative };

PathType path_type(ProcessContext &context, const Fields &uri) {
    const auto bytes = iolist_bytes(context, uri.get("path").value_or(binary_of(context, "")));
    if (bytes.empty()) {
        return PathType::empty;
    }
    return bytes[0] == std::byte{'/'} ? PathType::absolute : PathType::relative;
}

// RFC 3986 section 5.2 against a base with a scheme.
Term resolve_against(ProcessContext &context, Fields uri, const Fields &base) {
    const auto type = path_type(context, uri);
    if (uri.has("host")) {
        uri.set("scheme", *base.get("scheme"));
        remove_dots(context, uri);
        return uri.term();
    }
    switch (type) {
    case PathType::empty:
        uri.has("query") ? uri.merge_from(base, {"scheme", "userinfo", "host", "port", "path"})
                         : uri.merge_from(base, {"scheme", "userinfo", "host", "port", "path", "query"});
        return uri.term();
    case PathType::absolute:
        break;
    case PathType::relative:
        uri.set("path", merged_path(context, *uri.get("path"), base));
        break;
    }
    uri.merge_from(base, {"scheme", "userinfo", "host", "port"});
    remove_dots(context, uri);
    return uri.term();
}

// The resolved map, or the {error, ...} answer.
Term resolve_map(ProcessContext &context, Fields fields, const Term &base) {
    if (fields.has("scheme")) {
        remove_dots(context, fields);
        return fields.term();
    }
    Term base_map = base;
    if (!base.is_map()) {
        Term error = base;
        const auto parsed = parsed_map(context, base, error);
        if (!parsed) {
            return error;
        }
        base_map = *parsed;
    }
    const Fields base_fields(context, base_map);
    if (!base_fields.has("scheme")) {
        const std::array items{atom(context, "error"), atom(context, "invalid_scheme"),
                               need(TermFactory(context).nil())};
        return need(TermFactory(context).tuple(items));
    }
    return resolve_against(context, fields, base_fields);
}

Term resolve3(ProcessContext &context, const UriArg &uri, const BaseArg &base, const OptionsArg &options) {
    Term map = uri.term;
    if (!uri.term.is_map()) {
        Term error = uri.term;
        const auto parsed = parsed_map(context, uri.term, error);
        if (!parsed) {
            return error;
        }
        map = *parsed;
    }
    Term target = map;
    try {
        target = resolve_map(context, Fields(context, map), base.term);
    } catch (const Failure &failure) {
        throw_failure(context, failure);
    }
    if (!target.is_map() || return_map(options.term)) {
        return target;
    }
    if (!options.term.is_nil()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause, .payload = options.term.word()};
    }
    return recompose(context, target);
}

Term resolve2(ProcessContext &context, const UriArg &uri, const BaseArg &base) {
    return resolve3(context, uri, base, {need(TermFactory(context).nil())});
}

Term recompose1(ProcessContext &context, const Term &map) { return recompose(context, map); }

constexpr std::array URI_COMPOSE_BUILTINS{
    typed_entry<recompose1>("uri_string", "recompose"), typed_entry<normalize1>("uri_string", "normalize"),
    typed_entry<normalize2>("uri_string", "normalize"), typed_entry<resolve2>("uri_string", "resolve"),
    typed_entry<resolve3>("uri_string", "resolve"),
};
} // namespace

Fields::Fields(ProcessContext &context, const Term &map) : context_(context), entries_(need(map.map_entries())) {}

std::ptrdiff_t Fields::find(std::string_view key) const {
    const auto found = std::ranges::find_if(entries_, [key](const auto &entry) { return atom_is(entry.first, key); });
    return found == entries_.end() ? -1 : found - entries_.begin();
}

std::optional<Term> Fields::get(std::string_view key) const {
    const auto index = find(key);
    return index < 0 ? std::nullopt : std::optional{entries_[static_cast<std::size_t>(index)].second};
}

void Fields::set(std::string_view key, const Term &value) {
    const auto index = find(key);
    if (index < 0) {
        entries_.emplace_back(atom(context_, key), value);
    } else {
        entries_[static_cast<std::size_t>(index)].second = value;
    }
}

void Fields::erase(std::string_view key) {
    const auto index = find(key);
    if (index >= 0) {
        entries_.erase(entries_.begin() + index);
    }
}

void Fields::merge_from(const Fields &other, std::initializer_list<std::string_view> keys) {
    for (const auto key : keys) {
        if (const auto value = other.get(key)) {
            set(key, *value);
        }
    }
}

Term Fields::term() const { return need(TermFactory(context_).map(entries_)); }

Term recompose(ProcessContext &context, const Term &map) {
    if (!map.is_map()) {
        no_clause();
    }
    const Fields fields(context, map);
    const bool known = std::ranges::all_of(fields.entries(), [](const auto &entry) {
        return std::ranges::any_of(COMPONENTS, [&](std::string_view name) { return atom_is(entry.first, name); });
    });
    if (!fields.has("path") || !known) {
        const std::array items{atom(context, "error"), atom(context, "invalid_map"), map};
        return need(TermFactory(context).tuple(items));
    }
    try {
        return Recomposed(context, fields).run();
    } catch (const Failure &failure) {
        return error_tuple(context, failure);
    }
}

Term without_dots(ProcessContext &context, const Term &path) { return rewritten(context, path, dot_segments); }
} // namespace clause::runtime::builtins::uri

namespace clause::runtime {
std::span<const BuiltinEntry> uri_compose_builtins() noexcept { return builtins::uri::URI_COMPOSE_BUILTINS; }
} // namespace clause::runtime
