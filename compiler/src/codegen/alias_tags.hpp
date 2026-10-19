#pragma once
#include <cstdint>

namespace llvm {
class Instruction;
}

namespace clause::codegen {
// Disjoint kinds of memory generated code accesses, told to LLVM's type-based alias analysis so values kept in
// memory across a loop trip, such as a binary loop's cursor, stay in registers (SSA binary cursors, OPT1.4F). An
// untagged access may alias every kind.
enum class MemoryKind : std::uint8_t {
    // The process context's state words (heap top and limit, reductions, frame offsets, live count).
    state,
    // The process stack: frame headers and slots, and the register save area.
    frame,
    // The words of a sub_binary cell (header, offset, bit length, original).
    sub_binary,
    // The words of an inline or off-heap binary cell (header, offset, bit length, data address).
    direct_binary,
};

// Tag a load, store or memory intrinsic with the kind of memory it accesses; returns it.
llvm::Instruction *tag_memory(llvm::Instruction *access, MemoryKind kind);
} // namespace clause::codegen
