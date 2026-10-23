#pragma once
#include "inference.hpp"

// Measurements of --print-stage-times taken between inference passes, outside the hot inference code
// (docs/compile.md). Added for the incremental-potential measurements of --print-stage-times.
namespace clause::semantic::types {
// Note the graph's growth during the pass that just ran and which components could have kept the previous pass's
// results: their members' inputs and their callees' summaries were unchanged. Call before gathering its inputs.
void note_pass(Inference &inference, const CallGraph &calls);
// Note whether a recursive component's round `round` (from 0) of the current solve brought its members to the previous
// pass's summaries. Added for the solve repetition measurements of --print-stage-times.
void note_round(Inference &inference, const Component &component, std::size_t round);
} // namespace clause::semantic::types
