#include "environment.hpp"
#include "../codegen/target.hpp"
#include "../linking/link.hpp"
#include "../project/paths.hpp"
#include "options.hpp"
#include <array>
#include <clause/compiler/source.hpp>
#include <iostream>
#include <map>
#include <utility>

namespace clause::cli {
namespace {
// Quote text as a TOML basic string; control characters use \uXXXX escapes.
std::string toml_string(const std::string_view text) {
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::string result = "\"";
    for (const unsigned char character : text) {
        if (character == '"' || character == '\\') {
            result += '\\';
            result += static_cast<char>(character);
        } else if (character < 32 || character == 127) {
            result += "\\u00";
            result += hex[character >> 4];
            result += hex[character & 15];
        } else {
            result += static_cast<char>(character);
        }
    }
    return result + '"';
}

// Quote a path made absolute against the invocation directory, with / separators.
std::string toml_path(const std::filesystem::path &path) {
    return toml_string(project::path_text(project::absolute_path(std::filesystem::current_path(), path)));
}

// Quote each path for a TOML array.
std::vector<std::string> quoted_paths(const std::span<const std::filesystem::path> paths) {
    std::vector<std::string> result;
    for (const auto &path : paths) {
        result.push_back(toml_path(path));
    }
    return result;
}

// Quote each text for a TOML array.
std::vector<std::string> quoted_texts(const std::span<const std::string> texts) {
    std::vector<std::string> result;
    for (const auto &text : texts) {
        result.push_back(toml_string(text));
    }
    return result;
}

// Print `key = value`; the value is already TOML-encoded.
void print_value(const std::string_view key, const std::string_view value) {
    std::cout << key << " = " << value << '\n';
}

// Print an array with one item per line so long path lists stay readable.
void print_array(const std::string_view key, const std::vector<std::string> &items) {
    std::cout << key << " = [";
    for (const auto &item : items) {
        std::cout << "\n  " << item << ',';
    }
    std::cout << (items.empty() ? "]\n" : "\n]\n");
}

// Frontend check/print actions; --print-inputs replaces them, and none selected means --preprocess-check.
std::vector<std::string> frontend_actions(const Options &options) {
    if (options.print_inputs) {
        return {"print-inputs"};
    }
    static constexpr std::array flags{
        std::pair{&Options::print_pp, "print-pp"}, std::pair{&Options::parse_check, "parse-check"},
        std::pair{&Options::print_ast, "print-ast"}, std::pair{&Options::print_source, "print-source"},
        std::pair{&Options::print_abstr, "print-abstr"}};
    std::vector<std::string> result;
    for (const auto &[flag, name] : flags) {
        if (options.*flag) {
            result.emplace_back(name);
        }
    }
    return result.empty() ? std::vector<std::string>{"preprocess-check"} : result;
}

// Backend actions: inspections, else artifact emission, linking or in-memory compilation.
std::vector<std::string> backend_actions(const Options &options) {
    const auto &backend = options.backend;
    std::vector<std::string> result;
    for (const auto &[flag, name] :
         {std::pair{backend.print_types, "print-types"}, std::pair{backend.print_ir, "print-ir"},
          std::pair{backend.print_optimized_ir, "print-optimized-ir"}, std::pair{backend.print_asm, "print-asm"}}) {
        if (flag) {
            result.emplace_back(name);
        }
    }
    if (!result.empty()) {
        return result;
    }
    if (backend.emit) {
        return {"emit"};
    }
    return {options.output_explicit ? "link" : "compile"};
}

// The features enabled after OTP defaults and every requested change, as the preprocessor applies them.
std::vector<std::string> enabled_features(const PreprocessorOptions &options) {
    SourceManager sources;
    PreprocessorSession session(sources.add("<environment>", ""), options);
    while (session.next()) {
    }
    const auto features = session.features();
    return features ? features->enabled : std::vector<std::string>{};
}

// Print application overrides as an inline table; a later mapping of the same name wins, as in lookup.
void print_applications(const PreprocessorOptions &options) {
    std::map<std::string, std::filesystem::path> applications;
    for (const auto &[name, path] : options.applications) {
        applications[name] = path;
    }
    std::cout << "applications = {";
    std::string_view separator = " ";
    for (const auto &[name, path] : applications) {
        std::cout << separator << toml_string(name) << " = " << toml_path(path);
        separator = ", ";
    }
    std::cout << (applications.empty() ? "}\n" : " }\n");
}

// Print preprocessing and module search settings under the manifest's option names.
void print_options(const EnvironmentTarget &target) {
    const auto &preprocessing = target.preprocessing;
    std::cout << "\n[targets.options]\n";
    print_array("source_search_paths", quoted_paths(target.search_paths));
    // Added for ignored modules.
    if (target.ignore) {
        print_array("ignore_dirs", quoted_texts(target.ignore->patterns()));
        print_array("ignore_modules", quoted_texts(target.ignore->modules()));
    }
    if (target.cache_dirs) {
        print_array("cache_inference_in_dirs", quoted_texts(target.cache_dirs->patterns())); // Added for 58U.
    }
    print_array("include_dirs", quoted_paths(preprocessing.include_paths));
    print_array("defines", quoted_texts(preprocessing.definitions));
    print_applications(preprocessing);
    print_array("enabled_features", quoted_texts(enabled_features(preprocessing)));
}

// Spell an optimization level as its command-line switch without the dash.
std::string_view optimization_name(const codegen::OptimizationLevel level) {
    switch (level) {
    case codegen::OptimizationLevel::speed:
        return "O2";
    case codegen::OptimizationLevel::size:
        return "Os";
    case codegen::OptimizationLevel::none:
        break;
    }
    return "O0";
}

// Spell an artifact kind as its --emit operand; absent emission is "none".
std::string_view emission_name(const std::optional<codegen::OutputKind> kind) {
    if (!kind) {
        return "none";
    }
    switch (*kind) {
    case codegen::OutputKind::llvm_ir:
        return "llvm-ir";
    case codegen::OutputKind::llvm_bitcode:
        return "llvm-bc";
    case codegen::OutputKind::object:
    case codegen::OutputKind::assembly:
        break;
    }
    return "obj";
}

// Print what an executable carries for distribution: clause-epmd beside it, the bundled epmd, the built-in runtime
// options and the distribution modules. Added for plan 30's options (DIST3c, DIST3d, DIST4a3b, DIST4b1b).
void print_distribution(const BackendOptions &backend) {
    print_value("epmd_executable", backend.epmd_executable ? "true" : "false");
    print_value("bundled_epmd", backend.no_bundled_epmd ? "false" : "true");
    print_array("runtime_options", quoted_texts(backend.runtime_options));
    print_value("distribution", backend.distribution ? "true" : "false");
}

// Print code generation settings with the defaults compilation applies when an option is absent.
void print_backend(const BackendOptions &backend, const std::string &triple) {
    const auto level = backend.optimization.value_or(codegen::OptimizationLevel::none);
    std::cout << "\n[targets.backend]\n";
    print_value("target_triple", toml_string(triple));
    print_value("optimization", toml_string(optimization_name(level)));
    const bool specialization = level == codegen::OptimizationLevel::speed && !backend.disable_type_specialization;
    print_value("type_specialization", specialization ? "true" : "false");
    print_value("debug_info", backend.debug_info ? "true" : "false");
    print_value("lto", backend.lto ? "true" : "false");
    print_value("keep_exports", backend.keep_exports ? "true" : "false");
    print_value("shell", backend.shell ? "true" : "false");
    print_distribution(backend);
    print_value("with_crypto", backend.with_crypto ? "true" : "false"); // Added for the crypto runtime archive.
    print_value("fips", toml_path(backend.fips.value_or("")));          // Added for crypto's FIPS mode.
    print_value("emit", toml_string(emission_name(backend.emit)));
    print_value("artifact_dir", toml_path(backend.artifact_directory.value_or("build/aot")));
}

// Print the executable destination, entry and link tools; targets without an output link nothing.
void print_link(const EnvironmentTarget &target, const std::string &triple) {
    if (target.entry) {
        print_value("entry", toml_string(project::entry_spelling(target.entry->name_)));
    } else if (target.output) {
        print_value("entry", toml_string("auto"));
    }
    if (!target.output) {
        return;
    }
    linking::LinkRequest request;
    request.output = *target.output;
    request.target_triple = triple;
    request.linker = target.backend.linker;
    request.runtime_library = target.backend.runtime_library;
    const auto tools = linking::resolve_link_tools(request);
    print_value("output", toml_path(tools.output));
    print_value("linker", tools.linker.empty() ? toml_string("") : toml_path(project::native_path(tools.linker)));
    print_value("runtime_library", toml_path(tools.runtime_library));
}
} // namespace

void print_environment_header(const std::span<char *const> arguments, const Options &options) {
    std::vector<std::string> command{toml_string("clau")};
    for (const char *argument : arguments) {
        command.push_back(toml_string(argument));
    }
    std::cout << "# clau compile environment (--print-env)\n";
    print_array("command", command);
    print_array("actions", quoted_texts(options.preprocess ? frontend_actions(options) : backend_actions(options)));
    print_value("working_directory", toml_path(std::filesystem::current_path()));
}

void print_environment_target(const EnvironmentTarget &target) {
    const auto triple = codegen::resolve_triple(target.backend.target_triple);
    std::cout << "\n[[targets]]\n";
    if (!target.name.empty()) {
        print_value("name", toml_string(target.name));
    }
    if (target.project) {
        print_value("project", toml_path(*target.project));
    }
    print_array("sources", quoted_paths(target.sources));
    print_value("library_directory", toml_path(linking::library_directory()));
    print_value("build_dir", target.build_directory ? toml_path(*target.build_directory) : toml_string(""));
    print_link(target, triple);
    print_options(target);
    print_backend(target.backend, triple);
}

void print_positional_environment(const std::span<char *const> arguments, const Options &options) {
    print_environment_header(arguments, options);
    const auto cache_dirs = positional_cache_dirs(options); // Added for the inference cache (58U).
    std::optional<project::SelectedEntry> entry;
    if (options.entry) {
        entry = project::SelectedEntry{*options.entry, "--entry"};
    }
    print_environment_target({.name = {},
                              .project = {},
                              .sources = options.inputs,
                              .search_paths = {},
                              .preprocessing = options.preprocessing,
                              .backend = options.backend,
                              .output = options.output_explicit ? std::optional{options.output} : std::nullopt,
                              .entry = entry,
                              .ignore = nullptr,
                              .build_directory = options.build_directory
                                                     ? std::optional{project::absolute_path(
                                                           std::filesystem::current_path(), *options.build_directory)}
                                                     : std::nullopt,
                              .cache_dirs = &cache_dirs});
}
} // namespace clause::cli
