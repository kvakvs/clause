#include "backend_options.hpp"
#include "../project/paths.hpp"
#include "options.hpp"
#include <map>
#include <set>

namespace clause::cli {
namespace {
// Give every value-bearing switch the same absent/empty operand diagnostic.
std::optional<std::string> operand(const std::string_view option, std::span<char *> &remaining, std::string &value) {
    if (remaining.empty() || std::string_view(remaining.front()).empty()) {
        return "expected a value after " + std::string(option);
    }
    value = remaining.front();
    remaining = remaining.subspan(1);
    return {};
}

// Distinguish malformed artifact kinds from duplicate selections without choosing a fallback.
std::optional<std::string> emission(const std::string &value, BackendOptions &options) {
    if (options.emit) {
        return "--emit specified more than once";
    }
    static const std::map<std::string, codegen::OutputKind> kinds{{"obj", codegen::OutputKind::object},
                                                                  {"llvm-ir", codegen::OutputKind::llvm_ir},
                                                                  {"llvm-bc", codegen::OutputKind::llvm_bitcode}};
    const auto found = kinds.find(value);
    if (found == kinds.end()) {
        return "--emit expects obj, llvm-ir, or llvm-bc";
    }
    options.emit = found->second;
    return {};
}

// Store a native path operand once, preserving the option spelling in duplicate errors.
std::optional<std::string> path_option(const std::string_view option, const std::string &value,
                                       std::optional<std::filesystem::path> &path) {
    if (path) {
        return std::string(option) + " specified more than once";
    }
    path = project::native_path(value);
    return {};
}

// Assign native path/triple operands only once, preserving option spelling in errors.
std::optional<std::string> value_option(const std::string_view option, const std::string &value,
                                        BackendOptions &options) {
    static const std::map<std::string_view, std::optional<std::filesystem::path> BackendOptions::*> paths{
        {"--artifact-dir", &BackendOptions::artifact_directory},
        {"--linker", &BackendOptions::linker},
        {"--runtime-library", &BackendOptions::runtime_library}};
    if (option == "--emit") {
        return emission(value, options);
    }
    if (const auto found = paths.find(option); found != paths.end()) {
        return path_option(option, value, options.*(found->second));
    }
    if (!options.target_triple.empty()) {
        return "--target-triple specified more than once";
    }
    options.target_triple = value;
    return {};
}

// Parse idempotent inspection flags without consuming source operands.
bool inspection_flag(const std::string_view option, BackendOptions &options) {
    static const std::map<std::string_view, bool BackendOptions::*> flags{
        {"--print-types", &BackendOptions::print_types},
        {"--print-ir", &BackendOptions::print_ir},
        {"--print-optimized-ir", &BackendOptions::print_optimized_ir},
        {"--print-asm", &BackendOptions::print_asm},
        {"-g", &BackendOptions::debug_info},
        {"--lto", &BackendOptions::lto},
        {"--keep-exports", &BackendOptions::keep_exports}};
    const auto found = flags.find(option);
    if (found == flags.end()) {
        return false;
    }
    options.*(found->second) = true;
    return true;
}

// Semantic inspection cannot consume backend policy or select another output action.
bool type_conflict_options(const Options &options) {
    const auto &backend = options.backend;
    return backend.inspect_ir() || backend.emit || backend.artifact_directory || options.output_explicit ||
           !backend.target_triple.empty() || backend.optimization || backend.disable_type_specialization ||
           backend.debug_info;
}

// Inspection has no filesystem outputs and keeps executable output reserved.
std::optional<std::string> inspection_conflict(const Options &options) {
    if (options.backend.print_types && type_conflict_options(options)) {
        return "--print-types cannot be combined with IR, emission, output, target, or optimization options";
    }
    if (options.backend.inspect_ir() &&
        (options.backend.emit || options.backend.artifact_directory || options.output_explicit)) {
        return "IR inspection cannot be combined with --emit, --artifact-dir, or --output";
    }
    return {};
}

// A project build without emission, inspection or checks links every selected target to its output.
bool links_project(const Options &options) {
    return options.project.file.has_value() && !options.backend.emit && !options.backend.inspect_ir() &&
           !options.backend.print_types && !options.preprocess;
}

// Linking options need an invocation that links: an explicit --output or a linking project build.
std::optional<std::string> linking_conflict(const Options &options) {
    const auto &backend = options.backend;
    const bool links = options.output_explicit || links_project(options);
    if ((backend.linker || backend.runtime_library) && !links) {
        return "--linker and --runtime-library require --output or a linking project build";
    }
    if (backend.lto && !links) {
        return "--lto requires --output or a linking project build";
    }
    if (backend.keep_exports && !links && !backend.inspect_ir()) {
        return "--keep-exports requires --output, a linking project build or IR/assembly inspection";
    }
    return {};
}

// Keep artifact emission and executable linking options on their own sides of --output.
std::optional<std::string> output_conflict(const Options &options) {
    const auto &backend = options.backend;
    if (backend.emit && options.output_explicit) {
        return "--emit cannot be combined with --output";
    }
    if (backend.artifact_directory && !backend.emit) {
        return "--artifact-dir requires --emit";
    }
    return linking_conflict(options);
}

// Map an -O switch to its pipeline policy; other options yield nothing.
std::optional<codegen::OptimizationLevel> optimization_level(const std::string_view option) {
    static const std::map<std::string_view, codegen::OptimizationLevel> levels{
        {"-O0", codegen::OptimizationLevel::none},
        {"-O2", codegen::OptimizationLevel::speed},
        {"-Os", codegen::OptimizationLevel::size}};
    const auto found = levels.find(option);
    return found == levels.end() ? std::nullopt : std::optional(found->second);
}

// Remember any explicit backend policy so frontend-only actions cannot silently discard it.
bool explicit_backend(const BackendOptions &options) {
    return options.emit || options.artifact_directory || !options.target_triple.empty() || options.optimization ||
           options.disable_type_specialization || options.inspect_ir() || options.print_types || options.debug_info ||
           options.lto || options.keep_exports;
}
} // namespace

bool is_backend_option(const std::string_view option) {
    static const std::set<std::string_view> options{"--emit",
                                                    "--artifact-dir",
                                                    "--target-triple",
                                                    "-O0",
                                                    "-O2",
                                                    "-Os",
                                                    "--no-type-specialization",
                                                    "-g",
                                                    "--lto",
                                                    "--keep-exports",
                                                    "--print-ir",
                                                    "--print-optimized-ir",
                                                    "--print-asm",
                                                    "--print-types",
                                                    "--linker",
                                                    "--runtime-library"};
    return options.contains(option);
}

std::optional<std::string> parse_backend_option(const std::string_view option, std::span<char *> &remaining,
                                                BackendOptions &options) {
    if (inspection_flag(option, options)) {
        return {};
    }
    if (option == "--no-type-specialization") {
        options.disable_type_specialization = true;
        return {};
    }
    if (const auto level = optimization_level(option)) {
        if (options.optimization) {
            return "optimization level specified more than once";
        }
        options.optimization = level;
        return {};
    }
    std::string value;
    if (const auto error = operand(option, remaining, value)) {
        return error;
    }
    return value_option(option, value, options);
}

std::optional<std::string> validate_backend_options(const Options &options) {
    if (const auto error = inspection_conflict(options)) {
        return error;
    }
    if ((options.preprocess || options.project.create) && explicit_backend(options.backend)) {
        return "compilation switches cannot be combined with frontend actions or --new-project";
    }
    return output_conflict(options);
}
} // namespace clause::cli
