#include "file_paths.hpp"
#include "file_info.hpp"
#include <cctype>
#include <cerrno>
#include <cstring>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <direct.h>
#include <stdlib.h>
#include <windows.h>
#include <winioctl.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace clause::runtime::detail {
namespace {
// The UTF-8 bytes of a path in OTP's spelling: forward slashes and a lowercase drive letter on Windows.
std::vector<std::byte> otp_spelling(const std::filesystem::path &path) {
    auto text = path.generic_u8string();
    if (text.size() >= 2 && text[1] == u8':') {
        text[0] = static_cast<char8_t>(std::tolower(static_cast<unsigned char>(text[0])));
    }
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

// The current directory of drive "X:", for get_cwd/1 on Windows.
PathReply drive_directory(std::span<const std::byte> drive) {
#ifdef _WIN32
    const auto letter = std::toupper(std::to_integer<unsigned char>(drive[0]));
    if (drive.size() != 2 || std::to_integer<char>(drive[1]) != ':' || letter < 'A' || letter > 'Z') {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    wchar_t *directory = _wgetdcwd(letter - 'A' + 1, nullptr, 0);
    if (directory == nullptr) {
        return std::unexpected(std::make_error_code(std::errc::no_such_device));
    }
    const std::filesystem::path path(directory);
    std::free(directory);
    return otp_spelling(path);
#else
    static_cast<void>(drive);
    return std::unexpected(std::make_error_code(std::errc::not_supported));
#endif
}
#ifdef _WIN32
// The layout of a symbolic link's reparse data (REPARSE_DATA_BUFFER, which only the driver kit declares).
struct SymbolicLinkReparse {
    ULONG tag;
    USHORT data_length;
    USHORT reserved;
    USHORT substitute_offset;
    USHORT substitute_length;
    USHORT print_offset;
    USHORT print_length;
    ULONG flags;
};

// The target text of a symbolic link from its reparse point: the print name, else the substitute name.
std::expected<std::filesystem::path, std::error_code> read_link_text(const std::filesystem::path &path) {
    const auto failure = [] {
        return std::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    };
    const HANDLE handle =
        CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return failure();
    }
    std::vector<std::byte> buffer(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    DWORD size = 0;
    const bool read = DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer.data(),
                                      static_cast<DWORD>(buffer.size()), &size, nullptr) != 0;
    CloseHandle(handle);
    if (!read || size < sizeof(SymbolicLinkReparse)) {
        return failure();
    }
    SymbolicLinkReparse header{};
    std::memcpy(&header, buffer.data(), sizeof(header));
    const auto names = sizeof(SymbolicLinkReparse);
    const bool printable = header.print_length != 0;
    const auto offset = names + (printable ? header.print_offset : header.substitute_offset);
    const auto length = printable ? header.print_length : header.substitute_length;
    std::wstring text(length / sizeof(wchar_t), L'\0');
    std::memcpy(text.data(), buffer.data() + offset, length);
    return std::filesystem::path(text);
}
#else
// The target text of a symbolic link.
std::expected<std::filesystem::path, std::error_code> read_link_text(const std::filesystem::path &path) {
    std::string text(PATH_MAX, '\0');
    const auto size = ::readlink(path.c_str(), text.data(), text.size());
    if (size < 0) {
        return std::unexpected(std::error_code(errno, std::generic_category()));
    }
    text.resize(static_cast<std::size_t>(size));
    return std::filesystem::path(text);
}
#endif
} // namespace

PathReply current_directory(std::span<const std::byte> drive) {
    if (!drive.empty()) {
        return drive_directory(drive);
    }
    std::error_code code;
    const auto path = std::filesystem::current_path(code);
    return code ? PathReply(std::unexpected(code)) : PathReply(otp_spelling(path));
}

PathReply change_directory(const std::filesystem::path &path) {
    std::error_code code;
    std::filesystem::current_path(path, code);
    return code ? PathReply(std::unexpected(code)) : PathReply(std::vector<std::byte>{});
}

PathReply link_target(const std::filesystem::path &path) {
    std::error_code code;
    const auto status = std::filesystem::symlink_status(path, code);
    if (code || !std::filesystem::exists(status)) {
        return std::unexpected(code ? code : std::make_error_code(std::errc::no_such_file_or_directory));
    }
    if (!std::filesystem::is_symlink(status)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    const auto target = read_link_text(path);
    return target ? PathReply(otp_spelling(*target)) : PathReply(std::unexpected(target.error()));
}

PathReply link_status(const std::filesystem::path &path) {
    const auto status = file_status(path, false);
    return status ? PathReply(*status)
                  : PathReply(std::unexpected(std::error_code(status.error(), std::generic_category())));
}

PathReply make_hard_link(const std::filesystem::path &existing, const std::filesystem::path &link) {
    std::error_code code;
    std::filesystem::create_hard_link(existing, link, code);
    return code ? PathReply(std::unexpected(code)) : PathReply(std::vector<std::byte>{});
}

PathReply make_symbolic_link(const std::filesystem::path &existing, const std::filesystem::path &link) {
    std::error_code code;
    if (std::filesystem::is_directory(existing, code)) {
        std::filesystem::create_directory_symlink(existing, link, code);
    } else {
        std::filesystem::create_symlink(existing, link, code);
    }
    return code ? PathReply(std::unexpected(code)) : PathReply(std::vector<std::byte>{});
}
} // namespace clause::runtime::detail
