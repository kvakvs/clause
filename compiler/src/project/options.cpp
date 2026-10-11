#include "options.hpp"
#include "diagnostics.hpp"
#include "discovery.hpp"
#include "paths.hpp"

namespace clause::project {
namespace {
// The directories manifest paths and CLI paths are relative to.
struct Roots {
    const std::filesystem::path &manifest;
    const std::filesystem::path &invocation;
};

// Preserve last-CLI-first include order, followed by manifest declaration order (patterns expanded in place).
void includes(PreprocessorOptions &result, const TargetOptions &target, const Roots &roots,
              const DiscoveryLimits &limits) {
    for (auto &path : result.include_paths) {
        path = absolute_path(roots.invocation, path);
    }
    for (const auto &pattern : target.include_dirs) {
        const auto directories = pattern_directories(roots.manifest, pattern, "include directory", limits);
        result.include_paths.insert(result.include_paths.end(), directories.begin(), directories.end());
    }
}

// Keep macro spelling untouched so the existing preprocessor validates Erlang literals.
std::vector<std::string> definitions(const TargetOptions &target, const PreprocessorOptions &cli) {
    std::vector<std::string> result;
    result.reserve(target.defines.size() + cli.definitions.size());
    for (const auto &value : target.defines) {
        result.push_back(value.value);
    }
    result.insert(result.end(), cli.definitions.begin(), cli.definitions.end());
    return result;
}

// Apply manifest feature changes before the original ordered CLI changes.
std::vector<std::pair<std::string, bool>> features(const TargetOptions &target, const PreprocessorOptions &cli) {
    std::vector<std::pair<std::string, bool>> result;
    result.reserve(target.enable_features.size() + target.disable_features.size() + cli.features.size());
    for (const auto &value : target.enable_features) {
        result.emplace_back(value.value, true);
    }
    for (const auto &value : target.disable_features) {
        result.emplace_back(value.value, false);
    }
    result.insert(result.end(), cli.features.begin(), cli.features.end());
    return result;
}

// Overlay application names while resolving each layer against its own directory.
std::vector<std::pair<std::string, std::filesystem::path>> applications(const TargetOptions &target,
                                                                        const PreprocessorOptions &cli,
                                                                        const std::filesystem::path &base,
                                                                        const std::filesystem::path &invocation) {
    std::vector<std::pair<std::string, std::filesystem::path>> result;
    result.reserve(target.applications.size() + cli.applications.size());
    for (const auto &[name, path] : target.applications) {
        result.emplace_back(name, absolute_path(base, native_path(path.value)));
    }
    for (const auto &[name, path] : cli.applications) {
        result.emplace_back(name, absolute_path(invocation, path));
    }
    return result;
}

// Validate definitions and feature names using the real frontend before executing any source.
void validate(const PreprocessorOptions &options, const Site &site) {
    SourceManager sources;
    PreprocessorSession session(sources.add("<project-options>", ""), options);
    while (const auto event = session.next()) {
        const auto *diagnostic = std::get_if<Diagnostic>(&*event);
        if (diagnostic && diagnostic->severity == Severity::error) {
            fail(site, clause::render(*diagnostic));
        }
    }
}
} // namespace

PreprocessorOptions compose_options(const Target &target, const std::filesystem::path &base,
                                    const std::filesystem::path &invocation, const PreprocessorOptions &cli,
                                    const DiscoveryLimits &limits) {
    auto result = cli;
    result.working_directory = base;
    includes(result, target.options, {base, invocation}, limits);
    result.definitions = definitions(target.options, cli);
    result.features = features(target.options, cli);
    result.applications = applications(target.options, cli, base, invocation);
    validate(result, target.name.site);
    return result;
}
} // namespace clause::project
