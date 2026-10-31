#pragma once
#include "cache_dirs.hpp"
#include "discovery.hpp"
#include "entry.hpp"
#include "ignore.hpp"
#include "options.hpp"
#include <chrono>
#include <functional>

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
    // CLI --cache-inference-in-dirs patterns, added after each target's cache_inference_in_dirs. Added for the
    // inference cache (58U).
    std::vector<Text> cache_inference_in_dirs;
    // Receives the manifest and how long loading and planning it took. Added for the directory scan measurement
    // (62D3, --print-stage-times).
    std::function<void(const std::filesystem::path &, std::chrono::steady_clock::duration)> planned;
};

// One group of ASN.1 specifications with its asn1ct options and output directory. Added for ASN.1 sources.
struct PlannedAsn1 {
    // The specifications, patterns expanded, in manifest order.
    std::vector<std::filesystem::path> sources;
    // asn1ct options as spelled; checked when the target runs.
    std::vector<std::string> options;
    // Where Module.erl and Module.hrl are written; joins the target's include path.
    std::filesystem::path outdir;
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
    // The shell option. Added for the shell option (plan 21 step LIB74).
    bool shell_ = false;
    // The epmd_executable option. Added for clause-epmd beside programs (plan 30 step DIST3c).
    bool epmd_executable_ = false;
    // The bundled_epmd option. Added for programs without the bundled epmd (plan 30 step DIST3d).
    bool bundled_epmd_ = true;
    // Where Clause keeps its temporary files and caches: --build-dir, else build_dir, else <manifest-dir>/build.
    // Added for the build directory (62D1).
    std::filesystem::path build_directory_ = {};
    // The implicit OTP directories, cache_inference_in_dirs and --cache-inference-in-dirs. Added for the inference
    // cache (58U).
    InferenceCacheDirs cache_dirs_ = {};
    // The target's ASN.1 specifications, compiled before its Erlang sources. Added for ASN.1 sources.
    std::vector<PlannedAsn1> asn1_ = {};
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
