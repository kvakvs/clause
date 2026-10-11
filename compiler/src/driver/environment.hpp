#pragma once
#include "backend_options.hpp"
#include "project/entry.hpp"
#include "project/ignore.hpp"
#include <clause/compiler/preprocessor.hpp>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace clause::cli {
struct Options;

struct EnvironmentTarget {
    // Project target identity; both are empty for positional inputs.
    std::string name;
    std::optional<std::filesystem::path> project;
    // Resolved sources and source_search_paths directories, in processing order.
    std::span<const std::filesystem::path> sources;
    std::span<const std::filesystem::path> search_paths;
    // Effective preprocessing and backend settings; the artifact root is already resolved for projects.
    const PreprocessorOptions &preprocessing;
    const BackendOptions &backend;
    // Executable destination and entry selection; absent output means nothing is linked.
    std::optional<std::filesystem::path> output;
    std::optional<project::SelectedEntry> entry;
    // A project target's ignore_dirs and ignore_modules; absent for positional inputs. Added for ignored modules.
    const project::Ignore *ignore = nullptr;
};

// Print the invocation part of --print-env: command, actions and working directory, as TOML.
void print_environment_header(std::span<char *const> arguments, const Options &options);
// Print one target's resolved inputs and settings as a TOML [[targets]] table.
void print_environment_target(const EnvironmentTarget &target);
// Print the whole --print-env document for positional inputs.
void print_positional_environment(std::span<char *const> arguments, const Options &options);
} // namespace clause::cli
