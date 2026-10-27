#pragma once
// Added for the transform host (plan 21 LIB77A): Clause's own executable that runs the parse transforms Clause's
// library provides, so they need no host OTP (docs/transforms.md#transform-host).
#include "transforms/pipeline.hpp"
#include <memory>

namespace clause::cli {
// The transform host of this clau: the loader linked with the whole library (as --shell links it), built on first
// use below the user's cache directory and kept per identity of clau, its runtime archive and its library.
std::shared_ptr<transforms::ClauseHost> transform_host();
} // namespace clause::cli
