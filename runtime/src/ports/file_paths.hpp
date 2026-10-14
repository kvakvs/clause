#pragma once
#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <system_error>
#include <vector>

// The file driver's directory and link operations (plan step 41P, docs/ports.md#standard-io-and-files): the reply
// bytes of a success, or the error OTP reports (errc::not_supported is enotsup, invalid_argument einval).
namespace clause::runtime::detail {
using PathReply = std::expected<std::vector<std::byte>, std::error_code>;

// The current directory in OTP's spelling (forward slashes, a lowercase drive letter on Windows), or the current
// directory of a drive "X:" (Windows only, enotsup elsewhere).
PathReply current_directory(std::span<const std::byte> drive);
// Make `path` the current directory of the whole program.
PathReply change_directory(const std::filesystem::path &path);
// The target a symbolic link names; einval for a path that is not a link.
PathReply link_target(const std::filesystem::path &path);
// The status record of a path without following a final symbolic link (file_info.hpp's layout).
PathReply link_status(const std::filesystem::path &path);
// A new hard or symbolic link `link` to `existing`.
PathReply make_hard_link(const std::filesystem::path &existing, const std::filesystem::path &link);
PathReply make_symbolic_link(const std::filesystem::path &existing, const std::filesystem::path &link);
} // namespace clause::runtime::detail
