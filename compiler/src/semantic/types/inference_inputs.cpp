#include "inference_inputs.hpp"
#include "lattice.hpp"
#include <set>

namespace clause::semantic::types {
namespace {
// Add the functions of `module` that its fun F/A values name.
void add_fun_named(const Module &module, std::set<const Function *> &named) {
    for (const auto &fun : module.funs) {
        const auto found =
            fun.external || fun.expression ? module.lookup.end() : module.lookup.find({fun.function, fun.arity});
        if (found != module.lookup.end()) {
            named.insert(&module.functions.at(found->second));
        }
    }
}

// The functions of the batch that fun F/A names, whose callers are unknown.
std::set<const Function *> fun_named(const CallGraph &calls) {
    std::set<const Module *> modules;
    for (const auto function : calls.order) {
        modules.insert(function.module);
    }
    std::set<const Function *> result;
    for (const auto *module : modules) {
        add_fun_named(*module, result);
    }
    return result;
}

// The argument facts of each call of a function taking its callers' inputs, by function and argument.
std::map<const Function *, std::vector<std::vector<Id>>> call_arguments(const Inference &inference,
                                                                        const CallGraph &calls) {
    std::map<const Function *, std::vector<std::vector<Id>>> result;
    for (const auto &call : calls.calls) {
        if (!inference.inputs.contains(call.callee.function)) {
            continue;
        }
        const auto &syntax = *call.caller.module->syntax;
        const auto &arguments = std::get<ast::CallExpression>(syntax.expression(call.expression).value).arguments;
        auto &facts = result[call.callee.function];
        facts.resize(arguments.size());
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const auto found = inference.expressions.find(&syntax.expression(arguments[index]));
            facts[index].push_back(found == inference.expressions.end() ? inference.graph.top() : found->second.type);
        }
    }
    return result;
}
} // namespace

void prepare_inputs(Inference &inference, const CallGraph &calls) {
    const auto named = fun_named(calls);
    for (const auto function : calls.order) {
        // A reused function keeps the inputs a build cache kept, or none (term()). Added for the inference cache.
        if (!function.function->exported && !named.contains(function.function) &&
            !inference.reused.contains(function.function)) {
            inference.inputs.emplace(function.function,
                                     std::vector<Id>(function.function->key.arity, inference.graph.bottom()));
        }
    }
}

bool gather_inputs(Inference &inference, const CallGraph &calls, const bool widening) {
    const auto arguments = call_arguments(inference, calls);
    Lattice lattice(inference.graph);
    bool changed = false;
    for (auto &[function, inputs] : inference.inputs) {
        // Reused inputs stay as kept (58W).
        const auto found = inference.reused.contains(function) ? arguments.end() : arguments.find(function);
        for (std::size_t index = 0; found != arguments.end() && index < inputs.size(); ++index) {
            const auto seen = lattice.join(found->second.at(index), 0);
            const auto next = widening ? lattice.widen(inputs[index], lattice.join(inputs[index], seen))
                                       : lattice.join(inputs[index], seen);
            changed = changed || next != inputs[index];
            inputs[index] = next;
        }
    }
    return changed;
}

std::vector<Id> inputs_of(const Inference &inference, const Function &function) {
    const auto found = inference.inputs.find(&function);
    return found == inference.inputs.end() ? std::vector<Id>(function.key.arity, inference.graph.top()) : found->second;
}
} // namespace clause::semantic::types
