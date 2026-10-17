#include "write.hpp"
#include "../../terms/records.hpp"
#include "../../terms/term_text.hpp"
#include "../string/string.hpp"
#include "../text.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/keywords.hpp>
#include <clause/runtime/output.hpp>
#include <terms.hpp>

namespace clause::runtime::builtins::io_lib {
namespace {
using string::Terms;

// The term text style of an encoding.
TermStyle style_of(Encoding encoding) {
    return encoding == Encoding::latin1 ? TermStyle::write : TermStyle::write_unicode;
}

// The named escapes of string_char/4 and needs_escape/2.
std::optional<char> named_escape(std::int64_t code) {
    static constexpr std::array<std::pair<std::int64_t, char>, 8> NAMES{
        {{'\n', 'n'}, {'\r', 'r'}, {'\t', 't'}, {'\v', 'v'}, {'\b', 'b'}, {'\f', 'f'}, {0x1B, 'e'}, {0x7F, 'd'}}};
    const auto found = std::ranges::find_if(NAMES, [code](const auto &row) { return row.first == code; });
    return found == NAMES.end() ? std::nullopt : std::optional{found->second};
}

// \ooo of a control character, as string_char/4 computes it (arithmetic shifts, so negative codes too).
std::array<char32_t, 4> octal(std::int64_t code) {
    return {U'\\', static_cast<char32_t>((code >> 6) + '0'), static_cast<char32_t>(((code >> 3) & 7) + '0'),
            static_cast<char32_t>((code & 7) + '0')};
}

// Whether string_char/4 keeps a character beyond ASCII as it is.
bool kept(std::int64_t code, Escape escape) { return code >= 0xA0 && (escape == Escape::unicode || code <= 0xFF); }

// The characters of a write_binary_body/4 step: a byte value, or "Value:Bits" for the last bits.
std::string byte_text(std::uint64_t value) { return std::to_string(value); }

// tsub/2: a limit less what was written, never below zero; negative limits stay.
std::int64_t limit_sub(std::int64_t limit, std::int64_t used) {
    return limit < 0 ? limit : limit >= used ? limit - used : 0;
}

// The Latin-1 letters of io_lib's atom rules.
bool latin1_lower(std::int64_t c) { return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7); }

bool latin1_upper(std::int64_t c) { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7); }

// name_char/1.
bool name_char(std::int64_t c) {
    return latin1_lower(c) || latin1_upper(c) || (c >= '0' && c <= '9') || c == '_' || c == '@';
}

// name_chars/1: whether the characters up to the first that is not a name character are all name characters; no
// clause for a non-integer before it or an improper end.
bool name_chars(Term rest) {
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto head = rest.head_unchecked();
        if (!head.is_integer()) {
            no_clause();
        }
        if (!name_char(string::small_of(head).value_or(-1))) {
            return false;
        }
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return true;
}

// The number of Latin-1 characters beyond ASCII in an atom's text, which write_bin1 counts as two bytes.
std::int64_t wide_latin1(std::u32string_view text) {
    return std::ranges::count_if(text, [](char32_t c) { return c >= 0x80 && c <= 0xFF; });
}
} // namespace

std::vector<std::pair<Term, Term>> map_entries(const Term &map, MapsOrder order) {
    if (order == MapsOrder::other) {
        bad_argument();
    }
    auto entries = need(map.map_entries());
    if (order == MapsOrder::reversed) {
        std::ranges::reverse(entries);
    }
    return entries;
}

Deep::Ref atom_text(Deep &deep, const Term &atom, Encoding encoding) {
    return deep.text(code_points(need(format_term(atom, style_of(encoding)))));
}

bool quote_atom(std::string_view atom, const Term &chars) {
    if (abi::v1::reserved_word(atom)) {
        return true;
    }
    if (!chars.is_list() || (chars.is_cons() && !chars.head_unchecked().is_integer())) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause, .payload = chars.word()};
    }
    if (chars.is_nil()) {
        return true;
    }
    const auto first = string::small_of(chars.head_unchecked());
    return !first || !latin1_lower(*first) || !name_chars(chars.tail_unchecked());
}

