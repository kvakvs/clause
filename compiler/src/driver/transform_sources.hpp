#pragma once
// Added for parse transforms: finding a project transform module's sources (docs/transforms.md#transform-modules).
#include "project/ignore.hpp"
#include "transforms/pipeline.hpp"
#include <clause/compiler/preprocessor.hpp>
#include <filesystem>
#include <map>
#include <span>

namespace clause::cli {
struct SourcePlaces {
    // The batch's inputs, then the directories searched for Module.erl.
    std::span<const std::filesystem::path> inputs_;
    std::span<const std::filesystem::path> directories_;
    // ignore_dirs and ignore_modules: a search passes over the sources they leave out. Added for ignored modules.
    const project::Ignore &ignore_;
};

// Transform sources found for one batch, kept in memory so each transform and source is looked at once.
// Added for transform source caching.
struct TransformSourceCache {
    // The sources by transform module, and the modules each source calls by literal name.
    std::map<std::u32string, std::vector<std::filesystem::path>> sources_;
    std::map<std::filesystem::path, std::vector<std::u32string>> calls_;
};

// The source of a transform module and of the project modules it calls: Module.erl among the batch inputs, then
// in the search directories; modules found nowhere (OTP's own) and the host's sticky modules (whose calls stay on
// the host) are left to the host.
std::vector<std::filesystem::path> transform_sources(const std::u32string &module, const SourcePlaces &places,
                                                     const PreprocessorOptions &options,
                                                     const transforms::HostModule &host, TransformSourceCache &cache);
} // namespace clause::cli
