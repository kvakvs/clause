#pragma once
#include <cstdint>

namespace llvm {
class Function;
class Instruction;
class LoadInst;
} // namespace llvm

namespace clause::codegen {
// Disjoint kinds of memory generated code accesses, told to LLVM's type-based alias analysis so values kept in
// memory across a loop trip, such as a binary loop's cursor, stay in registers (SSA binary cursors, OPT1.4F). An
// untagged access may alias every kind; the two binary kinds are parts of `term`.
enum class MemoryKind : std::uint8_t {
    // The process context's state words (heap top and limit, reductions, frame offsets, live count).
    state,
    // The process stack: frame headers and slots, and the register save area.
    frame,
    // The words of a sub_binary cell (header, offset, bit length, original).
    sub_binary,
    // The words of an inline or off-heap binary cell (header, offset, bit length, data address).
    direct_binary,
    // Any word of a heap cell, literal or binary a term word points at: immutable but for the binary kinds' in-place
    // updates, which therefore alias it (cleanup across inlined boundaries, OPT1.10).
    term,
};

// Tag a load, store or memory intrinsic with the kind of memory it accesses; returns it.
llvm::Instruction *tag_memory(llvm::Instruction *access, MemoryKind kind);
// Mark a load of memory that no code of the function can change while it runs, so LLVM merges and hoists it past calls
// and stores (cleanup across inlined boundaries, OPT1.10); returns it.
llvm::LoadInst *invariant_load(llvm::LoadInst *load);
// Tag each untagged load of `function` whose address a term word forms as `term` memory, so stores of frame slots and
// state words no longer reload term fields (OPT1.10).
void tag_term_loads(llvm::Function &function);
} // namespace clause::codegen
