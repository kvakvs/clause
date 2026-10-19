#pragma once
#include <cstddef>
#include <llvm/IR/IRBuilder.h>
#include <string_view>

namespace clause::codegen {
struct ExpressionLowering;

// Placeholder for a native-form function's frame slots: (context, slot count, name descriptor) -> slots.
// lower_frames replaces it; it never reaches emitted objects.
inline constexpr std::string_view FRAME_MARKER = "clause.frame";
// Placeholder for a call of a function value: (context, arguments, FrameDescriptor) -> result, where `arguments`
// already holds the arguments and the fun's captured values (CLAUSE_apply_v1). lower_frames replaces it.
inline constexpr std::string_view APPLY_MARKER = "clause.apply";
// Marks native-form Erlang function definitions and declarations with their arity.
inline constexpr std::string_view ARITY_ATTRIBUTE = "erlang-arity";
// Float loops (OPT1.2): a variant's parameters holding the bits of a double (a bit mask, in decimal) and the function
// its service transfers enter instead, with those doubles boxed by FLOAT_BOX: (context, double) -> word, 0 on failure.
inline constexpr std::string_view FLOAT_PARAMETERS_ATTRIBUTE = "clause-float-parameters";
inline constexpr std::string_view FLOAT_GENERIC_ATTRIBUTE = "clause-float-generic";
inline constexpr std::string_view FLOAT_BOX = "clause.float.box";
// Binary loops (OPT1.4C): marks a variant, then its body or merged cycle, which gets one more GVN after the pipeline
// so its cursor stays in registers across trips (SSA binary cursors, OPT1.4F).
inline constexpr std::string_view BINARY_LOOP_ATTRIBUTE = "clause-binary-loop";

struct FunctionRoots {
    // Patch the frame's term slot count after all emitted candidate values have assigned their slots.
    llvm::CallInst *buffer;
    llvm::IntegerType *word;
    // Arguments persist across candidates; temporary slots are reused after candidate rejection.
    std::size_t arguments;
    std::size_t next = 0;
    std::size_t capacity = 0;
};

// The generated function's argument count: an anonymous fun's code takes its captured values after its arguments.
std::size_t frame_arity(const ExpressionLowering &state);
// Name the frame slots before any source body or heap allocation can execute.
FunctionRoots begin_roots(ExpressionLowering &state);
// Retain original arguments before attempting any candidate.
void root_arguments(ExpressionLowering &state);
// Publish a produced term after its fallible operation succeeds and before another allocation.
void root_value(ExpressionLowering &state, llvm::Value *value);
// Metadata naming an instruction whose value is a term, so frame lowering keeps it in a term slot wherever it
// crosses a call or safepoint (OPT1.2); values that cross none stay in registers.
inline constexpr const char *TERM_METADATA = "clause.term";
// Mark a computed term instead of storing it into a root slot eagerly (OPT1.2); constants and arguments need none.
void mark_term(ExpressionLowering &state, llvm::Value *value);
// Reserve a zero-initialized runtime slot for a service's success-only output or constructor argument.
llvm::Value *root_slot(ExpressionLowering &state);
// Drop rejected-candidate temporaries while retaining the original argument roots.
void reset_candidate_roots(ExpressionLowering &state);
// Fix the frame's term slot count once every candidate has been lowered.
void finish_roots(ExpressionLowering &state);
} // namespace clause::codegen
