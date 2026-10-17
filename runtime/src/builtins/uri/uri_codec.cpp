#include "../../terms/integers.hpp"
#include "../lists/list_walk.hpp"
#include "../text.hpp"
#include "../unicode/chardata.hpp"
#include "../unicode/encodings.hpp"
#include "uri_map.hpp"
#include <algorithm>
#include <array>

// uri_string's percent coding and form-urlencoded queries (plan 21 step LIB5G, docs/builtins.md#uri-string):
// percent_decode/1, quote/1,2, unquote/1, transcode/2, compose_query/1,2, dissect_query/1 and the character classes,
// with OTP's answers, raised errors and escaping throws.
namespace clause::runtime::builtins::uri {
namespace {
using lists::no_clause;

bool atom_is(const Term &term, std::string_view name) { return term.is_atom() && need(term.atom_spelling()) == name; }

std::string text_of(const Bytes &bytes) { return {reinterpret_cast<const char *>(bytes.data()), bytes.size()}; }

// OTP's raw_decode/2: escapes decoded; {error, invalid_percent_encoding, <<"%HL">>} for a bad one, {error,
// invalid_utf8, Bytes} when the result is not UTF-8.
std::string raw_decoded(ProcessContext &context, const std::string &text) {
    std::string out;
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] != '%' || at + 2 >= text.size()) {
            out.push_back(text[at]);
            continue;
        }
        const auto byte =
            escape_value(static_cast<std::uint8_t>(text[at + 1]), static_cast<std::uint8_t>(text[at + 2]));
        if (!byte) {
            fail(context, "invalid_percent_encoding", binary_of(context, text.substr(at, 3)));
        }
        out.push_back(static_cast<char>(*byte));
        at += 2;
    }
    if (!utf8_list(context, std::as_bytes(std::span(out)))) {
        fail(context, "invalid_utf8", binary_of(context, out));
    }
    return out;
}

// OTP's raw_decode/1: a list answers its errors, a binary throws them; function_clause for anything else.
Term raw_decode(ProcessContext &context, const Term &text) {
    if (text.is_binary()) {
        return binary_of(context, raw_decoded(context, text_of(bytes_of(text))));
    }
    if (!text.is_list()) {
        no_clause();
    }
    const auto bytes = characters_to_binary(context, text);
    if (!bytes.is_binary()) {
        no_clause();
    }
    try {
        return characters_to_list(context, binary_of(context, raw_decoded(context, text_of(bytes_of(bytes)))));
    } catch (const Failure &failure) {
        return error_tuple(context, failure);
    }
}

// The answer of a call whose binary errors escape as throws.
template <typename Call> Term thrown(ProcessContext &context, Call call) {
    try {
        return call();
    } catch (const Failure &failure) {
        throw_failure(context, failure);
    }
}

bool text_key(const Term &key) {
    return atom_is(key, "userinfo") || atom_is(key, "host") || atom_is(key, "path") || atom_is(key, "query") ||
           atom_is(key, "fragment");
}

// percent_decode(URIMap): every text component decoded; a list component's error as {error, {invalid, {Key,
// {Reason, Input}}}}, a binary's as its {error, Reason, Input}.
Term decode_map(ProcessContext &context, const Term &map) {
    auto entries = need(map.map_entries());
    for (auto &[key, value] : entries) {
        if (!text_key(key)) {
            continue;
        }
        try {
            value = raw_decode(context, value);
        } catch (const Failure &failure) {
            return error_tuple(context, failure);
        }
        if (value.is_tuple() && need(value.tuple_size()) == 3 && atom_is(need(value.tuple_element(0)), "error")) {
            TermFactory factory(context);
            const std::array detail{need(value.tuple_element(1)), need(value.tuple_element(2))};
            const std::array field{key, need(factory.tuple(detail))};
            const std::array invalid{atom(context, "invalid"), need(factory.tuple(field))};
            const std::array reason{atom(context, "error"), need(factory.tuple(invalid))};
            return need(factory.tuple(reason));
        }
    }
    return need(TermFactory(context).map(entries));
}

Term percent_decode1(ProcessContext &context, const Term &uri) {
    if (uri.is_map()) {
        return decode_map(context, uri);
    }
    if (!uri.is_list() && !uri.is_binary()) {
        no_clause();
    }
    return thrown(context, [&] { return raw_decode(context, uri); });
}

