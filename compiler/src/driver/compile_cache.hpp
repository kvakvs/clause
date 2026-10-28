#pragma once
// Added for the compile cache (plan step 62D4): a whole batch's compile outcome kept in the build cache.
#include "../cache/store.hpp"
#include "../codegen/request.hpp"
#include "../codegen/result.hpp"
#include "frontend.hpp"

namespace clause::cli {
// A compiled batch, fresh or reused: its outputs and what linking and publication need besides them.
struct Compiled {
    codegen::CompilationResult result;
    // The normalized target triple and its object file extension.
    std::string triple;
    std::string object_extension;
    // The batch's source paths, protected from being replaced by outputs.
    std::vector<std::filesystem::path> inputs;
    // The program calls the ssl engine, so it links the ssl runtime archive. Added for the ssl runtime archive (plan
    // 21 step LIB81B).
    bool ssl = false;
};

// A batch outcome as the cache keeps it: failure, the diagnostics in order, and the outputs when kept.
struct CompileOutcome {
    bool failed = false;
    std::vector<std::string> diagnostics;
    std::optional<Compiled> compiled;
};

// Where a batch's outcome is kept and under which key. The key hashes every input's fingerprint and path with
// clau's identity and every option compilation depends on; the unit is the whole batch, because analysis, type
// inference, specialization budgets and export liveness span every module (docs/compile.md#compile-cache).
struct CompileCache {
    cache::Store *store_;
    cache::Address address_;
    cache::Key key_;
    // Whether outputs are kept (an executable or --emit needs them), and whether misses are traced.
    bool outputs_;
    bool verbose_;
};

// The compile cache of a batch; none without a build cache, for inspection modes, or for inputs without
// fingerprints.
std::optional<CompileCache> compile_cache(std::span<const codegen::CompilationInput> inputs,
                                          const FrontendRequest &frontend);
// The outcome kept for the batch; none on a miss (traced under --verbose).
std::optional<CompileOutcome> load_outcome(const CompileCache &cache, std::span<const std::filesystem::path> inputs);
// Keep a fresh outcome.
void save_outcome(const CompileCache &cache, const CompileOutcome &outcome);
} // namespace clause::cli
