#pragma once
#include "../semantic/declarations.hpp"
#include <filesystem>
#include <set>
#include <string>

namespace clause::cli {
// The Erlang source of an ignored module's placeholder: it exports each function the batch names in it, and every
// one returns {module_ignored, Module}. Added for ignored modules.
std::string placeholder_source(const std::u32string &module, const std::set<semantic::FunctionKey> &functions);
// The synthetic path naming a placeholder in diagnostics, progress and debug information. Added for ignored modules.
std::filesystem::path placeholder_path(const std::u32string &module);
} // namespace clause::cli