Term unquote1(ProcessContext &context, const Term &text) {
    return thrown(context, [&] { return raw_decode(context, text); });
}

Term quoted(ProcessContext &context, const Term &text, std::u32string_view safe) {
    if (!text.is_binary() && !text.is_list()) {
        no_clause();
    }
    return thrown(context, [&] {
        return encoded_as(context, text, encode_bytes(context, text_bytes(context, text), is_unreserved, safe));
    });
}

Term quote1(ProcessContext &context, const Term &text) { return quoted(context, text, {}); }

Term quote2(ProcessContext &context, const Term &text, const SafeArg &safe) {
    std::u32string codes;
    Term rest = safe.term;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto c = need(rest.head());
        const auto code = c.is_integer() ? c.integer_value() : std::unexpected(TermError::wrong_type);
        if (code && *code >= 0 && *code < 0x110000) {
            codes.push_back(static_cast<char32_t>(*code));
        }
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return quoted(context, text, codes);
}

// proplists:get_value(Key, Options): none where the default applies.
std::optional<Term> option(ProcessContext &context, const Term &options, std::string_view key) {
    Term rest = options;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto entry = need(rest.head());
        if (atom_is(entry, key)) {
            return need(TermFactory(context).boolean(true));
        }
        if (entry.is_tuple() && need(entry.tuple_size()) >= 1 && atom_is(need(entry.tuple_element(0)), key)) {
            return need(entry.tuple_size()) == 2 ? std::optional{need(entry.tuple_element(1))} : std::nullopt;
        }
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return std::nullopt;
}

// transcode's in_encoding and out_encoding.
struct Encodings {
    Term in;
    Term out;
};

// OTP's to_list/2 and to_binary/3: characters_to_list/2 or characters_to_binary/3, {error, invalid_input, Rest}
// thrown at a bad character.
std::u32string decoded(ProcessContext &context, const Term &data, const Term &encoding) {
    auto converted = unicode::convert(context, data, unicode::encoding(encoding), false);
    if (converted.outcome != unicode::Conversion::Outcome::done) {
        fail(context, "invalid_input", converted.rest);
    }
    return std::move(converted.codes);
}

// The bytes of code points in an encoding; the index of the first one it cannot hold, if any.
std::pair<Bytes, std::optional<std::size_t>> encoding_of(std::u32string_view codes, const Term &encoding) {
    const auto target = unicode::encoding(encoding);
    unicode::Bytes out;
    for (std::size_t index = 0; index < codes.size(); ++index) {
        if (!unicode::scalar(codes[index]) || (target.kind == unicode::Encoding::Kind::latin1 && codes[index] > 255)) {
            return {out, index};
        }
        unicode::encode(codes[index], target, out);
    }
    return {out, std::nullopt};
}

// characters_to_binary/3 of a code point list: {error, invalid_input, Rest} thrown with the list from the first
// character the encoding cannot hold.
Bytes encoded(ProcessContext &context, std::u32string_view codes, const Term &encoding) {
    auto [bytes, bad] = encoding_of(codes, encoding);
    if (bad) {
        fail(context, "invalid_input", codes_list(context, codes.substr(*bad)));
    }
    return bytes;
}

// characters_to_binary(Bytes, In, Out) of an escape run: Rest is the binary from the character Out cannot hold.
Bytes reencoded(ProcessContext &context, const std::string &bytes, const Encodings &encodings) {
    const auto codes = decoded(context, binary_of(context, bytes), encodings.in);
    auto [result, bad] = encoding_of(codes, encodings.out);
    if (bad) {
        const auto [before, _] = encoding_of(std::u32string_view(codes).substr(0, *bad), encodings.in);
        fail(context, "invalid_input", binary_of(context, std::string_view(bytes).substr(before.size())));
    }
    return result;
}

bool is_char(const Term &term, char32_t c) {
    const auto code = term.is_integer() ? term.integer_value() : std::unexpected(TermError::wrong_type);
    return code && *code == static_cast<std::int64_t>(c);
}

bool hex_term(const Term &term) {
    const auto code = term.is_integer() ? term.integer_value() : std::unexpected(TermError::wrong_type);
    return code && *code >= 0 && *code < 128 && is_hex(static_cast<char32_t>(*code));
}

