#include "declarations.hpp"
#include "behaviours.hpp"
#include "escript.hpp"
#include "imports.hpp"
#include "records.hpp"
#include "symbols.hpp"
#include <algorithm>
#include <charconv>
#include <clause/compiler/printing.hpp>
#include <clause/compiler/source.hpp>

namespace clause::semantic {
void report(const Module &module, const ast::NodeSource *source, std::string message, const Reporter &reporter,
            const Severity severity) {
    if (source) {
        report(module.syntax->anchor(*source), std::move(message), reporter, severity);
        return;
    }
    reporter({.code = DiagnosticCode::invalid_context,
              .message = std::move(message),
              .primary = {},
              .related = {},
              .severity = severity,
              .location = LogicalLocation{module.file, 1, 1}});
}

void report(const ast::TokenOrigin &origin, std::string message, const Reporter &reporter, const Severity severity) {
    reporter({.code = DiagnosticCode::invalid_context,
              .message = std::move(message),
              .primary = origin.spelling,
              .related = origin.related,
              .severity = severity,
              .location = origin.location});
}

std::optional<std::size_t> arity(const Integer &value) {
    std::size_t result = 0;
    const auto &text = value.decimal;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result > 255) {
        return {};
    }
    return result;
}

bool defines_function(const ast::Module &syntax, const FunctionKey &key) {
    return std::ranges::any_of(syntax.forms(), [&](const auto &id) {
        const auto *function = std::get_if<ast::Function>(&syntax.form(id).value);
        return function && function->name.name == key.name && function->clauses.front().arguments.size() == key.arity;
    });
}

bool predefined_form(const Module &module, const ast::FormId &id) {
    const auto forms = module.syntax->forms();
    return std::ranges::contains(forms.last(std::min(module.predefined_, forms.size())), id);
}

const ast::TokenOrigin &attribute_name(const ast::Module &syntax, const ast::Form &form) {
    const auto extent = syntax.extent(form.source);
    return extent.size() > 1 ? extent[1] : syntax.anchor(form.source);
}

std::vector<ast::TermId> compile_options(const ast::Module &syntax) {
    std::vector<ast::TermId> result;
    for (const auto &id : syntax.forms()) {
        const auto *attribute = std::get_if<ast::GenericAttribute>(&syntax.form(id).value);
        if (!attribute || attribute->name.name != U"compile") {
            continue;
        }
        std::vector<ast::TermId> pending{attribute->value};
        while (!pending.empty()) {
            const auto term = pending.back();
            pending.pop_back();
            if (const auto *list = std::get_if<ast::TermList>(&syntax.term(term).value)) {
                pending.insert(pending.end(), list->elements.rbegin(), list->elements.rend());
            } else {
                result.push_back(term);
            }
        }
    }
    return result;
}

std::set<std::u32string> disabled_warnings(const ast::Module &syntax) {
    std::set<std::u32string> result;
    for (const auto &option : compile_options(syntax)) {
        const auto *atom = std::get_if<ast::Atom>(&syntax.term(option).value);
        if (atom && atom->name.starts_with(U"nowarn_")) {
            result.insert(atom->name.substr(7));
        }
    }
    return result;
}

bool strict_liveness(const ast::Module &syntax) {
    return std::ranges::any_of(compile_options(syntax), [&](const ast::TermId &option) {
        const auto *tuple = std::get_if<ast::TermTuple>(&syntax.term(option).value);
        if (!tuple || tuple->elements.size() != 2) {
            return false;
        }
        const auto *name = std::get_if<ast::Atom>(&syntax.term(tuple->elements[0]).value);
        const auto *value = std::get_if<ast::Atom>(&syntax.term(tuple->elements[1]).value);
        return name && value && name->name == U"clause_liveness" && value->name == U"strict";
    });
}

std::optional<std::size_t> source_function(const Module &module, const FunctionKey &key) {
    const auto found = module.lookup.find(key);
    if (found == module.lookup.end() || (module.behaviour_info_ && key == FunctionKey{U"behaviour_info", 1})) {
        return std::nullopt;
    }
    return found->second;
}