namespace {
// [$\\, C | Tail].
Deep::Ref backslash(Deep &deep, char32_t c, Deep::Ref tail) {
    return deep.cons(deep.code('\\'), deep.cons(deep.code(c), tail));
}

// "\\x{H}" (uppercase hexadecimal) in front of `tail`.
Deep::Ref hex_escape(Deep &deep, const detail::Integer &value, Deep::Ref tail) {
    return deep.ascii("\\x{" + integer_digits(value, 16) + "}", tail);
}

// string_char/4 of a big integer: kept by unicode lists, \x{...} as Latin-1, no clause for Latin-1 lists.
Deep::Ref big_char(Deep &deep, const Term &code, Escape escape, Deep::Ref tail) {
    if (!code.is_integer() || escape == Escape::latin1 || need(detail::integer_read(code)) < 0) {
        no_clause();
    }
    if (escape == Escape::unicode) {
        return deep.cons(deep.term(code), tail);
    }
    return hex_escape(deep, need(detail::integer_read(code)), tail);
}

// string_char/4 of a control character: its name, else \ooo; no clause from 160 on.
Deep::Ref control_char(Deep &deep, std::int64_t c, Deep::Ref tail) {
    if (const auto name = named_escape(c)) {
        return backslash(deep, static_cast<char32_t>(*name), tail);
    }
    if (c >= 0xA0) {
        no_clause();
    }
    const auto escaped = octal(c);
    return deep.text(std::u32string_view(escaped.data(), escaped.size()), tail);
}
} // namespace

Deep::Ref string_char(Deep &deep, const Term &code, Escape escape, std::int64_t quote, Deep::Ref tail) {
    const auto value = string::small_of(code);
    if (!value) {
        return big_char(deep, code, escape, tail);
    }
    const auto c = *value;
    if (c == quote || c == '\\') {
        return backslash(deep, static_cast<char32_t>(c), tail);
    }
    if ((c >= ' ' && c <= '~') || kept(c, escape)) {
        return deep.cons(deep.term(code), tail);
    }
    if (escape == Escape::unicode_as_latin1 && c > 0xFF) {
        return hex_escape(deep, detail::Integer{c}, tail);
    }
    return control_char(deep, c, tail);
}

Deep::Ref string_text(Deep &deep, const Term &string, Escape escape, const Term &quote) {
    std::vector<Term> codes;
    Term rest = string;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto head = rest.head_unchecked();
        if (!head.is_integer()) {
            no_clause();
        }
        codes.push_back(head);
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    const auto quote_code = quote.is_integer() ? string::small_of(quote).value_or(-1) : -1;
    auto text = deep.cons(deep.term(quote), Deep::NIL);
    for (auto it = codes.rbegin(); it != codes.rend(); ++it) {
        text = string_char(deep, *it, escape, quote_code, text);
    }
    return deep.cons(deep.term(quote), text);
}

namespace {
// needs_escape/2 of one byte of a binary: none when it is written as it is.
std::optional<std::u32string> byte_escape(std::uint8_t byte, std::int64_t quote, Encoding encoding) {
    if (byte == quote || byte == '\\') {
        return std::u32string{U'\\', static_cast<char32_t>(byte)};
    }
    if ((byte >= ' ' && byte <= '~') || byte >= 0xA0 || (encoding == Encoding::unicode && byte > 127)) {
        return std::nullopt;
    }
    if (const auto name = named_escape(byte)) {
        return std::u32string{U'\\', static_cast<char32_t>(*name)};
    }
    const auto escaped = octal(byte);
    return std::u32string(escaped.data(), escaped.size());
}

// The bytes of a binary with needs_escape/2 applied, as characters (Latin-1) or UTF-8 bytes (unicode).
std::pair<std::string, bool> escaped_bytes(const Term &binary, std::int64_t quote, Encoding encoding) {
    std::string out;
    bool any = false;
    for (const auto b : string::binary_bytes(binary)) {
        const auto byte = std::to_integer<std::uint8_t>(b);
        if (const auto escape = byte_escape(byte, quote, encoding)) {
            any = true;
            std::ranges::transform(*escape, std::back_inserter(out), [](char32_t c) { return static_cast<char>(c); });
        } else {
            out.push_back(static_cast<char>(byte));
        }
    }
    return {out, any};
}

// error({badmatch, Value}).
[[noreturn]] void badmatch(const Term &value) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = value.word()};
}

