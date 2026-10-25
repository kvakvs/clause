#pragma once
#include <cstdint>
#include <filesystem>
#include <system_error>

// Changes to a file other than its bytes (plan 21 step LIB50): a path's owner, permissions and times, and an open
// descriptor's length, reserved space and access advice, each done as OTP's prim_file does it on the host.
namespace clause::runtime::detail {
// A user and a group id; -1 keeps the current one.
struct FileOwner {
    std::int32_t user = -1;
    std::int32_t group = -1;
};

// Access, modification and creation times in POSIX seconds; the creation time is set on Windows only.
struct FileTimes {
    std::int64_t accessed = 0;
    std::int64_t modified = 0;
    std::int64_t created = 0;
};

// A range of an open file's bytes.
struct FileRange {
    std::int64_t offset = 0;
    std::int64_t length = 0;
};

// chown; nothing on Windows, as OTP.
std::error_code set_owner(const std::filesystem::path &path, FileOwner owner);
// chmod of the permission and set-id bits (retried without the set-id bits); on Windows the read-only attribute
// follows the owner's write bit.
std::error_code set_permissions(const std::filesystem::path &path, std::uint32_t mode);
// utime of the access and modification times; on Windows SetFileTime of all three, a read-only file made writable
// meanwhile.
std::error_code set_times(const std::filesystem::path &path, const FileTimes &times);

// Cut an open file at its descriptor's position.
std::error_code truncate_at_position(int fd);
// posix_fadvise where the host has it (advice 0 normal, 1 random, 2 sequential, 3 will_need, 4 dont_need, 5
// no_reuse); its result is ignored, as OTP's check of it never sees a failure.
void advise_access(int fd, FileRange range, unsigned advice);
// Reserve space for the range: fallocate then posix_fallocate on Linux, F_PREALLOCATE on macOS, enotsup elsewhere.
std::error_code allocate_space(int fd, FileRange range);
} // namespace clause::runtime::detail
