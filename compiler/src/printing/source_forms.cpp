#include "source_printer.hpp"
#include <array>

namespace clause::printing {
namespace {
// Body expressions start four columns right of their clause head.
constexpr std::size_t STEP = 4;

// `name/arity` entries of export and import lists.
std::string name_arities(const std::vector<ast::NameArity> &entries) {
    std::string result;
    for (const auto &entry : entries) {
        result += (result.empty() ? "" : ", ") + atom_text(entry.name) + '/' + entry.arity.decimal;
    }
    return '[' + result + ']';
}

// A {Name, Arity} term as a name and arity; none for any other term.
std::optional<ast::NameArity> name_arity(const ast::Module &syntax, const ast::TermId &id) {
    const auto *pair = std::get_if<ast::TermTuple>(&syntax.term(id).value);
    if (!pair || pair->elements.size() != 2) {
        return std::nullopt;
    }
    const auto *name = std::get_if<ast::Atom>(&syntax.term(pair->elements[0]).value);
    const auto *arity = std::get_if<ast::IntegerLiteral>(&syntax.term(pair->elements[1]).value);
    return name && arity ? std::optional{ast::NameArity{*name, arity->value}} : std::nullopt;
}

// The {Name, Arity} list of an -export_type or -optional_callbacks attribute, written `[name/arity, ...]` as in the
// source; none for other attributes or values.
std::optional<std::string> name_arity_terms(const SourcePrinter &printer, const ast::GenericAttribute &attribute) {
    if (attribute.name.name != U"export_type" && attribute.name.name != U"optional_callbacks") {
        return std::nullopt;
    }
    const auto *list = std::get_if<ast::TermList>(&printer.syntax().term(attribute.value).value);
    if (!list || list->tail) {
        return std::nullopt;
    }
    std::vector<ast::NameArity> entries;
    for (const auto &element : list->elements) {
        auto entry = name_arity(printer.syntax(), element);
        if (!entry) {
            return std::nullopt;
        }
        entries.push_back(std::move(*entry));
    }
    return name_arities(entries);
}

// One function clause: head, guard, then its body on the following lines.
std::string function_clause(const SourcePrinter &printer, const ast::Atom &name, const ast::FunctionClause &clause) {
    std::string arguments;
    for (const auto &argument : clause.arguments) {
        arguments += (arguments.empty() ? "" : ", ") + printer.pattern(argument, STEP);
    }
    auto head = atom_text(name) + '(' + arguments + ')';
    if (clause.guard) {
        head += " when " + printer.guard(*clause.guard, STEP);
    }
    return head + " ->\n" + printer.body(clause.body, STEP);
}

// The fields of a record declaration: `name = Default :: Type`.
std::string record_fields(const SourcePrinter &printer, const ast::RecordDeclaration &record) {
    std::string result;
    for (const auto &field : record.fields) {
        auto text = atom_text(field.name);
        if (field.default_value) {
            text += " = " + printer.expression(*field.default_value, {.annotated = false});
        }
        if (field.type) {
            text += " :: " + printer.type(*field.type);
        }
        result += (result.empty() ? "" : ", ") + text;
    }
    return result;
}

// One constraint: `V :: Bound`, or the legacy `is_subtype(V, Bound)`.
std::string constraint_text(const SourcePrinter &printer, const ast::TypeConstraint &constraint) {
    const auto variable = utf8(constraint.variable.name);
    const auto bound = printer.type(constraint.bound);
    return constraint.legacy ? "is_subtype(" + variable + ", " + bound + ')' : variable + " :: " + bound;
}

// One signature of a specification: `(Args) -> Result when Constraints`.
std::string signature(const SourcePrinter &printer, const ast::SpecificationSignature &signature) {
    const auto &function = signature.function;
    auto text = '(' + (function.arguments ? printer.types(*function.arguments) : std::string("...")) + ") -> " +
                (function.result ? printer.type(*function.result) : std::string("term()"));
    std::string constraints;
    for (const auto &constraint : signature.constraints) {
        constraints += constraints.empty() ? "" : ", ";
        constraints += constraint_text(printer, constraint);
    }
    return constraints.empty() ? text : text + " when " + constraints;
}

// The documentation metadata map of a -doc attribute, whose equiv value is an expression.
std::string documentation_entries(const SourcePrinter &printer, const std::vector<ast::DocumentationEntry> &entries) {
    std::string result;
    for (const auto &entry : entries) {
        const auto *term = std::get_if<ast::TermId>(&entry.value);
        const auto value =
            term ? printer.term(*term) : printer.expression(std::get<ast::ExprId>(entry.value), {.annotated = false});
        result += (result.empty() ? "" : ", ") + printer.term(entry.key) + " => " + value;
    }
    return "#{" + result + '}';
}

// One form as source text, ending in its full stop.
struct Form {
    const SourcePrinter &printer;

