#pragma once
// Module literals (literal heap): every compound literal of a module, encoded at compile time into one constant byte
// string beside its descriptor, then decoded once per runtime at registration into that runtime's literal heap. The
// encoding is BEAM's compact term encoding (a tag in the low three bits of the first byte, small values in its high
// bits); Clause's extended tags add the containers, floats and bitstrings that BEAM keeps in its LitT chunk instead.
// Shared by the compiler (Writer) and the runtime (Reader). Added for the literal heap.
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace clause::abi::literals {
// Base tags (low three bits): unsigned counts, signed integers (any size), atoms (module atom slot + 1; 0 is []) and
// the extended tags.
enum class Tag : std::uint8_t { unsigned_value = 0, integer = 1, atom = 2, extended = 7 };

// Extended tags (the high four bits of a byte whose base tag is extended). A list is its length, the elements and the
// tail; a tuple its arity and the elements; a float its IEEE 754 binary64 bits (8 bytes, big-endian); a bitstring its
// bit count and its bytes (the last one holding its final bits in the high positions); a map its pair count and the
// keys and values; a string a proper list of byte values, as its length and the bytes.
enum class Extended : std::uint8_t { list = 1, tuple = 5, floating = 6, bitstring = 7, map = 8, string = 9 };

// Malformed or truncated encoded literals.
class FormatError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Values below this fit in the first byte (small), below MEDIUM_LIMIT in two (medium); larger ones follow as
// big-endian bytes, signed for integers.
inline constexpr std::uint64_t SMALL_LIMIT = 16;
inline constexpr std::uint64_t MEDIUM_LIMIT = 0x800;
// Byte counts of at most this many are written in the first byte; longer ones follow it as an unsigned value.
inline constexpr std::size_t INLINE_COUNT_LIMIT = 8;

class Writer {
  public:
    explicit Writer(std::string &out) : out_(out) {}

    // An unsigned count, such as a length or an arity.
    void count(const std::uint64_t value) { unsigned_value(Tag::unsigned_value, value); }

    // A signed integer of at most 64 bits.
    void integer(const std::int64_t value) {
        if (value >= 0 && static_cast<std::uint64_t>(value) < MEDIUM_LIMIT) {
            unsigned_value(Tag::integer, static_cast<std::uint64_t>(value));
            return;
        }
        std::array<std::uint8_t, 8> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) >> (8 * (7 - i)));
        }
        integer_bytes(minimal(bytes));
    }

    // A signed integer of any size as big-endian two's complement bytes, at least the sign bit's worth.
    void integer_bytes(const std::span<const std::uint8_t> big_endian) { large(Tag::integer, big_endian); }

    // An atom of the module's atom slot `slot`, or [] for none.
    void atom(const std::size_t slot) { unsigned_value(Tag::atom, slot + 1); }

    void nil() { unsigned_value(Tag::atom, 0); }

    // The header of an extended term; its contents follow.
    void extended(const Extended tag) {
        out_.push_back(static_cast<char>((static_cast<unsigned>(tag) << 4U) | static_cast<unsigned>(Tag::extended)));
    }

    void floating(const double value) {
        extended(Extended::floating);
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (int shift = 56; shift >= 0; shift -= 8) {
            out_.push_back(static_cast<char>((bits >> shift) & 0xFFU));
        }
    }

    void bitstring(const std::string_view bytes, const std::size_t bit_count) {
        extended(Extended::bitstring);
        count(bit_count);
        out_ += bytes;
    }

    void string(const std::string_view bytes) {
        extended(Extended::string);
        count(bytes.size());
        out_ += bytes;
    }

  private:
    // The trailing bytes of a big-endian two's complement value that still hold its sign, at least two.
    static std::span<const std::uint8_t> minimal(const std::span<const std::uint8_t> bytes) {
        std::size_t first = 0;
        while (bytes.size() - first > 2) {
            const auto byte = bytes[first];
            const auto next_sign = bytes[first + 1] & 0x80U;
            if ((byte == 0 && next_sign == 0) || (byte == 0xFF && next_sign != 0)) {
                ++first;
            } else {
                break;
            }
        }
        return bytes.subspan(first);
    }

    // A non-negative value: small, medium or large form.
    void unsigned_value(const Tag tag, const std::uint64_t value) {
        const auto base = static_cast<unsigned>(tag);
        if (value < SMALL_LIMIT) {
            out_.push_back(static_cast<char>((value << 4U) | base));
        } else if (value < MEDIUM_LIMIT) {
            out_.push_back(static_cast<char>(((value >> 3U) & 0xE0U) | 0x08U | base));
            out_.push_back(static_cast<char>(value & 0xFFU));
        } else {
            std::array<std::uint8_t, 9> bytes{};
            for (std::size_t i = 1; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>(value >> (8 * (8 - i)));
            }
            // A leading zero byte keeps the top bit clear: the large form is read as two's complement for integers.
            large(tag, minimal(bytes));
        }
    }

    // The large form: the byte count in the first byte up to INLINE_COUNT_LIMIT, else as a count after it.
    void large(const Tag tag, const std::span<const std::uint8_t> bytes) {
        const auto base = static_cast<unsigned>(tag);
        if (bytes.size() <= INLINE_COUNT_LIMIT) {
            out_.push_back(static_cast<char>(((bytes.size() - 2) << 5U) | 0x18U | base));
        } else {
            out_.push_back(static_cast<char>(0xF8U | base));
            count(bytes.size() - INLINE_COUNT_LIMIT - 1);
        }
        out_.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }

    std::string &out_;
};

