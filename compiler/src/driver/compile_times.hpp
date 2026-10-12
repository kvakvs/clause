#pragma once
// Added for the compile phases of --print-stage-times.
#include "analysis.hpp"

namespace clause::cli {
// Print each timed phase of one batch's compile; after inference, its passes and its slowest components.
void print_compile_phases(const Analysis &analysis, std::string_view subject);
} // namespace clause::cli
