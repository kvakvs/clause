#pragma once
#include "inference.hpp"

// Inputs of local functions from their callers (docs/semantic.md#inference): a function that is neither exported nor
// named by fun F/A is entered only by the direct calls of its batch, so its inputs are their joined argument facts.
namespace clause::semantic::types {
// Start every such function's inputs at none(), before any call is seen.
void prepare_inputs(Inference &inference, const CallGraph &calls);
// Join the argument facts of every call of such a function into its inputs, widening them once `widening`; true when
// an input changed. Under a profile it also notes how inputs changed (added for --print-stage-times).
bool gather_inputs(Inference &inference, const CallGraph &calls, bool widening);
// The inputs a function's body starts from: the gathered ones, or term() for the other functions.
std::vector<Id> inputs_of(const Inference &inference, const Function &function);
// The profile's most changed input slots, with their names and last facts, once inference ended. Added for the input
// churn measurements of --print-stage-times.
std::vector<InferenceProfile::ChurnedSlot> churned_slots(const Inference &inference, const CallGraph &calls);
} // namespace clause::semantic::types
