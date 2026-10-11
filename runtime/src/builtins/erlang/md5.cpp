#include "md5.hpp"
#include "../typed.hpp"
#include "ports.hpp"
#include "terms.hpp"
#include <algorithm>
#include <bit>
#include <cstring>

namespace clause::runtime::builtins {
namespace {
// The per-step shift amounts and sine-derived constants of RFC 1321.
constexpr std::array<std::uint32_t, 16> SHIFTS{7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};

constexpr std::array<std::uint32_t, 64> SINES{
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

// The tag that starts a context binary.
constexpr std::array<std::byte, 4> TAG{std::byte{'C'}, std::byte{'M'}, std::byte{'D'}, std::byte{'5'}};

// A little-endian word of four bytes.
std::uint32_t word_at(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[at + i]) << (8 * i);
    }
    return value;
}

// Append `value` as `Count` little-endian bytes.
template <std::size_t Count> void put(std::vector<std::byte> &out, std::uint64_t value) {
    for (std::size_t i = 0; i < Count; ++i) {
        out.push_back(static_cast<std::byte>(value >> (8 * i)));
    }
}

// One round's mixing function and message index for step `i`.
std::pair<std::uint32_t, std::size_t> round_step(std::size_t i, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    if (i < 16) {
        return {(b & c) | (~b & d), i};
    }
    if (i < 32) {
        return {(d & b) | (~d & c), (5 * i + 1) % 16};
    }
    if (i < 48) {
        return {b ^ c ^ d, (3 * i + 5) % 16};
    }
    return {c ^ (b | ~d), (7 * i) % 16};
}
} // namespace

void Md5::block(std::span<const std::byte, 64> data) {
    std::array<std::uint32_t, 16> words{};
    for (std::size_t i = 0; i < words.size(); ++i) {
        words[i] = word_at(data, 4 * i);
    }
    auto [a, b, c, d] = state_;
    for (std::size_t i = 0; i < 64; ++i) {
        const auto [mixed, index] = round_step(i, b, c, d);
        const auto rotated =
            std::rotl(a + mixed + SINES[i] + words[index], static_cast<int>(SHIFTS[(i / 16) * 4 + i % 4]));
        a = d;
        d = c;
        c = b;
        b += rotated;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
}

void Md5::update(std::span<const std::byte> bytes) {
    for (const auto byte : bytes) {
        pending_[length_ % 64] = byte;
        ++length_;
        if (length_ % 64 == 0) {
            block(pending_);
        }
    }
}

std::array<std::byte, 16> Md5::finish() {
    const auto bits = length_ * 8;
    const std::array<std::byte, 1> marker{std::byte{0x80}};
    update(marker);
    const std::array<std::byte, 1> zero{};
    while (length_ % 64 != 56) {
        update(zero);
    }
    std::vector<std::byte> size;
    put<8>(size, bits);
    update(size);
    std::array<std::byte, 16> digest{};
    for (std::size_t i = 0; i < 16; ++i) {
        digest[i] = static_cast<std::byte>(state_[i / 4] >> (8 * (i % 4)));
    }
    return digest;
}

std::vector<std::byte> Md5::context() const {
    std::vector<std::byte> out(TAG.begin(), TAG.end());
    for (const auto word : state_) {
        put<4>(out, word);
    }
    put<8>(out, length_);
    out.insert(out.end(), pending_.begin(), pending_.end());
    out.resize(CONTEXT_BYTES);
    return out;
}

std::optional<Md5> Md5::from_context(std::span<const std::byte> bytes) {
    if (bytes.size() != CONTEXT_BYTES || !std::ranges::equal(bytes.first(4), TAG)) {
        return std::nullopt;
    }
    Md5 result;
    for (std::size_t i = 0; i < 4; ++i) {
        result.state_[i] = word_at(bytes, 4 + 4 * i);
    }
    result.length_ = word_at(bytes, 20) | (std::uint64_t{word_at(bytes, 24)} << 32);
    std::ranges::copy(bytes.subspan(28, 64), result.pending_.begin());
    return result;
}

namespace {
// The bytes of iodata; badarg for anything else.
std::vector<std::byte> data_of(const Term &data) {
    auto bytes = iodata_bytes(data);
    if (!bytes) {
        bad_argument();
    }
    return std::move(*bytes);
}

// The state of a context binary; badarg for anything else.
Md5 context_of(const BinaryArgument &context) {
    auto state = Md5::from_context(context.bytes);
    if (!state) {
        bad_argument();
    }
    return *state;
}

TermResult<Term> binary(ProcessContext &context, std::span<const std::byte> bytes) {
    return TermFactory(context).binary(bytes);
}

// md5(Data): the digest of iodata.
TermResult<Term> md5(ProcessContext &context, const Term &data) {
    Md5 state;
    state.update(data_of(data));
    return binary(context, state.finish());
}

TermResult<Term> md5_init(ProcessContext &context) { return binary(context, Md5{}.context()); }

TermResult<Term> md5_update(ProcessContext &context, const BinaryArgument &digest, const Term &data) {
    auto state = context_of(digest);
    state.update(data_of(data));
    return binary(context, state.context());
}

TermResult<Term> md5_final(ProcessContext &context, const BinaryArgument &digest) {
    auto state = context_of(digest);
    return binary(context, state.finish());
}

constexpr std::array MD5_BUILTINS{
    typed_entry<md5>("erlang", "md5"),
    typed_entry<md5_init>("erlang", "md5_init"),
    typed_entry<md5_update>("erlang", "md5_update"),
    typed_entry<md5_final>("erlang", "md5_final"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> md5_builtins() noexcept { return builtins::MD5_BUILTINS; }
} // namespace clause::runtime
