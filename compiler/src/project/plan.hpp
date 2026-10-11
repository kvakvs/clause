#pragma once
#include "discovery.hpp"
#include "entry.hpp"
#include "ignore.hpp"
#include "options.hpp"

namespace clause::project {
struct PlanOptions {
    // Preserve CLI target order, invocation path base, and per-invocation overrides.
    std::vector<std::string> selectors;
    std::filesystem::path working_directory;
    PreprocessorOptions preprocessing;
    std::optional<std::filesystem::path> output;
    // CLI --entry replaces the manifest entry of the single selected target.
    std::optional<EntryName> entry;
    // Frontend (check/print/emit/inspect) requests ignore manifest output paths and never plan executable writes.
    bool frontend = false;
    DiscoveryLimits discovery;
    // CLI --build-dir, invocation-relative; replaces every selected target's build directory. Added for the build
    // directory (62D1).
    std::optional<std::filesystem::path> build_directory;
};

struct PlannedTarget {
    // Own all resolved target work independently of the manifest decoding tree.
    std::string name;
    std::vector<std::filesystem::path> sources;
    PreprocessorOptions preprocessing;
    // Executable destination; absent for check/print requests and targets without output, entry, -o or --entry.
    std::optional<std::filesystem::path> output;
    // Explicit entry selection (CLI or manifest); absent means auto-detection when an executable is requested.
    std::optional<SelectedEntry> entry;
    // Expanded source_search_paths, also searched for a module the batch names but no source defines.
    std::vector<std::filesystem::path> search_paths_ = {};
    // The target's parse transforms and expanded transform_paths. Added for parse transforms.
    std::vector<std::string> transforms_ = {};
    std::vector<std::filesystem::path> transform_paths_ = {};
    // ignore_dirs and ignore_modules, with the module names of the sources they left out. Added for ignored modules.
    Ignore ignore_ = {};
    // The keep_exports option. Added for export liveness (62C).
    bool keep_exports_ = false;
    // Where Clause keeps its temporary files and caches: --build-dir, else build_dir, else <manifest-dir>/build.
    // Added for the build directory (62D1).
    std::filesystem::path build_directory_ = {};
};

struct Invocation {
    // Preserve project identity, selected target order, and execution mode.
    std::filesystem::path file;
    std::vector<PlannedTarget> targets;
    bool frontend = false;
};

// Resolve and validate all selected target work before any frontend processing.
Invocation prepare(const Manifest &manifest, const PlanOptions &options);
} // namespace clause::project
