#pragma once
#include "compilation.hpp"
#include <optional>
#include <string>

namespace clause::codegen {
// Print each input module's machine code as the batch's target emits it, every run of instructions commented with
// the IR instruction and Erlang line it came from. Leaves the modules unchanged. Created for --print-asm.
std::optional<std::vector<OutputBuffer>> snapshot_assembly(Compilation &compilation);
// The target's assembly comment marker, for headers printed around listings.
std::string assembly_comment(const Compilation &compilation);
} // namespace clause::codegen
