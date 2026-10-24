#pragma once

namespace clause::codegen {
class Compilation;

// Turn every native-form Erlang function into explicit-frame code (docs/execution-model.md): a body entered
// through its descriptor that leaves only by `musttail` transfers, plus a host entry for exported symbols.
// A batch lowered and verified before is left alone, so the stage may run again before optimization.
bool lower_frames(Compilation &compilation);
} // namespace clause::codegen
