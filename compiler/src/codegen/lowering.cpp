#include "lowering.hpp"
#include "../semantic/symbols.hpp"
#include "literals.hpp"
#include "llvm_state.hpp"
#include "lowering_expressions.hpp"
#include "lowering_roots.hpp"
#include "module_registration.hpp"
#include "progress.hpp"
#include "source_locations.hpp"
#include "specialization_analysis.hpp"
#include "specialization_lowering.hpp"
#include "startup.hpp"
#include "target.hpp"
#include "term_abi.hpp"
#include "verification.hpp"
#include <clause/compiler/source.hpp>
#include <llvm/IR/IRBuilder.h>
#include <stdexcept>

namespace clause::codegen {
namespace {
// Reject malformed phase inputs before generating a body or narrowing a literal.
void require(const bool condition, const char *message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

// Declare every reachable definition first, retaining private linkage for unexported entries.
void declare(llvm::Module &output, const semantic::Module &module, llvm::FunctionType *signature,
             const semantic::types::Inference &inferred) {
    output.setModuleIdentifier(utf8(module.name));
    for (const auto &function : module.functions) {
        if (!function.live_) {
            continue; // Export liveness (62C): no code for a function the executable cannot reach.
        }
        const auto &summary = inferred.functions.at(&function);
        require(summary.inputs.size() == function.key.arity, "lowering: inconsistent inferred arity");
        // Validate fact ownership, but never turn a source contract into an LLVM assumption.
        (void)inferred.graph.get(summary.result.type);
        require(output.getFunction(function.symbol) == nullptr, "lowering: duplicate definition");
        const auto linkage =
            function.exported ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage;
        auto *entry = llvm::Function::Create(signature, linkage, function.symbol, output);
        entry->setCallingConv(llvm::CallingConv::C);
        entry->addFnAttr(ARITY_ATTRIBUTE, std::to_string(function.key.arity));
        entry->getArg(0)->setName("context");
        entry->getArg(1)->setName("arguments");
    }
}

// Declare every anonymous fun's code, private, taking the fun's arguments and then its captured values.
void declare_lambdas(llvm::Module &output, const semantic::Module &module, llvm::FunctionType *signature) {
    for (const auto &fun : module.funs) {
        if (!fun.expression || !fun.owner->live_) {
            continue;
        }
        require(!fun.symbol.empty(), "lowering: an anonymous fun has more than 255 arguments and captured values");
        auto *entry = llvm::Function::Create(signature, llvm::GlobalValue::InternalLinkage, fun.symbol, output);
        entry->setCallingConv(llvm::CallingConv::C);
        entry->addFnAttr(ARITY_ATTRIBUTE, std::to_string(fun.arity + fun.captures.size()));
        entry->getArg(0)->setName("context");
        entry->getArg(1)->setName("arguments");
    }
}

// Bodies never trust declared types; with `proven` (O2 with type specialization) inferred facts replace checks.
void define(llvm::Module &output, const semantic::Module &module, llvm::IntegerType *word,
            const semantic::types::Inference &inferred, const bool proven) {
    const Proofs proofs(inferred.graph, word->getBitWidth());
    for (const auto &function : module.functions) {
        if (!function.live_) {
            continue;
        }
        auto *entry = output.getFunction(function.symbol);
        llvm::IRBuilder<> builder(llvm::BasicBlock::Create(output.getContext(), "entry", entry));
        lower_function(builder, *entry, module, function, word, inferred, proven ? &proofs : nullptr);
    }
    for (const auto &fun : module.funs) {
        if (fun.expression && fun.owner->live_) {
            auto *entry = output.getFunction(fun.symbol);
            llvm::IRBuilder<> builder(llvm::BasicBlock::Create(output.getContext(), "entry", entry));
            lower_lambda(builder, *entry, module, fun, word, inferred);
        }
    }
}

// Enforce the one-to-one syntax/module ownership contract before creating declarations.
void validate_inputs(const Compilation &compilation, const std::span<const std::unique_ptr<semantic::Module>> modules) {
    const auto &inputs = compilation.request().inputs;
    require(inputs.size() == modules.size(), "lowering: batch size mismatch");
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        require(modules[i] && modules[i]->syntax == &inputs[i].syntax, "lowering: foreign syntax owner");
    }
}

// Declare one module's functions and funs, with their source scopes when locations or debug information are asked.
void declare_module(Compilation &compilation, const std::size_t index, const semantic::Module &module,
                    llvm::FunctionType *signature, const semantic::types::Inference &inferred) {
    const auto &request = compilation.request();
    auto &output = *detail::state(compilation).modules[index];
    progress(request, "lowering", request.inputs[index].source_path, utf8(module.name));
    declare(output, module, signature, inferred);
    declare_lambdas(output, module, signature);
    if (request.annotate_source) {
        prepare_source_locations(output, module, request.optimization != OptimizationLevel::none,
                                 detail::state(compilation).source_scopes);
    }
    if (request.debug_info) {
        request_debug_format(output);
    }
}
} // namespace

bool lower(Compilation &compilation, const std::span<const std::unique_ptr<semantic::Module>> modules,
           const semantic::types::Inference &inferred) {
    if (!configure_target(compilation)) {
        return false;
    }
    auto *signature = generated_function_type(compilation);
    if (!signature) {
        return false;
    }
    try {
        validate_inputs(compilation, modules);
        auto &outputs = detail::state(compilation).modules;
        auto *word = llvm::cast<llvm::IntegerType>(signature->getReturnType());
        for (std::size_t i = 0; i < modules.size(); ++i) {
            declare_module(compilation, i, *modules[i], signature, inferred);
        }
        for (std::size_t i = 0; i < modules.size(); ++i) {
            emit_registration(*outputs[i], *modules[i], word);
            const auto &request = compilation.request();
            define(*outputs[i], *modules[i], word, inferred,
                   request.optimization == OptimizationLevel::speed && !request.disable_type_specialization);
        }
        progress_modules(compilation, "specialization");
        detail::state(compilation).specializations = analyze_specializations(compilation, modules, inferred);
        for (auto &output : outputs) {
            lower_specializations(*output, detail::state(compilation).specializations);
        }
        for (std::size_t i = 0; i < modules.size(); ++i) {
            finish_literals(*outputs[i], word, semantic::encode_symbol({utf8(modules[i]->name), "", 0}));
        }
        const auto &plan = detail::state(compilation).specializations;
        progress_modules(compilation, "specialization",
                         "batch installed=" + std::to_string(plan.lowered_variants) +
                             " skipped-measured-growth=" + std::to_string(plan.rejected_variants));
        if (compilation.request().startup) {
            emit_startup(compilation, modules, word);
        }
        return verify_ir(compilation);
    } catch (const std::exception &error) {
        compilation.result().report(
            {.level = DiagnosticLevel::error, .message = error.what(), .location = {}, .module_name = {}});
        return false;
    }
}
} // namespace clause::codegen
