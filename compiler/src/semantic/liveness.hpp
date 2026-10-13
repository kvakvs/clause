#pragma once
#include "calls.hpp"

namespace clause::semantic {
struct LivenessReport {
    // Modules that keep every export, each with the first reason found; reported under --verbose.
    std::map<std::u32string, std::string> kept_;
    // The number of exported functions left out of each module's export table.
    std::map<std::u32string, std::size_t> dropped_;
};

// Mark the functions an executable can reach from its entry (Function::live_): direct calls, funs, literal
// apply/spawn targets and every export of a module whose name escapes as a value. Added for export liveness (62C).
LivenessReport mark_live(std::span<const std::unique_ptr<Module>> modules, const CallGraph &calls, FunctionRef entry);
// The part of a call graph among live functions, so inference and contract checks skip code the executable cannot
// run. Added for liveness before inference.
CallGraph live_calls(const CallGraph &calls);
} // namespace clause::semantic
