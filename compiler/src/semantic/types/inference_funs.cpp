#include "inference_funs.hpp"
#include "../funs.hpp"
#include "function_types.hpp"
#include <algorithm>
#include <charconv>

namespace clause::semantic::types {
namespace {
// A literal arity as a host size; none for an invalid one.
std::optional<std::size_t> arity_of(const Integer &arity) {
    std::size_t value = 0;
    const auto &digits = arity.decimal;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() && value <= 255
               ? std::optional{value}
               : std::nullopt;
}

} // namespace

Id function_fun(Inference &inference, const Function *target, const std::size_t arity) {
    Lattice lattice(inference.graph);
    const auto summary = target ? inference.functions.find(target) : inference.functions.end();
    if (target && inference.reads) {
        inference.reads->insert(target);
    }
    if (inference.opaque_funs || summary == inference.functions.end()) {
        return lattice.fun(arity, inference.graph.top());
    }
    const auto fact = summary_fun(lattice, summary->second, arity);
    inference.fun_reads.insert_or_assign(target, fact);
    return fact;
}

std::optional<Id> fun_reference_fact(Inference &inference, const FunctionRef function,
                                     const ast::Expression &expression) {
    Lattice lattice(inference.graph);
    if (const auto *reference = std::get_if<ast::LocalFunReference>(&expression.value)) {
        const auto arity = arity_of(reference->arity);
        if (!arity) {
            return lattice.category("fun");
        }
        // A fun naming an auto-imported builtin is erlang:F/A, whose result is unknown here.
        const bool builtin = function.function->builtin_funs.contains(&expression);
        const auto *target = builtin ? nullptr : fun_target(*function.module, *reference);
        return function_fun(inference, target, *arity);
    }
    if (const auto *reference = std::get_if<ast::RemoteFunReference>(&expression.value)) {
        const auto *literal = std::get_if<Integer>(&reference->arity);
        const auto arity = literal ? arity_of(*literal) : std::nullopt;
        return arity ? lattice.fun(*arity, inference.graph.top()) : lattice.category("fun");
    }
    return std::nullopt;
}

Id summary_fun(Lattice &lattice, const Summary &summary, const std::size_t arity) {
    return summary.types.empty() ? lattice.fun(arity, summary.result.type) : fun_fact(lattice, summary.types);
}

namespace {
// The function types of a fun member as a function's: inputs, result and exactness, or none for another arity.
std::vector<FunctionType> member_types(Lattice &lattice, const Id member, const std::size_t arity) {
    std::vector<FunctionType> types;
    for (const auto type : lattice.function_types(member)) {
        const auto node = lattice.graph().get(type);
        if (node.kind != Kind::function || node.children.size() != arity + 1) {
            continue;
        }
        std::vector<Id> inputs(node.children.begin(), node.children.end() - 1);
        types.push_back({std::move(inputs), {node.children.back()}, std::ranges::contains(node.labels, "exact")});
    }
    return types;
}
} // namespace

Id call_value(Lattice &lattice, const Id callee, const std::vector<Fact> &arguments) {
    auto &graph = lattice.graph();
    std::vector<Id> results;
    for (const auto member : lattice.members(callee)) {
        const auto &node = graph.get(member);
        if (member == graph.top() || (node.kind == Kind::function && node.name == "any_arguments")) {
            return graph.top();
        }
        if (node.kind == Kind::function) {
            results.push_back(select(graph, member_types(lattice, member, arguments.size()), arguments).result.type);
        }
    }
    return lattice.join(results, 0);
}
} // namespace clause::semantic::types
