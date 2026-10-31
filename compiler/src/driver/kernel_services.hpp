#pragma once
#include <filesystem>
#include <span>
#include <string>

namespace clause::cli {
// The Erlang source of clause_kernel_services, which tells kernel:init/1 the services kernel_sup starts and links
// their modules into the program: every kernel service but the excluded ones. Added for opting out of kernel services
// (plan 30 step DIST4b1e).
std::string kernel_services_source(std::span<const std::string> excluded);
// The synthetic path naming clause_kernel_services in diagnostics, progress and debug information.
std::filesystem::path kernel_services_path();
} // namespace clause::cli