// write_string_bin/3 of a list: write_string/2 as UTF-8, sized by chars_length/1.
std::pair<Term, std::int64_t> list_bin(ProcessContext &context, const Term &string, const QuoteArg &quote) {
    Deep deep;
    const auto escaped = string_text(deep, string, Escape::unicode, quote.term);
    auto bin = Terms(context).characters_to_binary(deep.to_term(context, escaped));
    if (!bin.is_binary()) {
        badmatch(bin);
    }
    return {bin, deep.chars_length(context, escaped)};
}

// write_string_bin/3 of a Latin-1 binary: its bytes escaped and quoted, as UTF-8; the escaped size plus the quotes.
std::pair<Term, std::int64_t> latin1_bin(ProcessContext &context, const Term &string, const QuoteArg &quote,
                                         std::int64_t quote_code) {
    const bool quoted = !quote.term.is_nil();
    if (quoted && (quote_code < 0 || quote_code > 0xFF)) {
        bad_argument();
    }
    const auto text = escaped_bytes(string, quote_code, Encoding::latin1).first;
    std::u32string codes;
    std::ranges::transform(text, std::back_inserter(codes), [](char c) { return static_cast<unsigned char>(c); });
    if (quoted) {
        codes = static_cast<char32_t>(quote_code) + codes + static_cast<char32_t>(quote_code);
    }
    return {Terms(context).utf8(codes), static_cast<std::int64_t>(text.size()) + (quoted ? 2 : 0)};
}

// write_string_bin/3 of a UTF-8 binary: escaped and quoted (as it is without either), sized by string:length/1.
std::pair<Term, std::int64_t> unicode_bin(ProcessContext &context, const Term &string, const QuoteArg &quote,
                                          std::int64_t quote_code) {
    const Terms terms(context);
    const auto [text, any] = escaped_bytes(string, quote_code, Encoding::unicode);
    auto bin = string;
    if (!quote.term.is_nil() || any) {
        const auto middle = terms.binary(std::as_bytes(std::span(text)));
        bin = terms.characters_to_binary(terms.list(std::array{quote.term, middle, quote.term}));
    }
    return {bin, *string::small_of(string::length_of(context, bin))};
}
} // namespace

std::pair<Term, std::int64_t> string_bin(ProcessContext &context, const Term &string, const QuoteArg &quote,
                                         const EncodingArg &encoding_arg) {
    const auto &encoding = encoding_arg.term;
    if (string.is_list()) {
        return list_bin(context, string, quote);
    }
    const auto spelling = encoding.is_atom() ? encoding.atom_spelling_unchecked() : std::string_view{};
    if (!string.is_binary() || (spelling != "latin1" && spelling != "unicode")) {
        no_clause();
    }
    const auto quote_code = quote.term.is_integer() ? string::small_of(quote.term).value_or(-1) : -1;
    return spelling == "latin1" ? latin1_bin(context, string, quote, quote_code)
                                : unicode_bin(context, string, quote, quote_code);
}

Deep::Ref Writer::leaf(const Term &term) {
    const auto text = code_points(need(format_term(term, style_of(encoding_))));
    if (term.is_atom() && encoding_ == Encoding::latin1) {
        size_adjust_ += wide_latin1(text);
    }
    return deep_.text(text);
}

