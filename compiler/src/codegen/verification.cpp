#include "verification.hpp"
#include "llvm_state.hpp"
#include "progress.hpp"
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <string>

namespace clause::codegen {
namespace {
// Preserve verifier text and the LLVM module identifier without inventing Erlang source coordinates.
bool reject(CompilationResult &result, const llvm::Module &module, const std::string &reason) {
    result.report(
        {.level = DiagnosticLevel::error,
         .message = "LLVM IR verification failed for module '" + module.getModuleIdentifier() + "': " + reason,
         .location = {},
         .module_name = {}});
    return false;
}

// Refuse missing or stale target settings before IR can reach target-dependent emission.
bool verify_target(const llvm::Module &module, const llvm::TargetMachine &machine, CompilationResult &result) {
    if (module.getTargetTriple() != machine.getTargetTriple()) {
        return reject(result, module, "target triple does not match the configured target machine");
    }
    if (module.getDataLayout() != machine.createDataLayout()) {
        return reject(result, module, "data layout does not match the configured target machine");
    }
    return true;
}

// Diagnose a malformed body once, before whole-module verification would repeat the same failure.
bool verify_functions(const llvm::Module &module, CompilationResult &result) {
    for (const auto &function : module) {
        if (function.isDeclaration()) {
            continue;
        }
        std::string message;
        llvm::raw_string_ostream stream(message);
        if (llvm::verifyFunction(function, &stream)) {
            return reject(result, module, "function '" + function.getName().str() + "': " + message);
        }
    }
    return true;
}

// Include globals, declarations and metadata; function verification alone cannot establish module validity.
bool verify_module(const llvm::Module &module, CompilationResult &result) {
    if (!verify_functions(module, result)) {
        return false;
    }
    std::string message;
    llvm::raw_string_ostream stream(message);
    if (llvm::verifyModule(module, &stream)) {
        return reject(result, module, message);
    }
    return true;
}
} // namespace

bool verify_ir(Compilation &compilation) {
    auto &state = detail::state(compilation);
    if (state.result.status() != CompilationStatus::incomplete) {
        return false;
    }
    if (!state.target_machine) {
        state.result.report({.level = DiagnosticLevel::error,
                             .message = "LLVM IR verification requires a configured target machine",
                             .location = {},
                             .module_name = {}});
        return false;
    }
    std::size_t index = 0;
    for (const auto &module : state.modules) {
        progress_module(compilation, index++, "verification");
        if (!verify_module_ir(*module, *state.target_machine, state.result)) {
            return false;
        }
    }
    return true;
}

bool verify_module_ir(const llvm::Module &module, const llvm::TargetMachine &machine, CompilationResult &result) {
    return verify_target(module, machine, result) && verify_module(module, result);
}
} // namespace clause::codegen
