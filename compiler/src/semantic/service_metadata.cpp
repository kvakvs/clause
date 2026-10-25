#include "services.hpp"
#include <algorithm>
#include <array>
#include <clause/compiler/printing.hpp>

namespace clause::semantic {
namespace {
// Options that only steer optimization, debug information or reporting; Clause ignores them.
constexpr std::array<std::u32string_view, 13> HINTS{
    U"no_auto_import", U"inline",        U"inline_list_funcs", U"debug_info", U"deterministic",
    U"report",         U"report_errors", U"report_warnings",   U"verbose",    U"compressed",
    U"bin_opt_info",   U"recv_opt_info", U"line_coverage"};
// Hints with an argument: {inline, [F/A]}, {inline_size, N}, {inline_effort, N}.
constexpr std::array<std::u32string_view, 3> ARGUMENT_HINTS{U"inline", U"inline_size", U"inline_effort"};

// no_auto_import selectors contain literal names/arities; dynamic or malformed metadata stays unavailable.
bool selector(const ast::Module &syntax, const ast::TermId &id) {
    const auto *tuple = std::get_if<ast::TermTuple>(&syntax.term(id).value);
    if (!tuple || tuple->elements.size() != 2) {
        return false;
    }
    const auto *name = std::get_if<ast::Atom>(&syntax.term(tuple->elements[0]).value);
    const auto *number = std::get_if<ast::IntegerLiteral>(&syntax.term(tuple->elements[1]).value);
    return name && number && arity(number->value).has_value();
}

// Whether an option name only steers warnings: warn_* and nowarn_*.
bool warning_option(const std::u32string_view name) {
    return name.starts_with(U"nowarn_") || name.starts_with(U"warn_");
}

// {parse_transform, Module} options are applied before analysis (docs/transforms.md); here they are inert.
bool parse_transform(const ast::Module &syntax, const ast::TermTuple &tuple) {
    const auto *name =
        tuple.elements.size() == 2 ? std::get_if<ast::Atom>(&syntax.term(tuple.elements[0]).value) : nullptr;
    return name && name->name == U"parse_transform" &&
           std::holds_alternative<ast::Atom>(syntax.term(tuple.elements[1]).value);
}

// {clause_liveness, strict} steers export liveness (docs/compile.md#strict-liveness). Added for strict liveness.
bool strict_liveness_value(const ast::Module &syntax, const ast::TermId &value) {
    const auto *atom = std::get_if<ast::Atom>(&syntax.term(value).value);
    return atom && atom->name == U"strict";
}

// {no_auto_import, [F/A]} suppresses auto-imports, which Clause honors; other tuples must be hints.
bool tuple_option(const ast::Module &syntax, const ast::TermTuple &tuple) {
    const auto *name =
        tuple.elements.size() == 2 ? std::get_if<ast::Atom>(&syntax.term(tuple.elements[0]).value) : nullptr;
    if (!name) {
        return false;
    }
    if (name->name == U"clause_liveness") {
        return strict_liveness_value(syntax, tuple.elements[1]);
    }
    if (name->name != U"no_auto_import") {
        return std::ranges::contains(ARGUMENT_HINTS, name->name) || warning_option(name->name);
    }
    const auto *list = std::get_if<ast::TermList>(&syntax.term(tuple.elements[1]).value);
    return list && !list->tail &&
           std::ranges::all_of(list->elements, [&](const auto &id) { return selector(syntax, id); });
}

// Auto-import suppression, warning options and optimization hints are inert; parse transforms, export_all, macro
// definitions and unknown options would change the module and stay gated.
bool inert(const ast::Module &syntax, const ast::TermValue &value) {
    if (const auto *name = std::get_if<ast::Atom>(&value)) {
        return std::ranges::contains(HINTS, name->name) || warning_option(name->name);
    }
    const auto *tuple = std::get_if<ast::TermTuple>(&value);
    return tuple && (tuple_option(syntax, *tuple) || parse_transform(syntax, *tuple));
}

// The first option of a -compile value that is not inert; a bounded iterative walk flattens nested lists.
std::optional<ast::TermId> rejected_option(const ast::Module &syntax, const ast::TermId &root) {
    std::vector<ast::TermId> pending{root};
    std::size_t work = 0;
    while (!pending.empty()) {
        auto id = pending.back();
        pending.pop_back();
        if (++work > 100'000) {
            return root;
        }
        const auto &value = syntax.term(id).value;
        const auto *list = std::get_if<ast::TermList>(&value);
        if (list && list->tail) {
            return id;
        }
        if (list) {
            pending.insert(pending.end(), list->elements.rbegin(), list->elements.rend());
        } else if (!inert(syntax, value)) {
            return id;
        }
    }
    return std::nullopt;
}

// What a rejected generic attribute does: the offending -compile option, or the attribute itself.
std::optional<std::string> rejected_generic(const ast::Module &syntax, const ast::GenericAttribute &attribute) {
    if (attribute.name.name != U"compile") {
        return "-" + atom_source(utf8(attribute.name.name)) + " attribute";
    }
    const auto option = rejected_option(syntax, attribute.value);
    return option ? std::optional{"-compile option " + term_source(syntax, *option)} : std::nullopt;
}
} // namespace

std::optional<std::string> rejected_attribute(const ast::Module &syntax, const ast::FormValue &value) {
    if (const auto *attribute = std::get_if<ast::GenericAttribute>(&value)) {
        return rejected_generic(syntax, *attribute);
    }
    if (const auto *attribute = std::get_if<ast::ModuleAttribute>(&value); attribute && attribute->parameters) {
        return "-module parameters";
    }
    return "attribute";
}
} // namespace clause::semantic