// One value the reader decoded: its tag (or extended tag), a small value or count, and the big-endian two's
// complement bytes of a large one.
struct Item {
    Tag tag = Tag::unsigned_value;
    Extended extended = Extended::list;
    std::uint64_t value = 0;
    std::string_view bytes = {};
    bool large = false;
};

class Reader {
  public:
    explicit Reader(const std::string_view bytes) : bytes_(bytes) {}

    bool done() const { return at_ == bytes_.size(); }

    // The next value or extended header; a float's and a bitstring's or string's bytes are read by bytes().
    Item next() {
        const auto first = take();
        Item item{.tag = static_cast<Tag>(first & 7U)};
        if (item.tag == Tag::extended) {
            item.extended = static_cast<Extended>(first >> 4U);
            return item;
        }
        if ((first & 0x08U) == 0) {
            item.value = first >> 4U;
        } else if ((first & 0x10U) == 0) {
            item.value = (static_cast<std::uint64_t>(first & 0xE0U) << 3U) | take();
        } else {
            item.large = true;
            const auto inline_count = (first >> 5U) + 2U;
            const auto count = inline_count <= INLINE_COUNT_LIMIT ? inline_count : unsigned_count() + 9;
            item.bytes = bytes(count);
            item.value = small_value(item.bytes);
        }
        return item;
    }

    // A count written by Writer::count.
    std::uint64_t unsigned_count() {
        const auto item = next();
        if (item.tag != Tag::unsigned_value || item.bytes.size() > 9) {
            throw FormatError("expected a count");
        }
        return item.value;
    }

    std::string_view bytes(const std::size_t count) {
        if (count > bytes_.size() - at_) {
            throw FormatError("truncated literal");
        }
        const auto result = bytes_.substr(at_, count);
        at_ += count;
        return result;
    }

    double floating() {
        std::uint64_t bits = 0;
        for (const auto byte : bytes(8)) {
            bits = (bits << 8U) | static_cast<std::uint8_t>(byte);
        }
        return std::bit_cast<double>(bits);
    }

  private:
    std::uint8_t take() { return static_cast<std::uint8_t>(bytes(1).front()); }

    // The value of up to eight big-endian bytes read as unsigned; 0 for longer ones.
    static std::uint64_t small_value(const std::string_view bytes) {
        if (bytes.size() > 9 || (bytes.size() == 9 && bytes.front() != 0)) {
            return 0;
        }
        std::uint64_t value = 0;
        for (const auto byte : bytes) {
            value = (value << 8U) | static_cast<std::uint8_t>(byte);
        }
        return value;
    }

    std::string_view bytes_;
    std::size_t at_ = 0;
};
} // namespace clause::abi::literals
