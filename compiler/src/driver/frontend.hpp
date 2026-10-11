#pragma once
#include "backend_options.hpp"
#include "project/directory_index.hpp"
#include "project/entry.hpp"
#include "project/ignore.hpp"
#include "transforms/pipeline.hpp"
#include <clause/compiler/diagnostic.hpp>
#include <clause/compiler/preprocessor.hpp>
#include <functional>
#include <span>

namespace clause::cli {
struct Publication;
struct PendingExecutable;

struct FrontendRequest {
    // Select existing frontend output/check behavior independently of input selection.
    bool print_pp = false;
    bool print_ast = false;
    bool parse_check = false;
    // Continue successful parsing into the compilation placeholder for default requests.
    bool compile = false;
    // Report physical input filenames to stderr as each frontend stage ingests them.
    bool verbose = false;
    // Supply a fresh preprocessing configuration to each module session.
    PreprocessorOptions preprocessing;
    // Select shared backend policy for positional and project batches.
    BackendOptions backend = {};
    // Retain target identity and protect all selected project inputs during publication.
    std::string project_target = {};
    std::vector<std::filesystem::path> protected_inputs = {};
    // Borrow the invocation-owned queue so a later target failure discards every pending artifact.
    std::vector<Publication> *pending_publications = nullptr;
    // Preserve unambiguous inspection headers across independently processed project targets.
    bool multiple_targets = false;
    // Print each parsed module as Erlang source (--print-source).
    bool print_source = false;
    // Print each parsed module as abstract format forms (--print-abstr). Added for parse transforms.
    bool print_abstr = false;
    // Parse transforms settings: host erl, extra transforms, precompiled module paths. Added for parse transforms.
    transforms::TransformSettings transforms_ = {};
    // Executable destination (--output or a project target's output); present only when linking is requested.
    std::optional<std::filesystem::path> executable_output = {};
    // Create a missing executable directory; set for manifest outputs, never for an explicit --output.
    bool create_output_directory = false;
    // Borrow the project queue so linked executables replace their outputs only after every target succeeded.
    std::vector<PendingExecutable> *pending_executables = nullptr;
    // Explicit entry selection (CLI or manifest) validated during analysis.
    std::optional<project::SelectedEntry> entry = {};
    // Directories searched, in order and before the library, for Module.erl when the batch names a module no
    // input defines (a project target's source_search_paths).
    std::vector<std::filesystem::path> module_search_paths_ = {};
    // Sources and modules the batch leaves out; references to them reach placeholders. Added for ignored modules.
    project::Ignore ignore_ = {};
    // Parse and add referenced modules as compilation would, then list the batch's sources instead.
    // Added for --print-inputs.
    bool list_inputs = false;
    // Print stage durations on stderr: preprocess and parse per file, compile per batch, link per executable.
    // Added for --print-stage-times.
    bool stage_times = false;
    // The invocation's index of searched directories, shared by every lookup and target. Added for the directory
    // index (62E).
    project::DirectoryIndex *index_ = nullptr;
    // Where temporary files (below tmp) go; positional inputs have none without --build-dir. Added for the build
    // directory (62D1).
    std::optional<std::filesystem::path> build_directory_ = {};
};

// Process one isolated batch; project targets never share declaration tables.
bool process_files(std::span<const std::filesystem::path> paths, const FrontendRequest &request,
                   const DiagnosticSink &diagnostics);
// Report what the directory index did on stderr (--verbose). Added for the directory index (62E).
void trace_index(const project::DirectoryIndex &index);
} // namespace clause::cli
