#include "format.hpp"

namespace clause::runtime::builtins::dets {
int log2(std::uint64_t value) {
    if (value == 0) {
        return 0;
    }
    int bits = 0;
    while (power(bits) < value) {
        ++bits;
    }
    return bits == 0 ? 1 : bits;
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value = (value << 8U) | std::to_integer<std::uint32_t>(bytes[at + index]);
    }
    return value;
}

void append_u32(Bytes &out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::byte>((value >> static_cast<unsigned>(shift)) & 0xFFU));
    }
}

void put_u32(std::span<std::byte> bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[at + index] = static_cast<std::byte>((value >> (8U * (3 - index))) & 0xFFU);
    }
}

void append(Bytes &out, std::span<const std::byte> bytes) { out.insert(out.end(), bytes.begin(), bytes.end()); }

int max_object_size(const std::map<int, std::int64_t> &collections) {
    int biggest = 0;
    for (const auto &[size, count] : collections) {
        if (count != 0) {
            biggest = size;
        }
    }
    return biggest;
}
} // namespace clause::runtime::builtins::dets
