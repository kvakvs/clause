#pragma once
#include "inference.hpp"
#include <string>
#include <vector>

// Measurements of --print-stage-times taken between inference passes, outside the hot inference code
// (docs/compile.md). Added for the incremental-potential measurements of --print-stage-times.
namespace clause::semantic::types {
// Note the graph's growth during the pass that just ran and which components could have kept the previous pass's
// results: their members' inputs and their callees' summaries were unchanged. Call before gathering its inputs.
void note_pass(Inference &inference, const CallGraph &calls);
// Note whether a recursive component's round `round` (from 0) of the current solve brought its members to the previous
// pass's summaries. Added for the solve repetition measurements of --print-stage-times.
void note_round(Inference &inference, const Component &component, std::size_t round);

// A component's largest fact as text, with its tree size. Added for the fact size measurements of --print-stage-times.
struct LargestFact {
    const Component *component_ = nullptr;
    std::size_t size_ = 0;
    std::string text_;
};

// The largest fact of each of the `count` components that took longest, costliest first.
std::vector<LargestFact> largest_facts(const Inference &inference, std::size_t count);
} // namespace clause::semantic::types