Deep::Ref Writer::write(const Term &term, std::int64_t depth) { return node(term, {.depth = depth, .level = 0}); }

Deep::Ref Writer::node(const Term &term, Place place) {
    const auto [depth, level] = place;
    if (level > MAX_NESTING) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::system_limit};
    }
    if (depth == 0) {
        return deep_.ascii("...");
    }
    if (term.is_bitstring()) {
        return binary(term, {.depth = depth, .limit = -1}).first;
    }
    if (term.is_nil()) {
        return deep_.ascii("[]");
    }
    if (term.is_cons()) {
        return list(term, place);
    }
    if (term.is_tuple()) {
        return term.tuple_size_unchecked() == 0 ? deep_.ascii("{}") : tuple(term, place);
    }
    if (term.is_map()) {
        return map(term, place);
    }
    if (term.is_native_record()) {
        return record(term, place);
    }
    return leaf(term);
}

Deep::Ref Writer::list(const Term &term, Place place) {
    const auto [depth, level] = place;
    if (depth == 1) {
        return deep_.ascii("[...]");
    }
    // write_tail/4 from its end: [], "|...", or [$|, Other].
    std::vector<Deep::Ref> elements{node(term.head_unchecked(), {.depth = depth - 1, .level = level + 1})};
    Term rest = term.tail_unchecked();
    auto at = depth - 1;
    Deep::Ref tail = Deep::NIL;
    for (; !rest.is_nil(); --at) {
        if (at == 1) {
            tail = deep_.ascii("|...");
            size_adjust_ -= 4;
            break;
        }
        if (!rest.is_cons()) {
            tail = deep_.list({deep_.code('|'), node(rest, {.depth = at - 1, .level = level + 1})});
            break;
        }
        elements.push_back(node(rest.head_unchecked(), {.depth = at - 1, .level = level + 1}));
        rest = rest.tail_unchecked();
    }
    for (auto it = elements.rbegin(); it + 1 != elements.rend(); ++it) {
        tail = deep_.cons(deep_.code(','), deep_.cons(*it, tail));
    }
    return deep_.list({deep_.code('['), deep_.cons(elements.front(), tail), deep_.code(']')});
}

Deep::Ref Writer::tuple(const Term &term, Place place) {
    const auto [depth, level] = place;
    if (depth == 1) {
        return deep_.ascii("{...}");
    }
    const auto items = need(term.tuple_elements());
    std::vector<Deep::Ref> elements{node(items.front(), {.depth = depth - 1, .level = level + 1})};
    Deep::Ref tail = Deep::NIL;
    auto at = depth - 1;
    for (std::size_t i = 1; i < items.size(); ++i, --at) {
        if (at == 1) {
            tail = deep_.ascii(",...");
            size_adjust_ -= 1;
            break;
        }
        elements.push_back(node(items[i], {.depth = at - 1, .level = level + 1}));
    }
    for (auto it = elements.rbegin(); it + 1 != elements.rend(); ++it) {
        tail = deep_.cons(deep_.code(','), deep_.cons(*it, tail));
    }
    return deep_.list({deep_.code('{'), deep_.cons(elements.front(), tail), deep_.code('}')});
}

