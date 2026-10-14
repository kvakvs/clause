#include "file_info.hpp"
#include <cerrno>
#include <cstdint>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace clause::runtime::detail {
namespace {
#ifdef _WIN32
using Status = struct _stat64;
constexpr unsigned TYPE_MASK = _S_IFMT;
constexpr unsigned REGULAR = _S_IFREG;
constexpr unsigned DIRECTORY = _S_IFDIR;
constexpr unsigned DEVICE = _S_IFCHR;
constexpr unsigned SYMLINK = 0;
#else
using Status = struct stat;
constexpr unsigned TYPE_MASK = S_IFMT;
constexpr unsigned REGULAR = S_IFREG;
constexpr unsigned DIRECTORY = S_IFDIR;
constexpr unsigned DEVICE = S_IFCHR;
constexpr unsigned SYMLINK = S_IFLNK;
#endif

// Append `value` as `Size` big-endian bytes.
template <std::size_t Size> void field(std::vector<std::byte> &out, std::uint64_t value) {
    for (auto shift = Size; shift > 0; --shift) {
        out.push_back(static_cast<std::byte>((value >> (8 * (shift - 1))) & 0xFFU));
    }
}

// The type code of a mode.
std::uint8_t type_code(unsigned mode) {
    const auto type = mode & TYPE_MASK;
    if (type == REGULAR) {
        return 0;
    }
    if (type == DIRECTORY) {
        return 1;
    }
    if (SYMLINK != 0 && type == SYMLINK) {
        return 2;
    }
    return type == DEVICE ? 3 : 4;
}

// Whether the process may read and write the path: 1 read, 2 write, 3 both, 0 neither.
std::uint8_t access_code(const std::filesystem::path &path) {
#ifdef _WIN32
    const bool readable = _waccess(path.c_str(), 4) == 0;
    const bool writable = _waccess(path.c_str(), 2) == 0;
#else
    const bool readable = ::access(path.c_str(), R_OK) == 0;
    const bool writable = ::access(path.c_str(), W_OK) == 0;
#endif
    return static_cast<std::uint8_t>((readable ? 1 : 0) | (writable ? 2 : 0));
}

// The status of `path`, or false with errno set.
bool status_of(const std::filesystem::path &path, Status &status) {
#ifdef _WIN32
    return _wstat64(path.c_str(), &status) == 0;
#else
    return ::stat(path.c_str(), &status) == 0;
#endif
}
} // namespace

std::expected<std::vector<std::byte>, int> file_status(const std::filesystem::path &path) {
    Status status{};
    if (!status_of(path, status)) {
        return std::unexpected(errno);
    }
    std::vector<std::byte> out;
    field<8>(out, static_cast<std::uint64_t>(status.st_size));
    field<1>(out, type_code(static_cast<unsigned>(status.st_mode)));
    field<1>(out, access_code(path));
    field<8>(out, static_cast<std::uint64_t>(status.st_atime));
    field<8>(out, static_cast<std::uint64_t>(status.st_mtime));
    field<8>(out, static_cast<std::uint64_t>(status.st_ctime));
    field<4>(out, static_cast<std::uint64_t>(status.st_mode));
    field<4>(out, static_cast<std::uint64_t>(status.st_nlink));
    field<4>(out, static_cast<std::uint64_t>(status.st_dev));
    field<4>(out, static_cast<std::uint64_t>(status.st_rdev));
    field<8>(out, static_cast<std::uint64_t>(status.st_ino));
    field<4>(out, static_cast<std::uint64_t>(status.st_uid));
    field<4>(out, static_cast<std::uint64_t>(status.st_gid));
    return out;
}
} // namespace clause::runtime::detail
