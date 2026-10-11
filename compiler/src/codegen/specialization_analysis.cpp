#include "specialization_analysis.hpp"
#include "../semantic/capabilities.hpp"
#include "../semantic/records.hpp"
#include "integer_guards.hpp"
#include "llvm_state.hpp"
#include "progress.hpp"
#include "proofs.hpp"
#include <algorithm>

namespace clause::codegen {
namespace {
// Copy only observed per-argument representation proofs: integers (singletons or ranges) that are all immediates.
TypeProfile call_profile(const ast::Module &syntax, const ast::CallExpression &call,
                         const semantic::types::Inference &inferred, const unsigned bits) {
    const Proofs proofs(inferred.graph, bits);
    TypeProfile result;
    result.reserve(call.arguments.size());
    for (const auto &id : call.arguments) {
        const auto fact = inferred.expressions.find(&syntax.expression(id));
        const bool small = fact != inferred.expressions.end() && proofs.small(fact->second.type);
        result.push_back(small ? Representation::small_integer : Representation::generic);
    }
    return result;
}

// Bound source traversal by the caller's generic IR; preserve lexical order before deterministic ranking.
void observe(const semantic::Module &module, const semantic::Function &function,
             std::map<std::string, SpecializationInput> &inputs, const semantic::types::Inference &inferred,
             const unsigned bits) {
    const auto &syntax = *module.syntax;
    const auto &definition = std::get<ast::Function>(syntax.form(function.form).value);
    auto pending = semantic::function_roots(definition);
    std::ranges::reverse(pending);
    auto work = inputs.at(function.symbol).baseline * 2;
    while (!pending.empty() && work != 0) {
        --work;
        const auto id = pending.back();
        pending.pop_back();
        const auto &expression = syntax.expression(id);
        if (const auto *call = std::get_if<ast::CallExpression>(&expression.value);
            call && inferred.callees.contains(&expression)) {
            const auto callee = inferred.callees.at(&expression);
            if (call->arguments.size() > work) {
                break;
            }
            work -= call->arguments.size();
            inputs.at(callee.function->symbol).profiles.push_back(call_profile(syntax, *call, inferred, bits));
        }
        const auto children = semantic::expression_children(module, expression);
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
}

// Explain the policy shortcut without measuring or enumerating specialization profiles.
void trace_disabled(const Compilation &compilation, const std::span<const std::unique_ptr<semantic::Module>> modules) {
    if (!compilation.request().progress) {
        return;
    }
    for (std::size_t i = 0; i < modules.size(); ++i) {
        for (const auto &function : modules[i]->functions) {
            progress(compilation.request(), "specialization", compilation.request().inputs[i].source_path,
                     utf8(modules[i]->name),
                     "function=" + function.symbol + " profile=not-planned skipped=disabled-by-policy");
        }
    }
}

// Measure actual generic IR and recognize only checks implemented by the specialization rewriter.
std::map<std::string, SpecializationInput>
measurements(Compilation &compilation, const std::span<const std::unique_ptr<semantic::Module>> modules) {
    std::map<std::string, SpecializationInput> result;
    auto &outputs = detail::state(compilation).modules;
    for (std::size_t i = 0; i < modules.size(); ++i) {
        for (const auto &function : modules[i]->functions) {
            if (!function.live_) {
                continue;
            }
            auto &entry = *outputs[i]->getFunction(function.symbol);
            SpecializationInput input{utf8(modules[i]->name),
                                      function.symbol,
                                      entry.getInstructionCount(),
                                      std::vector<std::size_t>(function.key.arity),
                                      {},
                                      compilation.request().inputs[i].source_path};
            for (const auto &[check, argument] : integer_guards(entry, function.key.arity)) {
                (void)check;
                ++input.checks[argument];
            }
            result.emplace(function.symbol, std::move(input));
        }
    }
    return result;
}
} // namespace

SpecializationPlan analyze_specializations(Compilation &compilation,
                                           const std::span<const std::unique_ptr<semantic::Module>> modules,
                                           const semantic::types::Inference &inferred) {
    if (modules.empty()) {
        return {};
    }
    if (compilation.request().optimization != OptimizationLevel::speed ||
        compilation.request().disable_type_specialization) {
        trace_disabled(compilation, modules);
        SpecializationPlan disabled;
        disabled.decisions[SpecializationReason::disabled] = 1;
        return disabled;
    }
    auto inputs = measurements(compilation, modules);
    const auto bits = detail::state(compilation).modules.front()->getDataLayout().getPointerSizeInBits();
    for (const auto &module : modules) {
        for (const auto &function : module->functions) {
            if (function.live_) {
                observe(*module, function, inputs, inferred, bits);
            }
        }
    }
    std::vector<SpecializationInput> ordered;
    ordered.reserve(inputs.size());
    for (auto &[symbol, input] : inputs) {
        (void)symbol;
        ordered.push_back(std::move(input));
    }
    return plan_specializations(compilation.request(), ordered);
}
} // namespace clause::codegen
