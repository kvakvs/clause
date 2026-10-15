#pragma once
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace clause::project {
struct Site {
    // Own the manifest filename and key context for diagnostics after TOML destruction.
    std::filesystem::path file;
    std::string key;
    std::string target;
    // Preserve one-based source coordinates; zero means unavailable.
    std::size_t line = 0;
    std::size_t column = 0;
};

struct Text {
    // Keep user spelling distinct from eventual filesystem normalization.
    std::string value;
    Site site;
};

struct TargetOptions {
    // Retain ordered source fallback roots and header search paths.
    std::vector<Text> source_search_paths;
    std::vector<Text> include_dirs;
    // Preserve Erlang term spellings and feature overrides for frontend validation.
    std::vector<Text> defines;
    std::vector<Text> enable_features;
    std::vector<Text> disable_features;
    // Map include_lib application names to explicitly located roots.
    std::map<std::string, Text> applications;
    // Parse transforms applied to every module of the target, and directories (or patterns) of precompiled
    // transform modules (docs/transforms.md). Added for parse transforms.
    std::vector<Text> parse_transforms;
    std::vector<Text> transform_paths;
    // Path wildcards whose sources are never discovered, searched or compiled, and module names left out the same
    // way; references to such modules reach placeholders (docs/projects.md#ignored-sources). Added for ignored
    // modules.
    std::vector<Text> ignore_dirs;
    std::vector<Text> ignore_modules;
    // Keep every export of the target's executable callable (docs/projects.md). Added for export liveness (62C).
    bool keep_exports_ = false;
    // Directory wildcards whose modules keep their inference results in the build cache, besides the implicit OTP
    // application directories (docs/projects.md#inference-cache). Added for the inference cache (58U).
    std::vector<Text> cache_inference_in_dirs_;
};

struct Target {
    // Identify this independently configured compilation unit collection.
    Text name;
    // Preserve source selection order before expansion and deduplication.
    std::vector<Text> sources;
    std::vector<Text> source_dirs;
    // Distinguish omitted output (default build/<target>, or no executable without entry) from an explicit one.
    std::optional<Text> output;
    // Optional MODULE[:FUNCTION[/ARITY]] entry selection; validated spelling, resolved during compilation.
    std::optional<Text> entry;
    // The build directory (manifest-relative); absent means <manifest-dir>/build. Added for the build directory
    // (62D1).
    std::optional<Text> build_dir_;
    TargetOptions options;
};

struct Manifest {
    // Keep the supplied manifest identity and declaration order without TOML ownership.
    std::filesystem::path file;
    std::vector<Target> targets;
};

struct Limits {
    // Bound manifest parsing and total decoded configuration size.
    std::size_t manifest_bytes = 1'048'576;
    std::size_t targets = 1024;
    std::size_t entries = 100000;
};

struct Error {
    // Carry structured context and a stable CLI failure category.
    Site site;
    std::string message;
    int exit_code = 1;
};
} // namespace clause::project
