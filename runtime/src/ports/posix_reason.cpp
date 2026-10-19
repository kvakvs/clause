#include "posix_reason.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace clause::runtime::detail {
std::string_view posix_reason(std::error_code code) {
    static constexpr std::array<std::pair<std::errc, std::string_view>, 14> NAMES{{
        {std::errc::no_such_file_or_directory, "enoent"},
        {std::errc::permission_denied, "eacces"},
        {std::errc::file_exists, "eexist"},
        {std::errc::is_a_directory, "eisdir"},
        {std::errc::not_a_directory, "enotdir"},
        {std::errc::bad_file_descriptor, "ebadf"},
        {std::errc::invalid_argument, "einval"},
        {std::errc::no_space_on_device, "enospc"},
        {std::errc::too_many_files_open, "emfile"},
        {std::errc::directory_not_empty, "eexist"},
        {std::errc::operation_not_permitted, "eperm"},
        {std::errc::filename_too_long, "enametoolong"},
        {std::errc::not_supported, "enotsup"},
        {std::errc::no_such_device, "enodev"},
    }};
    const auto found = std::ranges::find_if(NAMES, [&](const auto &name) { return code == name.first; });
    return found == NAMES.end() ? std::string_view{"eio"} : found->second;
}
} // namespace clause::runtime::detail
