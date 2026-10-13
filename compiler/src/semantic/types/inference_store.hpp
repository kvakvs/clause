#pragma once
// Added for the inference cache (plan step 58V): one module's inference results as bytes that a later compile of the
// same source decodes into its own inference (docs/semantic.md#inference-cache).
#include "inference.hpp"
#include <string>
#include <string_view>

namespace clause::semantic::types {
// The results `inference` holds for `module`, without addresses: the graph nodes they reach, the summaries and local
// inputs of its functions by name and arity, its expression facts by AST arena position, and the dependent facts
// those facts index.
std::string encode_module(const Inference &inference, const Module &module);

// Install encoded results into `inference` for `module`, an analysis of the same source: summaries, local inputs,
// and the functions and expression facts passes keep (`Inference::reused`, `reused_expressions`). False, installing
// nothing, for damaged bytes or bytes lacking a function of `module` that is live.
bool decode_module(Inference &inference, const Module &module, std::string_view bytes);
} // namespace clause::semantic::types
