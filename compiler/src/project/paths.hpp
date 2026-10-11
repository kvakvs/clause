#pragma once
#include "model.hpp"
#include <span>

namespace clause::project {
class DirectoryIndex;

// Convert manifest UTF-8 to native paths without locale-dependent narrowing.
std::filesystem::path native_path(std::string_view text);
// Produce portable UTF-8 display and sorting spelling from a native path.
std::string path_text(const std::filesystem::path &path);
// Resolve paths lexically against an explicit absolute base without changing cwd.
std::filesystem::path absolute_path(const std::filesystem::path &base, const std::filesystem::path &path);
// Resolve a literal source with manifest-first, then ordered fallback lookup in absolute search directories; the
// directory index (62E), when given, rules out candidates without probing them.
std::filesystem::path literal_source(const std::filesystem::path &base, const Text &source,
                                     std::span<const std::filesystem::path> roots, DirectoryIndex *index = nullptr);
// Require an accessible regular input and retain its user-facing context on failure.
void require_source(const std::filesystem::path &path, const Site &site);
// Resolve and require an explicitly named directory, following its root symlink.
std::filesystem::path source_directory(const std::filesystem::path &base, const Text &directory);
} // namespace clause::project
