#include "../symbols.hpp"
#include "resolver.hpp"
#include <algorithm>

namespace clause::semantic::types {
namespace {
struct Frame {
    // Keep syntax provenance and partially resolved children on a bounded explicit stack.
    ast::TypeId syntax;
    Description description;
    std::size_t next = 0;
    // Indirection permits allocation-free frame moves on Windows, where map moves allocate a sentinel.
    std::unique_ptr<Uses> uses = std::make_unique<Uses>();
};

// Begin one syntax node while keeping variable occurrence analysis independent of interning.
Frame frame(const ast::Module &syntax, const ast::TypeId &id) {
    Frame result{id, describe(syntax.type(id).value)};
    const auto &node = result.description.node;
    if (node.kind == Kind::variable && node.name != "_") {
        result.uses->emplace(node.name, 1);
    }
    return result;
}

// The parser's unrestricted map/tuple categories still permit OTP local builtin-name overrides.
Node application_shape(Node node) {
    if (node.name == "any" && (node.kind == Kind::tuple || node.kind == Kind::map)) {
        node.name = node.kind == Kind::tuple ? "tuple" : "map";
        node.kind = Kind::application;
    }
    return node;
}

// Merge one completed child into its parent using Erlang's union-variable scope rule.
void append_child(Frame &parent, const Id child, const Uses &uses) {
    parent.description.node.children.push_back(child);
    merge_uses(*parent.uses, uses, parent.description.node.kind == Kind::union_type);
}
} // namespace

std::string scope_identity(const Key &key, const std::string_view role, const std::size_t variant) {
    return std::string(role) + ":" + encode_symbol({key.module, key.name, key.arity}) + ":" + std::to_string(variant);
}

void merge_uses(Uses &target, const Uses &source, const bool alternatives) {
    for (const auto &[name, count] : source) {
        auto &previous = target[name];
        previous = alternatives ? std::max(previous, count) : std::min(std::size_t{2}, previous + count);
    }
}

Id Resolver::type(const ast::TypeId &root) {
    std::vector<Frame> pending;
    pending.push_back(frame(*module.syntax, root));
    std::size_t work = 0;
    while (!pending.empty()) {
        if (++work > registry.graph.limits().type_work) {
            scope.incomplete = true;
            diagnostic(module.syntax->type(root).source, "type analysis budget exhausted; widened to term()",
                       Severity::warning);
            return registry.graph.exhausted();
        }
        auto &current = pending.back();
        if (current.next < current.description.children.size()) {
            const auto child = current.description.children[current.next++];
            pending.push_back(frame(*module.syntax, child));
            continue;
        }
        auto completed = std::move(current);
        pending.pop_back();
        const auto &source = module.syntax->type(completed.syntax).source;
        const auto id = finish(application_shape(std::move(completed.description.node)), source);
        if (pending.empty()) {
            merge_uses(scope.variables, *completed.uses, false);
            return id;
        }
        append_child(pending.back(), id, *completed.uses);
    }
    return registry.graph.bottom();
}

void check_scope(const Scope &scope, const Module &module, const ast::NodeSource &source, const Reporter &out) {
    if (scope.incomplete) {
        return;
    }
    for (const auto &[name, count] : scope.variables) {
        if (count == 1 && !name.starts_with('_')) {
            report(module, &source, "singleton type variable " + name, out);
        }
    }
}
} // namespace clause::semantic::types
