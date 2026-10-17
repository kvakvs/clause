#include "term_text.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <clause/abi/keywords.hpp>
#include <cstdint>
#include <span>

namespace clause::runtime::detail {
namespace {
// Decode one code point from validated atom UTF-8, advancing past its bytes.
char32_t next_code_point(std::string_view text, std::size_t &position) {
    const auto lead = static_cast<unsigned char>(text[position++]);
    const unsigned extra = lead < 0x80 ? 0 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
    char32_t value = extra == 0 ? lead : lead & (0x3FU >> extra);
    for (unsigned i = 0; i < extra && position < text.size(); ++i) {
        value = (value << 6U) | (static_cast<unsigned char>(text[position++]) & 0x3FU);
    }
    return value;
}

// Latin-1 letter classes shared by io_lib and the emulator printer.
bool lower(char32_t c) { return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7); }

bool upper(char32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7); }

// io_lib accepts '@' inside unquoted atoms; the emulator printer quotes it.
bool name_char(char32_t c, TermStyle style) {
    return lower(c) || upper(c) || (c >= '0' && c <= '9') || c == '_' || (c == '@' && style != TermStyle::display);
}

// Unquoted atoms start with a Latin-1 lowercase letter and continue with name characters.
bool needs_quotes(std::string_view spelling, TermStyle style) {
    if (spelling.empty() || (style != TermStyle::display && abi::v1::reserved_word(spelling))) {
        return true;
    }
    std::size_t position = 0;
    if (!lower(next_code_point(spelling, position))) {
        return true;
    }
    while (position < spelling.size()) {
        if (!name_char(next_code_point(spelling, position), style)) {
            return true;
        }
    }
    return false;
}

// Named escapes inside quoted atoms; only io_lib names ESC and DEL (the last two rows).
std::string_view named_escape(char32_t c, TermStyle style) {
    static constexpr std::array<std::pair<char32_t, std::string_view>, 10> names{{{'\'', "\\'"},
                                                                                  {'\\', "\\\\"},
                                                                                  {'\n', "\\n"},
                                                                                  {'\r', "\\r"},
                                                                                  {'\t', "\\t"},
                                                                                  {'\v', "\\v"},
                                                                                  {'\b', "\\b"},
                                                                                  {'\f', "\\f"},
                                                                                  {0x1B, "\\e"},
                                                                                  {0x7F, "\\d"}}};
    const auto rows = std::span(names).first(style != TermStyle::display ? names.size() : names.size() - 2);
    const auto found = std::ranges::find_if(rows, [c](const auto &row) { return row.first == c; });
    return found == rows.end() ? std::string_view{} : found->second;
}

// Append `value` in the given base with uppercase digits.
void append_number(std::uint64_t value, int base, TextOutput &out) {
    std::array<char, 24> digits{};
    auto *end = std::to_chars(digits.data(), digits.data() + digits.size(), value, base).ptr;
    std::transform(digits.data(), end, digits.data(), [](char c) { return c >= 'a' ? static_cast<char>(c - 32) : c; });
    out.append({digits.data(), end});
}

// Append one quoted-atom character: named escape, \x{H} (io_lib `~w`, beyond Latin-1), \ooo control, else raw.
void print_char(char32_t c, std::string_view bytes, TermStyle style, TextOutput &out) {
    if (const auto name = named_escape(c, style); !name.empty()) {
        out.append(name);
    } else if (style == TermStyle::write && c > 0xFF) {
        out.append("\\x{");
        append_number(c, 16, out);
        out.append("}");
    } else if (c < 0x20 || (c >= 0x80 && c < 0xA0)) {
        const std::array octal{'\\', static_cast<char>('0' + ((c >> 6U) & 7U)),
                               static_cast<char>('0' + ((c >> 3U) & 7U)), static_cast<char>('0' + (c & 7U))};
        out.append({octal.data(), octal.size()});
    } else {
        out.append(bytes);
    }
}

// Exact integers at or above these magnitudes switch fixed notation to scientific (OTP to_chars edge rule).
bool beyond_exact_integers(std::uint64_t mantissa, int exponent) {
    static constexpr std::array<std::uint64_t, 3> limits{(std::uint64_t{1} << 53U) - 1, (std::uint64_t{1} << 52U) / 5,
                                                         (std::uint64_t{1} << 51U) / 25};
    return exponent >= 0 && exponent <= 2 && mantissa > limits.at(static_cast<std::size_t>(exponent));
}

// OTP's Ryu variant prints `digits * 10^exponent` in fixed form only inside a length-dependent window.
bool fixed_form(std::string_view digits, int exponent) {
    const auto length = static_cast<int>(digits.size());
    const int scientific = exponent + length - 1;
    const int lowest = length == 1 ? -4 : -(length + 2);
    const int highest = length == 1 || scientific >= 10 ? 2 : 1;
    std::uint64_t mantissa = 0;
    std::from_chars(digits.data(), digits.data() + digits.size(), mantissa);
    return exponent >= lowest && exponent <= highest && !beyond_exact_integers(mantissa, exponent);
}

// Fixed notation always shows a fractional part: "100.0", "17.29", "0.001".
std::string fixed_text(std::string_view digits, int exponent) {
    const int whole = static_cast<int>(digits.size()) + exponent;
    if (exponent >= 0) {
        return std::string(digits) + std::string(static_cast<std::size_t>(exponent), '0') + ".0";
    }
    if (whole > 0) {
        const auto split = static_cast<std::size_t>(whole);
        return std::string(digits.substr(0, split)) + "." + std::string(digits.substr(split));
    }
    return "0." + std::string(static_cast<std::size_t>(-whole), '0') + std::string(digits);
}

