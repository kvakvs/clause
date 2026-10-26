#pragma once
#include "output.hpp"
#include <clause/compiler/ast/module.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clause::codegen {
// Generic O0, speed (O2 with specialization) or size (Os with dead-stripped executables).
enum class OptimizationLevel : std::uint8_t { none, speed, size };

struct CompilationInput {
    // Transfer syntax and its original path into the batch without copying AST ownership.
    CompilationInput(std::filesystem::path path, ast::Module module)
        : source_path(std::move(path)), syntax(std::move(module)) {}

    CompilationInput(CompilationInput &&) noexcept = default;
    CompilationInput &operator=(CompilationInput &&) noexcept = default;
    CompilationInput(const CompilationInput &) = delete;
    CompilationInput &operator=(const CompilationInput &) = delete;
    ~CompilationInput() = default;
    // Preserve the original native filename for diagnostics and future artifact planning.
    std::filesystem::path source_path;
    // Mark sources that began with a "#!" escript header (implicit main/1 export, escript exit codes).
    bool escript = false;
    // The number of trailing forms the frontend appended: OTP's predefined functions (module_info/0,1 and, for a
    // module declaring -callback, behaviour_info/1).
    std::size_t predefined_ = 0;
    // Retain immutable syntax and its source provenance beyond the parsing session.
    ast::Module syntax;
    // The hash of everything the syntax came from: each file read (path and bytes), the preprocessing options and
    // a parse transform reply. Keys of the driver's compile cache (plan step 62D4).
    std::string fingerprint_ = {};
    // The source is a Clause library module: types its headers name from modules outside the program are not
    // reported, since the user cannot act on them. Added for plan step 41K1.
    bool library_ = false;
};

struct StartupRequest {
    // Batch index of the entry module; its descriptor and spelling identify the entry at run time.
    std::size_t module = 0;
    // Exact UTF-8 entry function name and arity: 1 receives the argument list, 0 runs without it.
    std::string function;
    std::size_t arity = 1;
    // Escript entries exit with status 127 on uncaught exceptions.
    bool escript = false;
    // The program links the crypto runtime archive: the descriptor lists its extension entry. Added for the crypto
    // runtime archive.
    bool crypto = false;
    // The program is pinned to FIPS (--fips): the descriptor lists the archive's FIPS entry instead. Added for
    // crypto's FIPS mode (plan 21 step LIB79F).
    bool crypto_fips = false;
};

struct CompilationProgress {
    // Own one synchronous event's phase and original source/module context.
    std::string phase;
    std::filesystem::path source_path;
    std::string module_name;
    // Explain specialization policy or phase details without exposing LLVM objects.
    std::string detail;
};

using ProgressCallback = std::function<void(const CompilationProgress &)>;

struct CompilationLimits {
    // Bound retained module owners and total syntax work before semantic/backend traversal.
    std::size_t modules = 16384;
    std::size_t module_nodes = 1500000;
    std::size_t batch_nodes = 16000000;
    // Stop serialization before buffers exceed per-module or aggregate byte budgets.
    std::size_t module_bytes = std::size_t{64} * 1024 * 1024;
    std::size_t batch_bytes = std::size_t{256} * 1024 * 1024;
};

struct CompilationRequest {
    // Create an empty batch, then transfer it as a single owner into compilation.
    CompilationRequest() = default;
    CompilationRequest(CompilationRequest &&) noexcept = default;
    CompilationRequest &operator=(CompilationRequest &&) noexcept = default;
    CompilationRequest(const CompilationRequest &) = delete;
    CompilationRequest &operator=(const CompilationRequest &) = delete;
    ~CompilationRequest() = default;
    // Own one ordered batch; project targets must supply separate requests.
    std::vector<CompilationInput> inputs;
    // Keep injectable internal ceilings separate from language and optimization policy.
    CompilationLimits limits;
    // Attach project diagnostic context without interpreting it as a machine target.
    std::string project_target;
    // Observe only started phases; absent observers keep ordinary compilation silent.
    ProgressCallback progress;
    // Select a normalized LLVM triple; empty prefers the running host's triple, CPU and features.
    std::string target_triple;
    // Carry the requested pipeline policy without performing optimization yet.
    OptimizationLevel optimization = OptimizationLevel::none;
    // Threads that optimize and emit object modules in parallel; 0 uses every hardware thread, 1 works in one context
    // as before. Added for parallel code generation.
    std::size_t jobs = 1;
    // Keep the specialization override independent of option ordering.
    bool disable_type_specialization = false;
    // Preserve instruction locations for source comments in requested human-readable IR.
    bool annotate_source = false;
    // Emit those locations as debugger line tables (CodeView on MSVC targets, DWARF elsewhere), step 60.
    bool debug_info = false;
    // Select future in-memory serialization; filesystem publication belongs to the driver.
    OutputKind output_kind = OutputKind::object;
    // Add a startup module with a native `main` after the batch's modules when an entry is selected.
    std::optional<StartupRequest> startup;
};
} // namespace clause::codegen
