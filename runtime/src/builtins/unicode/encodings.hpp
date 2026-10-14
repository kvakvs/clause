#pragma once
#include "../typed.hpp"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Character encodings of the unicode module (plan step 41Y1, docs/builtins.md#unicode-module): latin1, utf8
// (also spelled unicode), utf16 and utf32 in either byte order, decoded from and encoded to bytes.
namespace clause::runtime::builtins::unicode {
using Bytes = std::vector<std::byte>;

// One encoding.
struct Encoding {
    enum class Kind : std::uint8_t { latin1, utf8, utf16, utf32 };
    Kind kind = Kind::utf8;
    // Byte order of utf16 and utf32.
    bool little = false;
};

// An encoding argument: latin1, unicode, utf8, utf16, {utf16, Endian}, utf32 or {utf32, Endian}; badarg otherwise.
Encoding encoding(const Term &argument);

// How decoding a byte sequence ended.
struct Decoded {
    // The bytes consumed: up to an invalid sequence, or up to an incomplete one at the end.
    std::size_t consumed = 0;
    // Whether decoding stopped at an invalid sequence (rather than at the end or an incomplete sequence).
    bool invalid = false;
};

// Decode `bytes` in `encoding`, appending code points to `codes`.
Decoded decode(std::span<const std::byte> bytes, Encoding encoding, std::u32string &codes);

// Whether `code` is a Unicode scalar value (no surrogate, at most 10FFFF).
bool scalar(std::int64_t code);

// Append the encoding of the scalar value `code` to `out`; latin1 takes only codes up to 255.
void encode(char32_t code, Encoding encoding, Bytes &out);

// The byte order mark of an encoding (empty for latin1).
Bytes byte_order_mark(Encoding encoding);

// An encoding argument as passed; encoding() checks it.
struct EncodingArgument {
    Term term;
};
} // namespace clause::runtime::builtins::unicode

namespace clause::runtime::builtins {
// Any term as an encoding argument.
template <> struct Argument<unicode::EncodingArgument> {
    static TermResult<std::optional<unicode::EncodingArgument>> convert(const Term &term) {
        return unicode::EncodingArgument{term};
    }
};
} // namespace clause::runtime::builtins
