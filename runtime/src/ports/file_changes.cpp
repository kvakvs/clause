#include "file_changes.hpp"
#include <array>
#include <cerrno>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#endif

namespace clause::runtime::detail {
namespace {
#ifdef _WIN32
// The error code of the calling thread's last Windows error.
std::error_code last_error() { return {static_cast<int>(GetLastError()), std::system_category()}; }

// A FILETIME of POSIX seconds: 100-nanosecond ticks since 1601.
FILETIME file_time(std::int64_t seconds) {
    constexpr std::int64_t EPOCH_SECONDS = 11644473600;
    constexpr std::int64_t TICKS_PER_SECOND = 10000000;
    const auto ticks = static_cast<std::uint64_t>((seconds + EPOCH_SECONDS) * TICKS_PER_SECOND);
    return FILETIME{static_cast<DWORD>(ticks & 0xFFFFFFFFU), static_cast<DWORD>(ticks >> 32U)};
}

// SetFileTime of a path that may be written.
std::error_code write_times(const std::filesystem::path &path, const FileTimes &times) {
    const HANDLE handle =
        CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return last_error();
    }
    const auto created = file_time(times.created);
    const auto accessed = file_time(times.accessed);
    const auto modified = file_time(times.modified);
    const auto code = SetFileTime(handle, &created, &accessed, &modified) != 0 ? std::error_code{} : last_error();
    CloseHandle(handle);
    return code;
}
#else
// The error code of errno.
std::error_code errno_code() { return {errno, std::generic_category()}; }
#endif
} // namespace

#ifdef _WIN32
std::error_code set_owner(const std::filesystem::path &, FileOwner) { return {}; }

std::error_code set_permissions(const std::filesystem::path &path, std::uint32_t mode) {
    auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return last_error();
    }
    attributes = (mode & _S_IWRITE) != 0 ? attributes & ~DWORD{FILE_ATTRIBUTE_READONLY}
                                         : attributes | DWORD{FILE_ATTRIBUTE_READONLY};
    return SetFileAttributesW(path.c_str(), attributes) != 0 ? std::error_code{} : last_error();
}

std::error_code set_times(const std::filesystem::path &path, const FileTimes &times) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return last_error();
    }
    const bool read_only = (attributes & FILE_ATTRIBUTE_READONLY) != 0;
    if (read_only && SetFileAttributesW(path.c_str(), attributes & ~DWORD{FILE_ATTRIBUTE_READONLY}) == 0) {
        return last_error();
    }
    const auto code = write_times(path, times);
    if (read_only) {
        SetFileAttributesW(path.c_str(), attributes);
    }
    return code;
}

std::error_code truncate_at_position(int fd) {
    const auto position = _lseeki64(fd, 0, SEEK_CUR);
    const auto error = position < 0 ? errno : _chsize_s(fd, position);
    return {error, std::generic_category()};
}

void advise_access(int, FileRange, unsigned) {}

std::error_code allocate_space(int, FileRange) { return std::make_error_code(std::errc::not_supported); }
#else
std::error_code set_owner(const std::filesystem::path &path, FileOwner owner) {
    return ::chown(path.c_str(), static_cast<uid_t>(owner.user), static_cast<gid_t>(owner.group)) == 0
               ? std::error_code{}
               : errno_code();
}

std::error_code set_permissions(const std::filesystem::path &path, std::uint32_t mode) {
    constexpr mode_t SET_IDS = S_ISUID | S_ISGID;
    const auto modes = static_cast<mode_t>(mode) & (SET_IDS | S_IRWXU | S_IRWXG | S_IRWXO);
    if (::chmod(path.c_str(), modes) == 0 || ::chmod(path.c_str(), modes & ~SET_IDS) == 0) {
        return {};
    }
    return errno_code();
}

std::error_code set_times(const std::filesystem::path &path, const FileTimes &times) {
    const utimbuf value{static_cast<time_t>(times.accessed), static_cast<time_t>(times.modified)};
    return ::utime(path.c_str(), &value) == 0 ? std::error_code{} : errno_code();
}

std::error_code truncate_at_position(int fd) {
    const auto position = ::lseek(fd, 0, SEEK_CUR);
    return position >= 0 && ::ftruncate(fd, position) == 0 ? std::error_code{} : errno_code();
}

#if defined(__linux__)
void advise_access(int fd, FileRange range, unsigned advice) {
    static constexpr std::array<int, 6> ADVICE{POSIX_FADV_NORMAL,   POSIX_FADV_RANDOM,   POSIX_FADV_SEQUENTIAL,
                                               POSIX_FADV_WILLNEED, POSIX_FADV_DONTNEED, POSIX_FADV_NOREUSE};
    if (advice < ADVICE.size()) {
        static_cast<void>(::posix_fadvise(fd, range.offset, range.length, ADVICE.at(advice)));
    }
}

std::error_code allocate_space(int fd, FileRange range) {
    int result = 0;
    do {
        result = ::fallocate(fd, 0, range.offset, range.length);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        return {};
    }
    do {
        result = ::posix_fallocate(fd, range.offset, range.length);
    } while (result == EINTR);
    return {result, std::generic_category()};
}
#elif defined(__APPLE__)
void advise_access(int, FileRange, unsigned) {}

std::error_code allocate_space(int fd, FileRange range) {
    const auto original = ::lseek(fd, 0, SEEK_CUR);
    const auto end = original < 0 ? original : ::lseek(fd, 0, SEEK_END);
    if (end < 0 || ::lseek(fd, original, SEEK_SET) < 0) {
        return errno_code();
    }
    if (range.offset + range.length <= end) {
        return {};
    }
    fstore_t store{F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, range.offset + range.length - end, 0};
    if (::fcntl(fd, F_PREALLOCATE, &store) < 0) {
        store.fst_flags = F_ALLOCATEALL;
        if (::fcntl(fd, F_PREALLOCATE, &store) < 0) {
            return errno_code();
        }
    }
    return ::ftruncate(fd, range.offset + range.length) == 0 ? std::error_code{} : errno_code();
}
#else
void advise_access(int, FileRange, unsigned) {}

std::error_code allocate_space(int, FileRange) { return std::make_error_code(std::errc::not_supported); }
#endif
#endif
} // namespace clause::runtime::detail