    std::string operator()(const ast::Function &value) const {
        std::string result;
        for (const auto &clause : value.clauses) {
            result += (result.empty() ? "" : ";\n") + function_clause(printer, value.name, clause);
        }
        return result + '.';
    }

    std::string operator()(const ast::Specification &value) const {
        auto text = std::string(value.callback ? "-callback " : "-spec ") +
                    (value.module ? atom_text(*value.module) + ':' : std::string()) + atom_text(value.name);
        std::string signatures;
        for (const auto &item : value.signatures) {
            signatures += (signatures.empty() ? "" : ";\n    ") + signature(printer, item);
        }
        return text + signatures + '.';
    }

    std::string operator()(const ast::TypeDeclaration &value) const {
        static constexpr std::array<std::string_view, 3> kinds{"-type ", "-opaque ", "-nominal "};
        std::string parameters;
        for (const auto &parameter : value.parameters) {
            parameters += (parameters.empty() ? "" : ", ") + utf8(parameter.name);
        }
        return std::string(kinds.at(static_cast<std::size_t>(value.kind))) + atom_text(value.name) + '(' + parameters +
               ") :: " + printer.type(value.type) + '.';
    }

    std::string operator()(const ast::ModuleAttribute &value) const {
        if (!value.parameters) {
            return "-module(" + atom_text(value.name) + ").";
        }
        std::string parameters;
        for (const auto &parameter : *value.parameters) {
            parameters += (parameters.empty() ? "" : ", ") + utf8(parameter.name);
        }
        return "-module(" + atom_text(value.name) + ", [" + parameters + "]).";
    }

    std::string operator()(const ast::FileAttribute &value) const {
        return "-file(" + literal_text(TokenKind::string, value.name) + ", " + value.line.decimal + ").";
    }

    std::string operator()(const ast::ExportAttribute &value) const {
        return "-export(" + name_arities(value.functions) + ").";
    }

    std::string operator()(const ast::ImportAttribute &value) const {
        return "-import(" + atom_text(value.module) + ", " + name_arities(value.functions) + ").";
    }

    std::string operator()(const ast::ImportRecordAttribute &value) const {
        std::string names;
        for (const auto &name : value.names) {
            names += (names.empty() ? "" : ", ") + atom_text(name);
        }
        return "-import_record(" + atom_text(value.module) + ", [" + names + "]).";
    }

    std::string operator()(const ast::GenericAttribute &value) const {
        const auto list = name_arity_terms(printer, value);
        return '-' + atom_text(value.name) + '(' + list.value_or(printer.term(value.value)) + ").";
    }

    std::string operator()(const ast::RecordDeclaration &value) const {
        const auto fields = record_fields(printer, value);
        if (value.native) {
            return "-record #" + atom_text(value.name) + '{' + fields + "}.";
        }
        return "-record(" + atom_text(value.name) + ", {" + fields + "}).";
    }

    std::string operator()(const ast::DocumentationAttribute &value) const {
        const auto *term = std::get_if<ast::TermId>(&value.value);
        const auto text =
            term ? printer.term(*term)
                 : documentation_entries(printer, std::get<std::vector<ast::DocumentationEntry>>(value.value));
        return std::string(value.module ? "-moduledoc " : "-doc ") + text + '.';
    }
};
} // namespace

std::string SourcePrinter::form(const ast::Form &form) const { return std::visit(Form{*this}, form.value); }

std::string SourcePrinter::pattern(const ast::PatternSyntaxId &id, std::size_t indent) const {
    const auto &value = syntax_.pattern(id).value;
    const auto expression = std::visit([](const auto &item) { return item.expression; }, value);
    return this->expression(expression, {.indent = indent, .annotated = false});
}

std::string SourcePrinter::guard(const ast::GuardSyntax &guard, std::size_t indent) const {
    std::string result;
    for (const auto &alternative : guard.alternatives) {
        std::string tests;
        for (const auto &test : alternative.tests) {
            tests += (tests.empty() ? "" : ", ") + expression(test, {.indent = indent, .annotated = false});
        }
        result += (result.empty() ? "" : "; ") + tests;
    }
    return result;
}

std::string SourcePrinter::body(const std::vector<ast::ExprId> &body, std::size_t indent) const {
    std::string result;
    result.reserve(256);
    for (const auto &item : body) {
        result += (result.empty() ? "" : ",\n") + spaces(indent) + expression(item, {indent, true, true});
    }
    return result;
}

std::string SourcePrinter::list(const std::vector<ast::ExprId> &items, Place place) const {
    std::string result;
    for (const auto &item : items) {
        result += (result.empty() ? "" : ", ") + expression(item, {place.indent, false, place.annotated});
    }
    return result;
}
} // namespace clause::printing
