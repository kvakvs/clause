#include "../preprocessor/expression.hpp"
#include "capabilities.hpp"
#include "erlang_module.hpp"
#include "imports.hpp"
#include "pattern_state.hpp"
#include "services.hpp"
#include <set>

namespace clause::semantic {
namespace {
// This legality catalog follows maint-29 erl_internal:guard_bif/2 and new_type_test/2, not PP evaluation.
bool guard_bif(const std::u32string &name, const std::size_t arity) {
    static const std::set<FunctionKey> signatures{
        {U"abs", 1},         {U"binary_part", 2},  {U"binary_part", 3},  {U"bit_size", 1},   {U"byte_size", 1},
        {U"ceil", 1},        {U"element", 2},      {U"float", 1},        {U"floor", 1},      {U"hd", 1},
        {U"is_integer", 3},  {U"is_map_key", 2},   {U"length", 1},       {U"map_size", 1},   {U"map_get", 2},
        {U"max", 2},         {U"min", 2},          {U"node", 0},         {U"node", 1},       {U"round", 1},
        {U"self", 0},        {U"size", 1},         {U"tl", 1},           {U"trunc", 1},      {U"tuple_size", 1},
        {U"is_atom", 1},     {U"is_binary", 1},    {U"is_bitstring", 1}, {U"is_boolean", 1}, {U"is_float", 1},
        {U"is_function", 1}, {U"is_function", 2},  {U"is_integer", 1},   {U"is_list", 1},    {U"is_map", 1},
        {U"is_number", 1},   {U"is_pid", 1},       {U"is_port", 1},      {U"is_record", 1},  {U"is_record", 2},
        {U"is_record", 3},   {U"is_reference", 1}, {U"is_tuple", 1}};
    return signatures.contains({name, arity});
}

// Match normalized Name/Arity compile-option tuples without converting arbitrary integers to host widths.
bool signature(const ast::Module &syntax, const ast::TermId &id, const FunctionKey &key) {
    const auto *tuple = std::get_if<ast::TermTuple>(&syntax.term(id).value);
    if (!tuple || tuple->elements.size() != 2) {
        return false;
    }
    const auto *name = std::get_if<ast::Atom>(&syntax.term(tuple->elements[0]).value);
    const auto *number = std::get_if<ast::IntegerLiteral>(&syntax.term(tuple->elements[1]).value);
    return name && number && name->name == key.name && arity(number->value) == key.arity;
}

// Selective suppression applies only to the named signature; unrelated compile tuples remain opaque.
bool selective(BindingAnalysis &state, const ast::ExprId &site, const ast::TermTuple &tuple, const FunctionKey &key) {
    const auto &syntax = *state.module.syntax;
    if (tuple.elements.size() != 2) {
        return false;
    }
    const auto *name = std::get_if<ast::Atom>(&syntax.term(tuple.elements[0]).value);
    const auto *list = std::get_if<ast::TermList>(&syntax.term(tuple.elements[1]).value);
    if (!name || name->name != U"no_auto_import" || !list) {
        return false;
    }
    for (const auto &entry : list->elements) {
        if (!state.spend(site) || signature(syntax, entry, key)) {
            return true;
        }
    }
    return false;
}

// Only no_auto_import options affect the embedded-call namespace.
bool suppresses(BindingAnalysis &state, const ast::ExprId &site, const ast::TermValue &value, const FunctionKey &key) {
    if (const auto *name = std::get_if<ast::Atom>(&value)) {
        return name->name == U"no_auto_import";
    }
    const auto *tuple = std::get_if<ast::TermTuple>(&value);
    return tuple && selective(state, site, *tuple, key);
}

// Compile options can be lists; walk them iteratively and account for each inspected metadata item.
bool suppressed(BindingAnalysis &state, const ast::ExprId &site, const ast::TermId &root, const FunctionKey &key) {
    std::vector<ast::TermId> pending{root};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!state.spend(site)) {
            return true;
        }
        const auto &value = state.module.syntax->term(id).value;
        if (suppresses(state, site, value, key)) {
            return true;
        }
        if (const auto *list = std::get_if<ast::TermList>(&value)) {
            pending.insert(pending.end(), list->elements.begin(), list->elements.end());
        }
    }
    return false;
}

// Explicit imports identify an owner independently of auto-import suppression.
std::optional<bool> imported(BindingAnalysis &state, const ast::ExprId &site, const ast::ImportAttribute &value,
                             const FunctionKey &key) {
    for (const auto &entry : value.functions) {
        if (!state.spend(site)) {
            return false;
        }
        if (entry.name.name == key.name && arity(entry.arity) == key.arity) {
            return value.module.name == U"erlang";
        }
    }
    return {};
}

