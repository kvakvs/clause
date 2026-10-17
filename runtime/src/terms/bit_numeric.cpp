#include "bitstrings.hpp"
#include "floats.hpp"
#include "terms.hpp"
#include <algorithm>
#include <bit>
#include <clause/runtime/process_context.hpp>
#include <cmath>
#include <limits>
#include <optional>

namespace clause::runtime::detail {
namespace {
// Grow packed staging storage; only a bit count past the address range is refused (no size cap).
TermResult<void> grow(BitWriter &writer, std::size_t count) {
    if (count > std::numeric_limits<std::size_t>::max() - 7 - writer.length) {
        return std::unexpected(TermError::resource_limit);
    }
    writer.bytes.resize((writer.length + count + 7) / 8);
    return {};
}

// Set a logical output bit without exposing host byte order or retaining unused padding.
void put(BitWriter &writer, bool bit) {
    if (bit) {
        writer.bytes[writer.length / 8] |= static_cast<std::byte>(1U << (7 - writer.length % 8));
    }
    ++writer.length;
}

// Build a bounded power of two through the explicit bit API rather than shifting an inline limb.
Integer power(std::size_t bit) {
    Integer value = 0;
    boost::multiprecision::bit_set(value, static_cast<unsigned>(bit));
    return value;
}

// Convert extracted numeric bits only after width/endian validation has completed.
TermResult<Term> numeric_extract(ProcessContext &context, const BitView &view, std::size_t cursor, std::size_t width,
                                 const BitSegment &segment) {
    Integer bits = bit_integer({view.bytes, view.offset + cursor, width}, segment.little);
    if (segment.type == abi::v1::BitType::integer && segment.signed_value && width != 0 &&
        boost::multiprecision::bit_test(bits, static_cast<unsigned>(width - 1))) {
        bits = integer_sum(bits, power(width), true);
    }
    return segment.type == abi::v1::BitType::integer ? IntegerAccess::make(context.heap(), bits)
                                                     : bit_read_float(context, bits, width);
}

// Append a proved binary prefix without duplicating numeric or UTF conversion paths.
TermResult<void> binary_construct(BitWriter &writer, const BitSegment &segment) {
    const auto source = bit_view(segment.value);
    if (!source) {
        return std::unexpected(source.error());
    }
    const auto width = bit_width(segment, source->length);
    if (!width) {
        return std::unexpected(width.error());
    }
    if (*width > source->length) {
        return std::unexpected(TermError::invalid_argument);
    }
    return writer.append({source->bytes, source->offset, *width});
}

// The value of a small integer term; none for any other term (OPT0.9).
std::optional<std::int64_t> small_value(const Term &term) {
    if (term.kind() != TermKind::smallint) {
        return std::nullopt;
    }
    const auto value = decode_integer(term.word());
    return value ? std::optional{*value} : std::nullopt;
}

// An integer segment of at most one word: the two's complement bits of its value, its width and its byte order.
struct WordSegment {
    std::uint64_t bits;
    std::size_t count;
    bool little;
};

// The word form of an integer segment whose value and size are small integers and whose width is at most 64 bits;
// none for every other segment, which takes the multiprecision path (OPT0.9).
std::optional<WordSegment> word_segment(const BitSegment &segment) {
    const auto size = small_value(segment.size);
    const auto value = small_value(segment.value);
    if (segment.type != abi::v1::BitType::integer || !size || !value || *size < 0 ||
        static_cast<std::uint64_t>(*size) > 64 / segment.unit) {
        return std::nullopt;
    }
    return WordSegment{static_cast<std::uint64_t>(*value), static_cast<std::size_t>(*size) * segment.unit,
                       segment.little};
}

// Whole bytes at a byte boundary, most significant first unless little.
void word_bytes(BitWriter &writer, const WordSegment &word) {
    const auto bytes = word.count / 8;
    for (std::size_t i = 0; i < bytes; ++i) {
        const auto shift = 8 * (word.little ? i : bytes - 1 - i);
        writer.bytes[writer.length / 8 + i] = static_cast<std::byte>(word.bits >> shift);
    }
    writer.length += word.count;
}

// Write the low bits of a word segment in the endian groups of BitWriter::integer (OPT0.9).
TermResult<void> word_bits(BitWriter &writer, const WordSegment &word) {
    const auto grown = grow(writer, word.count);
    if (!grown) {
        return grown;
    }
    if (writer.length % 8 == 0 && word.count % 8 == 0) {
        word_bytes(writer, word);
        return {};
    }
    const auto count = word.count;
    for (std::size_t i = 0; i < count; ++i) {
        const auto group = i / 8 * 8;
        const auto index = word.little ? group + std::min(std::size_t{8}, count - group) - 1 - i % 8 : count - 1 - i;
        put(writer, ((word.bits >> index) & 1U) != 0);
    }
    return {};
}
} // namespace

TermResult<void> BitWriter::append(const BitView &source) {
    const auto grown = grow(*this, source.length);
    if (!grown) {
        return grown;
    }
    std::size_t i = 0;
    // Byte-aligned whole bytes copy at once; the remaining bits go one at a time.
    if (length % 8 == 0 && source.offset % 8 == 0) {
        i = source.length / 8 * 8;
        std::ranges::copy(source.bytes.subspan(source.offset / 8, i / 8),
                          bytes.begin() + static_cast<std::ptrdiff_t>(length / 8));
        length += i;
    }
    for (; i < source.length; ++i) {
        put(*this, bit_at(source, i));
    }
    return {};
}

TermResult<void> BitWriter::integer(const Integer &value, std::size_t count, bool little) {
    const auto grown = grow(*this, count);
    if (!grown) {
        return grown;
    }
    // Two's complement of a negative value: bit i is the inverse of bit i of -(value + 1), so no integer as wide as
    // the segment is built.
    const bool negative = value < 0;
    const Integer bits = negative ? Integer(-integer_sum(value, 1, false)) : value;
    const auto significant = integer_bits(bits);
    for (std::size_t i = 0; i < count; ++i) {
        const auto group = i / 8 * 8;
        const auto index = little ? group + std::min(std::size_t{8}, count - group) - 1 - i % 8 : count - 1 - i;
        const bool set = index < significant && boost::multiprecision::bit_test(bits, static_cast<unsigned>(index));
        put(*this, set != negative);
    }
    return {};
}

Integer bit_integer(const BitView &view, bool little) {
    const auto count = view.length;
    Integer result = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (bit_at(view, i)) {
            const auto group = i / 8 * 8;
            const auto index = little ? group + std::min(std::size_t{8}, count - group) - 1 - i % 8 : count - 1 - i;
            boost::multiprecision::bit_set(result, static_cast<unsigned>(index));
        }
    }
    return result;
}

