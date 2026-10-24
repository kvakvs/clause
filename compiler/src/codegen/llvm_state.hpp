#pragma once
#include "compilation.hpp"
#include "source_locations.hpp"
#include "specialization.hpp"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#include <memory>
#include <vector>

namespace clause::codegen::detail {
// Backend-only storage; field order is part of the module/context/callback lifetime contract.
struct CompilationState {
    // Initialize a separate context and one empty IR module for each owned syntax input.
    explicit CompilationState(CompilationRequest request);
    // Retain the batch and source provenance until all LLVM state has been released.
    CompilationRequest request;
    // Outlive the context whose diagnostic callback borrows this result's stable address.
    CompilationResult result;
    // Isolate LLVM types, constants and diagnostics between compilation instances.
    std::unique_ptr<llvm::LLVMContext> context;
    // Retain target layout/code-generation policy until modules are destroyed.
    std::unique_ptr<llvm::TargetMachine> target_machine;
    // One context per module once optimization moved the modules out of `context` to work on them in parallel, by
    // module index; empty otherwise. Added for parallel code generation.
    std::vector<std::unique_ptr<llvm::LLVMContext>> module_contexts;
    // Destroy all modules before their contexts; indices match request input order.
    std::vector<std::unique_ptr<llvm::Module>> modules;
    // Match surviving LLVM lexical scopes directly to retained physical source text.
    SourceScopes source_scopes;
    // Retain bounded decisions independently of temporary semantic ownership for later tracing/lowering.
    SpecializationPlan specializations;
    // Whether lower_frames lowered and verified the batch, so running it again before optimization is free. Added for
    // parallel code generation.
    bool frames_lowered = false;
};

// Copy LLVM diagnostics into the result; catch callback failures before returning to LLVM.
void capture_diagnostic(const llvm::DiagnosticInfo *diagnostic, void *destination) noexcept;
// Register only CMake-selected SDK backends once, safely across compilation instances.
void initialize_target_backends();
} // namespace clause::codegen::detail
