#include "emission.hpp"
#include "bounded_stream.hpp"
#include "limits.hpp"
#include "llvm_state.hpp"
#include "parallel.hpp"
#include "progress.hpp"
#include "verification.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <stdexcept>

namespace clause::codegen {
namespace {
// Retain module context and discard the entire batch if code generation cannot produce an object.
bool reject(detail::CompilationState &state, const llvm::Module &module, const std::string &reason) {
    state.result.report(
        {.level = DiagnosticLevel::error,
         .message = "LLVM object emission failed for module '" + module.getModuleIdentifier() + "': " + reason,
         .location = {},
         .module_name = {}});
    return false;
}

// Run LLVM's machine-code pipeline on a clone so repeated emission preserves the original IR; the object's bytes,
// within `capacity`.
std::vector<std::byte> object_code(const llvm::Module &module, llvm::TargetMachine &machine,
                                   const std::size_t capacity) {
    const auto working = llvm::CloneModule(module);
    BoundedStream stream(capacity);
    llvm::legacy::PassManager passes;
    if (machine.addPassesToEmitFile(passes, stream, nullptr, llvm::CodeGenFileType::ObjectFile, false)) {
        throw std::runtime_error("target does not support object emission");
    }
    passes.run(*working);
    return stream.take_bytes();
}

// Add a module's object to the batch's outputs, unless emission failed or produced nothing.
bool add_object(detail::CompilationState &state, const llvm::Module &module, std::vector<std::byte> bytes,
                const bool startup) {
    if (state.result.status() == CompilationStatus::failed) {
        return false;
    }
    if (bytes.empty()) {
        return reject(state, module, "target produced an empty object");
    }
    OutputBuffer output{.module_name = module.getModuleIdentifier(),
                        .kind = OutputKind::object,
                        .bytes = std::move(bytes),
                        .startup = startup};
    return state.result.add_output(std::move(output));
}

// Emit one module's object after the outputs before it.
bool emit_module(detail::CompilationState &state, const llvm::Module &module, const bool startup) {
    auto bytes =
        object_code(module, *state.target_machine, output_capacity(state.request.limits, state.result.outputs()));
    return add_object(state, module, std::move(bytes), startup);
}

// Add one module's object from a parallel emission after the outputs before it, within the batch's byte limits. Added
// for parallel code generation.
bool add_parallel_object(detail::CompilationState &state, const std::size_t index, std::vector<std::byte> bytes,
                         const std::exception_ptr &failure) {
    const auto &module = *state.modules[index];
    try {
        if (failure) {
            std::rethrow_exception(failure);
        }
        if (bytes.size() > output_capacity(state.request.limits, state.result.outputs())) {
            throw std::length_error("compilation artifact byte limit exceeded");
        }
    } catch (const std::exception &error) {
        return reject(state, module, error.what());
    }
    // The startup module, when present, follows the batch's inputs.
    return add_object(state, module, std::move(bytes), index >= state.request.inputs.size());
}

// Verify and emit each module, in a context of its own, on parallel threads, then add their objects in module order.
// Added for parallel code generation.
bool emit_parallel(Compilation &compilation) {
    auto &state = detail::state(compilation);
    if (state.result.status() != CompilationStatus::incomplete) {
        return false;
    }
    state.result.discard_outputs();
    const auto count = state.modules.size();
    const auto capacity = std::min(state.request.limits.module_bytes, state.request.limits.batch_bytes);
    std::vector<std::vector<std::byte>> objects(count);
    for (std::size_t index = 0; index < count; ++index) {
        progress_module(compilation, index, "emission");
    }
    const auto failures =
        run_module_jobs(state, [&](const std::size_t index, llvm::TargetMachine &machine, CompilationResult &sink) {
            state.module_contexts[index]->setDiagnosticHandlerCallBack(detail::capture_diagnostic, &sink, true);
            if (verify_module_ir(*state.modules[index], machine, sink)) {
                objects[index] = object_code(*state.modules[index], machine, capacity);
            }
        });
    for (std::size_t index = 0; index < count; ++index) {
        if (!add_parallel_object(state, index, std::move(objects[index]), failures[index])) {
            return false;
        }
    }
    return true;
}
} // namespace

bool emit_objects(Compilation &compilation) {
    auto &state = detail::state(compilation);
    if (own_contexts(state)) {
        return emit_parallel(compilation);
    }
    if (!verify_ir(compilation)) {
        return false;
    }
    state.result.discard_outputs();
    std::size_t index = 0;
    for (const auto &module : state.modules) {
        // The startup module, when present, follows the batch's inputs.
        const bool startup = index >= state.request.inputs.size();
        progress_module(compilation, index++, "emission");
        try {
            if (!emit_module(state, *module, startup)) {
                return false;
            }
        } catch (const std::exception &error) {
            return reject(state, *module, error.what());
        }
    }
    return true;
}
} // namespace clause::codegen
