#include "attribute_values.hpp"
#include "forms.hpp"
#include "term_value.hpp"
#include <algorithm>

namespace clause {
std::vector<ast::ExprId> attribute_list(const ast::Module &module, const ast::ExprId &id) {
    std::vector<ast::ExprId> result;
    auto current = id;
    for (;;) {
        const auto &list = attribute_as<ast::List>(module, current);
        result.insert(result.end(), list.elements.begin(), list.elements.end());
        if (!list.tail) {
            return result;
        }
        current = *list.tail;
    }
}

std::vector<ast::NameArity> attribute_arities(const ast::Module &module, const ast::ExprId &id) {
    const auto items = attribute_list(module, id);
    std::vector<ast::NameArity> result;
    result.reserve(items.size());
    for (const auto &item : items) {
        const auto &division = attribute_as<ast::BinaryExpression>(module, item);
        if (operator_spelling(division.operation) != U"/") {
            throw EvaluationFailure();
        }
        result.push_back({.name = attribute_as<ast::Atom>(module, division.left),
                          .arity = attribute_as<ast::IntegerLiteral>(module, division.right).value});
    }
    return result;
}

ast::FormValue FormParser::ordinary_attribute(ast::Atom name, const std::vector<ast::ExprId> &arguments) {
    try {
        return checked_attribute(std::move(name), arguments);
    } catch (const EvaluationFailure &) {
        fail(DiagnosticCode::parser_syntax, "bad attribute value");
    } catch (const EvaluationLimit &) {
        fail(DiagnosticCode::resource_limit, "attribute literal budget exhausted");
    }
}

namespace {
// Preserve the legacy module builder's variable-list checks.
ast::ModuleAttribute module_attribute(const ast::Module &module, const std::vector<ast::ExprId> &arguments) {
    if (arguments.size() > 2) {
        throw EvaluationFailure();
    }
    ast::ModuleAttribute result{.name = attribute_as<ast::Atom>(module, arguments.front()), .parameters = {}};
    if (arguments.size() == 2) {
        const auto items = attribute_list(module, arguments[1]);
        result.parameters.emplace();
        result.parameters->reserve(items.size());
        for (const auto &id : items) {
            result.parameters->push_back(attribute_as<ast::Variable>(module, id));
        }
    }
    return result;
}

// Two-argument attributes have distinct shapes and never fall through to literals.
ast::FormValue paired_attribute(const ast::Module &module, const ast::Atom &name,
                                const std::vector<ast::ExprId> &arguments) {
    if (name.name == U"file") {
        return ast::FileAttribute{.name = attribute_as<ast::StringLiteral>(module, arguments[0]).value,
                                  .line = attribute_as<ast::IntegerLiteral>(module, arguments[1]).value};
    }
    auto module_name = attribute_as<ast::Atom>(module, arguments[0]);
    if (name.name == U"import") {
        return ast::ImportAttribute{.module = std::move(module_name),
                                    .functions = attribute_arities(module, arguments[1])};
    }
    if (name.name == U"import_record") {
        ast::ImportRecordAttribute result{.module = std::move(module_name), .names = {}};
        const auto items = attribute_list(module, arguments[1]);
        result.names.reserve(items.size());
        for (const auto &id : items) {
            result.names.push_back(attribute_as<ast::Atom>(module, id));
        }
        return result;
    }

    throw EvaluationFailure();
}
} // namespace

ast::FormValue FormParser::checked_attribute(ast::Atom name, const std::vector<ast::ExprId> &arguments) {
    const auto &module = builder_.view();
    if (name.name == U"module") {
        return module_attribute(module, arguments);
    }
    if (arguments.size() == 2) {
        return paired_attribute(module, name, arguments);
    }
    if (arguments.size() != 1) {
        throw EvaluationFailure();
    }
    if (name.name == U"export") {
        return ast::ExportAttribute{attribute_arities(module, arguments.front())};
    }
    if (name.name == U"doc" || name.name == U"moduledoc") {
        return documentation(name.name == U"moduledoc", arguments.front());
    }
    constexpr std::u32string_view paired[]{U"file", U"import", U"import_record", U"native_record"};
    // Kept sorted for the binary search (optimization step OPTM.1).
    static_assert(std::ranges::is_sorted(paired));
    if (std::ranges::binary_search(paired, name.name)) {
        throw EvaluationFailure();
    }
    return ast::GenericAttribute{.name = std::move(name), .value = term(arguments.front())};
}
} // namespace clause
