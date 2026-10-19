#include "binary_loops.hpp"
#include "../semantic/capabilities.hpp"
#include "lowering_expressions.hpp"
#include "source_locations.hpp"
#include <algorithm>
#include <clause/compiler/source.hpp>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Module.h>

namespace clause::codegen {
namespace {
// The arguments at `parameter` of the self calls in tail position: where a loop passes its rest on.
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

// Sort the reads of the parameter's rests and of the whole parameter (escaping rests, OPT1.4E): a rest read as a
// looping argument is passed on; any other body read gets a copy, while guards only inspect the value.
void sort_reads(const ExpressionLowering &state, const ParameterUse &use,
                const std::set<const ast::Expression *> &arguments, LoopReads &reads) {
    for (const auto &binding : state.function.bindings) {
        const bool rest = use.rests.contains(binding.identity);
        if (binding.use != semantic::BindingUse::read || (!rest && !use.wholes.contains(binding.identity))) {
            continue;
        }
        const auto *read = &state.module.syntax->expression(binding.expression);
        if (rest && arguments.contains(read)) {
            reads.passed.insert(read);
        } else if (binding.context == semantic::BindingContext::body) {
            reads.copied.insert(read);
        }
    }
}

// How the parameter loops, when every clause's head lets it and some rest is passed back to the function.
std::optional<LoopReads> loop_reads(const ExpressionLowering &state, std::span<const semantic::MatchPlan> plans,
                                    std::size_t parameter, const std::set<const ast::Expression *> &tails) {
    ParameterUse use;
    for (const auto &plan : plans) {
        if (!parameter_use(plan, parameter, use)) {
            return std::nullopt;
        }
    }
    LoopReads reads{.parameter = parameter, .passed = {}, .copied = {}, .wholes = use.wholes};
    sort_reads(state, use, looping_arguments(state, parameter, tails), reads);
    return reads.passed.empty() ? std::nullopt : std::optional{std::move(reads)};
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

std::optional<LoopReads> binary_loop_reads(const ExpressionLowering &state, std::size_t clauses,
                                           const std::set<const ast::Expression *> &tails) {
    if (state.lambda) {
        return std::nullopt;
    }
    const auto plans = head_plans(state, clauses);
    for (std::size_t parameter = 0; !plans.empty() && parameter < state.function.key.arity; ++parameter) {
        if (auto reads = loop_reads(state, plans, parameter, tails)) {
            return reads;
        }
    }
    return std::nullopt;
}

llvm::Value *binary_loop_read(ExpressionLowering &state, const semantic::BindingId identity, llvm::Value *value,
                              const ast::Expression *read) {
    auto *loop = state.binary_loop;
    if (!loop || (read && !loop->reads.copied.contains(read))) {
        return value;
    }
    if (loop->in_variant && loop->whole && loop->reads.wholes.contains(identity)) {
        return loop_copy(state, *loop->whole, value);
    }
    const auto rest = loop->rests.find(value);
    return rest == loop->rests.end() ? value : loop_copy(state, rest->second, value);
}

void keep_invariants_alone(llvm::Function &variant) {
    // Walk the uses of each Erlang function: an instruction of the variant using one calls it.
    const auto used_here = [&](const llvm::User *user) {
        const auto *instruction = llvm::dyn_cast<llvm::Instruction>(user);
        return instruction && instruction->getFunction() == &variant;
    };
    const auto called = [&](const llvm::Function &function) {
        return &function != &variant && function.hasFnAttribute(ARITY_ATTRIBUTE) &&
               std::ranges::any_of(function.users(), used_here);
    };
    if (std::ranges::none_of(*variant.getParent(), called)) {
        return;
    }
    for (auto &instruction : llvm::instructions(variant)) {
        instruction.setMetadata(llvm::LLVMContext::MD_invariant_load, nullptr);
    }
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
