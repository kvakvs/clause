#include "imports.hpp"
#include "services.hpp"
#include <algorithm>
#include <clause/compiler/printing.hpp>

namespace clause::semantic {
namespace {
// Name/arity as erl_lint prints it: F/A with the name quoted when it needs to be.
std::string function_text(const FunctionKey &key) {
    return atom_source(utf8(key.name)) + "/" + std::to_string(key.arity);
}

// Whether a {Name, Arity} literal names the function.
bool names_function(const ast::Module &syntax, const ast::TermId &item, const FunctionKey &key) {
    const auto *pair = std::get_if<ast::TermTuple>(&syntax.term(item).value);
    if (!pair || pair->elements.size() != 2) {
        return false;
    }
    const auto *name = std::get_if<ast::Atom>(&syntax.term(pair->elements[0]).value);
    const auto *count = std::get_if<ast::IntegerLiteral>(&syntax.term(pair->elements[1]).value);
    return name && count && name->name == key.name && arity(count->value) == key.arity;
}

// Whether one -compile option keeps the function from being auto-imported: no_auto_import or {no_auto_import, [F/A]}.
bool no_auto_import(const ast::Module &syntax, const ast::TermId &option, const FunctionKey &key) {
    const auto &value = syntax.term(option).value;
    if (const auto *atom = std::get_if<ast::Atom>(&value)) {
        return atom->name == U"no_auto_import";
    }
    const auto *tuple = std::get_if<ast::TermTuple>(&value);
    if (!tuple || tuple->elements.size() != 2) {
        return false;
    }
    const auto *name = std::get_if<ast::Atom>(&syntax.term(tuple->elements[0]).value);
    const auto *list = std::get_if<ast::TermList>(&syntax.term(tuple->elements[1]).value);
    return name && list && name->name == U"no_auto_import" &&
           std::ranges::any_of(list->elements, [&](const auto &item) { return names_function(syntax, item, key); });
}

struct Importer {
    // The module indexed, its no-warning categories and where diagnostics go.
    Module &module_;
    std::set<std::u32string> disabled_;
    const Reporter &out_;

    // Add one attribute's imports; erl_lint adds none of them when one is already imported.
    void attribute(const ast::Form &form, const ast::ImportAttribute &value) const {
        const auto &origin = attribute_name(*module_.syntax, form);
        std::map<FunctionKey, std::u32string> added;
        bool failed = false;
        for (const auto &entry : value.functions) {
            const auto count = arity(entry.arity);
            if (!count) {
                continue;
            }
            const FunctionKey key{entry.name.name, *count};
            if (const auto *owner = import_owner(module_, key)) {
                report(origin, "function " + function_text(key) + " already imported from " + atom_source(utf8(*owner)),
                       out_);
                failed = true;
                continue;
            }
            bif_clash(origin, key);
            added.emplace(key, value.module.name);
        }
        if (!failed) {
            module_.imports_.insert(added.begin(), added.end());
        }
    }

    // An import overrides an auto-imported BIF of the same name; OTP warns unless no_auto_import names it.
    void bif_clash(const ast::TokenOrigin &origin, const FunctionKey &key) const {
        if (!auto_imported_bif(key) || disabled_.contains(U"bif_clash") || auto_import_off(*module_.syntax, key)) {
            return;
        }
        const auto text = function_text(key);
        report(origin,
               "import directive overrides auto-imported BIF " + text + " --\nuse \"-compile({no_auto_import,[" + text +
                   "]}).\" to resolve name clash",
               out_, Severity::warning);
    }
};
} // namespace

void index_imports(Module &module, const Reporter &out) {
    const Importer importer{module, disabled_warnings(*module.syntax), out};
    for (const auto &id : module.syntax->forms()) {
        const auto &form = module.syntax->form(id);
        if (const auto *value = std::get_if<ast::ImportAttribute>(&form.value)) {
            importer.attribute(form, *value);
        }
    }
    for (const auto &function : module.functions) {
        if (module.imports_.contains(function.key)) {
            report(module, &module.syntax->form(function.form).source,
                   "defining imported function " + function_text(function.key), out);
        }
    }
}

bool auto_import_off(const ast::Module &syntax, const FunctionKey &key) {
    return std::ranges::any_of(compile_options(syntax),
                               [&](const auto &option) { return no_auto_import(syntax, option, key); });
}

const std::u32string *import_owner(const Module &module, const FunctionKey &key) {
    const auto found = module.imports_.find(key);
    return found == module.imports_.end() ? nullptr : &found->second;
}
} // namespace clause::semantic
