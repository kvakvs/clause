#pragma once
#include <cstddef>
#include <expected>
#include <filesystem>
#include <vector>

// The file driver's read_file_info operation (plan step 41Y2, docs/ports.md#standard-io-and-files): a path's
// status as a fixed record the library's file module turns into OTP's #file_info{}.
namespace clause::runtime::detail {
// The status of `path`, following symbolic links, as big-endian fields: size (8 bytes), type (1: 0 regular,
// 1 directory, 2 symlink, 3 device, 4 other), access (1: 0 none, 1 read, 2 write, 3 read_write), access,
// modification and change times in POSIX seconds (8 each, signed), mode, links, major and minor device (4 each),
// inode (8), uid and gid (4 each). The errno value when the path has no status. Without `follow` a final symbolic
// link is described itself (read_link_info, step 41P; on Windows its target's fields with the symlink type).
std::expected<std::vector<std::byte>, int> file_status(const std::filesystem::path &path, bool follow = true);
} // namespace clause::runtime::detail