std::vector<Term> integer_terms(ProcessContext &context, std::u32string_view codes) {
    std::vector<Term> terms;
    for (const auto code : codes) {
        terms.push_back(need(TermFactory(context).integer(static_cast<std::int64_t>(code))));
    }
    return terms;
}

// The bytes of the run of escapes from `at`, which it leaves after them; {error, invalid_percent_encoding, Rest}
// at an escape without two hex digits.
std::string escape_run(ProcessContext &context, const std::vector<Term> &chars, std::size_t &at) {
    std::string bytes;
    while (at + 2 < chars.size() && is_char(chars[at], '%')) {
        if (!hex_term(chars[at + 1]) || !hex_term(chars[at + 2])) {
            fail(context, "invalid_percent_encoding", need(TermFactory(context).list(std::span(chars).subspan(at))));
        }
        bytes.push_back(static_cast<char>(*escape_value(static_cast<std::uint8_t>(*chars[at + 1].integer_value()),
                                                        static_cast<std::uint8_t>(*chars[at + 2].integer_value()))));
        at += 3;
    }
    return bytes;
}

// OTP's transcode_chars: each run of escapes decoded in In and encoded again, as escapes, in Out; other elements
// kept as they are.
std::vector<Term> transcoded(ProcessContext &context, const std::vector<Term> &chars, const Encodings &encodings) {
    if (chars.empty()) {
        no_clause();
    }
    std::vector<Term> result;
    for (std::size_t at = 0; at < chars.size();) {
        if (!is_char(chars[at], '%') || at + 2 >= chars.size()) {
            result.push_back(chars[at++]);
            continue;
        }
        std::string escaped;
        percent_encode(escaped, reencoded(context, escape_run(context, chars, at), encodings));
        for (const auto c : escaped) {
            result.push_back(need(TermFactory(context).integer(static_cast<unsigned char>(c))));
        }
    }
    return result;
}

// The characters of a binary inside transcode's input, decoded in In; its {error, invalid_input, Rest} thrown.
void flatten_binary(ProcessContext &context, const Term &binary, const Term &in, std::vector<Term> &chars) {
    try {
        const auto codes = integer_terms(context, decoded(context, binary, in));
        chars.insert(chars.end(), codes.begin(), codes.end());
    } catch (const Failure &failure) {
        throw_failure(context, failure);
    }
}

// One head of transcode's input: a binary's characters, a nested list walked next (badarg for an improper one, as
// OTP's ++), anything else kept.
void flatten_head(ProcessContext &context, const Term &head, const Term &in, std::vector<Term> &pending,
                  std::vector<Term> &chars) {
    if (head.is_binary()) {
        flatten_binary(context, head, in, chars);
    } else if (head.is_list()) {
        if (!need(head.is_proper_list())) {
            bad_argument();
        }
        pending.push_back(head);
    } else {
        chars.push_back(head);
    }
}

// OTP's flatten_input: a deep list's elements, its binaries decoded in In; an improper tail thrown as
// invalid_input.
std::vector<Term> flatten_input(ProcessContext &context, const Term &list, const Encodings &encodings) {
    std::vector<Term> chars;
    std::vector<Term> pending{list};
    while (!pending.empty()) {
        const auto term = pending.back();
        pending.pop_back();
        if (term.is_nil()) {
            continue;
        }
        if (!term.is_cons()) {
            throw_failure(context, {.reason = "invalid_input", .term = term});
        }
        pending.push_back(need(term.tail()));
        flatten_head(context, need(term.head()), encodings.in, pending, chars);
    }
    return chars;
}

Term transcode2(ProcessContext &context, const UriArg &uri, const OptionsArg &options) {
    const auto &string = uri.term;
    if (!string.is_binary() && !string.is_list()) {
        no_clause();
    }
    const auto utf8 = atom(context, "utf8");
    const Encodings encodings{.in = option(context, options.term, "in_encoding").value_or(utf8),
                              .out = option(context, options.term, "out_encoding").value_or(utf8)};
    if (string.is_list()) {
        const auto chars = flatten_input(context, string, encodings);
        try {
            return need(TermFactory(context).list(transcoded(context, chars, encodings)));
        } catch (const Failure &failure) {
            return error_tuple(context, failure);
        }
    }
    try {
        const auto decoded_chars = integer_terms(context, decoded(context, string, encodings.in));
        const auto chars = transcoded(context, decoded_chars, encodings);
        std::u32string codes;
        for (const auto &c : chars) {
            codes.push_back(static_cast<char32_t>(*c.integer_value()));
        }
        return binary_of(context, encoded(context, codes, encodings.out));
    } catch (const Failure &failure) {
        return error_tuple(context, failure);
    }
}

