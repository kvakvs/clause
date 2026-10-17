#include "encodings.hpp"
#include <terms.hpp>

namespace clause::runtime::builtins::unicode {
namespace {
using Kind = Encoding::Kind;

// The byte at `at` as an unsigned value.
unsigned byte_at(std::span<const std::byte> bytes, std::size_t at) { return std::to_integer<unsigned>(bytes[at]); }

// The length of the UTF-8 sequence a lead byte starts, or 0 for a byte that cannot start one.
std::size_t utf8_length(unsigned lead) {
    if (lead < 0x80) {
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        return 2;
    }
    if (lead >= 0xE0 && lead <= 0xEF) {
        return 3;
    }
    return lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
}

// Decode one UTF-8 sequence of `length` complete bytes at `at`; none when it is overlong, a surrogate or too large.
std::optional<char32_t> utf8_code(std::span<const std::byte> bytes, std::size_t at, std::size_t length) {
    static constexpr std::array<unsigned, 5> LEAD_MASK{0, 0x7F, 0x1F, 0x0F, 0x07};
    static constexpr std::array<char32_t, 5> SMALLEST{0, 0, 0x80, 0x800, 0x10000};
    char32_t code = byte_at(bytes, at) & LEAD_MASK.at(length);
    for (std::size_t next = 1; next < length; ++next) {
        const auto byte = byte_at(bytes, at + next);
        if ((byte & 0xC0U) != 0x80U) {
            return std::nullopt;
        }
        code = (code << 6) | (byte & 0x3FU);
    }
    if (code < SMALLEST.at(length) || !scalar(code)) {
        return std::nullopt;
    }
    return code;
}

// Whether the bytes from `at` to the end can still begin a valid UTF-8 sequence of `length` bytes.
bool utf8_prefix(std::span<const std::byte> bytes, std::size_t at) {
    for (auto next = at + 1; next < bytes.size(); ++next) {
        if ((byte_at(bytes, next) & 0xC0U) != 0x80U) {
            return false;
        }
    }
    return true;
}

Decoded decode_utf8(std::span<const std::byte> bytes, std::u32string &codes) {
    std::size_t at = 0;
    while (at < bytes.size()) {
        const auto length = utf8_length(byte_at(bytes, at));
        if (length == 0) {
            return {.consumed = at, .invalid = true};
        }
        if (at + length > bytes.size()) {
            return {.consumed = at, .invalid = !utf8_prefix(bytes, at)};
        }
        const auto code = utf8_code(bytes, at, length);
        if (!code) {
            return {.consumed = at, .invalid = true};
        }
        codes.push_back(*code);
        at += length;
    }
    return {.consumed = at};
}

// The code unit of `width` bytes at `at` in the byte order.
char32_t unit(std::span<const std::byte> bytes, std::size_t at, std::size_t width, bool little) {
    char32_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        const auto byte = byte_at(bytes, little ? at + width - 1 - index : at + index);
        value = (value << 8) | byte;
    }
    return value;
}

// The code of the UTF-16 surrogate pair starting with the high surrogate at `at`: the code, none when the pair is
// incomplete at the end, or 0 for an invalid pair.
std::optional<char32_t> surrogate_pair(std::span<const std::byte> bytes, std::size_t at, bool little) {
    if (at + 4 > bytes.size()) {
        return std::nullopt;
    }
    const auto first = unit(bytes, at, 2, little);
    const auto second = unit(bytes, at + 2, 2, little);
    if (first > 0xDBFF || second < 0xDC00 || second > 0xDFFF) {
        return 0;
    }
    return 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
}

Decoded decode_utf16(std::span<const std::byte> bytes, bool little, std::u32string &codes) {
    std::size_t at = 0;
    while (at + 2 <= bytes.size()) {
        const auto first = unit(bytes, at, 2, little);
        if (first < 0xD800 || first > 0xDFFF) {
            codes.push_back(first);
            at += 2;
            continue;
        }
        const auto pair = first > 0xDBFF ? std::optional<char32_t>{0} : surrogate_pair(bytes, at, little);
        if (!pair || *pair == 0) {
            return {.consumed = at, .invalid = pair.has_value()};
        }
        codes.push_back(*pair);
        at += 4;
    }
    return {.consumed = at};
}

Decoded decode_utf32(std::span<const std::byte> bytes, bool little, std::u32string &codes) {
    std::size_t at = 0;
    for (; at + 4 <= bytes.size(); at += 4) {
        const auto code = unit(bytes, at, 4, little);
        if (!scalar(code)) {
            return {.consumed = at, .invalid = true};
        }
        codes.push_back(code);
    }
    return {.consumed = at};
}

// Append the `Width` bytes of `value` in the encoding's byte order.
template <std::size_t Width> void append_unit(char32_t value, Encoding encoding, Bytes &out) {
    for (std::size_t index = 0; index < Width; ++index) {
        const auto shift = 8 * (encoding.little ? index : Width - 1 - index);
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void encode_utf8(char32_t code, Bytes &out) {
    if (code < 0x80) {
        out.push_back(static_cast<std::byte>(code));
        return;
    }
    const std::size_t length = code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
    static constexpr std::array<unsigned, 5> LEAD{0, 0, 0xC0, 0xE0, 0xF0};
    out.push_back(static_cast<std::byte>(LEAD.at(length) | (code >> (6 * (length - 1)))));
    for (auto shift = 6 * (length - 1); shift > 0; shift -= 6) {
        out.push_back(static_cast<std::byte>(0x80U | ((code >> (shift - 6)) & 0x3FU)));
    }
}

// The atom spelling of a term, or empty for a non-atom.
std::string_view spelling(const Term &term) { return term.is_atom() ? term.atom_spelling_unchecked() : ""; }

// A {utf16 | utf32, big | little} encoding.
Encoding endian_encoding(const Term &argument) {
    if (argument.tuple_size_unchecked() != 2) {
        bad_argument();
    }
    const auto name = spelling(argument.tuple_element_unchecked(0));
    const auto order = spelling(argument.tuple_element_unchecked(1));
    if ((name != "utf16" && name != "utf32") || (order != "big" && order != "little")) {
        bad_argument();
    }
    return {.kind = name == "utf16" ? Kind::utf16 : Kind::utf32, .little = order == "little"};
}
} // namespace

Encoding encoding(const Term &argument) {
    if (argument.is_tuple()) {
        return endian_encoding(argument);
    }
    const auto name = spelling(argument);
    if (name == "latin1") {
        return {.kind = Kind::latin1};
    }
    if (name == "unicode" || name == "utf8") {
        return {.kind = Kind::utf8};
    }
    if (name == "utf16" || name == "utf32") {
        return {.kind = name == "utf16" ? Kind::utf16 : Kind::utf32};
    }
    bad_argument();
}

Decoded decode(std::span<const std::byte> bytes, Encoding encoding, std::u32string &codes) {
    switch (encoding.kind) {
    case Kind::latin1:
        for (const auto byte : bytes) {
            codes.push_back(std::to_integer<char32_t>(byte));
        }
        return {.consumed = bytes.size()};
    case Kind::utf8:
        return decode_utf8(bytes, codes);
    case Kind::utf16:
        return decode_utf16(bytes, encoding.little, codes);
    case Kind::utf32:
        return decode_utf32(bytes, encoding.little, codes);
    }
    return {};
}

bool scalar(std::int64_t code) { return code >= 0 && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF); }

void encode(char32_t code, Encoding encoding, Bytes &out) {
    switch (encoding.kind) {
    case Kind::latin1:
        out.push_back(static_cast<std::byte>(code));
        break;
    case Kind::utf8:
        encode_utf8(code, out);
        break;
    case Kind::utf16:
        if (code < 0x10000) {
            append_unit<2>(code, encoding, out);
        } else {
            append_unit<2>(0xD800 + ((code - 0x10000) >> 10), encoding, out);
            append_unit<2>(0xDC00 + ((code - 0x10000) & 0x3FFU), encoding, out);
        }
        break;
    case Kind::utf32:
        append_unit<4>(code, encoding, out);
        break;
    }
}

Bytes byte_order_mark(Encoding encoding) {
    Bytes mark;
    if (encoding.kind != Kind::latin1) {
        encode(0xFEFF, encoding, mark);
    }
    return mark;
}
} // namespace clause::runtime::builtins::unicode
