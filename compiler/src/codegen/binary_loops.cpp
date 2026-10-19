#include "binary_loops.hpp"
#include "../semantic/capabilities.hpp"
#include "lowering_expressions.hpp"
#include "source_locations.hpp"
#include <clause/compiler/source.hpp>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Module.h>

namespace clause::codegen {
namespace {
// The arguments at `parameter` of the self calls in tail position: the only places a loop's rest may be read.
std::set<const ast::Expression *> looping_arguments(const ExpressionLowering &state, std::size_t parameter,
                                                    const std::set<const ast::Expression *> &tails) {
    std::set<const ast::Expression *> arguments;
    for (const auto *expression : tails) {
        const auto *call = std::get_if<ast::CallExpression>(&expression->value);
        const auto callee = state.inferred.callees.find(expression);
        if (call && callee != state.inferred.callees.end() && callee->second.function == &state.function &&
            callee->second.module == &state.module && call->arguments.size() > parameter) {
            arguments.insert(&state.module.syntax->expression(call->arguments[parameter]));
        }
    }
    return arguments;
}

struct ParameterUse {
    // The rest bindings of a binary at the parameter, and the bindings of the whole parameter.
    std::set<semantic::BindingId> rests;
    std::set<semantic::BindingId> wholes;
};

// The plan values holding a rest of the binary at the parameter.
std::set<std::size_t> rest_slots(const semantic::MatchPlan &plan, std::size_t parameter) {
    std::set<std::size_t> slots;
    for (const auto &node : plan.nodes) {
        if (node.input == parameter && rest_extraction(node)) {
            slots.insert(node.output);
        }
    }
    return slots;
}

// Record what one clause's head binds from the parameter; false when the head compares it with a binding.
bool parameter_use(const semantic::MatchPlan &plan, std::size_t parameter, ParameterUse &use) {
    const auto rests = rest_slots(plan, parameter);
    for (const auto &node : plan.nodes) {
        if (node.operation == semantic::MatchOperation::exact_binding && node.input == parameter) {
            return false;
        }
        if (node.binding && node.input == parameter) {
            use.wholes.insert(*node.binding);
        }
        if (node.binding && rests.contains(node.input)) {
            use.rests.insert(*node.binding);
        }
    }
    return true;
}

// How a non-defining binding event bears on a loop: it reads the whole binary or a rest anywhere but a looping
// argument (stop), passes a rest on (passed), or concerns another binding (other).
enum class Event : std::uint8_t { other, passed, stop };

Event event(const ExpressionLowering &state, const semantic::Binding &binding, const ParameterUse &use,
            const std::set<const ast::Expression *> &arguments) {
    if (use.wholes.contains(binding.identity)) {
        return Event::stop;
    }
    if (!use.rests.contains(binding.identity)) {
        return Event::other;
    }
    const bool looping = binding.use == semantic::BindingUse::read &&
                         arguments.contains(&state.module.syntax->expression(binding.expression));
    return looping ? Event::passed : Event::stop;
}

// Whether the parameter loops: the whole binary is never read, and its rests are read only as looping arguments.
bool loops(const ExpressionLowering &state, const ParameterUse &use,
           const std::set<const ast::Expression *> &arguments) {
    bool passed = false;
    for (const auto &binding : state.function.bindings) {
        const auto kind =
            binding.use == semantic::BindingUse::definition ? Event::other : event(state, binding, use, arguments);
        if (kind == Event::stop) {
            return false;
        }
        passed = passed || kind == Event::passed;
    }
    return passed;
}

// Whether every clause's head lets `parameter` loop.
bool qualifies(const ExpressionLowering &state, std::span<const semantic::MatchPlan> plans, std::size_t parameter,
               const std::set<const ast::Expression *> &tails) {
    ParameterUse use;
    for (const auto &plan : plans) {
        if (!parameter_use(plan, parameter, use)) {
            return false;
        }
    }
    return loops(state, use, looping_arguments(state, parameter, tails));
}

} // namespace

std::vector<semantic::MatchPlan> head_plans(const ExpressionLowering &state, std::size_t clauses) {
    const auto out = [](const Diagnostic &diagnostic) { throw std::invalid_argument(render(diagnostic)); };
    std::vector<semantic::MatchPlan> plans;
    for (std::size_t clause = 0; clause < clauses; ++clause) {
        const semantic::MatchOptions options{.clause = clause, .word_bits = state.word->getBitWidth()};
        auto plan = semantic::make_match_plan(state.module, state.function, out, options);
        if (!plan) {
            return {};
        }
        plans.push_back(std::move(*plan));
    }
    return plans;
}

std::optional<std::size_t> binary_loop_parameter(const ExpressionLowering &state, std::size_t clauses,
                                                 const std::set<const ast::Expression *> &tails) {
    // Rests are extracted inline only on 64-bit targets.
    if (state.lambda || state.word->getBitWidth() != 64) {
        return std::nullopt;
    }
    const auto plans = head_plans(state, clauses);
    for (std::size_t parameter = 0; !plans.empty() && parameter < state.function.key.arity; ++parameter) {
        if (qualifies(state, plans, parameter, tails)) {
            return parameter;
        }
    }
    return std::nullopt;
}

llvm::Function *declare_loop_variant(const ExpressionLowering &state, const std::string_view suffix) {
    auto &entry = state.entry;
    auto &output = *entry.getParent();
    auto *variant = llvm::Function::Create(entry.getFunctionType(), llvm::GlobalValue::InternalLinkage,
                                           entry.getName() + llvm::StringRef(suffix), output);
    variant->setCallingConv(entry.getCallingConv());
    variant->addFnAttr(entry.getFnAttribute(ARITY_ATTRIBUTE));
    variant->getArg(0)->setName("context");
    variant->getArg(1)->setName("arguments");
    // The variant's own scope, named and placed like the function's, so its source locations stay valid.
    const auto *scope = entry.getSubprogram();
    if (scope && !output.debug_compile_units().empty()) {
        llvm::DIBuilder debug(output, true, *output.debug_compile_units_begin());
        const auto &site =
            source_site(state.module.syntax->anchor(state.module.syntax->form(state.function.form).source));
        auto *file = debug.createFile(site.source->name, "");
        variant->setSubprogram(debug.createFunction(file, utf8(state.function.key.name), "", file, scope->getLine(),
                                                    debug.createSubroutineType(debug.getOrCreateTypeArray({})),
                                                    scope->getScopeLine(), scope->getFlags(), scope->getSPFlags()));
        debug.finalize();
    }
    return variant;
}
} // namespace clause::codegen
