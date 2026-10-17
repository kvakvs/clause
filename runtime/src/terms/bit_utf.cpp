#include "bitstrings.hpp"
#include "terms.hpp"
#include <clause/runtime/process_context.hpp>

namespace clause::runtime::detail {
namespace {
// Unicode scalar validity excludes surrogate code points and values above the Unicode range.
bool scalar(std::uint32_t value) { return value <= 0x10ffff && (value < 0xd800 || value > 0xdfff); }

// Decode only proved in-range fields, leaving the enclosing cursor untouched on failure.
TermResult<std::uint32_t> field(const BitView &view, std::size_t &cursor, std::size_t width, bool little) {
    if (width > view.length - cursor) {
        return std::unexpected(TermError::invalid_argument);
    }
    const auto value = bit_integer({view.bytes, view.offset + cursor, width}, little).convert_to<std::uint32_t>();
    cursor += width;
    return value;
}

// Classify valid leading-byte ranges independently from continuation and scalar validation.
unsigned utf8_count(std::uint32_t first) {
    if (first >= 0xc2 && first <= 0xdf) {
        return 2;
    }
    if (first >= 0xe0 && first <= 0xef) {
        return 3;
    }
    if (first >= 0xf0 && first <= 0xf4) {
        return 4;
    }
    return 0;
}

// Reject overlong UTF-8, invalid leading/continuation bytes and surrogate encodings.
TermResult<std::uint32_t> utf8(const BitView &view, std::size_t &cursor) {
    const auto first = field(view, cursor, 8, false);
    if (!first) {
        return std::unexpected(first.error());
    }
    if (*first < 128) {
        return *first;
    }
    const auto count = utf8_count(*first);
    if (count == 0) {
        return std::unexpected(TermError::invalid_argument);
    }
    std::uint32_t value = *first & (0x7fU >> count);
    for (unsigned i = 1; i < count; ++i) {
        const auto next = field(view, cursor, 8, false);
        if (!next || (*next & 0xc0) != 0x80) {
            return std::unexpected(TermError::invalid_argument);
        }
        value = (value << 6) | (*next & 63);
    }
    const std::array minimum{0U, 0U, 0x80U, 0x800U, 0x10000U};
    return scalar(value) && value >= minimum[count] ? TermResult<std::uint32_t>{value}
                                                    : std::unexpected(TermError::invalid_argument);
}

// UTF-16 consumes a second code unit only for a high surrogate and requires a matching low surrogate.
TermResult<std::uint32_t> utf16(const BitView &view, std::size_t &cursor, bool little) {
    const auto first = field(view, cursor, 16, little);
    if (!first) {
        return std::unexpected(first.error());
    }
    if (*first < 0xd800 || *first > 0xdbff) {
        return scalar(*first) ? first : std::unexpected(TermError::invalid_argument);
    }
    const auto second = field(view, cursor, 16, little);
    if (!second || *second < 0xdc00 || *second > 0xdfff) {
        return std::unexpected(TermError::invalid_argument);
    }
    return 0x10000 + ((*first - 0xd800) << 10) + *second - 0xdc00;
}

// Construct UTF-8 bytes without depending on host text encodings.
TermResult<void> write_utf8(BitWriter &writer, std::uint32_t value) {
    if (value < 0x80) {
        return writer.integer(Integer{value}, 8, false);
    }
    const unsigned count = value < 0x800 ? 2 : (value < 0x10000 ? 3 : 4);
    const auto leading = (0xffU << (8 - count)) | (value >> (6 * (count - 1)));
    auto result = writer.integer(Integer{leading}, 8, false);
    for (unsigned i = count - 1; i != 0 && result; --i) {
        result = writer.integer(Integer{0x80U | ((value >> (6 * (i - 1))) & 63)}, 8, false);
    }
    return result;
}
} // namespace

TermResult<void> bit_utf_construct(BitWriter &writer, const BitSegment &segment) {
    const auto integer = integer_read(segment.value);
    if (!integer) {
        return std::unexpected(integer.error());
    }
    if (*integer < 0 || *integer > 0x10ffff) {
        return std::unexpected(TermError::invalid_argument);
    }
    const auto value = integer->convert_to<std::uint32_t>();
    if (!scalar(value)) {
        return std::unexpected(TermError::invalid_argument);
    }
    if (segment.type == abi::v1::BitType::utf8) {
        return write_utf8(writer, value);
    }
    if (segment.type == abi::v1::BitType::utf16 && value >= 0x10000) {
        const auto first = writer.integer(Integer{0xd800U + ((value - 0x10000) >> 10)}, 16, segment.little);
        if (!first) {
            return first;
        }
        return writer.integer(Integer{0xdc00U + ((value - 0x10000) & 1023)}, 16, segment.little);
    }
    return writer.integer(Integer{value}, segment.type == abi::v1::BitType::utf16 ? 16 : 32, segment.little);
}

TermResult<BitExtract> bit_utf_extract(ProcessContext &context, const BitView &view, std::size_t cursor,
                                       const BitSegment &segment) {
    using Type = abi::v1::BitType;
    const auto value = segment.type == Type::utf8    ? utf8(view, cursor)
                       : segment.type == Type::utf16 ? utf16(view, cursor, segment.little)
                                                     : field(view, cursor, 32, segment.little);
    if (!value || !scalar(*value)) {
        return std::unexpected(TermError::invalid_argument);
    }
    return TermFactory(context).integer(*value).transform([&](Term term) { return BitExtract{term, cursor}; });
}
} // namespace clause::runtime::detail