TermResult<std::size_t> bit_width(const BitSegment &segment, std::size_t remaining) {
    if (segment.all) {
        if (remaining % segment.unit != 0) {
            return std::unexpected(TermError::invalid_argument);
        }
        return remaining;
    }
    const auto size = integer_read(segment.size);
    if (!size) {
        return std::unexpected(size.error());
    }
    if (*size < 0) {
        return std::unexpected(TermError::invalid_argument);
    }
    if (*size > std::numeric_limits<std::size_t>::max() / segment.unit) {
        return std::unexpected(TermError::out_of_range);
    }
    return size->convert_to<std::size_t>() * segment.unit;
}

TermResult<void> bit_word_construct(BitWriter &writer, std::uint64_t bits, std::size_t count, bool little) {
    return word_bits(writer, {bits, count, little});
}

TermResult<void> bit_construct(BitWriter &writer, const BitSegment &segment) {
    using Type = abi::v1::BitType;
    if (segment.empty) {
        if (segment.type >= Type::utf8) {
            return {};
        }
        return bit_width(segment, 0).transform([](std::size_t) {});
    }
    if (segment.type >= Type::utf8) {
        return bit_utf_construct(writer, segment);
    }
    if (segment.type == Type::binary) {
        return binary_construct(writer, segment);
    }
    if (const auto word = word_segment(segment)) {
        return word_bits(writer, *word);
    }
    const auto width = bit_width(segment, 0);
    if (!width) {
        return std::unexpected(width.error());
    }
    const auto bits =
        segment.type == Type::integer ? integer_read(segment.value) : bit_float_bits(segment.value, *width);
    if (!bits) {
        return std::unexpected(bits.error());
    }
    return writer.integer(*bits, *width, segment.little);
}

TermResult<BitExtract> bit_extract(ProcessContext &context, const Term &source, std::size_t cursor,
                                   const BitSegment &segment) {
    const auto view = bit_view(source);
    if (!view) {
        return std::unexpected(view.error());
    }
    if (cursor > view->length) {
        return std::unexpected(TermError::out_of_range);
    }
    using Type = abi::v1::BitType;
    if (segment.type >= Type::utf8) {
        return bit_utf_extract(context, *view, cursor, segment);
    }
    const auto width = bit_width(segment, view->length - cursor);
    if (!width) {
        return std::unexpected(width.error());
    }
    if (*width > view->length - cursor) {
        return std::unexpected(TermError::out_of_range);
    }
    if (segment.type == Type::binary) {
        return BitAccess::slice(context.heap(), source, cursor, *width).transform([&](Term value) {
            return BitExtract{value, cursor + *width};
        });
    }
    const auto result = numeric_extract(context, *view, cursor, *width, segment);
    return result.transform([&](Term value) { return BitExtract{value, cursor + *width}; });
}
} // namespace clause::runtime::detail
