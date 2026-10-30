#pragma once
#include "v1.hpp"
#include <cstddef>

namespace clause::abi::v1 {
// Every frame starts with these header words: the caller's header offset, the function's FrameDescriptor, the
// resume index its body switches on and the active handler index (0 while exceptions return through callers).
inline constexpr std::size_t frame_header_words = 4;
inline constexpr std::size_t frame_resume_word = 2;
// Argument and result registers (BEAM X registers): one per possible argument, results in the first.
inline constexpr std::size_t register_count = 256;

// FrameState::slow bits: entries take the service path while the heap wants a collection or the process profiles;
// returns only while it profiles (OPT0.1).
inline constexpr std::size_t slow_collect = 1;
inline constexpr std::size_t slow_profile = 2;

// Process state that generated code reads and updates inline on its fast transfer paths (OPT0.1). Every context
// passed to generated code starts with it, so a context pointer is also a FrameState pointer. All fields are one
// target word wide; the services keep them current and take over whenever a fast path does not apply.
struct FrameState {
    // Frame words, addressed as base plus offset; the block moves when it grows.
    TermWord *stack;
    // First free word, and the words a push may reach without growing or passing the stack limit.
    std::size_t top;
    std::size_t limit;
    // Header offset of the current frame.
    std::size_t frame;
    // Function entries left in the time slice; at zero an entry suspends through the service.
    std::size_t reductions;
    // The process registers; their address is stable for the context's lifetime.
    TermWord *registers;
    // Leading registers that are roots; every push and pop clears it.
    std::size_t live;
    // slow_collect / slow_profile bits.
    std::size_t slow;
    // Nonzero while the failure channel holds a failure or no host invocation is active (CLAUSE_call_failed_v2).
    std::size_t failed;
    // Free words of the heap block that generated code may bump-allocate inline: [heap_top, heap_limit). The runtime
    // takes inline allocations into its accounting before any heap operation and resets both after one; both are
    // null while no block exists or a heap budget applies (OPT0.3).
    TermWord *heap_top;
    TermWord *heap_limit;
    // The identity of the context's runtime, which a module atom cache names when its words are this runtime's
    // (OPT0.8).
    std::size_t runtime;
    // The context's own pid word, which self/0 reads (OPT0.2D).
    TermWord self;
    // The lowest word of [young, heap_top) whose objects were all created since the latest collection, so generated
    // code may write a cons cell's tail there without making an old object point at a young one; null with
    // heap_top (forward-built lists, OPT1.12).
    TermWord *young;
};

// Word index of each FrameState field, as generated code addresses them (OPT0.1, heap fields OPT0.3).
enum class StateField : std::uint8_t {
    stack,
    top,
    limit,
    frame,
    reductions,
    registers,
    live,
    slow,
    failed,
    heap_top,
    heap_limit,
    runtime,
    self,
    young
};
static_assert(sizeof(TermWord *) == sizeof(TermWord) && sizeof(std::size_t) == sizeof(TermWord));
static_assert(offsetof(FrameState, registers) == static_cast<unsigned>(StateField::registers) * sizeof(TermWord));
static_assert(offsetof(FrameState, failed) == static_cast<unsigned>(StateField::failed) * sizeof(TermWord));
static_assert(offsetof(FrameState, heap_limit) == static_cast<unsigned>(StateField::heap_limit) * sizeof(TermWord));
static_assert(offsetof(FrameState, self) == static_cast<unsigned>(StateField::self) * sizeof(TermWord));
static_assert(offsetof(FrameState, young) == static_cast<unsigned>(StateField::young) * sizeof(TermWord));
static_assert(sizeof(FrameState) == (static_cast<unsigned>(StateField::young) + 1) * sizeof(TermWord));
} // namespace clause::abi::v1

// Generated code moves between functions only by tail transfers to the Code these services return.
// Push a zeroed frame for `frame` (a FrameDescriptor), copy its arguments from the registers into its first slots
// and return its body. When the stack budget is exhausted, record the failure and return the caller's body instead.
// Entry is a safepoint: it first collects when the heap asks for it, with the arguments as register roots.
void *CLAUSE_enter_v1(void *context, const void *frame) noexcept;
// Tail call: release the current frame, then enter `frame` as CLAUSE_enter_v1 does.
void *CLAUSE_tail_v1(void *context, const void *frame) noexcept;
// Return `result` in the first register, release the current frame and return the body of the frame below.
void *CLAUSE_return_v1(void *context, clause::abi::v1::TermWord result) noexcept;
// Loop-head safepoint: collect when the heap asks for it. Term slots may be rewritten; the stack never moves.
void CLAUSE_safepoint_v1(void *context) noexcept;
// Header of the current frame, read once when a body is entered; its slots follow the header.
clause::abi::v1::TermWord *CLAUSE_frame_v1(void *context) noexcept;
// The process registers; their address is stable for the context's lifetime.
clause::abi::v1::TermWord *CLAUSE_registers_v1(void *context) noexcept;
// Host entry: run the function of `frame` with `arguments` to completion above a bottom frame and return its
// result, or 0 with a failure in the checked channel.
clause::abi::v1::TermWord CLAUSE_invoke_v1(void *context, const void *frame,
                                           const clause::abi::v1::TermWord *arguments) noexcept;
