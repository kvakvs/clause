#pragma once
#include "../codegen/request.hpp"
#include "../semantic/types/contracts.hpp"
#include "entry.hpp"
#include "inference_cache.hpp"
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
    // How many modules the inference cache covered and how many of them reused kept results. Added for the
    // inference cache (58W).
    std::size_t covered_modules_ = 0;
    std::size_t reused_modules_ = 0;
};

// Run declaration, binding, call, declared-type and inference phases without creating LLVM state. With `live_only`
// (export liveness, 62C) functions the entry cannot reach are left out right after call
// resolution, so inference, contract checks and code generation never see them. With `cache` (58W), covered
// modules reuse inference results kept in the build cache, and fresh ones are kept.
bool analyze(const codegen::CompilationRequest &request, const EntryRequest &entry, bool live_only, Analysis &analysis,
             const DiagnosticSink &sink, const InferenceCache *cache = nullptr);
} // namespace clause::cli