// HTML5's application/x-www-form-urlencoded byte serializer.
std::string form_bytes(std::span<const std::byte> bytes) {
    std::string out;
    for (const auto b : bytes) {
        const auto c = std::to_integer<char>(b);
        if (c == ' ') {
            out.push_back('+');
        } else if (c == '*' || c == '-' || c == '.' || c == '_' || is_digit(std::to_integer<char32_t>(b)) ||
                   is_alpha(std::to_integer<char32_t>(b))) {
            out.push_back(c);
        } else {
            percent_encode(out, std::span(&b, 1));
        }
    }
    return out;
}

// Characters past Latin-1 as HTML numeric character references "&#N;", the others as their bytes.
std::string numeric_references(const Bytes &bytes) {
    std::string out;
    for (std::size_t at = 0; at < bytes.size();) {
        const auto code = code_at(bytes, at);
        if (!code) {
            no_clause();
        }
        if (*code > 255) {
            out += "&#" + std::to_string(static_cast<std::uint32_t>(*code)) + ";";
        } else {
            out.push_back(static_cast<char>(*code));
        }
    }
    return out;
}

// The encoding of a [{encoding, E}] option list for a list or binary text; {error, invalid_input, Text} otherwise.
Term query_encoding(ProcessContext &context, const Term &text, const OptionsArg &options) {
    const bool single = options.term.is_cons() && need(options.term.tail()).is_nil();
    const auto entry = single ? need(options.term.head()) : options.term;
    const bool shaped = single && entry.is_tuple() && need(entry.tuple_size()) == 2 &&
                        atom_is(need(entry.tuple_element(0)), "encoding");
    if (!shaped || (!text.is_list() && !text.is_binary())) {
        fail(context, "invalid_input", text);
    }
    return need(entry.tuple_element(1));
}

// A text's bytes: a binary's, or a list's in `encoding` (OTP's to_binary(Text, utf8, Encoding)).
Bytes text_in(ProcessContext &context, const Term &text, const Term &encoding) {
    return text.is_binary() ? bytes_of(text)
                            : encoded(context, decoded(context, text, atom(context, "utf8")), encoding);
}

// form_encode/2 of a key or value under the [{encoding, E}] option.
std::string form_encode(ProcessContext &context, const Term &text, const OptionsArg &options) {
    const auto encoding = query_encoding(context, text, options);
    if (atom_is(encoding, "latin1")) {
        const auto references = numeric_references(text_in(context, text, atom(context, "utf8")));
        return form_bytes(std::as_bytes(std::span(references)));
    }
    if (!atom_is(encoding, "utf8") && !atom_is(encoding, "unicode")) {
        fail(context, "invalid_encoding", encoding);
    }
    return form_bytes(text_in(context, text, encoding));
}

// One {Key, Value} of compose_query: "Key=Value", or "Key" for a true value; whether either is a list.
std::string compose_pair(ProcessContext &context, const Term &pair, const OptionsArg &options, bool &lists) {
    if (!pair.is_tuple() || need(pair.tuple_size()) != 2) {
        no_clause();
    }
    const auto key = need(pair.tuple_element(0));
    const auto value = need(pair.tuple_element(1));
    auto text = form_encode(context, key, options);
    if (!atom_is(value, "true")) {
        text += "=" + form_encode(context, value, options);
    }
    lists = lists || key.is_list() || value.is_list();
    return text;
}

