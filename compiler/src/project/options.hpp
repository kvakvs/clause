#pragma once
#include "discovery.hpp"
#include "model.hpp"
#include <clause/compiler/preprocessor.hpp>

namespace clause::project {
// Compose immutable per-target settings with explicit manifest and CLI path bases; `limits` bound the include_dirs
// pattern walks and carry the directory index they feed.
PreprocessorOptions compose_options(const Target &target, const std::filesystem::path &base,
                                    const std::filesystem::path &invocation, const PreprocessorOptions &cli,
                                    const DiscoveryLimits &limits = {});
} // namespace clause::project