struct ImportState {
    // Distinguish explicit erlang imports from suppressed or unrelated auto-imported names.
    bool disabled = false;
    std::optional<bool> explicit_import;
};

// Merge one declaration without letting unrelated imports erase a previously found owner.
void import_form(BindingAnalysis &state, const ast::ExprId &site, const ast::FormValue &value, const FunctionKey &key,
                 ImportState &result) {
    if (const auto *attribute = std::get_if<ast::GenericAttribute>(&value);
        attribute && attribute->name.name == U"compile") {
        result.disabled = suppressed(state, site, attribute->value, key) || result.disabled;
    }
    if (const auto *attribute = std::get_if<ast::ImportAttribute>(&value)) {
        const auto owner = imported(state, site, *attribute, key);
        if (owner) {
            result.explicit_import = owner;
        }
    }
}

// Resolve only import/suppression metadata; capability analysis separately admits its inert supported forms.
bool auto_import(BindingAnalysis &state, const ast::ExprId &site, const FunctionKey &key) {
    ImportState result;
    for (const auto &id : state.module.syntax->forms()) {
        if (!state.spend(site)) {
            return false;
        }
        import_form(state, site, state.module.syntax->form(id).value, key, result);
    }
    return result.explicit_import.value_or(!result.disabled);
}

// Explicit erlang operator calls follow the same closed operator catalog as ordinary guard operators.
bool qualified(const ast::Module &syntax, const ast::RemoteExpression &remote, const std::size_t count) {
    const auto *owner = std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, remote.module)).value);
    const auto *name = std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, remote.function)).value);
    return owner && name && owner->name == U"erlang" &&
           (guard_bif(name->name, count) || operator_signature(name->name, count));
}
} // namespace

bool guard_signature(const FunctionKey &key) {
    return guard_bif(key.name, key.arity) || operator_signature(key.name, key.arity);
}

bool pattern_call(BindingAnalysis &state, const ast::ExprId &id, const ast::CallExpression &call) {
    const auto &syntax = *state.module.syntax;
    const auto &target = syntax.expression(ungroup(syntax, call.target)).value;
    if (const auto *name = std::get_if<ast::Atom>(&target)) {
        const FunctionKey key{name->name, call.arguments.size()};
        return guard_bif(key.name, key.arity) && !state.module.lookup.contains(key) && auto_import(state, id, key);
    }
    const auto *remote = std::get_if<ast::RemoteExpression>(&target);
    return remote && qualified(syntax, *remote, call.arguments.size());
}

namespace {
// Legacy aliases ignore modern auto-import suppression but cannot authorize an unrelated explicit owner.
bool legacy_owner(BindingAnalysis &state, const ast::ExprId &site, const FunctionKey &key) {
    ImportState result;
    for (const auto &id : state.module.syntax->forms()) {
        if (!state.spend(site)) {
            return false;
        }
        import_form(state, site, state.module.syntax->form(id).value, key, result);
    }
    return !state.module.lookup.contains(key) && result.explicit_import.value_or(true);
}

// Legacy tests resolve separately from expression calls and cannot be recovered through suppression metadata.
bool legacy_name(const ast::Atom *name, const std::size_t count) {
    static const std::set<std::u32string> legacy{U"integer", U"float",     U"number", U"atom",   U"list",    U"tuple",
                                                 U"pid",     U"reference", U"port",   U"binary", U"function"};
    return name && count == 1 && legacy.contains(name->name);
}

// Both the obsolete name and its modern equivalent must remain unshadowed in this module.
std::optional<FunctionKey> legacy(BindingAnalysis &state, const ast::ExprId &id, const ast::Atom &name) {
    const FunctionKey old{name.name, 1};
    FunctionKey modern{U"is_" + name.name, 1};
    if (state.module.lookup.contains(old) || !legacy_owner(state, id, modern) || !auto_import(state, id, old)) {
        return {};
    }
    return modern;
}
} // namespace

std::optional<FunctionKey> guard_identity(BindingAnalysis &state, const ast::ExprId &id,
                                          const ast::CallExpression &call, const bool top_test) {
    const auto &syntax = *state.module.syntax;
    const auto &target = syntax.expression(ungroup(syntax, call.target)).value;
    const auto *name = std::get_if<ast::Atom>(&target);
    if (top_test && legacy_name(name, call.arguments.size())) {
        return legacy(state, id, *name);
    }
    if (top_test && name && name->name == U"record" && call.arguments.size() == 2) {
        const FunctionKey modern{U"is_record", 2};
        return legacy_owner(state, id, modern) ? std::optional{modern} : std::nullopt;
    }
    if (!pattern_call(state, id, call)) {
        return {};
    }
    if (!name) {
        const auto &remote = std::get<ast::RemoteExpression>(target);
        name = std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, remote.function)).value);
    }
    return FunctionKey{name->name, call.arguments.size()};
}

