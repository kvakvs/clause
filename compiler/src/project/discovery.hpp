#pragma once
#include "glob.hpp"
#include "model.hpp"

namespace clause::project {
struct DiscoveryLimits {
    // Bound visited directory entries, recursion depth, and total matching work per expansion.
    std::size_t entries = 100000;
    std::size_t depth = 128;
    std::size_t work = 16000000;
};

// Separate the explicitly named root from wildcard components before traversal.
std::pair<std::filesystem::path, std::filesystem::path> split_pattern(const std::filesystem::path &path);
// Expand one wildcard selection deterministically, rejecting an unmatched pattern.
std::vector<std::filesystem::path> wildcard_sources(const std::filesystem::path &base, const Text &pattern,
                                                    DiscoveryLimits limits = {});
// Expand one include_dirs or source_search_paths entry: a literal directory as it is, a pattern to the existing
// directories it matches (directory symlinks skipped), sorted; an unmatched pattern is an error naming `kind`.
std::vector<std::filesystem::path> pattern_directories(const std::filesystem::path &base, const Text &pattern,
                                                       std::string_view kind, DiscoveryLimits limits = {});
// Recursively collect regular Erlang sources under one explicitly named directory.
std::vector<std::filesystem::path> directory_sources(const std::filesystem::path &base, const Text &directory,
                                                     DiscoveryLimits limits = {});
} // namespace clause::project
