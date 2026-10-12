#include "resolver.hpp"
#include <algorithm>
#include <array>

namespace clause::semantic::types {
namespace {
// OTP maint-29 erl_internal:is_type/2; local declarations take precedence over builtins.
bool builtin(const std::string &name, const std::size_t arity) {
    constexpr std::array zero{"any",
                              "arity",
                              "atom",
                              "binary",
                              "bitstring",
                              "bool",
                              "boolean",
                              "byte",
                              "char",
                              "dynamic",
                              "float",
                              "function",
                              "identifier",
                              "integer",
                              "iodata",
                              "iolist",
                              "list",
                              "map",
                              "maybe_improper_list",
                              "mfa",
                              "module",
                              "neg_integer",
                              "nil",
                              "no_return",
                              "node",
                              "non_neg_integer",
                              "none",
                              "nonempty_binary",
                              "nonempty_bitstring",
                              "nonempty_list",
                              "nonempty_maybe_improper_list",
                              "nonempty_string",
                              "number",
                              "pid",
                              "port",
                              "pos_integer",
                              "record",
                              "reference",
                              "string",
                              "term",
                              "timeout",
                              "tuple"};
    if (arity == 0) {
        return std::ranges::contains(zero, name);
    }
    if (arity == 1) {
        return name == "list" || name == "nonempty_list";
    }
    return arity == 2 && (name == "maybe_improper_list" || name == "nonempty_improper_list" ||
                          name == "nonempty_maybe_improper_list");
}

// Whether the module is a placeholder for a module ignored in project settings. Added for 41K2.
bool ignored_module(const Resolver &r, const std::string &module) {
    const auto found = r.registry.modules.find(module);
    return found != r.registry.modules.end() && found->second->file.starts_with(IGNORED_MODULE_DIRECTORY);
}

// Distinguish a missing declaration in this batch from unavailable external metadata.
Id unavailable_application(const Resolver &r, const Key &key, const ast::NodeSource &source) {
    if (ignored_module(r, key.module)) {
        r.diagnostic(source,
                     "attempt to import a type from a module ignored in project settings: " + key.module + ":" +
                         key.name,
                     Severity::warning);
        return r.registry.graph.top();
    }
    const bool known = r.registry.modules.contains(key.module);
    if (!known && r.module.library_) {
        return r.registry.graph.top(); // Clause's own headers: nothing the user can act on (41K1).
    }
    r.diagnostic(source,
                 (known ? "undefined remote type " : "unavailable external type metadata: ") + key.module + ":" +
                     key.name,
                 known ? Severity::error : Severity::warning);
    return r.registry.graph.top();
}

// Unknown external metadata is top with a warning; missing batch/local declarations are errors.
Id missing_application(Resolver &r, Node node, const Key &key, const ast::NodeSource &source) {
    const bool predefined = builtin(node.name, node.children.size());
    const bool known_builtin = key.module == "erlang" && predefined;
    if (key.module != utf8(r.module.name) && !known_builtin) {
        return unavailable_application(r, key, source);
    }
    if (!predefined) {
        r.diagnostic(source, "undefined local type " + node.name + "/" + std::to_string(node.children.size()));
        return r.registry.graph.top();
    }
    constexpr std::array tops{"term", "any", "dynamic"};
    if (std::ranges::contains(tops, node.name)) {
        return r.registry.graph.top();
    }
    if (node.name == "none" || node.name == "no_return") {
        return r.registry.graph.bottom();
    }
    node.module = "erlang";
    return r.registry.graph.intern(std::move(node));
}

// Leave applications as finite declaration references, including recursive parameter transformations.
Id application(Resolver &r, Node node, const ast::NodeSource &source) {
    const auto current = utf8(r.module.name);
    const auto owner = node.module.empty() ? current : node.module;
    const Key key{owner, node.name, node.children.size()};
    const auto found = r.registry.lookup.find(key);
    if (found == r.registry.lookup.end()) {
        return missing_application(r, std::move(node), key, source);
    }
    const auto &decl = r.registry.declarations[found->second];
    if (owner != current && !decl.exported) {
        r.diagnostic(source, "remote type is not exported: " + owner + ":" + node.name);
        return r.registry.graph.top();
    }
    node.kind = Kind::reference;
    node.module = owner;
    node.labels = {std::to_string(static_cast<unsigned>(decl.kind))};
    return r.registry.graph.intern(std::move(node));
}

// Validate field names independently of record lookup and remote metadata availability.
void record_fields(const Resolver &r, const Node &node, const ast::RecordDeclaration &syntax,
                   const ast::NodeSource &source) {
    std::vector<std::string> seen;
    seen.reserve(node.labels.size());
    for (const auto &name : node.labels) {
        if (std::ranges::contains(seen, name)) {
            r.diagnostic(source, "duplicate record type field " + name);
        }
        seen.push_back(name);
        if (!std::ranges::any_of(syntax.fields, [&](const auto &field) { return utf8(field.name.name) == name; })) {
            r.diagnostic(source, "undefined record type field " + name);
        }
    }
}

// Record refinements validate existing fields without enabling executable record construction.
Id record(Resolver &r, Node node, const ast::NodeSource &source) {
    const auto owner = node.module.empty() ? utf8(r.module.name) : node.module;
    const auto found = r.registry.records.find({owner, node.name});
    if (found == r.registry.records.end()) {
        const bool local = owner == utf8(r.module.name);
        r.diagnostic(source, (local ? "undefined record type " : "unavailable external record metadata: ") + node.name,
                     local ? Severity::error : Severity::warning);
        return r.registry.graph.top();
    }
    const auto &syntax = std::get<ast::RecordDeclaration>(found->second.module->syntax->form(found->second.form).value);
    record_fields(r, node, syntax, source);
    node.module = owner;
    return r.registry.graph.intern(std::move(node));
}
} // namespace

void Resolver::diagnostic(const ast::NodeSource &source, std::string message, const Severity severity) const {
    report(module, &source, std::move(message), out, severity);
}

Id Resolver::finish(Node node, const ast::NodeSource &source) {
    constexpr std::array constants{Kind::unary, Kind::binary, Kind::range, Kind::bitstring};
    if (std::ranges::contains(constants, node.kind)) {
        return constant_node(*this, std::move(node), source);
    }
    switch (node.kind) {
    case Kind::annotation:
        return node.children.front();
    case Kind::union_type:
        return registry.graph.join(node.children);
    case Kind::variable:
        if (node.name == "_") {
            return registry.graph.top();
        }
        node.module = scope.identity;
        return registry.graph.intern(std::move(node));
    case Kind::application:
        return application(*this, std::move(node), source);
    case Kind::record:
        return record(*this, std::move(node), source);
    default:
        return registry.graph.intern(std::move(node));
    }
}
} // namespace clause::semantic::types
