#include "../../terms/integers.hpp"
#include "binary_search.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// The binary module's number and hex encodings (plan step 41W, docs/builtins.md#binary-module):
// decode_unsigned/1,2, encode_unsigned/1,2, encode_hex/1,2 and decode_hex/1.
namespace clause::runtime::builtins::binary {
namespace {
// Whether an endianness argument is little; badarg unless it is big or little.
bool little_endian(const AtomArgument &endianness) {
    if (endianness.spelling != "big" && endianness.spelling != "little") {
        bad_argument();
    }
    return endianness.spelling == "little";
}

// decode_unsigned(Subject, Endianness): the unsigned integer the bytes encode.
TermResult<Term> decode_unsigned2(ProcessContext &context, const BinaryArgument &subject,
                                  const AtomArgument &endianness) {
    auto bytes = subject.bytes;
    if (little_endian(endianness)) {
        std::ranges::reverse(bytes);
    }
    detail::Integer value = 0;
    for (const auto byte : bytes) {
        value = (value << 8) | std::to_integer<unsigned>(byte);
    }
    return TermFactory(context).integer_decimal(detail::integer_text(value));
}

TermResult<Term> decode_unsigned1(ProcessContext &context, const BinaryArgument &subject) {
    return decode_unsigned2(context, subject, AtomArgument{.term = {}, .spelling = "big"});
}

// encode_unsigned(Unsigned, Endianness): the fewest bytes encoding a non-negative integer (one for 0).
TermResult<Term> encode_unsigned2(ProcessContext &context, const detail::Integer &value,
                                  const AtomArgument &endianness) {
    if (value < 0) {
        bad_argument();
    }
    Bytes bytes;
    for (auto rest = value; bytes.empty() || rest != 0; rest >>= 8) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned>(rest & 0xFF)));
    }
    if (!little_endian(endianness)) {
        std::ranges::reverse(bytes);
    }
    return TermFactory(context).binary(bytes);
}

TermResult<Term> encode_unsigned1(ProcessContext &context, const detail::Integer &value) {
    return encode_unsigned2(context, value, AtomArgument{.term = {}, .spelling = "big"});
}

// encode_hex(Subject, Case): two hex digits per byte, uppercase or lowercase.
TermResult<Term> encode_hex2(ProcessContext &context, const BinaryArgument &subject, const AtomArgument &letters) {
    if (letters.spelling != "uppercase" && letters.spelling != "lowercase") {
        bad_argument();
    }
    const std::string_view digits = letters.spelling == "uppercase" ? "0123456789ABCDEF" : "0123456789abcdef";
    Bytes text;
    for (const auto byte : subject.bytes) {
        const auto value = std::to_integer<unsigned>(byte);
        text.push_back(static_cast<std::byte>(digits[value >> 4]));
        text.push_back(static_cast<std::byte>(digits[value & 0xF]));
    }
    return TermFactory(context).binary(text);
}

TermResult<Term> encode_hex1(ProcessContext &context, const BinaryArgument &subject) {
    return encode_hex2(context, subject, AtomArgument{.term = {}, .spelling = "uppercase"});
}

// The value of one hex digit in either case; badarg for anything else.
unsigned hex_digit(std::byte character) {
    const auto code = std::to_integer<char>(character);
    if (code >= '0' && code <= '9') {
        return static_cast<unsigned>(code - '0');
    }
    if (code >= 'a' && code <= 'f') {
        return static_cast<unsigned>(code - 'a' + 10);
    }
    if (code >= 'A' && code <= 'F') {
        return static_cast<unsigned>(code - 'A' + 10);
    }
    bad_argument();
}

// decode_hex(Hex): the bytes of an even number of hex digits.
TermResult<Term> decode_hex(ProcessContext &context, const BinaryArgument &hex) {
    if (hex.bytes.size() % 2 != 0) {
        bad_argument();
    }
    Bytes bytes;
    for (std::size_t at = 0; at < hex.bytes.size(); at += 2) {
        bytes.push_back(static_cast<std::byte>(hex_digit(hex.bytes[at]) << 4 | hex_digit(hex.bytes[at + 1])));
    }
    return TermFactory(context).binary(bytes);
}

constexpr std::array ENCODING_BUILTINS{
    typed_entry<decode_unsigned1>("binary", "decode_unsigned"),
    typed_entry<decode_unsigned2>("binary", "decode_unsigned"),
    typed_entry<encode_unsigned1>("binary", "encode_unsigned"),
    typed_entry<encode_unsigned2>("binary", "encode_unsigned"),
    typed_entry<encode_hex1>("binary", "encode_hex"),
    typed_entry<encode_hex2>("binary", "encode_hex"),
    typed_entry<decode_hex>("binary", "decode_hex"),
};
} // namespace
} // namespace clause::runtime::builtins::binary

namespace clause::runtime {
std::span<const BuiltinEntry> binary_encoding_builtins() noexcept { return builtins::binary::ENCODING_BUILTINS; }
} // namespace clause::runtime
