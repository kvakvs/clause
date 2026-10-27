#pragma once
// Added for parse transforms: finding a project transform module's sources (docs/transforms.md#transform-modules).
#include "project/directory_index.hpp"
#include "project/ignore.hpp"
#include "transforms/pipeline.hpp"
#include <clause/compiler/preprocessor.hpp>
#include <filesystem>
#include <map>
#include <set>
#include <span>

namespace clause::cli {
struct SourcePlaces {
    // The batch's inputs, then the directories searched for Module.erl.
    std::span<const std::filesystem::path> inputs_;
    std::span<const std::filesystem::path> directories_;
    // ignore_dirs and ignore_modules: a search passes over the sources they leave out. Added for ignored modules.
    const project::Ignore &ignore_;
    // The invocation's directory index, which rules out directories without probing them. Added for the
    // directory index (62E).
    project::DirectoryIndex *index_ = nullptr;
};

// Transform sources found for one batch, kept in memory so each transform and source is looked at once.
// Added for transform source caching.
struct TransformSourceCache {
    // The sources by transform module, and the modules each source calls by literal name.
    std::map<std::u32string, std::vector<std::filesystem::path>> sources_;
    std::map<std::filesystem::path, std::vector<std::u32string>> calls_;
    // The files each source includes; transform cache keys hash them. Added for build caches (62D2).
    std::map<std::filesystem::path, std::vector<std::filesystem::path>> includes_;
    // Each source's forms as the loader gets them, and the sources being read now (a transform of its own source
    // fails). Added for transform sources (plan 21 LIB77C).
    std::map<std::filesystem::path, transforms::SourceForms> forms_;
    std::set<std::filesystem::path> reading_;
};

// The source of a transform module and of the project modules it calls: Module.erl among the batch inputs, then
// in the search directories; modules found nowhere (OTP's own) and the host's sticky modules (whose calls stay on
// the host) are left to the host.
std::vector<std::filesystem::path> transform_sources(const std::u32string &module, const SourcePlaces &places,
                                                     const PreprocessorOptions &options,
                                                     const transforms::HostModule &host, TransformSourceCache &cache);

// A transform source as the loader gets it: preprocessed with the batch's options and parsed by Clause, its own
// parse transforms applied with `settings`, as forms; read once per batch. Added for transform sources (LIB77C).
transforms::SourceForms read_transform_source(const std::filesystem::path &path, const PreprocessorOptions &options,
                                              const transforms::TransformSettings &settings,
                                              TransformSourceCache &cache);
} // namespace clause::cli
