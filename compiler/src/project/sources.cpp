#include "sources.hpp"
#include "diagnostics.hpp"
#include "identity.hpp"
#include "paths.hpp"
#include <unordered_set>

namespace clause::project {
namespace {
struct Collection {
    // Preserve first-spelling order while identifying physical aliases in expected linear time.
    std::vector<std::filesystem::path> paths;
    std::unordered_set<std::string> identities;
};

// Append only the first spelling of each existing filesystem object; ignored sources are noted and left out.
void append(Collection &result, const std::vector<std::filesystem::path> &paths, const Site &site, Ignore &ignore) {
    for (const auto &path : paths) {
        if (ignore.skips(path)) {
            ignore.note(path);
        } else if (result.identities.insert(file_identity(path, site)).second) {
            result.paths.push_back(path);
        }
    }
}
} // namespace

std::vector<std::filesystem::path> search_directories(const std::filesystem::path &base, const Target &target,
                                                      const DiscoveryLimits limits) {
    std::vector<std::filesystem::path> result;
    for (const auto &pattern : target.options.source_search_paths) {
        const auto directories = pattern_directories(base, pattern, "source search path", limits);
        result.insert(result.end(), directories.begin(), directories.end());
    }
    return result;
}

std::vector<std::filesystem::path> target_sources(const std::filesystem::path &base, const Target &target,
                                                  const std::span<const std::filesystem::path> search, Ignore &ignore,
                                                  DiscoveryLimits limits) {
    Collection result;
    for (const auto &source : target.sources) {
        if (is_pattern(source.value)) {
            append(result, wildcard_sources(base, source, limits), source.site, ignore);
        } else {
            append(result, {literal_source(base, source, search, limits.index)}, source.site, ignore);
        }
    }
    for (const auto &directory : target.source_dirs) {
        append(result, directory_sources(base, directory, limits), directory.site, ignore);
    }
    if (result.paths.empty()) {
        fail(target.name.site, "target resolves to zero source files");
    }
    return result.paths;
}
} // namespace clause::project
