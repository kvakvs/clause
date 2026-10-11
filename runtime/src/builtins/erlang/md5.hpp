#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// MD5 (RFC 1321) for md5/1, md5_init/0, md5_update/2, md5_final/1 and fun_info/1's new_uniq (plan step 41G3).
namespace clause::runtime::builtins {
class Md5 final {
  public:
    // Bytes of an md5_init/0 context binary: a tag, the state words, the length and the pending block.
    static constexpr std::size_t CONTEXT_BYTES = 96;

    // Add bytes to the digest.
    void update(std::span<const std::byte> bytes);
    // The 16-byte digest of everything added; the object is left finished.
    std::array<std::byte, 16> finish();

    // The state as an md5_init/0 context binary, and back; none for bytes that are no context.
    std::vector<std::byte> context() const;
    static std::optional<Md5> from_context(std::span<const std::byte> bytes);

  private:
    // Process one 64-byte block into the state.
    void block(std::span<const std::byte, 64> data);

    // The A, B, C, D words, the message length in bytes, and the bytes of the unfinished block.
    std::array<std::uint32_t, 4> state_{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    std::uint64_t length_ = 0;
    std::array<std::byte, 64> pending_{};
};
} // namespace clause::runtime::builtins
