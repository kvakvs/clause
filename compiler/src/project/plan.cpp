#include "plan.hpp"
#include "diagnostics.hpp"
#include "discovery.hpp"
#include "identity.hpp"
#include "paths.hpp"
#include "selection.hpp"
#include "sources.hpp"
#include <unordered_set>

namespace clause::project {
namespace {
// Reserve the native platform's default executable spelling without creating it.
std::filesystem::path default_output(const Target &target) {
#ifdef _WIN32
    return std::filesystem::path("build") / native_path(target.name.value + ".exe");
#else
    return std::filesystem::path("build") / native_path(target.name.value);
#endif
}

// A compiling target links when the manifest or CLI asks for an executable (output or entry).
bool executable(const Target &target, const PlanOptions &options) {
    return !options.frontend && (options.output || options.entry || target.output || target.entry);
}

// Apply output precedence only for executable requests; other targets compile in memory.
std::optional<std::filesystem::path> output_path(const Target &target, const PlanOptions &options,
                                                 const std::filesystem::path &base) {
    if (!executable(target, options)) {
        return std::nullopt;
    }
    if (options.output) {
        return absolute_path(options.working_directory, *options.output);
    }
    const auto path = target.output ? native_path(target.output->value) : default_output(target);
    return absolute_path(base, path);
}

// Prefer the CLI override, then the manifest key; both were validated before planning.
std::optional<SelectedEntry> entry_selection(const Target &target, const PlanOptions &options) {
    if (options.entry) {
        return SelectedEntry{*options.entry, "--entry"};
    }
    if (!target.entry) {
        return std::nullopt;
    }
    const auto entry = parse_entry(target.entry->value);
    return entry ? std::optional{SelectedEntry{*entry, where(target.entry->site)}} : std::nullopt;
}

// The target's build directory: --build-dir, else its build_dir, else <manifest-dir>/build. Added for the build
// directory (62D1).
std::filesystem::path build_directory(const Target &target, const PlanOptions &options,
                                      const std::filesystem::path &base) {
    if (options.build_directory) {
        return absolute_path(options.working_directory, *options.build_directory);
    }
    return absolute_path(base, target.build_dir_ ? native_path(target.build_dir_->value) : "build");
}

// Identify existing aliases and normalize unresolved paths without writing directories.
std::string output_identity(const std::filesystem::path &path, const Site &site) {
    std::error_code error;
    const auto present = std::filesystem::exists(path, error);
    if (error) {
        fail(site, "cannot inspect output " + path_text(path) + ": " + error.message());
    }
    if (present) {
        return "file:" + file_identity(path, site);
    }
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error) {
        fail(site, "cannot normalize output " + path_text(path) + ": " + error.message());
    }
    return "path:" + path_text(normalized);
}

// Refuse colliding compilation destinations across the selected target set.
void outputs(const Invocation &invocation) {
    std::unordered_set<std::string> identities;
    for (const auto &target : invocation.targets) {
        if (!target.output) {
            continue;
        }
        const Site site{invocation.file, "output", target.name, 0, 0};
        if (!identities.insert(output_identity(*target.output, site)).second) {
            fail(site, "selected targets have colliding output destinations");
        }
    }
}

// The target's parse transforms and transform_paths, patterns expanded like source_search_paths. Added for parse
// transforms.
void transform_options(const std::filesystem::path &base, const Target &target, const DiscoveryLimits limits,
                       PlannedTarget &planned) {
    for (const auto &name : target.options.parse_transforms) {
        planned.transforms_.push_back(name.value);
    }
    for (const auto &pattern : target.options.transform_paths) {
        const auto directories = pattern_directories(base, pattern, "transform path", limits);
        planned.transform_paths_.insert(planned.transform_paths_.end(), directories.begin(), directories.end());
    }
}

// The target's inference cache directories: the implicit ones, its cache_inference_in_dirs, then the CLI's. Added
// for the inference cache (58U).
InferenceCacheDirs cache_dirs(const Target &target, const PlanOptions &options) {
    InferenceCacheDirs result;
    for (const auto &pattern : target.options.cache_inference_in_dirs_) {
        result.add(pattern);
    }
    for (const auto &pattern : options.cache_inference_in_dirs) {
        result.add(pattern);
    }
    return result;
}
} // namespace

namespace {
// The target's ASN.1 groups: their specifications, options and output directories (manifest-relative; by default
// asn1/ in the build directory). Added for ASN.1 sources.
std::vector<PlannedAsn1> asn1_groups(const Target &target, const std::filesystem::path &base,
                                     const DiscoveryLimits limits, const std::filesystem::path &build) {
    std::vector<PlannedAsn1> result;
    for (const auto &group : target.asn1_) {
        PlannedAsn1 planned;
        for (const auto &pattern : group.sources) {
            const auto files = asn1_sources(base, pattern, limits);
            planned.sources.insert(planned.sources.end(), files.begin(), files.end());
        }
        for (const auto &option : group.options) {
            planned.options.push_back(option.value);
        }
        planned.outdir = build / "asn1";
        if (const auto &outdir = group.outdir) {
            planned.outdir = absolute_path(base, native_path(outdir->value));
        }
        result.push_back(std::move(planned));
    }
    return result;
}
} // namespace

Invocation prepare(const Manifest &manifest, const PlanOptions &options) {
    const auto selected = select_targets(manifest, options.selectors);
    if (options.output && (options.frontend || selected.size() != 1)) {
        fail({manifest.file, "output", {}, 0, 0},
             "--output requires exactly one compilation target and no check/print mode", 2);
    }
    if (options.entry && selected.size() != 1) {
        fail({manifest.file, "entry", {}, 0, 0}, "--entry requires exactly one selected target", 2);
    }
    const auto file = absolute_path(options.working_directory, manifest.file);
    const auto base = file.parent_path();
    Invocation result{file, {}, options.frontend};
    result.targets.reserve(selected.size());
    for (const auto index : selected) {
        const auto &target = manifest.targets[index];
        auto search = search_directories(base, target, options.discovery);
        Ignore ignore(base, target.options);
        auto sources = target_sources(base, target, search, ignore, options.discovery);
        result.targets.push_back(
            {target.name.value, std::move(sources),
             compose_options(target, base, options.working_directory, options.preprocessing, options.discovery),
             output_path(target, options, base), entry_selection(target, options), std::move(search)});
        transform_options(base, target, options.discovery, result.targets.back());
        result.targets.back().ignore_ = std::move(ignore);
        result.targets.back().keep_exports_ = target.options.keep_exports_;
        result.targets.back().shell_ = target.options.shell_;
        result.targets.back().build_directory_ = build_directory(target, options, base);
        result.targets.back().cache_dirs_ = cache_dirs(target, options);
        result.targets.back().asn1_ =
            asn1_groups(target, base, options.discovery, result.targets.back().build_directory_);
    }
    outputs(result);
    return result;
}
} // namespace clause::project
