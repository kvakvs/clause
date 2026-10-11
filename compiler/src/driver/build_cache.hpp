#pragma once
// Added for build caches (plan step 62D2): the cache stores of one invocation and the transform results kept there.
#include "../cache/store.hpp"
#include "transform_sources.hpp"
#include "transforms/loader.hpp"
#include <map>
#include <memory>

namespace clause::cli {
// The cache stores of one invocation, one per build directory, pruned when the invocation ends.
class BuildCaches {
  public:
    // The store of a build directory (below <build_dir>/cache), opened on first use.
    cache::Store &store(const std::filesystem::path &build_directory);
    // Drop superseded results of every store this invocation wrote to.
    void prune();

  private:
    std::map<std::filesystem::path, std::unique_ptr<cache::Store>> stores_;
};

// Loader replies and sticky module lists kept in a build cache. A key hashes the request bytes (the module's
// preprocessed forms and compile options) with clau's and the host OTP's identity, the transform sources and the
// files they include, and every .beam on the code paths.
class TransformCache final : public transforms::HostCache {
  public:
    // Use `store`; the includes of transform sources come from `sources`; misses are traced under `verbose`.
    TransformCache(cache::Store &store, std::shared_ptr<const TransformSourceCache> sources, bool verbose)
        : store_(store), sources_(std::move(sources)), verbose_(verbose) {}

    std::optional<std::string> find(const transforms::HostQuery &query) override;
    void store(const transforms::HostQuery &query, std::string_view result) override;

  private:
    // The key of everything a query's result depends on.
    cache::Key key(const transforms::HostQuery &query);
    // The host OTP release of an erl, and the .beam files of a code path directory, hashed once per batch.
    const std::string &erl_identity(const std::filesystem::path &erl);
    const std::string &beam_digest(const std::filesystem::path &directory);

    cache::Store &store_;
    std::shared_ptr<const TransformSourceCache> sources_;
    bool verbose_;
    std::map<std::filesystem::path, std::string> erls_;
    std::map<std::filesystem::path, std::string> beams_;
};
} // namespace clause::cli
