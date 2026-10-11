#pragma once
#include "../codegen/compilation.hpp"
#include "../linking/link.hpp"
#include <filesystem>

namespace clause::cli {
struct Publication {
    // Keep complete serialized batches independent of destroyed LLVM/AST ownership until project success.
    codegen::CompilationResult result;
    std::filesystem::path root;
    std::vector<std::filesystem::path> inputs;
    std::string object_extension;
    // Retain target context for publication failures after project execution has returned.
    std::string project_target;
};

struct PendingExecutable {
    // Retain target context for publication failures after project execution has returned.
    std::string project_target;
    // Linked executable waiting in its staging directory until the whole project succeeded.
    linking::StagedExecutable executable;
};

// Publish only complete batches; a platform replacement failure may leave earlier complete files installed.
void publish(const Publication &pending);
} // namespace clause::cli