Deep::Ref Writer::map(const Term &term, Place place) {
    const auto [depth, level] = place;
    const auto entries = depth == 1 ? std::vector<std::pair<Term, Term>>{} : map_entries(term, order_);
    if (entries.empty()) {
        return deep_.ascii("#{}");
    }
    const auto inner = depth - 1;
    const auto assoc = [&](const std::pair<Term, Term> &entry) {
        return deep_.list({node(entry.first, {.depth = inner, .level = level + 1}), deep_.ascii(" => "),
                           node(entry.second, {.depth = inner, .level = level + 1})});
    };
    std::vector<Deep::Ref> associations{assoc(entries.front())};
    Deep::Ref tail = Deep::NIL;
    // write_map_body/5: ",..." once the depth runs out, even after the last association.
    for (auto [at, i] = std::pair{inner, std::size_t{1}};; --at, ++i) {
        if (at == 1) {
            tail = deep_.ascii(",...");
            break;
        }
        if (i == entries.size()) {
            break;
        }
        associations.push_back(assoc(entries[i]));
    }
    for (auto it = associations.rbegin(); it + 1 != associations.rend(); ++it) {
        tail = deep_.cons(deep_.code(','), deep_.cons(*it, tail));
    }
    return deep_.list({deep_.code('#'), deep_.code('{'), deep_.cons(associations.front(), tail), deep_.code('}')});
}

Deep::Ref Writer::record(const Term &term, Place place) {
    const auto [depth, level] = place;
    const auto [module, name] = need(detail::record_identity(term));
    const auto fields = need(term.record_fields());
    // write_record_1/5 from its end.
    std::vector<Deep::Ref> parts;
    Deep::Ref tail = Deep::NIL;
    auto at = depth;
    std::size_t i = 0;
    for (; i < fields.size() && at != 1; ++i, --at) {
        parts.push_back(leaf(fields[i].first));
        parts.push_back(node(fields[i].second, {.depth = at - 1, .level = level + 1}));
    }
    if (i < fields.size()) {
        tail = deep_.ascii("...");
    }
    for (std::size_t k = parts.size(); k > 0; k -= 2) {
        const bool last = k == parts.size() && tail == Deep::NIL;
        tail = deep_.list({parts[k - 2], deep_.ascii(" = "), parts[k - 1]},
                          last ? Deep::NIL : deep_.cons(deep_.code(','), tail));
        size_adjust_ += last ? 0 : 1;
    }
    return deep_.list(
        {deep_.code('#'), leaf(module), deep_.code(':'), leaf(name), deep_.code('{'), tail, deep_.code('}')});
}

std::pair<Deep::Ref, Term> Writer::binary(const Term &bits, Cut cut) {
    const auto [depth, limit] = cut;
    const auto size = bits.bit_size_unchecked();
    const auto bytes = need(bits.bitstring_bytes());
    std::vector<Deep::Ref> parts;
    auto at = depth;
    auto room = limit_sub(limit, 4);
    std::size_t index = 0;
    for (; index * 8 < size; ++index, --at) {
        if (at == 1 || room == 0) {
            parts.push_back(deep_.ascii("..."));
            break;
        }
        const auto remaining = size - index * 8;
        if (remaining < 8) {
            const auto value = std::to_integer<unsigned>(bytes[index]) >> (8 - remaining);
            parts.push_back(deep_.ascii(byte_text(value)));
            parts.push_back(deep_.code(':'));
            parts.push_back(deep_.ascii(byte_text(remaining)));
            index = (size + 7) / 8;
            break;
        }
        const auto text = byte_text(std::to_integer<unsigned>(bytes[index]));
        parts.push_back(deep_.ascii(text));
        if (remaining > 8) {
            parts.push_back(deep_.code(','));
        }
        room = limit_sub(room, static_cast<std::int64_t>(text.size()) + 1);
    }
    const auto rest_bits = index * 8 >= size ? 0 : size - index * 8;
    const auto from = std::min(index, bytes.size());
    const auto rest = need(TermFactory(context_).bitstring(std::span(bytes).subspan(from), rest_bits));
    const auto text =
        deep_.list({deep_.code('<'), deep_.code('<'), deep_.list(parts), deep_.code('>'), deep_.code('>')});
    return {text, rest};
}

std::int64_t Writer::size_of(Deep::Ref ref) const {
    return static_cast<std::int64_t>(deep_.flat(ref).size()) + size_adjust_;
}
} // namespace clause::runtime::builtins::io_lib
