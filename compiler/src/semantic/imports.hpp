#pragma once
#include "declarations.hpp"

namespace clause::semantic {
// Record every -import in Module::imports_, reporting erl_lint's conflicts: a function imported twice, an import
// overriding an auto-imported BIF (warning) and a definition of an imported function.
void index_imports(Module &module, const Reporter &out);
// Whether the module's -compile options keep the function from being auto-imported (no_auto_import).
bool auto_import_off(const ast::Module &syntax, const FunctionKey &key);
// The module a local call of the function reaches through -import; null when it is not imported.
const std::u32string *import_owner(const Module &module, const FunctionKey &key);
} // namespace clause::semantic
