#include "erlang_module.hpp"
#include "imports.hpp"
#include "services.hpp"
#include <algorithm>
#include <set>

namespace clause::semantic {
namespace {
// Whether a -import attribute of the module names the function.
bool imported(const ast::Module &syntax, const FunctionKey &key) {
    return std::ranges::any_of(syntax.forms(), [&](const auto &id) {
        const auto *import = std::get_if<ast::ImportAttribute>(&syntax.form(id).value);
        return import && std::ranges::any_of(import->functions, [&](const ast::NameArity &function) {
                   return function.name.name == key.name && arity(function.arity) == key.arity;
               });
    });
}
} // namespace

bool otp_auto_import(const FunctionKey &key) {
    static const std::set<FunctionKey> names{
#include "erlang_auto_imports.inc"
    };
    return names.contains(key);
}

bool compiler_handled(const FunctionKey &key) { return auto_imported_bif(key) || bridge_builtin(key); }

bool auto_imported(const ast::Module &syntax, const FunctionKey &key) {
    return otp_auto_import(key) && !defines_function(syntax, key) && !imported(syntax, key) &&
           !auto_import_off(syntax, key);
}

bool library_auto_import(const ast::Module &syntax, const FunctionKey &key) {
    return auto_imported(syntax, key) && !compiler_handled(key);
}
} // namespace clause::semantic