Term compose_query2(ProcessContext &context, const PairsArg &list, const OptionsArg &options) {
    if (list.term.is_nil()) {
        return list.term;
    }
    try {
        std::string query;
        bool lists = false;
        Term rest = list.term;
        for (; rest.is_cons(); rest = need(rest.tail())) {
            query += compose_pair(context, need(rest.head()), options, lists);
            if (!need(rest.tail()).is_nil()) {
                query += "&";
            }
        }
        if (!rest.is_nil()) {
            no_clause();
        }
        const auto binary = binary_of(context, query);
        return lists ? codes_list(context, decoded(context, binary, atom(context, "utf8"))) : binary;
    } catch (const Failure &failure) {
        return error_tuple(context, failure);
    }
}

Term compose_query1(ProcessContext &context, const PairsArg &list) {
    TermFactory factory(context);
    const std::array option{atom(context, "encoding"), atom(context, "utf8")};
    const std::array options{need(factory.tuple(option))};
    return compose_query2(context, list, {need(factory.list(options))});
}

// The byte of the escape at `at` of a form value; {error, invalid_percent_encoding, Rest} for a bad one.
char form_escape(ProcessContext &context, const std::string &text, std::size_t at) {
    const auto byte = escape_value(static_cast<std::uint8_t>(text[at + 1]), static_cast<std::uint8_t>(text[at + 2]));
    if (!byte) {
        fail(context, "invalid_percent_encoding",
             codes_list(context, decoded(context, binary_of(context, text.substr(at)), atom(context, "utf8"))));
    }
    return static_cast<char>(*byte);
}

// A UTF-8 character at `at`, appended to `out`; {error, Reason, [Byte]} for an invalid first byte.
void form_character(ProcessContext &context, const std::string &text, std::size_t &at, std::string &out,
                    std::string_view reason) {
    const auto start = at;
    if (!code_at(std::as_bytes(std::span(text)), at)) {
        fail(context, reason, codes_list(context, std::u32string(1, static_cast<unsigned char>(text[start]))));
    }
    out.append(text, start, at - start);
}

// OTP's form_unescape: '+' as a space, escapes decoded.
std::string form_unescaped(ProcessContext &context, const std::string &text) {
    std::string out;
    for (std::size_t at = 0; at < text.size();) {
        if (text[at] == '+') {
            out.push_back(' ');
            ++at;
        } else if (text[at] == '%' && at + 2 < text.size()) {
            out.push_back(form_escape(context, text, at));
            at += 3;
        } else {
            form_character(context, text, at, out, "invalid_character");
        }
    }
    return out;
}

// The code point of a reference's digits from `at` (past "&#") up to ';', leaving `at` after it; function_clause
// at the end, {error, invalid_input, [Byte]} at another byte, badarg for a code no character has.
char32_t reference_code(ProcessContext &context, const std::string &text, std::size_t &at) {
    detail::Integer code = 0;
    while (at < text.size() && is_digit(static_cast<unsigned char>(text[at]))) {
        code = code * 10 + (text[at++] - '0');
    }
    if (at == text.size()) {
        no_clause();
    }
    if (text[at] != ';') {
        fail(context, "invalid_input", codes_list(context, std::u32string(1, static_cast<unsigned char>(text[at]))));
    }
    ++at;
    if (code > 0x10FFFF || !unicode::scalar(code.convert_to<std::int64_t>())) {
        bad_argument();
    }
    return code.convert_to<char32_t>();
}

// OTP's from_references: HTML numeric character references "&#N;" decoded.
std::string from_references(ProcessContext &context, const std::string &text) {
    std::string out;
    for (std::size_t at = 0; at < text.size();) {
        if (text.compare(at, 2, "&#") == 0) {
            at += 2;
            encode(reference_code(context, text, at), out);
        } else {
            form_character(context, text, at, out, "invalid_input");
        }
    }
    return out;
}

std::string form_decoded(ProcessContext &context, const std::string &text) {
    return from_references(context, form_unescaped(context, text));
}

// One key or value of dissect_query/1: true stays, text is decoded (to a list for a list query).
Term form_value(ProcessContext &context, const std::optional<std::string> &text, bool lists) {
    if (!text) {
        return need(TermFactory(context).boolean(true));
    }
    const auto value = binary_of(context, form_decoded(context, *text));
    return lists ? codes_list(context, decoded(context, value, atom(context, "utf8"))) : value;
}

