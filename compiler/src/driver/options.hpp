#pragma once
#include "backend_options.hpp"
#include "project/cache_dirs.hpp"
#include "project/cli.hpp"
#include "project/entry.hpp"
#include "transforms/pipeline.hpp"
#include <clause/compiler/preprocessor.hpp>
#include <span>

namespace clause::cli {
struct Options {
    // Select informational output after validating all command-line arguments.
    bool show_help = false;
    bool show_version = false;
    // Trace input ingestion without selecting a different compiler action.
    bool verbose = false;
    // Print elapsed preprocess/parse time per file, compile time per batch and link time per executable.
    // Added for --print-stage-times.
    bool print_stage_times = false;
    // Retain the requested destination for the future code-generation stage.
    std::filesystem::path output = "a.out";
    // Distinguish explicit output overrides from the positional-mode default.
    bool output_explicit = false;
    // Explicit --entry MODULE[:FUNCTION] selection; validated by compilation against the batch.
    std::optional<project::EntryName> entry;
    // --build-dir: where Clause keeps temporary files and caches; replaces the build directory of every selected
    // project target. Added for the build directory (62D1).
    std::optional<std::filesystem::path> build_directory;
    // --no-cache: neither read nor write build caches. Added for build caches (62D2).
    bool no_cache = false;
    // --cache-inference-in-dirs patterns, in order, validated while parsing. Added for the inference cache (58U).
    std::vector<project::Text> cache_inference_in_dirs;
    // Distinguish explicit frontend settings from defaults for standalone commands.
    bool frontend_options_explicit = false;
    // Retain backend policy independently of frontend and project selection.
    BackendOptions backend;
    // Delegate project selection data and policy to the project component.
    clause::project::Request project;
    // Preserve source order for input validation and future compilation.
    std::vector<std::filesystem::path> inputs;
    // Select an explicit check/print action instead of the default compiler pipeline.
    bool preprocess = false;
    // Emit expanded forms while sharing the diagnostics-only preprocessing path.
    bool print_pp = false;
    // Parse the expanded token stream and emit its typed syntax tree.
    bool print_ast = false;
    // Parse the expanded token stream and emit it as Erlang source (docs/compile.md#source-printing).
    bool print_source = false;
    // Parse and print each module as OTP abstract format forms (docs/transforms.md). Added for parse transforms.
    bool print_abstr = false;
    // Print the resolved compile environment as TOML instead of running the validated command.
    bool print_env = false;
    // Parse inputs and add referenced modules, then list the batch instead of compiling; replaces other actions.
    // Added for --print-inputs.
    bool print_inputs = false;
    // Request syntax diagnostics without requiring tree output.
    bool parse_check = false;
    // --erl, --parse-transform and --transform-path (docs/transforms.md#options).
    transforms::TransformSettings transforms;
    // Recreate preprocessing options independently for every input module.
    clause::PreprocessorOptions preprocessing;
};

// The inference cache directories of positional inputs: the implicit ones, then --cache-inference-in-dirs. Added for
// the inference cache (58U).
project::InferenceCacheDirs positional_cache_dirs(const Options &options);
// Parse and validate usage independently of reading source files.
std::optional<std::string> parse_options(std::span<char *> remaining, Options &options);
// Process every input using an independent frontend ownership context.
int process_inputs(const Options &options);
// Execute independent project batches and publish only after the selected invocation succeeds.
int run_project(const Options &options, std::span<char *const> arguments);
// Check physical input paths before running the selected frontend mode.
bool validate_inputs(const std::vector<std::filesystem::path> &inputs);
} // namespace clause::cli
