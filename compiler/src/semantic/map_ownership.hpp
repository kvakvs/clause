#pragma once
#include "declarations.hpp"

namespace clause::semantic {
// Mark the map updates whose source map the function itself built and reads nowhere else (plan 20 step OPT0.5B):
// in one clause body, `M = <map expression or map comprehension>` followed by the single read of M as the base of
// a later update that is a body item, a match's right side or a direct call argument; M is never captured by a fun.
// The runtime may then update that map's off-heap tree in place (Function::owned_map_updates).
void mark_owned_map_updates(Module &module);
} // namespace clause::semantic