// OTP's query_key: the key up to '=' or a '&' that starts no "&#" reference.
std::string query_key(const std::string &query, std::size_t &at) {
    std::string key;
    while (at < query.size() && query[at] != '=' && !(query[at] == '&' && query.compare(at, 2, "&#") != 0)) {
        const auto length = query.compare(at, 2, "&#") == 0 ? 2 : 1;
        key.append(query, at, length);
        at += length;
    }
    return key;
}

// OTP's query_value: the value after '=' up to '&'; none (true) without '='.
std::optional<std::string> query_value(const std::string &query, std::size_t &at) {
    if (at >= query.size() || query[at] != '=') {
        return std::nullopt;
    }
    std::string value;
    for (++at; at < query.size() && query[at] != '&'; ++at) {
        value.push_back(query[at]);
    }
    return value;
}

// Pairs split at '&' (a "&#" inside a key is kept), a key without '=' true.
Term dissected(ProcessContext &context, const std::string &query, bool lists) {
    std::vector<Term> pairs;
    std::size_t at = 0;
    while (true) {
        const auto key = query_key(query, at);
        const auto value = query_value(query, at);
        const std::array pair{form_value(context, key, lists), form_value(context, value, lists)};
        pairs.push_back(need(TermFactory(context).tuple(pair)));
        if (at >= query.size()) {
            return need(TermFactory(context).list(pairs));
        }
        ++at;
    }
}

Term dissect_query1(ProcessContext &context, const Term &query) {
    if (query.is_nil() || (query.is_binary() && bytes_of(query).empty())) {
        return need(TermFactory(context).nil());
    }
    if (!query.is_list() && !query.is_binary()) {
        no_clause();
    }
    try {
        const auto bytes =
            query.is_binary() ? bytes_of(query)
                              : encoded(context, decoded(context, query, atom(context, "utf8")), atom(context, "utf8"));
        return dissected(context, text_of(bytes), query.is_list());
    } catch (const Failure &failure) {
        return error_tuple(context, failure);
    }
}

Term allowed_characters0(ProcessContext &context) {
    constexpr std::array<std::pair<std::string_view, bool (*)(char32_t)>, 11> CLASSES{{
        {"scheme", is_scheme},
        {"userinfo", is_userinfo},
        {"host", is_host},
        {"ipv4", [](char32_t c) { return c == '.' || is_digit(c); }},
        {"ipv6", is_ipv6},
        {"regname", is_reg_name},
        {"path", is_path},
        {"query", is_query},
        {"fragment", is_fragment},
        {"reserved", is_reserved},
        {"unreserved", is_unreserved},
    }};
    std::vector<Term> classes;
    for (const auto &[name, test] : CLASSES) {
        std::u32string chars;
        for (char32_t c = 0; c < 128; ++c) {
            if (test(c)) {
                chars.push_back(c);
            }
        }
        const std::array entry{atom(context, name), codes_list(context, chars)};
        classes.push_back(need(TermFactory(context).tuple(entry)));
    }
    return need(TermFactory(context).list(classes));
}

template <bool (*Test)(char32_t)> Term class1(ProcessContext &context, const Term &c) {
    const auto code = c.is_integer() ? c.integer_value() : std::unexpected(TermError::wrong_type);
    return need(
        TermFactory(context).boolean(code && *code >= 0 && *code < 0x110000 && Test(static_cast<char32_t>(*code))));
}

constexpr std::array URI_CODEC_BUILTINS{
    typed_entry<percent_decode1>("uri_string", "percent_decode"),
    typed_entry<quote1>("uri_string", "quote"),
    typed_entry<quote2>("uri_string", "quote"),
    typed_entry<unquote1>("uri_string", "unquote"),
    typed_entry<transcode2>("uri_string", "transcode"),
    typed_entry<compose_query1>("uri_string", "compose_query"),
    typed_entry<compose_query2>("uri_string", "compose_query"),
    typed_entry<dissect_query1>("uri_string", "dissect_query"),
    typed_entry<allowed_characters0>("uri_string", "allowed_characters"),
    typed_entry<class1<is_host>>("uri_string", "is_host"),
    typed_entry<class1<is_path>>("uri_string", "is_path"),
};
} // namespace
} // namespace clause::runtime::builtins::uri

namespace clause::runtime {
std::span<const BuiltinEntry> uri_codec_builtins() noexcept { return builtins::uri::URI_CODEC_BUILTINS; }
} // namespace clause::runtime
