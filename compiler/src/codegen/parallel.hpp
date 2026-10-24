#pragma once
#include "llvm_state.hpp"
#include <exception>
#include <functional>
#include <string>
#include <vector>

// Parallel code generation: optimization moves each module of an object batch into its own LLVM context, so modules
// optimize and emit on worker threads, each with its own target machine. Added for parallel code generation.
namespace clause::codegen {
// A module job: the module's index, the worker's target machine and the sink its LLVM diagnostics go to.
using ModuleJob = std::function<void(std::size_t, llvm::TargetMachine &, CompilationResult &)>;

// Whether optimization moves the batch's modules into contexts of their own: object output with more than one worker,
// and the modules still share one context.
bool parallel_codegen(const detail::CompilationState &state);
// Whether the batch's modules have contexts of their own, so emission may run on parallel threads.
bool own_contexts(const detail::CompilationState &state);
// Run `job` for every module on worker threads, then report each module's diagnostics in module order; returns the
// exception each job ended with, if any, by module index.
std::vector<std::exception_ptr> run_module_jobs(detail::CompilationState &state, const ModuleJob &job);
// Report the diagnostics of every module context to the batch result again after a parallel phase.
void attach_diagnostics(detail::CompilationState &state);
// The message of an exception a module job ended with.
std::string failure_message(const std::exception_ptr &failure);
} // namespace clause::codegen
