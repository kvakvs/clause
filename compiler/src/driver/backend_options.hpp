#pragma once
#include "../codegen/request.hpp"
#include <optional>
#include <span>

namespace clause::cli {
struct Options;

struct BackendOptions {
    // Report declared and inferred types before constructing any LLVM state.
    bool print_types = false;
    // Select verified before/after LLVM snapshots instead of machine-code emission.
    bool print_ir = false;
    bool print_optimized_ir = false;
    // Print annotated machine assembly after the selected pipeline. Added for --print-asm.
    bool print_asm = false;

    // Identify the shared IR/assembly inspection action independently of requested stage order.
    bool inspect_ir() const { return print_ir || print_optimized_ir || print_asm; }

    // Absence runs the complete backend in memory without publishing artifacts.
    std::optional<codegen::OutputKind> emit;
    // Override the invocation-relative artifact root; projects append encoded target names.
    std::optional<std::filesystem::path> artifact_directory;
    // Select a machine triple independently of project target selection.
    std::string target_triple;
    // Retain explicit optimization presence for conflicts, defaulting to generic O0 at execution.
    std::optional<codegen::OptimizationLevel> optimization;
    // Disable variants independently of optimization option order.
    bool disable_type_specialization = false;
    // Threads that optimize and emit object modules (--jobs); absent uses every hardware thread. Added for parallel
    // code generation.
    std::optional<std::size_t> jobs;
    // Emit Erlang line tables into objects and keep them in linked executables (-g).
    bool debug_info = false;
    // Link the modules as bitcode with link-time optimization (--lto); only with an executable output.
    bool lto = false;
    // Keep every export of an executable's modules callable (--keep-exports or the project option keep_exports)
    // instead of dropping those it cannot reach. Added for export liveness (62C).
    bool keep_exports = false;
    // Link the interactive shell with every library module and keep every export callable (--shell or the project
    // option shell); without a selected entry the shell is the entry. Added for the shell option (plan 21 LIB74).
    bool shell = false;
    // Override the Clang driver and runtime archive used to link an explicit --output executable.
    std::optional<std::filesystem::path> linker;
    std::optional<std::filesystem::path> runtime_library;
    // Link the crypto runtime archive and add the library crypto module even without a static call to it
    // (--with-crypto[=PATH]); PATH is an OpenSSL installation or source tree to link instead of the recorded one.
    // Added for the crypto runtime archive (plan 21 step LIB79).
    bool with_crypto = false;
    std::optional<std::filesystem::path> openssl;
};

// Recognize backend options separately from preprocessing and project operands.
bool is_backend_option(std::string_view option);
// Validate one backend operand before storing its value; perform no filesystem I/O.
std::optional<std::string> parse_backend_option(std::string_view option, std::span<char *> &remaining,
                                                BackendOptions &options);
// Reject action conflicts before informational dispatch or source/path access.
std::optional<std::string> validate_backend_options(const Options &options);
} // namespace clause::cli
