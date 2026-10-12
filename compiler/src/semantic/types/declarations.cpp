#include "resolver.hpp"

namespace clause::semantic::types {
namespace {
// Resolve one overload in its own variable scope, keeping constraints as declared provenance.
Overload overload(Registry &registry, const Contract &contract, const ast::SpecificationSignature &signature,
                  const std::size_t index, const Reporter &out) {
    const auto &module = *contract.module;
    Scope scope{scope_identity(contract.key, contract.callback ? "callback" : "spec", index), {}};
    Resolver resolver{registry, module, out, scope};
    auto description = describe(ast::TypeValue{signature.function});
    for (const auto &child : description.children) {
        description.node.children.push_back(resolver.type(child));
    }
    Overload result{registry.graph.intern(std::move(description.node)), {}, signature.source};
    result.constraints.reserve(signature.constraints.size());
    if (!signature.function.arguments || signature.function.arguments->size() != contract.key.arity) {
        resolver.diagnostic(signature.source, "specification overload arity mismatch");
    }
    for (const auto &constraint : signature.constraints) {
        const auto name = utf8(constraint.variable.name);
        ++scope.variables[name];
        result.constraints.emplace_back(name, resolver.type(constraint.bound));
    }
    check_scope(scope, module, signature.source, out);
    return result;
}

// Resolve formal variables and bodies once; references encode recursive edges without expansion.
void body(Registry &registry, Declaration &declaration, const Reporter &out) {
    const auto &module = *declaration.module;
    const auto &form = module.syntax->form(declaration.form);
    const auto &syntax = std::get<ast::TypeDeclaration>(form.value);
    Scope scope{scope_identity(declaration.key, "type"), {}};
    for (const auto &parameter : declaration.parameters) {
        ++scope.variables[parameter];
    }
    Resolver resolver{registry, module, out, scope};
    declaration.body = resolver.type(syntax.type);
    check_scope(scope, module, form.source, out);
}

// Record annotations belong to the defining module, with no executable default evaluation. True when a record of
// a module other than Clause's library widened the graph (plan step 41K1).
bool records(Registry &registry, const Reporter &out) {
    bool widened = false;
    for (auto &[key, record] : registry.records) {
        const auto &module = *record.module;
        const auto before = registry.graph.widenings();
        const auto &form = module.syntax->form(record.form);
        Scope scope{scope_identity({key.first, key.second, 0}, "record"), {}};
        Resolver resolver{registry, module, out, scope};
        const auto &fields = std::get<ast::RecordDeclaration>(form.value).fields;
        record.fields.reserve(fields.size());
        for (const auto &field : fields) {
            record.fields.emplace_back(utf8(field.name.name),
                                       field.type ? resolver.type(*field.type) : registry.graph.top());
        }
        check_scope(scope, module, form.source, out);
        widened = widened || (registry.graph.widenings() > before && !module.library_);
    }
    return widened;
}
} // namespace

void contracts(Registry &registry, const Reporter &out) {
    for (auto &contract : registry.contracts) {
        const auto &syntax = std::get<ast::Specification>(contract.module->syntax->form(contract.form).value);
        contract.overloads.reserve(syntax.signatures.size());
        for (std::size_t i = 0; i < syntax.signatures.size(); ++i) {
            contract.overloads.push_back(overload(registry, contract, syntax.signatures[i], i, out));
        }
    }
}

std::unique_ptr<Registry> resolve_declarations(const std::span<const std::unique_ptr<Module>> modules,
                                               const Reporter &out, const Limits limits) {
    auto registry = std::make_unique<Registry>(limits);
    collect(*registry, modules, out);
    // Widening inside Clause's library headers (OTP's wide unions) is not reported: the user cannot act on it.
    bool widened = false;
    for (auto &declaration : registry->declarations) {
        const auto before = registry->graph.widenings();
        body(*registry, declaration, out);
        widened = widened || (registry->graph.widenings() > before && !declaration.module->library_);
    }
    widened = records(*registry, out) || widened;
    const auto before = registry->graph.widenings();
    contracts(*registry, out);
    widened = widened || registry->graph.widenings() > before;
    if (widened && !modules.empty()) {
        report(*modules.front(), nullptr, "type graph budget exhausted; widened to term()", out, Severity::warning);
    }
    return registry;
}
} // namespace clause::semantic::types