namespace {
// Keep a module's first declaration as its identity while reporting subsequent declarations.
void module_name(Module &module, const ast::FormId &id, const ast::ModuleAttribute &attribute, const Reporter &out) {
    const auto &source = module.syntax->form(id).source;
    if (module.declaration) {
        report(module, &source, "duplicate module declaration", out);
        return;
    }
    module.declaration = id;
    module.name = attribute.name.name;
    if (module.name.empty() || module.name.size() > 255) {
        report(module, &source, "invalid module name", out);
    }
}

// Retain unique function definitions in source order, rejecting noncontiguous duplicates.
void function(Module &module, const ast::FormId &id, const ast::Function &syntax, const Reporter &out) {
    FunctionKey key{syntax.name.name, syntax.clauses.front().arguments.size()};
    const auto &source = module.syntax->form(id).source;
    if (key.arity > 255) {
        report(module, &source, "function arity exceeds 255", out);
        return;
    }
    if (key == FunctionKey{U"record_info", 2}) {
        // OTP expands record_info/2 at compile time, so the module already defines it; index it anyway.
        report(module, &source, "function record_info/2 already defined", out);
    }
    if (key.name == U"module_info" && key.arity < 2 && !predefined_form(module, id)) {
        // The compiler adds module_info/0,1 to every module (the frontend skipped this arity).
        report(module, &source, "function module_info/" + std::to_string(key.arity) + " already defined", out);
    }
    if (!module.lookup.emplace(key, module.functions.size()).second) {
        report(module, &source, "duplicate function " + utf8(key.name) + "/" + std::to_string(key.arity), out);
        return;
    }
    module.functions.push_back({key, id, false, encode_symbol({utf8(module.name), utf8(key.name), key.arity})});
}

// Resolve each export after all functions are indexed, allowing forward declarations.
// Diagnostics point at the attribute name, as erl_lint's do.
void export_function(Module &module, const ast::NameArity &value, const ast::TokenOrigin &origin, const Reporter &out) {
    const auto count = arity(value.arity);
    if (!count) {
        report(origin, "export arity must be in 0..255", out);
        return;
    }
    const auto found = source_function(module, {value.name.name, *count});
    if (!found) {
        report(origin, "export of undefined function " + utf8(value.name.name) + "/" + std::to_string(*count), out);
        return;
    }
    auto &entry = module.functions[*found];
    const bool module_info = value.name.name == U"module_info" && *count < 2;
    if (entry.exported || module_info) {
        // OTP only warns about a repeated export, including one of module_info/0,1, which are always exported.
        report(origin,
               "function " + atom_source(utf8(value.name.name)) + "/" + std::to_string(*count) + " already exported",
               out, Severity::warning);
    }
    entry.exported = true;
}
} // namespace

namespace {
// Establish module identity before constructing function symbols.
void index_name(Module &module, const Reporter &out) {
    const auto &syntax = *module.syntax;
    for (const auto &id : syntax.forms()) {
        if (const auto *attribute = std::get_if<ast::ModuleAttribute>(&syntax.form(id).value)) {
            module_name(module, id, *attribute, out);
        }
    }
    if (!module.declaration) {
        const auto *source = syntax.forms().empty() ? nullptr : &syntax.form(syntax.forms().front()).source;
        report(module, source, "missing module declaration", out);
    }
}

// Index the complete function set independently of export declaration order.
void index_functions(Module &module, const Reporter &out) {
    const auto &syntax = *module.syntax;
    for (const auto &id : syntax.forms()) {
        if (const auto *value = std::get_if<ast::Function>(&syntax.form(id).value)) {
            function(module, id, *value, out);
        }
    }
}

// Report each malformed export without hiding later declarations.
void index_exports(Module &module, const Reporter &out) {
    const auto &syntax = *module.syntax;
    for (const auto &id : syntax.forms()) {
        const auto &form = syntax.form(id);
        if (const auto *exports = std::get_if<ast::ExportAttribute>(&form.value)) {
            for (const auto &value : exports->functions) {
                export_function(module, value, attribute_name(syntax, form), out);
            }
        }
    }
}

// Export the functions the frontend generated (OTP's predefined functions); behaviour_info/1 among them is hidden
// from the module's own source.
void index_predefined(Module &module) {
    const auto found = module.lookup.find({U"behaviour_info", 1});
    module.behaviour_info_ =
        found != module.lookup.end() && predefined_form(module, module.functions[found->second].form);
    for (auto &function : module.functions) {
        function.exported = function.exported || predefined_form(module, function.form);
    }
}
} // namespace

std::unique_ptr<Module> index(const ast::Module &syntax, std::string file, const Reporter &out,
                              const IndexOptions options) {
    auto module = std::make_unique<Module>();
    module->syntax = &syntax;
    module->file = std::move(file);
    module->predefined_ = options.predefined_;
    module->library_ = options.library_;
    module->strict_liveness_ = strict_liveness(syntax);
    index_name(*module, out);
    index_functions(*module, out);
    index_predefined(*module);
    index_exports(*module, out);
    index_imports(*module, out);
    index_records(*module, out);
    index_callbacks(*module, out);
    if (options.escript_) {
        index_escript(*module, out);
    }
    return module;
}
} // namespace clause::semantic
