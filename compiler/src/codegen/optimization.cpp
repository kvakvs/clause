#include "optimization.hpp"
#include "frames.hpp"
#include "llvm_state.hpp"
#include "lowering_roots.hpp"
#include "progress.hpp"
#include "verification.hpp"
#include <exception>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/Scalar/GVN.h>

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
    pipeline(builder, module, level).run(module, modules);
}
} // namespace

bool optimize(Compilation &compilation) {
    if (!lower_frames(compilation)) {
        return false;
    }
    auto &state = detail::state(compilation);
    state.result.discard_outputs();
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
