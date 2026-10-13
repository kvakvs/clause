#pragma once
// Added for the inference cache (plan step 58W): the inference results of covered modules kept in the build cache
// (docs/projects.md#inference-cache).
#include "../cache/store.hpp"
#include "../codegen/request.hpp"
#include "../project/cache_dirs.hpp"
#include "../semantic/types/inference.hpp"
#include <span>

namespace clause::cli {
// Where a batch keeps the inference results of covered modules, and whether lookups are traced.
struct InferenceCache {
    cache::Store *store_ = nullptr;
    const project::InferenceCacheDirs *dirs_ = nullptr;
    // The project target (empty for positional inputs): each target keeps its own entries.
    std::string target_;
    bool verbose_ = false;
};

// The covered modules of one batch with their keys: installs their kept results and keeps fresh ones.
class BatchInferenceCache {
  public:
    // Key each covered module with a fingerprint by its own and every reached module's source identity.
    BatchInferenceCache(const InferenceCache &cache, std::span<const codegen::CompilationInput> inputs,
                        std::span<const std::unique_ptr<semantic::Module>> modules);

    // Install the results kept for covered modules into `inference`, tracing hits and misses under --verbose.
    void load(semantic::types::Inference &inference);

    // Keep the results of covered modules that were inferred afresh.
    void save(const semantic::types::Inference &inference) const;

    // How many modules are covered, and how many of them reused kept results, for --print-stage-times.
    std::size_t covered() const { return entries_.size(); }

    std::size_t reused() const;

  private:
    struct Entry {
        // The module's batch index, where its results are kept and under which key, and whether they were reused.
        std::size_t module_;
        cache::Address address_;
        cache::Key key_;
        bool reused_ = false;
    };

    // Print one entry's hit or miss with the miss reason (--verbose).
    static void trace(const Entry &entry, const cache::Lookup &found);

    const InferenceCache &cache_;
    std::span<const codegen::CompilationInput> inputs_;
    std::span<const std::unique_ptr<semantic::Module>> modules_;
    std::vector<Entry> entries_;
};
} // namespace clause::cli