// Scientific notation keeps ".0" for one digit and omits the exponent's plus sign: "1.0e16", "1.5e-7".
std::string scientific_text(std::string_view digits, int scientific) {
    std::string text(1, digits.front());
    text += '.';
    text += digits.size() > 1 ? digits.substr(1) : std::string_view("0");
    return text + "e" + std::to_string(scientific);
}

// float_to_list(F, [short]): shortest round-trip digits from std::to_chars, laid out by OTP's rules.
void print_short(double value, TextOutput &out) {
    std::array<char, 32> buffer{};
    const auto *end =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::scientific).ptr;
    std::string_view text(buffer.data(), end);
    if (text.front() == '-') {
        out.append("-");
        text.remove_prefix(1);
    }
    const auto mark = text.find('e');
    std::string digits(1, text.front());
    if (mark > 1) {
        digits.append(text.substr(2, mark - 2));
    }
    const auto power = text.substr(mark + 2);
    int scientific = 0;
    std::from_chars(power.data(), power.data() + power.size(), scientific);
    scientific = text[mark + 1] == '-' ? -scientific : scientific;
    const int exponent = scientific - static_cast<int>(digits.size()) + 1;
    out.append(fixed_form(digits, exponent) ? fixed_text(digits, exponent) : scientific_text(digits, scientific));
}

// Display strings and binaries print only these bytes literally.
bool display_char(std::int64_t c) {
    return (c >= 0x20 && c != 0x7F && (c < 0x80 || c >= 0xA0) && c <= 0xFF) || c == '\n' || c == '\t' || c == '\r';
}

// A list element qualifies only as a small integer byte in the printable set.
bool display_element(const Term &value) {
    const auto number = value.is_integer() ? value.integer_value() : TermResult<std::int64_t>{-1};
    return number && display_char(*number);
}

// Walk the whole spine first, as the emulator does, so non-strings print element by element.
TermResult<bool> printable(const Term &list) {
    auto cursor = list;
    while (cursor.is_cons()) {
        auto head = cursor.head();
        auto tail = cursor.tail();
        if (!head || !tail) {
            return std::unexpected(head ? tail.error() : head.error());
        }
        if (!display_element(*head)) {
            return false;
        }
        cursor = *tail;
    }
    return cursor.is_nil();
}

// Numeric bitstring form shared by both styles: "<<1,2,5:3>>".
void numeric_bits(std::span<const std::byte> bytes, std::size_t size, TextOutput &out) {
    out.append("<<");
    for (std::size_t i = 0; i < size / 8; ++i) {
        out.append(i == 0 ? "" : ",");
        append_number(std::to_integer<unsigned>(bytes[i]), 10, out);
    }
    if (const auto tail = size % 8; tail != 0) {
        out.append(size >= 8 ? "," : "");
        append_number(std::to_integer<unsigned>(bytes[size / 8]) >> (8 - tail), 10, out);
        out.append(":");
        append_number(tail, 10, out);
    }
    out.append(">>");
}
} // namespace

TermResult<void> print_atom(const Term &value, TermStyle style, TextOutput &out) {
    const auto spelling = value.atom_spelling();
    if (!spelling) {
        return std::unexpected(spelling.error());
    }
    if (!needs_quotes(*spelling, style)) {
        out.append(*spelling);
        return {};
    }
    out.append("'");
    for (std::size_t position = 0; position < spelling->size();) {
        const auto start = position;
        const auto c = next_code_point(*spelling, position);
        print_char(c, spelling->substr(start, position - start), style, out);
    }
    out.append("'");
    return {};
}

TermResult<void> print_float(const Term &value, TermStyle style, TextOutput &out) {
    const auto number = value.float_value();
    if (!number) {
        return std::unexpected(number.error());
    }
    if (style != TermStyle::display) {
        print_short(*number, out);
        return {};
    }
    // The emulator printer uses C "%.6e": "1.500000e+00".
    std::array<char, 40> buffer{};
    const auto *end =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), *number, std::chars_format::scientific, 6).ptr;
    out.append({buffer.data(), end});
    return {};
}

TermResult<void> print_bits(const Term &value, TermStyle style, TextOutput &out) {
    const auto size = value.bit_size();
    if (!size) {
        return std::unexpected(size.error());
    }
    const auto bytes = value.bitstring_bytes();
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const bool ascii = std::ranges::all_of(*bytes, [](std::byte b) {
        const auto c = std::to_integer<unsigned>(b);
        return c >= 0x20 && c < 0x7F;
    });
    if (style != TermStyle::display || *size % 8 != 0 || bytes->empty() || !ascii) {
        numeric_bits(*bytes, *size, out);
        return {};
    }
    // The emulator prints printable ASCII binaries as <<"text">>, escaping only the double quote.
    out.append("<<\"");
    for (const auto byte : *bytes) {
        const auto c = static_cast<char>(std::to_integer<unsigned>(byte));
        out.append(c == '"' ? std::string_view("\\\"") : std::string_view(&c, 1));
    }
    out.append("\">>");
    return {};
}

TermResult<bool> print_display_string(const Term &list, TextOutput &out) {
    const auto check = printable(list);
    if (!check || !*check) {
        return check;
    }
    out.append("\"");
    for (auto cursor = list; cursor.is_cons(); cursor = cursor.tail().value()) {
        const auto c = static_cast<char>(static_cast<unsigned char>(cursor.head().value().integer_value().value()));
        out.append(c == '\n' ? std::string_view("\\n") : c == '"' ? std::string_view("\\\"") : std::string_view(&c, 1));
    }
    out.append("\"");
    return true;
}
} // namespace clause::runtime::detail
