#include "optimization.hpp"
#include "alias_tags.hpp"
#include "frames.hpp"
#include "llvm_state.hpp"
#include "lowering_roots.hpp"
#include "parallel.hpp"
#include "progress.hpp"
#include "verification.hpp"
#include <exception>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <mutex>
#include <stdexcept>

namespace clause::codegen {
namespace {
// Select the standard LLVM pipeline; like Clang -Os, size mode is O2 over definitions marked optsize.
llvm::ModulePassManager pipeline(llvm::PassBuilder &builder, llvm::Module &module, const OptimizationLevel level) {
    if (level == OptimizationLevel::none) {
        return builder.buildO0DefaultPipeline(llvm::OptimizationLevel::O0);
    }
    if (level == OptimizationLevel::size) {
        for (auto &function : module) {
            if (!function.isDeclaration()) {
                function.addFnAttr(llvm::Attribute::OptimizeForSize);
            }
        }
    }
    return builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
}

// One more GVN over binary loops once the pipeline made their trips loops: the cursor a trip stores into the loop's
// cell becomes a register value of the next trip (SSA binary cursors, OPT1.4F).
struct LoopCursors : llvm::PassInfoMixin<LoopCursors> {
    // Run GVN on a function marked as a binary loop; keep every other one.
    llvm::PreservedAnalyses run(llvm::Function &function, llvm::FunctionAnalysisManager &analyses) {
        if (!function.hasFnAttribute(BINARY_LOOP_ATTRIBUTE)) {
            return llvm::PreservedAnalyses::all();
        }
        return llvm::GVNPass().run(function, analyses);
    }
};

// Analysis managers are local to a module and destroyed in reverse dependency order.
void optimize_module(llvm::Module &module, llvm::TargetMachine &machine, const OptimizationLevel level) {
    llvm::LoopAnalysisManager loops;
    llvm::FunctionAnalysisManager functions;
    llvm::CGSCCAnalysisManager call_graph;
    llvm::ModuleAnalysisManager modules;
    llvm::PassBuilder builder(&machine);
    builder.registerOptimizerLastEPCallback(
        [](llvm::ModulePassManager &passes, llvm::OptimizationLevel, llvm::ThinOrFullLTOPhase) {
            passes.addPass(llvm::createModuleToFunctionPassAdaptor(LoopCursors()));
        });
    builder.registerModuleAnalyses(modules);
    builder.registerCGSCCAnalyses(call_graph);
    builder.registerFunctionAnalyses(functions);
    builder.registerLoopAnalyses(loops);
    builder.crossRegisterProxies(loops, functions, call_graph, modules);
    for (auto &function : module) {
        tag_term_loads(function);
    }
    pipeline(builder, module, level).run(module, modules);
}

// Read a module's bitcode into `context`, optimize and verify it there. Added for parallel code generation.
std::unique_ptr<llvm::Module> optimize_alone(const llvm::SmallVector<char, 0> &bitcode, const std::string &identifier,
                                             llvm::LLVMContext &context, llvm::TargetMachine &machine,
                                             const OptimizationLevel level, CompilationResult &sink) {
    context.setDiagnosticHandlerCallBack(detail::capture_diagnostic, &sink, true);
    auto parsed = llvm::parseBitcodeFile(
        llvm::MemoryBufferRef(llvm::StringRef(bitcode.data(), bitcode.size()), identifier), context);
    if (!parsed) {
        throw std::runtime_error(llvm::toString(parsed.takeError()));
    }
    auto module = std::move(*parsed);
    optimize_module(*module, machine, level);
    (void)verify_module_ir(*module, machine, sink);
    return module;
}

// Optimize each module in a context of its own on parallel threads, through its bitcode, and replace the batch's
// modules with the optimized ones. Workers take turns at the shared context to write a module's bitcode and to replace
// the module. Added for parallel code generation.
bool optimize_parallel(Compilation &compilation) {
    auto &state = detail::state(compilation);
    for (std::size_t index = 0; index < state.modules.size(); ++index) {
        progress_module(compilation, index, "optimization");
    }
    std::mutex shared;
    state.module_contexts.resize(state.modules.size());
    const auto failures =
        run_module_jobs(state, [&](const std::size_t index, llvm::TargetMachine &machine, CompilationResult &sink) {
            auto context = std::make_unique<llvm::LLVMContext>();
            llvm::SmallVector<char, 0> bitcode;
            std::string identifier;
            {
                const std::lock_guard lock(shared);
                llvm::raw_svector_ostream stream(bitcode);
                // Use lists keep their order, which the optimizations see, so objects match those of one context.
                llvm::WriteBitcodeToFile(*state.modules[index], stream, true);
                identifier = state.modules[index]->getModuleIdentifier();
            }
            auto module = optimize_alone(bitcode, identifier, *context, machine, state.request.optimization, sink);
            const std::lock_guard lock(shared);
            state.modules[index] = std::move(module);
            state.module_contexts[index] = std::move(context);
        });
    for (std::size_t index = 0; index < failures.size(); ++index) {
        if (failures[index]) {
            state.result.report({.level = DiagnosticLevel::error,
                                 .message = "LLVM optimization failed: " + failure_message(failures[index]),
                                 .location = {},
                                 .module_name = state.modules[index]->getModuleIdentifier()});
            return false;
        }
    }
    return state.result.status() == CompilationStatus::incomplete;
}
} // namespace

bool optimize(Compilation &compilation) {
    if (!lower_frames(compilation)) {
        return false;
    }
    auto &state = detail::state(compilation);
    state.result.discard_outputs();
    if (parallel_codegen(state)) {
        return optimize_parallel(compilation);
    }
    std::size_t index = 0;
    for (const auto &module : state.modules) {
        progress_module(compilation, index++, "optimization");
        try {
            optimize_module(*module, *state.target_machine, state.request.optimization);
        } catch (const std::exception &error) {
            state.result.report({.level = DiagnosticLevel::error,
                                 .message = "LLVM optimization failed: " + std::string(error.what()),
                                 .location = {},
                                 .module_name = module->getModuleIdentifier()});
            return false;
        }
    }
    return verify_ir(compilation);
}
} // namespace clause::codegen
