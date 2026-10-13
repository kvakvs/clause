#pragma once
// Added for the inference cache (plan step 58U): which modules keep their inference results in the build cache.
#include "glob.hpp"
#include <array>
#include <filesystem>
#include <string_view>

namespace clause::project {
// OTP application directories (source checkouts and versioned installs) every target covers implicitly.
inline constexpr std::array<std::string_view, 8> IMPLICIT_INFERENCE_CACHE_DIRS{
    "*/stdlib",   "*/stdlib-*",   "*/kernel",       "*/kernel-*",
    "*/compiler", "*/compiler-*", "*/syntax_tools", "*/syntax_tools-*"};

// The directories of cache_inference_in_dirs and --cache-inference-in-dirs (docs/projects.md#inference-cache).
class InferenceCacheDirs {
  public:
    // Start with the implicit OTP application patterns.
    InferenceCacheDirs();

    // Add one pattern: a relative one matches the trailing directories of any path, an absolute one the whole
    // directory path. Throws Failure at the pattern's site for an unsupported wildcard construct.
    void add(const Text &pattern);

    // Whether a directory containing `source` matches one of the patterns.
    bool covers(const std::filesystem::path &source) const;

    // The pattern spellings in order, implicit ones first, for --print-env.
    const std::vector<std::string> &patterns() const { return patterns_; }

  private:
    // Each pattern as a glob over a whole directory path, ending in `**` so it covers every directory below.
    std::vector<Glob> globs_;
    std::vector<std::string> patterns_;
};
} // namespace clause::project
