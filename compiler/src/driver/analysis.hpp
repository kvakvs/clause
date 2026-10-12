#pragma once
#include "../codegen/request.hpp"
#include "../semantic/types/contracts.hpp"
#include "entry.hpp"
#include "stage_times.hpp"

namespace clause::cli {
struct Analysis {
    // Own side tables while the input batch retains every borrowed AST and source location.
    std::vector<std::unique_ptr<semantic::Module>> modules;
    semantic::CallGraph calls;
    std::unique_ptr<semantic::types::Registry> declared;
    std::unique_ptr<semantic::types::Inference> inferred;
    // Validated entry function, present when selected explicitly or required for an executable.
    std::optional<ResolvedEntry> entry;
    // The compile's phase times and where inference spent its time, when timed. Added for --print-stage-times.
    PhaseTimes times;
    semantic::types::InferenceProfile profile;
};

// Run declaration, binding, call, declared-type and inference phases without creating LLVM state.
bool analyze(const codegen::CompilationRequest &request, const EntryRequest &entry, Analysis &analysis,
             const DiagnosticSink &sink);
// Leave out the functions an executable cannot reach from its entry, tracing under --verbose which modules keep
// every export and why. Added for export liveness (62C).
void drop_unreachable(const codegen::CompilationRequest &request, Analysis &analysis);
} // namespace clause::cli
