// Created for -on_load (plan 21 step MIM9d).
#include "on_load.hpp"
#include <clause/compiler/printing.hpp>

namespace clause::semantic {
namespace {
// The Name/Arity an on_load value names (f/0 parses as {f, 0}, as OTP's {f, 0} does); none when badly formed.
std::optional<FunctionKey> on_load_key(const ast::Module &syntax, const ast::TermId &value) {
    const auto *tuple = std::get_if<ast::TermTuple>(&syntax.term(value).value);
    if (!tuple || tuple->elements.size() != 2) {
        return std::nullopt;
    }
    const auto *name = std::get_if<ast::Atom>(&syntax.term(tuple->elements[0]).value);
    const auto *number = std::get_if<ast::IntegerLiteral>(&syntax.term(tuple->elements[1]).value);
    const auto count = number ? arity(number->value) : std::nullopt;
    if (!name || !count) {
        return std::nullopt;
    }
    return FunctionKey{name->name, *count};
}

// Check one -on_load attribute with erl_lint's messages at its name, and record the function of the first one.
void attribute(Module &module, const ast::Form &form, const ast::GenericAttribute &value, bool &seen,
               const Reporter &out) {
    const auto &origin = attribute_name(*module.syntax, form);
    if (seen) {
        report(origin, "more than one on_load attribute", out);
        return;
    }
    seen = true;
    const auto key = on_load_key(*module.syntax, value.value);
    if (!key) {
        report(origin, "badly formed on_load attribute: " + term_source(*module.syntax, value.value), out);
        return;
    }
    const auto text = atom_source(utf8(key->name)) + "/" + std::to_string(key->arity);
    if (key->arity != 0) {
        report(origin, "function " + text + " has wrong arity (must be 0)", out);
        return;
    }
    const auto found = source_function(module, *key);
    if (!found) {
        report(origin, "function " + text + " undefined", out);
        return;
    }
    module.on_load_ = *found;
}
} // namespace

void index_on_load(Module &module, const Reporter &out) {
    bool seen = false;
    for (const auto &id : module.syntax->forms()) {
        const auto &form = module.syntax->form(id);
        if (const auto *value = std::get_if<ast::GenericAttribute>(&form.value);
            value && value->name.name == U"on_load") {
            attribute(module, form, *value, seen, out);
        }
    }
}
} // namespace clause::semantic
