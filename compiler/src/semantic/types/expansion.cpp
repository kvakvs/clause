#include "resolver.hpp"
#include <algorithm>

namespace clause::semantic::types {
namespace {
// Substitute this declaration's quantified formals; repeated names follow OTP's last-position mapping.
std::optional<Id> replacement(const Node &node, const Declaration &decl, const std::span<const Id> arguments) {
    const auto scope = scope_identity(decl.key, "type");
    if (node.kind != Kind::variable || node.module != scope) {
        return {};
    }
    const auto found = std::find(decl.parameters.rbegin(), decl.parameters.rend(), node.name);
    if (found == decl.parameters.rend()) {
        return {};
    }
    const auto reverse_index = static_cast<std::size_t>(found - decl.parameters.rbegin());
    return arguments[decl.parameters.size() - 1 - reverse_index];
}

struct Substitution {
    // Borrow one bounded graph/declaration and retain actual arguments for one-layer expansion.
    Registry &registry;
    const Declaration &declaration;
    // The declaration's body, which the expansion rebuilds.
    Id body;
    std::span<const Id> arguments;
    // Memoized nodes and an explicit postorder stack avoid repeated work or host recursion.
    std::map<Id, Id> memo;
    std::vector<std::pair<Id, bool>> pending;

    // Queue child substitutions before interning the reconstructed parent.
    void visit(const Id id, const bool ready) {
        auto node = registry.graph.get(id);
        if (const auto replaced = replacement(node, declaration, arguments)) {
            memo.emplace(id, *replaced);
            return;
        }
        if (!ready) {
            pending.emplace_back(id, true);
            for (const auto child : node.children) {
                pending.emplace_back(child, false);
            }
            return;
        }
        for (auto &child : node.children) {
            child = memo.at(child);
        }
        const auto result =
            node.kind == Kind::union_type ? registry.graph.join(node.children) : registry.graph.intern(std::move(node));
        memo.emplace(id, result);
    }

    // Bound traversal work independently of how much interning happens to reuse.
    Id run() {
        std::size_t work = 0;
        while (!pending.empty()) {
            if (++work > registry.graph.limits().type_work) {
                return registry.graph.exhausted();
            }
            const auto [id, ready] = pending.back();
            pending.pop_back();
            if (!memo.contains(id)) {
                visit(id, ready);
            }
        }
        return memo.at(body);
    }
};

// Opaque and nominal structure is visible only inside the defining module; elsewhere the name stays closed.
bool can_expand(const Declaration &decl, const std::string_view requesting_module) {
    const bool closed = decl.kind == ast::TypeDeclarationKind::opaque || decl.kind == ast::TypeDeclarationKind::nominal;
    return !closed || requesting_module == decl.key.module;
}
} // namespace

std::optional<Id> expand_reference(Registry &registry, const Id reference, const std::string_view requesting_module) {
    const auto node = registry.graph.get(reference);
    if (node.kind != Kind::reference) {
        return {};
    }
    const auto found = registry.lookup.find({node.module, node.name, node.children.size()});
    if (found == registry.lookup.end()) {
        return {};
    }
    const auto &decl = registry.declarations[found->second];
    if (!decl.body || !can_expand(decl, requesting_module)) {
        return {};
    }
    const auto body = *decl.body;
    if (const auto cached = registry.expansions.find(reference); cached != registry.expansions.end()) {
        return cached->second;
    }
    Substitution substitution{registry, decl, body, node.children, {}, {{body, false}}};
    const auto result = substitution.run();
    registry.expansions.emplace(reference, result);
    return result;
}
} // namespace clause::semantic::types