namespace {
// Body builtins auto-imported like OTP's (erl_internal:bif/2, from the owned inventory): every bridge builtin OTP
// auto-imports (error/1, setelement/3, spawn/3, put/2, ...; not make_tuple/2,3 or port_info/1,2), and the dynamic
// calls apply/2,3.
bool auto_imported(const FunctionKey &key) {
    const bool apply = key.name == U"apply" && (key.arity == 2 || key.arity == 3);
    return apply || (otp_auto_import(key) && bridge_builtin(key));
}

// Other erlang bridge builtins (display/1, raise/3, function_exported/3) are callable only qualified.
std::optional<FunctionKey> qualified_builtin(const ast::Module &syntax, const ast::RemoteExpression &remote,
                                             const std::size_t count) {
    const auto *owner = std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, remote.module)).value);
    const auto *name = std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, remote.function)).value);
    if (!owner || !name || owner->name != U"erlang") {
        return {};
    }
    const FunctionKey key{name->name, count};
    return auto_imported(key) || bridge_builtin(key) ? std::optional{key} : std::nullopt;
}
} // namespace

bool auto_imported_bif(const FunctionKey &key) { return guard_bif(key.name, key.arity) || auto_imported(key); }

std::optional<std::pair<FunctionKey, std::size_t>> imported_builtin(const Module &module,
                                                                    const ast::CallExpression &call) {
    const auto *name = std::get_if<ast::Atom>(&module.syntax->expression(ungroup(*module.syntax, call.target)).value);
    FunctionKey key{name ? name->name : U"", call.arguments.size()};
    const auto *owner = name ? import_owner(module, key) : nullptr;
    const auto builtin = owner && *owner != U"erlang" ? bridge_builtin(*owner, key) : std::nullopt;
    return builtin ? std::optional{std::pair{std::move(key), *builtin}} : std::nullopt;
}

std::optional<FunctionKey> builtin_fun(BindingAnalysis &state, const ast::ExprId &id,
                                       const ast::LocalFunReference &reference) {
    const auto count = arity(reference.arity);
    if (!count) {
        return {};
    }
    FunctionKey key{reference.name.name, *count};
    // fun get/1 is the external fun erlang:get/1 of the library module (step 41B).
    if (library_auto_import(*state.module.syntax, key)) {
        return key;
    }
    const bool imported = guard_bif(key.name, key.arity) || auto_imported(key);
    if (!imported || !bridge_builtin(key) || state.module.lookup.contains(key) || !auto_import(state, id, key)) {
        return {};
    }
    return key;
}

std::optional<std::pair<FunctionKey, std::size_t>> module_builtin(const ast::Module &syntax,
                                                                  const ast::CallExpression &call) {
    const auto *remote = std::get_if<ast::RemoteExpression>(&syntax.expression(ungroup(syntax, call.target)).value);
    const auto *owner =
        remote ? std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, remote->module)).value) : nullptr;
    const auto *name =
        remote ? std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, remote->function)).value) : nullptr;
    if (!owner || !name || owner->name == U"erlang") {
        return {};
    }
    FunctionKey key{name->name, call.arguments.size()};
    const auto builtin = bridge_builtin(owner->name, key);
    return builtin ? std::optional{std::pair{std::move(key), *builtin}} : std::nullopt;
}

std::optional<FunctionKey> body_builtin(BindingAnalysis &state, const ast::ExprId &id,
                                        const ast::CallExpression &call) {
    const auto &syntax = *state.module.syntax;
    const auto &target = syntax.expression(ungroup(syntax, call.target)).value;
    if (const auto *remote = std::get_if<ast::RemoteExpression>(&target)) {
        return qualified_builtin(syntax, *remote, call.arguments.size());
    }
    const auto *name = std::get_if<ast::Atom>(&target);
    if (!name) {
        return {};
    }
    // A local definition or no_auto_import keeps the unqualified name an ordinary local call; -import of erlang
    // also reaches the builtins callable only qualified (display/1).
    FunctionKey key{name->name, call.arguments.size()};
    const auto *owner = import_owner(state.module, key);
    if (owner && *owner == U"erlang" && bridge_builtin(key)) {
        return key;
    }
    const bool imported = auto_imported(key) && !state.module.lookup.contains(key) && auto_import(state, id, key);
    return imported ? std::optional{key} : std::nullopt;
}
} // namespace clause::semantic
