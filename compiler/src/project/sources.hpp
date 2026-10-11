#pragma once
#include "discovery.hpp"
#include "ignore.hpp"
#include <span>

namespace clause::project {
// The target's source_search_paths as absolute directories, patterns expanded in place.
std::vector<std::filesystem::path> search_directories(const std::filesystem::path &base, const Target &target,
                                                      DiscoveryLimits limits = {});
// Assemble ordered translation units and deduplicate physical aliases within one target; literal sources fall back
// to the search directories. Sources `ignore` skips are left out and noted in it.
std::vector<std::filesystem::path> target_sources(const std::filesystem::path &base, const Target &target,
                                                  std::span<const std::filesystem::path> search, Ignore &ignore,
                                                  DiscoveryLimits limits = {});
} // namespace clause::project
